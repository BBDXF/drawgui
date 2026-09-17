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
#include <vector>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/graphics/types.h"
#include "drawgui/render/damage.h"
#include "drawgui/render/font_catalog.h"
#include "drawgui/render/image_catalog.h"

namespace dg {

// Identifies one node in one tree. A struct rather than a bare index so it
// cannot be passed where a count or a coordinate was meant.
//
// `{index, generation}`, not a bare index - doc/widgets.md's own recorded
// choice from years before removal existed: "a generation counter in NodeId
// ... the one that also catches use-after-remove in the C ABI, where a host
// can hold an id indefinitely". `AnimHandle{index, generation}`
// (animation_engine.h) is the working precedent this mirrors exactly, and
// for the identical reason: a removed node's slot IS reused (RenderTree
// never compacts, but it DOES recycle a tombstoned index for a future
// add_child(), the same free-list shape AnimationEngine's own
// allocate_slot()/free_slot() already have), which reopens the ABA problem
// a bare index cannot detect. `index` is the field's name on purpose,
// RENAMED from the field's previous name (`value`) rather than added
// alongside it: the rename is what forces every existing `.value` access on
// a NodeId to fail to compile, walking a reviewer through every call site
// this change touches instead of letting a stale one compile silently
// against two fields where it only meant one.
struct NodeId {
  std::uint32_t index = 0;
  std::uint32_t generation = 0;

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

  // Routes this run through SkParagraph instead of the plain SkFont path
  // above when true - 7-2's multi-line/shaping/BiDi/CJK substrate
  // (doc/text-layout.md). FALSE BY DEFAULT, and that default is what keeps
  // every scene built through 7-1 byte-for-byte unaffected: paint_text()
  // only calls into skia_paint.cpp's paragraph path when a caller opts in,
  // so the golden `drawgui_render_png` hash cannot move by this field
  // existing. A wrapping node's HEIGHT is not derived here - doc/text-
  // layout.md section 2 is the argument for why that stays the caller's
  // job (a `dg::Paragraph::build()` measurement before the node is
  // constructed), the same shape design.md section 5.10.3 already forces
  // on `ImageStyle`: a box must know its own size before its content is
  // resolved, never the other way around.
  bool wrap = false;

  // Read only when `wrap` is true. Zero means unlimited lines; a positive
  // value truncates with an ellipsis appended to the last kept line
  // (SkParagraph's own `ParagraphStyle::setMaxLines`/`setEllipsis`).
  int max_lines = 0;

  friend bool operator==(const TextStyle&, const TextStyle&) = default;
};

// How a decoded bitmap is scaled to fill the box it was given.
//
// design.md section 5.10.1 names ImageSource but never enumerates fit modes;
// this is this slice's own decision, scoped to the four CSS `object-fit`
// values that examples/13_image can actually exercise with a hand-derived
// pixel oracle rather than to every mode a later slice might invent. All four
// keep the box's own SIZE untouched - fit only changes what is drawn INSIDE
// it, never what LayoutTree computed, which is the whole point of settling
// size before decode (ImageStyle below).
enum class ImageFit : std::uint8_t {
  // Stretches the source to exactly the node's bounds, independently on each
  // axis. Ignores the source's own aspect ratio, which is what makes this the
  // cheapest mode to verify: at a 1:1 node/source size it is pixel-identical
  // to drawing the bitmap unscaled.
  kFill,

  // Scales uniformly (one factor for both axes) so the WHOLE source fits
  // inside the bounds, centred, without stretching - letterboxed on the axis
  // that has spare room, never cropped.
  kContain,

  // Scales uniformly so the source COVERS the whole bounds, centred, without
  // stretching - cropped on the axis that overshoots, never letterboxed.
  kCover,

  // No scaling at all: drawn at its own decoded pixel size, centred. The
  // mode a caller reaches for when the source was already prepared at
  // exactly the size it should appear.
  kNone,
};

// One decoded bitmap, painted inside the node that carries it.
//
// THIS IS CONTENT, NOT A NEW NODE KIND - the same shape TextStyle already is
// one field over, and the argument is the identical one doc/clipping.md
// section 6 and doc/compositing.md section 5 already made for `overflow` and
// `opacity`: a clip, a fade and now an image all need painting, hit testing
// and (for image, uniquely) layout to agree on ONE rule, and the only shape
// that guarantees agreement is a field every reader already sees, not a
// second node kind a reader would have to be taught about separately.
// doc/image.md records the decision in full, including why `Image` being
// named in design.md's MVP-8 does not automatically make it a `WidgetKind`
// the way `Box`/`Row`/`Column` are correctly not ones either.
struct ImageStyle {
  // Which ImageCatalog entry to paint. Invalid (the default) means either
  // "no image at all" or "requested but not decoded yet" - see `placeholder`
  // below for how those two are told apart, and design.md section 5.10.3 for
  // why a synchronous decode still needs the distinction: nothing about a
  // future async decode would have to change this field or its meaning, only
  // WHEN it flips from invalid to valid.
  ImageId source;

  ImageFit fit = ImageFit::kFill;

  // Painted in place of a real image whenever `source` is invalid AND this
  // colour is not fully transparent - design.md section 5.10.3's "未就绪时
  // 绘制主题 token 指定的占位色，不留空洞" ("paint the theme-token placeholder
  // colour while not ready, never a hole"), with a plain configurable colour
  // standing in for the theme-token system this project does not have
  // (design.md section 5.7 is out of every phase through this one). A fully
  // transparent placeholder (the default) is how an ordinary node with no
  // image at all stays inert: `carries_image()` below is false for it, so
  // every scene built before this slice is unaffected byte for byte.
  Color placeholder;

  friend bool operator==(const ImageStyle&, const ImageStyle&) = default;
};

// Whether this node has image semantics at all - a real source, or a
// placeholder standing in for one not yet assigned. False for a
// default-constructed ImageStyle, which is what every node had implicitly
// before this field existed, so an ordinary Box/Panel/Label is unaffected.
//
// Read by three places, and reading the same predicate is what keeps them
// from disagreeing the way three private copies of "does this count as an
// image" could: LayoutTree::Impl::measure() (the size-before-decode
// diagnostic, box_layout.cpp), skia_paint.cpp's paint_node() (whether to call
// paint_image() at all) and doc/image.md's own description of the rule.
[[nodiscard]] constexpr bool carries_image(const ImageStyle& image) {
  return image.source.is_valid() || image.placeholder.alpha() != 0;
}

// One colour stop of a linear gradient, at `offset` along the gradient axis
// (0 = the line's start, 1 = its end). `LinearGradientStyle::stops` is an
// ABI-contract ORDER exactly the way an enum property's `values` list is
// (prop_ids.generated.h) - a consumer paints the list in the order it was
// given, not a re-sorted one - except a gradient descriptor never crosses the
// dispatch that orders those, because it is not a scalar (see below).
struct GradientStop {
  float offset = 0.0F;
  Color color;

  friend bool operator==(const GradientStop&, const GradientStop&) = default;
};

// A linear gradient fill, replacing `NodeStyle::fill` for the border box it
// paints rather than layering over it - design.md section 5.9.5 lists
// `background_gradient` as its own decoration layer, and this slice keeps the
// two mutually exclusive rather than building the compositing this project
// has never needed for two background layers at once (section 5.9.5 also
// states "不支持多重背景" - no multiple backgrounds - for the analogous
// background_image case).
//
// LINEAR ONLY. design.md section 5.9.6 names radial and sweep too;
// doc/complex-properties.md declines both by name - a linear axis is the one
// case whose damage story is trivial (paints strictly inside the node's own
// box, exactly like a flat fill), and CSS's own linear-gradient direction
// convention (`angle_deg`: 0 = left-to-right, 90 = top-to-bottom, clockwise)
// is what `angle_deg` spells out, matching the CSS-name-means-CSS-behaviour
// rule (design.md section 5.9.3).
//
// NOT A SCALAR PropValue - a stop list has no fixed size, so it cannot fit
// PropType's tagged union any more than an ImageId does for a different
// reason. It travels through the dedicated `dg::set_gradient()` channel
// (design.md section 5.9.5), not through `dg::set_prop()`.
struct LinearGradientStyle {
  float angle_deg = 0.0F;
  std::vector<GradientStop> stops;

  friend bool operator==(const LinearGradientStyle&, const LinearGradientStyle&) = default;
};

// An outer drop shadow, painted BEHIND everything else the node paints -
// design.md section 5.9.5's decoration order puts `shadow` first, underneath
// the background colour/gradient/border.
//
// BUDGETED, not free. doc/cpu-raster-findings.md measured blur and drop
// shadow at 52% of a dense scene's raster time - the single most expensive
// thing this engine can be asked to paint - so `blur_radius` is capped by the
// dedicated setter (`dg::set_shadow()`, doc/complex-properties.md) rather than
// left open, and it is why this is a per-node OPT-IN field (`std::optional`,
// absent by default) rather than a primitive every node carries the cost of.
//
// PAINTS OUTSIDE THE NODE'S DECLARED BOUNDS BY DEFINITION - the one property
// in this table that does, alongside `transform` (still not implemented).
// `render_tree.cpp`'s `shadow_reach()`/`declared_paint_bounds()` are what
// teach damage tracking about the outset; doc/complex-properties.md section
// 3 is the argument for why that is safe rather than a hazard.
struct ShadowStyle {
  float offset_x = 0.0F;
  float offset_y = 0.0F;

  // Gaussian sigma, not a CSS blur-radius (Skia's own unit; CSS multiplies by
  // roughly 2 to get a comparable visual size). Budgeted - see above.
  float blur_radius = 0.0F;

  // Grows the shadow's own shape before blurring, CSS's `spread` term.
  // Negative shrinks it. Applied via `SkRect::makeOutset`, so a spread large
  // enough to invert the rectangle collapses it to nothing rather than
  // producing a negative-area shape.
  float spread = 0.0F;

  Color color;

  friend bool operator==(const ShadowStyle&, const ShadowStyle&) = default;
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
// Fills, borders, one run of text, one decoded image, one linear gradient and
// one budgeted drop shadow. Blur - the shadow's own blur, and any general
// background blur - is deliberately still not an unconditional primitive: it
// is 52% of a frame's raster time (doc/cpu-raster-findings.md), so it exists
// here only behind an opt-in `std::optional` field with a capped radius
// (`ShadowStyle::blur_radius`, validated by `dg::set_shadow()`), never as
// something every node pays for by default.
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

  // Painted after `fill`/`radii` and before the border, so a border frames an
  // image the same way it already frames a flat colour, and after any of
  // this node's positioned children (measure_leaf lays those out at the
  // content origin; paint order there is unaffected). Clipped to `radii`
  // exactly like the fill is - src/render/skia_paint.cpp's paint_image()
  // reuses apply_clip() rather than a second radius arithmetic.
  ImageStyle image;

  // Painted INSTEAD OF `fill` when present with at least two stops, in the
  // node's own paint step (before `image`, matching design.md section
  // 5.9.5's decoration order: colour/gradient share one slot). Absent by
  // default, so every scene built before this slice is byte-for-byte
  // unaffected - `set_prop()`'s scalar `background_color` path is completely
  // untouched by this field's existence. Set only through the dedicated
  // `dg::set_gradient()` channel (doc/complex-properties.md), never through
  // `dg::set_prop()` - a multi-stop list does not fit `PropValue`'s scalar
  // tagged union any more than an `ImageId` does for a different reason.
  std::optional<LinearGradientStyle> background_gradient;

  // Painted BEFORE everything else (`fill`/`background_gradient`/`image`/
  // border), extending OUTSIDE the node's own declared bounds by definition -
  // design.md section 5.9.5's decoration order puts `shadow` first for
  // exactly this reason: it sits behind and around the shape, not on top of
  // it. Absent by default; `dg::set_shadow()` is the only writer
  // (doc/complex-properties.md), and it caps `blur_radius` because
  // doc/cpu-raster-findings.md measured blur at 52% of a frame's raster time.
  //
  // TEACHES DAMAGE TRACKING A NEW FACT: a node's painted pixels are no longer
  // always contained in its own `absolute` rectangle. `shadow_reach()` and
  // `declared_paint_bounds()` (src/render/tree_impl.h) are the two places
  // that know it, and `Node::visible_bounds()` is where the outset actually
  // reaches every reader (damage, the opacity layer's subtree extent, the
  // "wanted" test in `paint_node_and_children`) through one shared rule
  // rather than a private one - the same argument doc/clipping.md section 6
  // and doc/compositing.md section 5 already made for `overflow` and
  // `opacity`.
  std::optional<ShadowStyle> shadow;
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

  // The decoded images every image node in this tree may name - the same
  // "absent means no consumer" shape `fonts` already has, for the identical
  // reason: every tree built before this slice draws no image, and an
  // optional is what leaves that unchanged rather than inventing an empty
  // catalog nobody asked for.
  std::optional<ImageCatalog> images;

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

  // `id`'s children, in paint order (the same order add_child() was called
  // in) - the storage tree_impl.h's Node::children already keeps, exposed
  // rather than re-derived by scanning every node for parent() == id. Added
  // for 6-3's dg_dump_layout_tree, whose whole job is to walk a subtree; no
  // earlier slice needed a public "list my children" primitive because
  // painting and hit testing both walk from the OTHER direction (parent
  // pushing its own bounds down), so this is new surface rather than an
  // existing private detail promoted to public.
  [[nodiscard]] std::vector<NodeId> children(NodeId id) const;

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

  // Same "damages nothing when unchanged" shape as set_text(), for the same
  // reason: this is the call design.md section 5.10.3's async decode would
  // land through (source id changes once the future thread pool's result
  // arrives), and swapping which decoded bitmap paints here must not disturb
  // anything set_local_bounds()/layout() already decided - proven in
  // tests/unit/test_image.cpp by re-laying-out after a swap and checking
  // LayoutStats::nodes_relaid_out stayed zero.
  void set_image(NodeId id, const ImageStyle& image);

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
