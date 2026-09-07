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
//
// A ROUNDED CLIP OBEYS THE SAME NUMBER, and that was measured rather than
// assumed - it is the question this slice was warned about. Reference clip
// stack {rrect} against subject {damage rect, rrect}, compared inside the
// damage rect, 600 randomized damage rectangles per cell:
//
//     clip                     slack 0    slack 1    slack 2
//     square clipRect(aa=0)      0          0          0
//     rounded clipRRect r=6     18          0          0
//     rounded clipRRect r=18   204          0          0
//     nested rounded r=6        86          0          0
//     nested rounded r=18      476          0          0
//
// So one pixel is necessary and sufficient for a rounded clip too, nesting
// included, and NO NEW RULE IS NEEDED: a rounded clipper has non-zero radii,
// which already makes clips_atomically() true for it, so expand() already
// grows any damage rectangle touching it to contain it and its halo. Its
// descendants are confined to its box, so they are inside that rectangle as
// well and the damage clip never cuts one. doc/clipping.md records the cost.
constexpr int kAntiAliasSlack = 1;

}  // namespace

void RenderTree::Impl::paint_subtree(const PaintPass& pass, std::uint32_t index) const {
  const Node& node = nodes[index];
  const PixelRect visible = node.visible_bounds();

  const bool wanted = !pass.region.has_value() || intersects(visible, *pass.region);
  if (wanted) {
    detail::paint_node(*pass.canvas, node.absolute, node.style, pass.fonts);
    ++pass.stats->nodes_drawn;
  }

  if (node.children.empty()) {
    return;
  }

  const bool clipping = clips_subtree(node.style);
  if (clipping) {
    // A clip with no area removes everything under it, so the subtree is not
    // traversed at all rather than traversed under an empty clip.
    //
    // MEASURED TO BE INERT TODAY, and kept anyway with the reason written
    // down rather than left to be rediscovered. Deleting it passes every test
    // in this project, because both of the things it looks like it is
    // protecting are already guaranteed elsewhere: PixelRect::from_edges
    // cannot produce an inverted rectangle, so Skia never sees one; and
    // fit_radii() returns zero radii for an empty box, so apply_clip() takes
    // clipRect rather than handing SkRRect a degenerate one. An empty clip
    // then removes the subtree by itself and the pixels are identical.
    //
    // What it buys is that the traversal STOPS, which is the only thing that
    // will still be true when a clip shape arrives that is not derived from a
    // PixelRect. tests/unit/test_clip.cpp asserts the guarantee at its real
    // source instead - fit_radii of an empty rectangle is zero.
    if (node.absolute.is_empty() || visible.is_empty()) {
      return;
    }
    if (pass.region.has_value() && !intersects(visible, *pass.region)) {
      return;
    }
    pass.canvas->save();
    detail::apply_clip(*pass.canvas, node.absolute, node.style.radii);
  }

  for (const std::uint32_t child : node.children) {
    paint_subtree(pass, child);
  }

  if (clipping) {
    pass.canvas->restore();
  }
}

namespace {

// A node an ancestor clip removes entirely paints nothing, so nothing about
// it can be cut by a damage rectangle and it must not drag one wider. Without
// this a rounded node scrolled far outside its clipping container would keep
// forcing its own area into every damage rectangle that reached it.
[[nodiscard]] bool can_be_cut(const Node& node) {
  return node.clip_atomic && !node.visible_bounds().is_empty();
}

}  // namespace

PixelRect RenderTree::Impl::expand_to_whole_nodes(PixelRect region) const {
  bool grew = true;
  while (grew) {
    grew = false;
    for (const Node& node : nodes) {
      const PixelRect halo = node.absolute.inflated_by(kAntiAliasSlack);
      if (can_be_cut(node) && intersects(node.absolute, region) && !contains(region, halo)) {
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
  RepaintStats discarded;
  PaintPass pass;
  pass.canvas = canvas;
  pass.fonts = fonts.has_value() ? &*fonts : nullptr;
  pass.stats = &discarded;
  paint_subtree(pass, 0);
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

  // Switched rather than branched, so that a third paint mode is a compile
  // error here instead of quietly taking the direct path.
  switch (paint_mode) {
    case PaintMode::kPicture:
      if (picture) {
        canvas.drawPicture(picture.get());
        stats.nodes_drawn += nodes.size();
        break;
      }
      // Recording can fail; drawing nothing would be worse than traversing.
      [[fallthrough]];
    case PaintMode::kDirect: {
      // Depth-first, and that is what a clip costs structurally: a clip is a
      // canvas state that has to be pushed before a subtree and popped after
      // it, so the flat pre-order list this used to walk - which had
      // forgotten where the subtrees were - is gone rather than kept beside
      // a traversal that no longer reads it. The order visited is identical,
      // so the z-order is unchanged.
      PaintPass pass;
      pass.canvas = &canvas;
      pass.fonts = fonts.has_value() ? &*fonts : nullptr;
      pass.region = region;
      pass.stats = &stats;
      paint_subtree(pass, 0);
      break;
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
