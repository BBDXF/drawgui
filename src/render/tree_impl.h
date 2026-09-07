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
};

[[nodiscard]] bool clips_atomically(const NodeStyle& style);

struct RenderTree::Impl {
  PixelSize viewport;
  PaintMode paint_mode = PaintMode::kDirect;
  std::size_t max_damage_rects = DamageRegion::kDefaultMaxRects;
  std::vector<Node> nodes;
  std::vector<std::uint32_t> paint_order;
  DamageRegion damage;
  DamageRegion painted;
  sk_sp<SkPicture> picture;
  bool picture_stale = true;

  // Depth-first pre-order: a parent paints before its children, and siblings
  // paint in the order they were added. That order IS the z-order, and a
  // damage repaint must honour it or a node above the changed one gets
  // clipped through - the characteristic partial-repaint artifact.
  void rebuild_paint_order();

  void reposition(std::uint32_t root_index);
  void damage_subtree(std::uint32_t root_index);
  void invalidate(std::uint32_t index);
  void retire_damage(DamageRegion just_painted);

  // Grows `region` until every clip-atomic node it touches is inside it.
  // Monotone and bounded by the union of the node bounds, so it terminates;
  // repeated because growing to swallow one node can reach another.
  [[nodiscard]] PixelRect expand_to_whole_nodes(PixelRect region) const;
  [[nodiscard]] DamageRegion expand(const DamageRegion& raw) const;

  void record();
  void paint_region(SkCanvas& canvas, const PixelRect& region, RepaintStats& stats);
  RepaintStats paint(RasterSurface& surface, const DamageRegion& region);
};

}  // namespace dg
