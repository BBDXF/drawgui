#include "clip_check.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <vector>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/graphics/types.h"
#include "drawgui/render/render_tree.h"

#include "clip_scene.h"

namespace clip_check {
namespace {

using dg::NodeId;
using dg::PixelPoint;
using dg::PixelRect;
using dg::PixelSize;
using dg::RenderTree;

// Wide enough that the panels are roomy, narrow enough that the fixed-width
// children overrun them by very different amounts.
constexpr int kWidths[] = {1500, 1180, 960, 820, 700, 620};
constexpr int kHeight = 520;

// A distinct opaque colour per node, recoverable from one channel, exactly as
// the unit test's pixel oracle does it. Green and blue are fixed and non-zero
// so an unpainted surface cannot be mistaken for node 0.
dg::Color colour_for(std::uint32_t index) {
  return dg::Color::rgba(static_cast<std::uint8_t>(index + 1), 0x40, 0x80);
}

// Repaints every node in a flat, unique colour, so that the colour at a pixel
// names the node that reached the screen there.
//
// The BORDERS AND RADII ARE STRIPPED for this pass, and that is not a
// convenience: an anti-aliased border blends two nodes' colours and the
// oracle would be reading a colour belonging to neither. What is kept is the
// geometry and the OVERFLOW, which is the thing under test - so the rounded
// panel keeps its rounded clip, and the band of blended pixels along its
// curve is what the caller counts and bounds.
void recolour(RenderTree& tree) {
  for (std::uint32_t index = 0; index < tree.node_count(); ++index) {
    const NodeId id{index};
    dg::NodeStyle style = tree.style(id);
    style.fill = colour_for(index);
    style.border_width = dg::BorderWidths{};
    style.text = dg::TextStyle{};
    tree.set_style(id, style);
  }
}

// The visible node at `point` by painting's own definition - the last node in
// pre-order to cover it, among those no ancestor clip removes.
//
// Derived from parent() and absolute_bounds(), which are the two things the
// tree reports about itself, and from this file's own containment test. It
// shares no code with hit_test().
std::uint32_t topmost(const RenderTree& tree, PixelPoint point, std::uint32_t miss,
                      bool honour_clips) {
  std::uint32_t found = miss;
  for (std::uint32_t index = 0; index < tree.node_count(); ++index) {
    if (!dg::contains(tree.absolute_bounds(NodeId{index}), point)) {
      continue;
    }
    bool admitted = true;
    for (std::uint32_t walk = index; honour_clips && walk != 0;) {
      walk = tree.parent(NodeId{walk}).index;
      if (tree.style(NodeId{walk}).overflow == dg::Overflow::kClip &&
          !dg::contains(tree.absolute_bounds(NodeId{walk}), point)) {
        admitted = false;
        break;
      }
    }
    if (admitted) {
      found = index;
    }
  }
  return found;
}

std::uint32_t hit_index(const RenderTree& tree, PixelPoint point) {
  const std::optional<NodeId> hit = tree.hit_test(point);
  return hit.value_or(NodeId{static_cast<std::uint32_t>(tree.node_count())}).index;
}

// The four corner squares of the rounded panels, and nothing else.
//
// Both oracles here work in whole rectangles, and a curve is where that stops
// being exact: inside the bounding box but outside the arc, the rasterizer
// blends and the rectangle oracle says "inside". Excluding the corner SQUARES
// rather than the whole rounded panel is what keeps the exclusion honest -
// the straight edges of a rounded clip, which is most of it, stay under
// exhaustive test, and the excluded area is a few thousand pixels rather than
// two fifths of the window. The curve itself is pinned pixel by pixel against
// the rasterizer's alpha in tests/unit/test_clip.cpp.
bool in_rounded_corner(const clip_scene::Scene& scene, PixelPoint point) {
  const std::array<NodeId, 2> rounded{scene.handles.round, scene.handles.nest_inner};
  return std::ranges::any_of(rounded, [&scene, point](NodeId id) {
    const dg::Radii radii = scene.tree.render().style(id).radii;
    if (radii.is_zero()) {
      return false;
    }
    const dg::PixelRect box = scene.tree.bounds(id);
    const auto radius = static_cast<int>(radii.top_left) + 1;
    const bool horizontal = point.x < box.left() + radius || point.x >= box.right() - radius;
    const bool vertical = point.y < box.top() + radius || point.y >= box.bottom() - radius;
    return horizontal && vertical && dg::contains(box.inflated_by(1), point);
  });
}

struct Tally {
  std::size_t pixels = 0;
  std::size_t order_mismatches = 0;
  std::size_t pixel_mismatches = 0;
  std::size_t excluded = 0;
  std::size_t clipped_away = 0;
  std::string first_failure;
};

void sweep(const clip_scene::Scene& scene, const RenderTree& tree, const dg::PixelView& view,
           Tally& tally) {
  const PixelSize viewport = tree.viewport();
  const auto miss = static_cast<std::uint32_t>(tree.node_count());

  for (int y = 0; y < viewport.height; ++y) {
    for (int x = 0; x < viewport.width; ++x) {
      const PixelPoint point{x, y};
      ++tally.pixels;

      // What the clips are worth, measured through the CLIP-BLIND oracle the
      // previous slice shipped. A width at which this is small is a width
      // that proves nothing, whatever the mismatch counts say.
      if (topmost(tree, point, miss, true) != topmost(tree, point, miss, false)) {
        ++tally.clipped_away;
      }

      if (in_rounded_corner(scene, point)) {
        ++tally.excluded;
        continue;
      }

      const std::uint32_t said = hit_index(tree, point);
      const std::uint32_t expected = topmost(tree, point, miss, true);
      if (said != expected) {
        ++tally.order_mismatches;
        if (tally.first_failure.empty()) {
          tally.first_failure = "paint order at " + std::to_string(x) + "," +
                                std::to_string(y) + ": hit " + std::to_string(said) +
                                " expected " + std::to_string(expected);
        }
      }

      const std::size_t offset =
          (static_cast<std::size_t>(y) * view.row_bytes) + (static_cast<std::size_t>(x) * 4);
      const auto drawn = static_cast<std::uint32_t>(view.pixels[offset + 2]) - 1;
      if (drawn != said) {
        ++tally.pixel_mismatches;
        if (tally.first_failure.empty()) {
          tally.first_failure = "the surface at " + std::to_string(x) + "," +
                                std::to_string(y) + ": hit " + std::to_string(said) + " drew " +
                                std::to_string(drawn);
        }
      }
    }
  }
}

}  // namespace

int run(std::ostream& out) {
  out << "CLIPPING: does what the screen shows agree with what hit testing says?\n"
      << "  Every pixel, at " << std::size(kWidths) << " window widths. Two oracles: a\n"
      << "  paint-order scan that re-derives the ancestor clip chain here, and the\n"
      << "  rasterizer's own output read back through a unique colour per node.\n\n"
      << "  width  overflow  clipped px  order  surface  excluded\n";

  std::set<int> overflows;
  bool ok = true;

  for (const int width : kWidths) {
    dg::TreeSpec spec;
    spec.viewport = PixelSize{width, kHeight};
    spec.background.fill = dg::Color::from_argb(0xFF14171C);
    clip_scene::Scene scene = clip_scene::build(spec);
    overflows.insert(clip_scene::overflow_amount(scene));

    RenderTree& tree = scene.tree.render();
    recolour(tree);

    std::optional<dg::RasterSurface> surface = dg::RasterSurface::create(width, kHeight);
    if (!surface.has_value()) {
      out << "  could not allocate a surface\n";
      return 1;
    }
    tree.repaint_full(*surface);
    const dg::PixelView view = surface->peek_pixels();
    if (view.pixels == nullptr || !view.is_bgra8888) {
      out << "  the surface is not the format this check reads\n";
      return 1;
    }

    Tally tally;
    sweep(scene, tree, view, tally);

    out << "  " << width << "   " << clip_scene::overflow_amount(scene) << "        "
        << tally.clipped_away << "      " << tally.order_mismatches << "      "
        << tally.pixel_mismatches << "        " << tally.excluded << "\n";
    if (!tally.first_failure.empty()) {
      out << "    FIRST FAILURE: " << tally.first_failure << "\n";
    }

    ok = ok && tally.order_mismatches == 0 && tally.pixel_mismatches == 0;

    // A sweep over a scene whose clips remove nothing agrees for the trivial
    // reason. Required to be substantial, not merely non-zero.
    // Scale-free, because the ladder changes the window size: one per cent of
    // the frame is a large, obvious region and stays a meaningful bar at every
    // width, where an absolute count would quietly become easy or impossible.
    if (tally.clipped_away * 100 < tally.pixels) {
      out << "    FAIL: the clips only removed " << tally.clipped_away << " of " << tally.pixels
          << " pixels, so this width is not evidence about clipping\n";
      ok = false;
    }

    // The excluded set is eight corner squares. A few thousand pixels is
    // eight corners; a large fraction of the window would mean the exclusion
    // had swallowed the comparison it is carved out of.
    if (tally.excluded * 50 > tally.pixels) {
      out << "    FAIL: " << tally.excluded << " of " << tally.pixels
          << " pixels were excluded as corners\n";
      ok = false;
    }
  }

  out << "\n  distinct overflow amounts across the ladder: " << overflows.size() << "\n";
  if (overflows.size() < 4) {
    out << "  FAIL: the resize ladder has to move the clip boundary, or it is one case\n"
        << "  run six times - the trap doc/wrapping.md records for the wrap ladder\n";
    ok = false;
  }

  out << (ok ? "\nPASS\n" : "\nFAIL\n");
  return ok ? 0 : 1;
}

}  // namespace clip_check
