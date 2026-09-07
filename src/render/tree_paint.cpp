#include <cstdint>
#include <utility>

#include "include/core/SkCanvas.h"
#include "include/core/SkPicture.h"
#include "include/core/SkPictureRecorder.h"

#include "render/skia_paint.h"
#include "render/tree_impl.h"

namespace dg {
namespace {

// A clip-atomic node needs one pixel of slack around it, not merely
// containment. Measured: a clip laid exactly on a rounded rectangle's bounds
// still changes its coverage, while one pixel of margin makes the result
// bit-identical - 3200 randomized clips, zero differing pixels at a slack of
// one, 19544 at a slack of zero. Skia's analytic anti-aliasing evidently
// walks a row and a column past the geometry.
constexpr int kAntiAliasSlack = 1;

}  // namespace

PixelRect RenderTree::Impl::expand_to_whole_nodes(PixelRect region) const {
  bool grew = true;
  while (grew) {
    grew = false;
    for (const Node& node : nodes) {
      const PixelRect halo = node.absolute.inflated_by(kAntiAliasSlack);
      if (node.clip_atomic && intersects(node.absolute, region) && !contains(region, halo)) {
        region = join(region, halo);
        grew = true;
      }
    }
  }
  return region;
}

DamageRegion RenderTree::Impl::expand(const DamageRegion& raw) const {
  DamageRegion grown{max_damage_rects};
  for (const PixelRect& rect : raw.rects()) {
    grown.add(expand_to_whole_nodes(rect));
  }
  // Merging two expanded rectangles - either because they now overlap or
  // because the cap forced it - can produce one that cuts a node again.
  for (bool stable = false; !stable;) {
    stable = true;
    DamageRegion next{max_damage_rects};
    for (const PixelRect& rect : grown.rects()) {
      const PixelRect expanded = expand_to_whole_nodes(rect);
      stable = stable && expanded == rect;
      next.add(expanded);
    }
    grown = std::move(next);
  }
  return grown;
}

void RenderTree::Impl::record() {
  SkPictureRecorder recorder;
  SkCanvas* canvas = recorder.beginRecording(
      detail::to_sk_rect(PixelRect{0, 0, viewport.width, viewport.height}));
  for (const std::uint32_t index : paint_order) {
    detail::paint_node(*canvas, nodes[index].absolute, nodes[index].style);
  }
  picture = recorder.finishRecordingAsPicture();
  picture_stale = false;
}

void RenderTree::Impl::paint_region(SkCanvas& canvas, const PixelRect& region,
                                    RepaintStats& stats) {
  ++stats.rects;
  stats.pixels += region.area();

  canvas.save();
  // Anti-aliasing off: the clip is a pixel mask, not a shape, and an
  // anti-aliased edge would blend the boundary of a damage rectangle
  // differently from the same pixels in a full repaint.
  canvas.clipRect(detail::to_sk_rect(region), false);
  if (paint_mode == PaintMode::kPicture && picture) {
    canvas.drawPicture(picture.get());
    stats.nodes_drawn += nodes.size();
  } else {
    for (const std::uint32_t index : paint_order) {
      const Node& node = nodes[index];
      if (intersects(node.absolute, region)) {
        detail::paint_node(canvas, node.absolute, node.style);
        ++stats.nodes_drawn;
      }
    }
  }
  canvas.restore();
}

RepaintStats RenderTree::Impl::paint(RasterSurface& surface, const DamageRegion& region) {
  RepaintStats stats;
  stats.nodes_total = nodes.size();
  if (paint_mode == PaintMode::kPicture && picture_stale) {
    record();
    stats.recorded = true;
  }

  const PixelRect surface_bounds{0, 0, surface.width(), surface.height()};
  SkCanvas* canvas = surface.sk_canvas();
  for (const PixelRect& requested : region.rects()) {
    const PixelRect clipped = intersect(requested, surface_bounds);
    if (!clipped.is_empty()) {
      paint_region(*canvas, clipped, stats);
    }
  }
  return stats;
}

}  // namespace dg
