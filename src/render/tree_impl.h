// The render tree's internal representation, shared by the two translation
// units that implement it.
//
// render_tree.cpp owns tree structure and dirty marking; tree_paint.cpp owns
// turning damage into draw calls. They are separate files because those are
// separate jobs, and together they exceeded the size at which one file stays
// reviewable. Neither is an interface: this header lives under src/, nothing
// outside the library can include it, and it exists because two translation
// units share a struct rather than because a second implementation is
// anticipated.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/render/render_tree.h"

#include "include/core/SkRefCnt.h"

class SkCanvas;
class SkPicture;

namespace dg {

// Children hold indices rather than pointers: nothing is ever removed, so an
// index stays valid for the life of the tree, and the whole tree is one
// contiguous allocation that a repaint walks in order.
struct Node {
  PixelRect local;
  PixelRect absolute;
  NodeStyle style;
  std::uint32_t parent = 0;
  std::vector<std::uint32_t> children;

  // True when this node must be rasterized whole or not at all, because
  // clipping it partway changes the pixels it produces even inside the clip.
  // Measured, not assumed: over 1800 randomized clips, Skia's anti-aliased
  // ROUNDED rectangles differ by a level or two of coverage when the clip
  // cuts them, while integer-aligned axis-aligned rectangles - fills and
  // half-pixel-inset strokes alike - were bit-identical every time. So a node
  // with square corners can be clipped anywhere, and a node with rounded ones
  // forces the damage rectangle to grow around it. doc/damage-repaint.md has
  // the measurement.
  bool clip_atomic = false;

  // The rectangle every ancestor clip together confines this node to, or
  // nothing when no ancestor clips.
  //
  // ABSENT RATHER THAN "THE VIEWPORT", so that a tree containing no clip
  // behaves exactly as it did before this field existed - every damage
  // rectangle, every reported area and every painted pixel. A sentinel of the
  // viewport would silently start intersecting damage that used to be allowed
  // to run past the edge, which is a change nobody asked for riding along with
  // one that was.
  //
  // For a ROUNDED ancestor clip this is the bounding rectangle, not the
  // rounded shape. That is conservative in the only direction that is safe:
  // it may keep a corner pixel that the curve removes, so damage is a
  // superset of what changed and painting is still exact, while the shape
  // itself is applied by the canvas and by clip_contains().
  std::optional<PixelRect> clip_bounds;

  // Where this node may put pixels: its own box, minus whatever its ancestors
  // clip away. Empty means it is entirely hidden and paints nothing.
  [[nodiscard]] PixelRect visible_bounds() const {
    return clip_bounds.has_value() ? intersect(absolute, *clip_bounds) : absolute;
  }
};

[[nodiscard]] bool clips_atomically(const NodeStyle& style);

// Whether this node confines its descendants. One reader would be a private
// rule; this has three - painting, damage and hit testing - which is the
// point of the slice.
[[nodiscard]] constexpr bool clips_subtree(const NodeStyle& style) {
  return style.overflow == Overflow::kClip;
}

// Whether this node's subtree has to be composited offscreen before it is
// drawn, which is what makes `opacity` a GROUP opacity rather than a per-draw
// alpha. False at 1, which is the whole point: `saveLayer` allocates, and
// compositing at alpha 1 is the identity, so a layer opened here would be
// invisible waste.
//
// False at 0 as well, and for a different reason: at 0 the subtree cannot
// change a pixel at all, so painting skips it rather than composites it into
// a buffer that is then multiplied away. `paints_nothing` is that test, kept
// separate because the two answers are not opposites - a node at 1 needs no
// layer AND paints normally.
[[nodiscard]] constexpr bool needs_layer(const NodeStyle& style) {
  return style.opacity < 1.0F && style.opacity > 0.0F;
}

[[nodiscard]] constexpr bool paints_nothing(const NodeStyle& style) {
  return !(style.opacity > 0.0F);
}

// Everything one traversal of the tree needs, so that painting takes two
// arguments rather than six. `region` absent means "paint everything", which
// is what recording a picture wants and what culling against a damage
// rectangle must not do.
struct PaintPass {
  SkCanvas* canvas = nullptr;
  const FontCatalog* fonts = nullptr;
  std::optional<PixelRect> region;
  RepaintStats* stats = nullptr;
};

struct RenderTree::Impl {
  PixelSize viewport;
  std::optional<FontCatalog> fonts;
  PaintMode paint_mode = PaintMode::kDirect;
  std::size_t max_damage_rects = DamageRegion::kDefaultMaxRects;
  std::vector<Node> nodes;
  DamageRegion damage;
  DamageRegion painted;
  sk_sp<SkPicture> picture;
  bool picture_stale = true;

  // Depth-first, children in REVERSE order, first match wins. That order is
  // the reverse of paint_subtree()'s, which is what makes hit testing
  // agree with what the screen shows. Returns the node count when nothing is
  // hit, so the caller has one out-of-range value to test rather than a
  // node index that could be mistaken for the root.
  [[nodiscard]] std::uint32_t hit_test(PixelPoint point) const;

  void reposition(std::uint32_t root_index);
  void damage_subtree(std::uint32_t root_index);
  void invalidate(std::uint32_t index);
  void retire_damage(DamageRegion just_painted);

  // Grows `region` until every clip-atomic node it touches is inside it.
  // Monotone and bounded by the union of the node bounds, so it terminates;
  // repeated because growing to swallow one node can reach another.
  [[nodiscard]] PixelRect expand_to_whole_nodes(PixelRect region) const;
  [[nodiscard]] DamageRegion expand(const DamageRegion& raw) const;

  // Every pixel the subtree rooted at `index` may paint, which is the union
  // of each node's VISIBLE bounds - so an ancestor clip shrinks it and a
  // child overflowing its parent grows it.
  //
  // Computed on demand rather than cached on the node, for the reason
  // hit_test.cpp already records against the same optimisation: a stored
  // subtree extent is a second copy of the geometry carrying an invalidation
  // obligation on every move, resize and insertion, and this project has
  // already deleted one speculative structure. It is asked for only by nodes
  // that actually open a layer.
  [[nodiscard]] PixelRect subtree_extent(std::uint32_t index) const;

  void record();
  void paint_subtree(const PaintPass& pass, std::uint32_t index) const;
  void paint_node_and_children(const PaintPass& pass, std::uint32_t index) const;
  void paint_region(SkCanvas& canvas, const PixelRect& region, RepaintStats& stats);
  RepaintStats paint(RasterSurface& surface, const DamageRegion& region);
};

}  // namespace dg
