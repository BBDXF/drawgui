// The verification that makes partial repaint trustworthy.
//
// Damage bugs do not look like bugs in a still frame. A node that is not
// redrawn when something underneath it changes, or a node that moves without
// invalidating the pixels it vacated, produces a picture that is correct
// until it is not, and then stays wrong. Watching the window is not evidence.
//
// The check that is evidence is cheap and exact: run the same scene twice,
// once repainting only what was marked dirty and once repainting everything
// from scratch, and require the two framebuffers to be byte-identical after
// every frame. It needs no display, so unlike the demos it runs in CI.
//
// It exercises the scene examples/03_damage_repaint actually animates, at
// several damage-list caps and in both paint modes, plus one case per class
// of mutation the design notes single out.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/render/render_tree.h"

#include "scene.h"

namespace {

using dg::PixelRect;
using dg::RasterSurface;

// Odd on purpose: the surface then pads its rows, so anything that assumed a
// row is width * 4 bytes fails here rather than on somebody else's machine.
constexpr int kWidth = 481;
constexpr int kHeight = 331;
constexpr int kScriptFrames = 120;

dg::TreeSpec spec_for(std::size_t max_rects, dg::PaintMode mode) {
  dg::TreeSpec spec;
  spec.viewport = dg::PixelSize{kWidth, kHeight};
  spec.background.fill = dg::Color::from_argb(0xFF14171C);
  spec.max_damage_rects = max_rects;
  spec.paint_mode = mode;
  return spec;
}

// Row by row and only the pixels, never the padding: the bytes past the end
// of a row are allocated but never written, so comparing them would compare
// whatever the allocator happened to leave behind.
std::vector<std::uint8_t> snapshot(const RasterSurface& surface) {
  const dg::PixelView view = surface.peek_pixels();
  const auto row = static_cast<std::size_t>(view.width) * 4;
  std::vector<std::uint8_t> pixels(row * static_cast<std::size_t>(view.height));
  for (int y = 0; y < view.height; ++y) {
    std::memcpy(pixels.data() + (static_cast<std::size_t>(y) * row),
                view.pixels + (static_cast<std::size_t>(y) * view.row_bytes), row);
  }
  return pixels;
}

// Read back as unpremultiplied ARGB, which is safe only because every colour
// in the scene is opaque - the one case where premultiplied and straight
// agree.
std::uint32_t pixel_at(const RasterSurface& surface, int x, int y) {
  const dg::PixelView view = surface.peek_pixels();
  const std::uint8_t* pixel = view.pixels + (static_cast<std::size_t>(y) * view.row_bytes) +
                              (static_cast<std::size_t>(x) * 4);
  return (static_cast<std::uint32_t>(pixel[3]) << 24) |
         (static_cast<std::uint32_t>(pixel[2]) << 16) |
         (static_cast<std::uint32_t>(pixel[1]) << 8) | static_cast<std::uint32_t>(pixel[0]);
}

struct Pair {
  scene::Scene damaged;
  scene::Scene reference;
  RasterSurface damaged_surface;
  RasterSurface reference_surface;

  [[nodiscard]] bool identical() const {
    return snapshot(damaged_surface) == snapshot(reference_surface);
  }
};

// Two independent copies of one scene, each with its own surface. Handed to
// the body rather than returned so that a failed surface allocation has
// exactly one place to be reported from.
template <typename Body>
void with_pair(const dg::TreeSpec& spec, const Body& body) {
  std::optional<RasterSurface> damaged =
      RasterSurface::create(spec.viewport.width, spec.viewport.height);
  std::optional<RasterSurface> reference =
      RasterSurface::create(spec.viewport.width, spec.viewport.height);
  if (!damaged.has_value() || !reference.has_value()) {
    FAIL("could not allocate a raster surface");
    return;
  }
  Pair pair{scene::build(spec), scene::build(spec), *std::move(damaged), *std::move(reference)};
  body(pair);
}

void run_script(const dg::TreeSpec& spec) {
  with_pair(spec, [](Pair& pair) {
    for (int frame = 0; frame < kScriptFrames; ++frame) {
      scene::apply_frame(pair.damaged.tree, pair.damaged.handles, frame);
      scene::apply_frame(pair.reference.tree, pair.reference.handles, frame);

      pair.damaged.tree.repaint(pair.damaged_surface);
      pair.reference.tree.repaint_full(pair.reference_surface);

      REQUIRE_MESSAGE(pair.identical(), "frame " << frame << " of " << kScriptFrames);
    }
  });
}

TEST_CASE("damage-driven repaint matches a full repaint byte for byte") {
  SUBCASE("one damage rectangle - the always-union policy") {
    run_script(spec_for(1, dg::PaintMode::kDirect));
  }
  SUBCASE("two damage rectangles") {
    run_script(spec_for(2, dg::PaintMode::kDirect));
  }
  SUBCASE("eight damage rectangles") {
    run_script(spec_for(8, dg::PaintMode::kDirect));
  }
  SUBCASE("replaying a recorded picture under the clip") {
    run_script(spec_for(8, dg::PaintMode::kPicture));
  }
}

TEST_CASE("skipping a repaint defers the frame rather than losing it") {
  // Damage accumulates until something paints it, so a dropped repaint is
  // caught up by the next one. This is worth pinning because it is also the
  // reason a naive arming test does not work: skipping a middle frame leaves
  // no trace at all.
  with_pair(spec_for(8, dg::PaintMode::kDirect), [](Pair& pair) {
    for (int frame = 0; frame < 6; ++frame) {
      scene::apply_frame(pair.damaged.tree, pair.damaged.handles, frame);
      scene::apply_frame(pair.reference.tree, pair.reference.handles, frame);
      if (frame != 3) {
        pair.damaged.tree.repaint(pair.damaged_surface);
      }
      pair.reference.tree.repaint_full(pair.reference_surface);
    }
    CHECK(pair.identical());
  });
}

TEST_CASE("the equivalence check is armed") {
  // Given both surfaces in step, When the damage side misses the LAST frame,
  // Then the two must differ. A comparison that cannot fail proves nothing
  // about the ones that pass.
  constexpr int kFrames = 6;
  with_pair(spec_for(8, dg::PaintMode::kDirect), [](Pair& pair) {
    for (int frame = 0; frame < kFrames; ++frame) {
      scene::apply_frame(pair.damaged.tree, pair.damaged.handles, frame);
      scene::apply_frame(pair.reference.tree, pair.reference.handles, frame);
      if (frame + 1 < kFrames) {
        pair.damaged.tree.repaint(pair.damaged_surface);
      }
      pair.reference.tree.repaint_full(pair.reference_surface);
    }
    CHECK_FALSE(pair.identical());
  });
}

TEST_CASE("a node under an overlapping one is not painted over it") {
  // The characteristic z-order bug: the badge overlaps the pulsing card and
  // never changes, so a repaint driven only by the card's damage still has to
  // redraw the badge on top of it.
  with_pair(spec_for(8, dg::PaintMode::kDirect), [](Pair& pair) {
    dg::RenderTree& tree = pair.damaged.tree;
    const PixelRect overlap = dg::intersect(tree.absolute_bounds(pair.damaged.handles.badge),
                                            tree.absolute_bounds(pair.damaged.handles.pulse));
    REQUIRE_FALSE(overlap.is_empty());
    const int x = overlap.x + (overlap.width / 2);
    const int y = overlap.y + (overlap.height / 2);

    tree.repaint(pair.damaged_surface);
    const std::uint32_t before = pixel_at(pair.damaged_surface, x, y);

    tree.set_fill(pair.damaged.handles.pulse, dg::Color::from_argb(0xFF00FF00));
    tree.repaint(pair.damaged_surface);

    CHECK(pixel_at(pair.damaged_surface, x, y) == before);
    CHECK(pixel_at(pair.damaged_surface, x, y) != 0xFF00FF00);
  });
}

TEST_CASE("a moving node leaves no stale pixels behind") {
  with_pair(spec_for(8, dg::PaintMode::kDirect), [](Pair& pair) {
    const PixelRect start = pair.damaged.tree.local_bounds(pair.damaged.handles.marker);
    for (int step = 0; step <= 30; ++step) {
      const int x = start.x + (step * 7);
      pair.damaged.tree.set_local_origin(pair.damaged.handles.marker, x, start.y);
      pair.reference.tree.set_local_origin(pair.reference.handles.marker, x, start.y);
      pair.damaged.tree.repaint(pair.damaged_surface);
      pair.reference.tree.repaint_full(pair.reference_surface);
      REQUIRE_MESSAGE(pair.identical(), "step " << step);
    }
  });
}

// Dirties the two square-cornered dots at opposite ends of the content area,
// having first drained the initial full-viewport damage.
void dirty_far_apart_pair(Pair& pair) {
  dg::RenderTree& tree = pair.damaged.tree;
  tree.repaint(pair.damaged_surface);
  tree.set_fill(pair.damaged.handles.corner_a, dg::Color::from_argb(0xFFFF0000));
  tree.set_fill(pair.damaged.handles.corner_b, dg::Color::from_argb(0xFF0000FF));
}

TEST_CASE("two far-apart nodes stay two rectangles instead of one window") {
  with_pair(spec_for(8, dg::PaintMode::kDirect), [](Pair& pair) {
    dirty_far_apart_pair(pair);
    CHECK(pair.damaged.tree.damage().size() == 2);

    // The bounding box of the two is most of the content area; their combined
    // area is two small squares. That gap is the whole argument for a list.
    const dg::DamageRegion& damage = pair.damaged.tree.damage();
    CHECK(damage.area() * 4 < damage.bounds().area());
  });
}

TEST_CASE("a far-apart pair repaints two regions and a fraction of the nodes") {
  with_pair(spec_for(8, dg::PaintMode::kDirect), [](Pair& pair) {
    dirty_far_apart_pair(pair);
    const dg::RepaintStats stats = pair.damaged.tree.repaint(pair.damaged_surface);
    CHECK(stats.rects == 2);
    CHECK(stats.nodes_drawn < stats.nodes_total);
  });
}

}  // namespace
