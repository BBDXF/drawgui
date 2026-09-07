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
  // IT DOES NOT CLIP A CHILD TO ITS PARENT, and that is a decision rather than
  // an omission. paint_node() does not clip either - a child whose box runs
  // past its parent's is drawn in the overflow region, and the layout tree
  // reports the overrun as a diagnostic instead of hiding it. Hit testing that
  // clipped would therefore disagree with the screen in exactly the region the
  // screen is already telling the user is interactive. When a scrolling
  // container introduces a real clip, the clip becomes a property of the node,
  // painting honours it, and hit testing honours the same property - one rule,
  // read by both. doc/widgets.md records the reasoning.
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
