// RenderTree - a retained tree of nodes that remembers what changed.
//
// This is the smallest thing that can answer the question step 2 left open:
// full-window repaint at 1080p costs 3.91 ms of raster plus 7.71 ms of
// presentation and is already over the 16.6 ms budget at p95, while the same
// scene under a 260x72 clip costs 0.12 ms and does not grow with resolution.
// A widget system that repaints everything would have to be retrofitted with
// damage tracking, so damage tracking comes first and widgets are born
// knowing about it.
//
// There is no virtual function here, no node interface and no visitor. A node
// is a rectangle plus a fixed set of appearance fields, and painting is a
// switch-free function over those fields - design.md section 5.15.3 asks for
// exactly that storage shape, and this project has already once written an
// abstraction ahead of its implementation and deleted it. When a node needs
// to draw something this struct cannot express, the struct grows a field or
// the set of node kinds grows an enumerator; neither requires a vtable.
//
// Coordinates are physical device pixels throughout. Layout will introduce
// logical pixels and a DPI transform (design.md section 5.4.9); this layer is
// below that and speaks the framebuffer's units, because damage rectangles
// have to line up with the pixels present() copies.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/graphics/types.h"
#include "drawgui/render/damage.h"
#include "drawgui/render/font_catalog.h"

namespace dg {

// Identifies one node in one tree. A struct rather than a bare index so it
// cannot be passed where a count or a coordinate was meant.
struct NodeId {
  std::uint32_t value = 0;

  friend bool operator==(NodeId, NodeId) = default;
};

// Where a run of text sits inside the node that carries it, horizontally.
enum class TextAlign : std::uint8_t {
  kLeft,
  kCenter,
  kRight,
};

// One run of text, painted inside one node's box.
//
// Text arrived in this struct with sub-step 3's label, and it is the first
// thing a node paints that is not a rectangle. Three consequences that are
// easy to get wrong and are therefore pinned here:
//
//   It is CLIPPED to the node's bounds. Every other field in NodeStyle is
//   naturally contained by the box; a string is not, and a glyph escaping the
//   rectangle its node declared is exactly the pixel damage tracking will
//   never invalidate.
//
//   A node carrying text is NOT clip-atomic, and that is a MEASUREMENT rather
//   than an omission. The obvious assumption was that glyphs behave like
//   rounded rectangles - both are anti-aliased - and it is wrong: under 600
//   random clips that cut the shape, a rounded rectangle differs by 582 pixels
//   at zero slack and text differs by ZERO. Glyphs are rasterized into masks
//   and blitted, so a clip masks the blit instead of changing the coverage.
//   A label may therefore be cut in half by a damage rectangle, which is worth
//   1.57x of the widget demo's damage. `--clip-probe` reprints the table and
//   doc/widgets.md records it.
//
//   The node does NOT size itself to the text. Sub-step 2 excluded intrinsic
//   sizing deliberately (see LayoutTree), so a label is given a box and the
//   text is placed inside it. Shrink-wrapping a label needs intrinsic sizing
//   and the caching design.md section 5.4.6 requires with it.
struct TextStyle {
  std::string text;

  // Which family the caller wants FIRST. An invalid id still draws nothing -
  // "I forgot to set a font" stays a visible failure rather than becoming a
  // silent pick of whichever family sorted first.
  //
  // A codepoint this family does not cover is NOT nothing any more. It goes to
  // the catalog's fallback chain, which is what lets one style render a string
  // mixing Latin, CJK and emoji without the caller naming a family per script.
  // The named family still wins wherever it can draw the codepoint itself.
  FontId font;

  // BCP 47, and the only thing that can resolve Han unification: the same
  // codepoint is a different glyph in zh-Hans, zh-Hant, ja and ko, and no
  // property of the codepoint says which (design.md section 5.13.5). Empty
  // means the generic chain, which is a deliberate "unspecified" rather than a
  // guess at the user's locale.
  std::string language;

  float size = 0.0F;
  Color color;
  TextAlign align = TextAlign::kCenter;

  // Space kept clear at the leading and trailing edge, so that left- and
  // right-aligned text does not sit against the node's own border. Ignored by
  // kCenter, which is already clear of both.
  int inset = 0;

  friend bool operator==(const TextStyle&, const TextStyle&) = default;
};

// Whether a node confines its descendants to its own rectangle.
//
// The two ordinals of the property table's `overflow` (id 29), and the
// property that closes the decision hit_test() below records: painting and hit
// testing must honour ONE rule, and this is it.
//
// WHICH RECTANGLE: the node's own bounds - the border box - together with its
// `radii`. That is a deliberate deviation from CSS, which clips at the padding
// box, and the reason is the invariant the rest of this layer already rests
// on: the rectangle a node declares is exactly the rectangle it paints, and
// the render tree is the layer that does not know what padding is. Clipping at
// the padding box would put a second copy of layout's insets in NodeStyle and
// make a clipping container look different from a non-clipping one along its
// own border, which has nothing to do with overflow. doc/clipping.md records
// the deviation and what would force the other choice.
//
// WHAT IS CLIPPED: the descendants, not the node itself. Everything the node
// paints is inside its bounds by construction - the fill and border are drawn
// to the box, and text carries its own containment clip - so clipping the node
// with its own shape would change nothing except the anti-aliased coverage of
// its own rounded fill, which is a measured way to make pixels move for no
// reason.
enum class Overflow : std::uint8_t {
  kVisible,
  kClip,
};

// Everything a node paints.
//
// Fills, borders and one run of text. Blur is still absent deliberately - it
// is 52% of a frame's raster time and belongs in a budgeted feature rather
// than in the primitive every node carries.
struct NodeStyle {
  Color fill;

  // Rounded corners are not free here, and the reason is measured rather than
  // aesthetic: Skia's anti-aliased rounded-rect rasterization is NOT
  // clip-invariant. Cutting a rounded node with a clip changes the coverage
  // it produces even at pixels well inside the clip, by a level or two, so a
  // damage repaint that clipped one in half would not match a full repaint.
  // Integer-aligned square-cornered rectangles have no such problem - 1800
  // randomized clips produced not one differing pixel.
  //
  // A rounded node is therefore repainted whole or not at all: any damage
  // rectangle touching one grows to contain it. Square corners cost nothing.
  Radii radii;

  Color border_color;

  // Painted entirely inside the node's bounds, not straddling the edge the
  // way a centred stroke would. A node that draws outside the rectangle it
  // declared is the exact bug damage tracking cannot survive: the pixels it
  // touched are not the pixels it said it would touch.
  //
  // FOUR WIDTHS, not one. The property table has always carried
  // border_width_l/t/r/b; until this slice the painter collapsed them to their
  // minimum, which was exact only when the four agreed. A uniform border still
  // takes the original route - one centred stroke inset by half its width - so
  // that every pixel this project has already measured is unchanged, and an
  // unequal one fills the ring between the outer border box and the box the
  // four widths inset it to. Both stay inside the declared rectangle.
  BorderWidths border_width;

  Overflow overflow = Overflow::kVisible;

  // How opaque this node AND ITS DESCENDANTS are, TOGETHER, in 0..1.
  //
  // GROUP OPACITY, NOT PER-OBJECT ALPHA, and the difference is visible rather
  // than pedantic. Per-object alpha draws every operation translucently, so
  // two overlapping children inside the subtree show through each other and
  // the overlap comes out darker than either. Group opacity composites the
  // subtree into an offscreen buffer first, resolves the overlaps there at
  // full opacity, and then draws that one image translucently - so the group
  // fades as a single picture and its internal overlaps do not accumulate.
  // `opacity` is the CSS property, so it is the second one. Per-object alpha
  // is still available and costs nothing: it is the alpha channel of `fill`,
  // `border_color` and `text.color`.
  //
  // examples/02_skia_cpu_gallery has drawn the two side by side since step 2,
  // and examples/08_opacity now draws them out of the same node table with
  // one field different. tests/unit/test_opacity.cpp pins the difference with
  // hand-derived pixel values rather than with a comparison against another
  // run of this code.
  //
  // A VALUE OF 1 COSTS NOTHING. `SkCanvas::saveLayer` allocates an offscreen
  // buffer, so a layer created for a node that is fully opaque would be pure
  // waste - and, worse, a layer nobody can see is a defect no pixel
  // comparison can report, since compositing at alpha 1 is the identity. That
  // is why RepaintStats::layers exists: it is the only place the absence of a
  // needless layer is observable.
  //
  // A VALUE OF 0 PAINTS NOTHING, and the subtree is skipped rather than
  // composited at alpha 0. The two are pixel-identical (a source of alpha
  // zero leaves its destination untouched) and tests/unit/test_opacity.cpp
  // requires them to be.
  //
  // HIT TESTING IGNORES THIS FIELD ENTIRELY, including at 0. That is a
  // decision, not an oversight; RenderTree::hit_test() below records why, and
  // the exhaustive pixel oracle in tests/unit/test_hit_test.cpp checks that
  // painting and hit testing agree about it at every pixel of a faded scene.
  float opacity = 1.0F;

  TextStyle text;
};

// How a repaint turns nodes into draw calls.
//
// design.md section 5.15.2 argues that a retained-mode GUI spends its time
// traversing the tree and recording draw commands rather than rasterizing,
// and that SkPicture caching at repaint boundaries therefore matters more
// than dirty rectangles. That is a claim about this codebase's own cost, and
// it is measurable here: kDirect walks the tree and issues draw calls under
// the clip, kPicture records the whole tree once and replays it, letting
// Skia cull. examples/03_damage_repaint reports both.
enum class PaintMode : std::uint8_t {
  kDirect,
  kPicture,
};

// What one repaint actually did, so that a demo can report cost per unit of
// work rather than a bare millisecond count.
struct RepaintStats {
  std::size_t rects = 0;

  // Under kDirect this is the number of node-into-rectangle draws issued,
  // after culling. Under kPicture the culling happens inside Skia and is not
  // reported back, so it counts the nodes submitted instead - the two are not
  // comparable, and the demo labels them separately for that reason.
  std::size_t nodes_drawn = 0;

  std::size_t nodes_total = 0;

  // Pixels actually repainted, and pixels the dirty nodes asked for. The two
  // differ because a damage rectangle is grown to swallow whole rounded
  // nodes - see NodeStyle::radii - so the gap between them is what that rule
  // costs.
  std::int64_t pixels = 0;
  std::int64_t requested_pixels = 0;

  // True when a kPicture repaint had to re-record the scene, which is the
  // cost that caching is trading against.
  bool recorded = false;

  // Offscreen buffers opened for group opacity, under kDirect.
  //
  // Reported because it is the ONLY observable consequence of a layer that
  // should not exist. Compositing at alpha 1 is the identity, so a painter
  // that opened a layer for every node would produce byte-identical pixels
  // while allocating a buffer per node per damage rectangle - a defect the
  // whole acceptance technique of this project is structurally blind to. The
  // counter is incremented at the `saveLayerAlphaf` call itself, not derived
  // from the styles a second time, so it cannot report a layer that was not
  // opened or miss one that was.
  //
  // Zero under kPicture, where the layers are inside the recording and Skia
  // does not report them back - the same asymmetry `nodes_drawn` already has.
  std::size_t layers = 0;
};

struct TreeSpec {
  PixelSize viewport;

  // The root node's appearance. The root covers the viewport and is painted
  // first, which is what guarantees a damage rectangle starts from a known
  // background instead of from whatever was there last frame.
  NodeStyle background;

  // The fonts every text node in this tree may name. Absent means no node in
  // this tree draws text - which is the case for every scene built before
  // sub-step 3, and is why this is an optional rather than a required
  // argument that existing callers would have to invent a value for.
  std::optional<FontCatalog> fonts;

  std::size_t max_damage_rects = DamageRegion::kDefaultMaxRects;
  PaintMode paint_mode = PaintMode::kDirect;
};

class RenderTree {
 public:
  explicit RenderTree(const TreeSpec& spec);

  RenderTree(RenderTree&&) noexcept;
  RenderTree& operator=(RenderTree&&) noexcept;
  RenderTree(const RenderTree&) = delete;
  RenderTree& operator=(const RenderTree&) = delete;
  ~RenderTree();

  [[nodiscard]] static constexpr NodeId root() { return NodeId{0}; }

  // Appends a child. `bounds` is relative to `parent`'s top-left, so moving a
  // parent moves its children with it. Siblings paint in the order they were
  // added, and a child paints over its parent.
  NodeId add_child(NodeId parent, const PixelRect& bounds, const NodeStyle& style);

  [[nodiscard]] std::size_t node_count() const;
  [[nodiscard]] PixelSize viewport() const;
  [[nodiscard]] const NodeStyle& style(NodeId id) const;

  // The node this one hangs from. The root is its own parent, which is what
  // lets a caller climbing towards the root stop on `id == parent(id)` without
  // a sentinel value that could be confused with a real node.
  [[nodiscard]] NodeId parent(NodeId id) const;

  // The topmost node covering `point`, or nothing when the point is outside
  // the viewport entirely.
  //
  // THIS IS THE EXACT INVERSE OF PAINTING, and that is the whole specification.
  // Painting walks the tree in depth-first pre-order, so the last node to
  // paint a pixel is the one visible at it; hit testing walks children in
  // REVERSE order and takes the first match, which is the same sequence read
  // backwards. Anything else produces the two defects that have no other
  // symptom: a widget you can see but cannot click, and a click landing on
  // something hidden underneath what you aimed at.
  //
  // IT HONOURS `Overflow::kClip`, AND NOTHING ELSE CLIPS. A child is not
  // confined to its parent by being its child: an overflowing child of an
  // ordinary node is painted in the overflow region and is hittable there, and
  // the layout tree reports the overrun as a diagnostic rather than hiding it.
  // Only a node that has asked to clip confines what is under it, and then
  // both readers ask the same question of the same field - painting applies
  // the shape, this applies `clip_contains` to the same shape. Ancestors, not
  // just the immediate parent: the traversal stops at the first clip the point
  // is outside, so a grandparent's clip is honoured for free.
  //
  // The equivalence that makes that checkable rather than arguable: A PIXEL
  // THAT WAS NOT PAINTED BECAUSE IT WAS CLIPPED IS NOT HITTABLE. Both oracles
  // in tests/unit/test_hit_test.cpp are extended to say so at every pixel of a
  // clipped scene.
  //
  // IT IGNORES `NodeStyle::opacity`, INCLUDING AT ZERO, and that is the
  // deliberate other half of the sentence above: a pixel that was not painted
  // because it was faded away IS still hittable. The reason is that opacity
  // has no threshold to put the boundary at. A group at 0.5 is obviously
  // still clickable, a fade is a continuous animation through every value
  // between 1 and 0, and any cut-off would make one frame of that animation
  // silently stop responding - a defect visible only at the moment it
  // happens. CSS draws the line in the same place and for a related reason:
  // `opacity: 0` stays hit-testable while `visibility: hidden` does not,
  // because they are two properties and only one of them is about
  // interaction. drawgui has no `visibility` yet, and when it arrives it is
  // that property - not this one - that removes a node from hit testing.
  //
  // A clip is different in kind, which is why the two rules differ: a clip
  // says the pixels belong to someone else, and something ELSE is visible
  // there to be clicked. A fade leaves the node exactly where it was and puts
  // nothing in its place.
  [[nodiscard]] std::optional<NodeId> hit_test(PixelPoint point) const;

  // Where the node sits relative to its parent, and where it sits in the
  // framebuffer. Both are answered rather than recomputed by callers, because
  // a damage rectangle is only correct in absolute coordinates.
  [[nodiscard]] PixelRect local_bounds(NodeId id) const;
  [[nodiscard]] PixelRect absolute_bounds(NodeId id) const;

  void set_style(NodeId id, const NodeStyle& style);
  void set_fill(NodeId id, Color fill);

  // Damages nothing when the text is unchanged. Every other setter here
  // damages unconditionally, which is right for them because comparing two
  // colours is not obviously cheaper than repainting a small node - but a
  // label re-asserting the string it already shows is the common case for an
  // interaction loop that refreshes a widget on every state change, and
  // repainting for it would make the damage a function of the loop rather
  // than of the change.
  void set_text(NodeId id, const TextStyle& text);

  // Both damage the node's old subtree extent and its new one. Damaging only
  // the new extent leaves the pixels it vacated showing last frame's paint,
  // which is the classic stale-pixel corruption partial repaint is prone to.
  void set_local_bounds(NodeId id, const PixelRect& bounds);
  void set_local_origin(NodeId id, int x, int y);

  // Shifts `id`'s CHILDREN - not `id` itself - by `offset`, the same way
  // margin is applied by a parent rather than by the child carrying it
  // (design.md section 5.9.4) and a clip confines descendants rather than the
  // clipping node's own paint (doc/clipping.md section 2). A child's
  // `local_bounds()` never changes; only the absolute position `reposition()`
  // derives from it does, exactly as it already does for an ordinary move.
  //
  // RUNTIME STATE, NOT A PROPERTY. `props/drawgui.props.toml` has no entry
  // for this, on purpose: an offset is inherently the thing a wheel or a drag
  // mutates every frame, which is the same shape hover and press already are
  // in `WidgetSet` - interaction state that outlives one event, not a
  // declarative style a caller writes once. `doc/scrolling.md` section 2 is
  // the argument in full.
  //
  // NOT CLAMPED HERE. This tree has no notion of a scrollable child's full
  // content extent - it only ever sees `local_bounds()`, which does not
  // change - so an offset past what there is to scroll is the caller's
  // mistake to prevent, exactly as this tree does not know what padding is
  // (doc/clipping.md section 2). `WidgetSet::scroll_by()` is where the
  // clamp lives.
  //
  // Damages the old positions of every descendant, then the new ones - the
  // same damage-then-move shape `set_local_bounds()` already has, and NO
  // layout pass runs: `reposition()` only recomputes `absolute` and
  // `clip_bounds`, it never calls `measure()`, so a scroll costs a repaint,
  // never a relayout. `doc/scrolling.md` section 4 measures this.
  void set_scroll_offset(NodeId id, PixelPoint offset);
  [[nodiscard]] PixelPoint scroll_offset(NodeId id) const;

  void resize(PixelSize viewport);
  void set_paint_mode(PaintMode mode);
  [[nodiscard]] PaintMode paint_mode() const;

  [[nodiscard]] const DamageRegion& damage() const;

  // Regions covered by the most recent repaint. This is what present() wants,
  // and holding it here rather than returning a vector keeps a per-frame heap
  // allocation out of the loop being timed.
  [[nodiscard]] const DamageRegion& painted() const;

  // For pixels inside the viewport that the tree does not own - an overlay
  // drawn straight onto the surface after repaint(), for instance. Without
  // it such an overlay would never be presented.
  void damage_rect(const PixelRect& rect);
  void damage_all();

  // Redraws only the damaged regions, then moves the damage to painted().
  // `surface` must be the size of the viewport; anything outside it is
  // clipped away rather than trusted.
  RepaintStats repaint(RasterSurface& surface);

  // Redraws everything, discarding accumulated damage. This is the reference
  // the damage path is verified against, and it is what the demo switches to
  // when asked to show what damage tracking is worth.
  RepaintStats repaint_full(RasterSurface& surface);

 private:
  struct Impl;

  std::unique_ptr<Impl> impl_;
};

}  // namespace dg
