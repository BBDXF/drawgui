// The verification that makes incremental layout trustworthy.
//
// Layout bugs do not look like bugs. A node laid out under the wrong
// constraints, or a dirty mark that stopped one level too early, produces an
// arrangement that is correct everywhere anybody looked and one pixel wrong
// in the case nobody tried - and unlike a crash, it survives every run.
// Watching the window is not evidence.
//
// The check that is evidence is the same one sub-step 1 used for damage and
// is cheap and exact: mutate the tree, lay it out incrementally, lay an
// identical tree out from scratch, and require every node's computed bounds
// to be identical. It needs no display, so unlike the demo it runs in CI.
//
// It exercises the arrangement examples/04_layout actually shows, one case
// per class of change the acceptance bar names, and it finishes by rendering
// both trees and comparing the framebuffers - because bounds agreeing is the
// claim, but pixels agreeing is what a user would notice.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/render_tree.h"

#include "layout_scene.h"

namespace {

using dg::LayoutStats;
using dg::LayoutTree;
using dg::NodeId;
using dg::PixelRect;
using dg::RasterSurface;
using layout_scene::Mutation;

// Odd on purpose, and small enough that a card is only a few dozen pixels
// across: an off-by-one in an integer distribution hides at 1920 and shows
// here. The surface pads its rows at this width too, so anything assuming a
// row is width * 4 bytes fails here rather than on somebody else's machine.
constexpr int kWidth = 641;
constexpr int kHeight = 421;
constexpr int kScriptFrames = 180;

layout_scene::Options options_at(int width, int height) {
  layout_scene::Options options;
  options.spec.viewport = dg::PixelSize{width, height};
  options.spec.background.fill = dg::Color::from_argb(0xFF14171C);
  return options;
}

std::vector<PixelRect> bounds_of(const LayoutTree& tree) {
  std::vector<PixelRect> bounds;
  bounds.reserve(tree.node_count());
  for (std::uint32_t index = 0; index < tree.node_count(); ++index) {
    bounds.push_back(tree.bounds(NodeId{index}));
  }
  return bounds;
}

std::string first_difference(const std::vector<PixelRect>& left,
                             const std::vector<PixelRect>& right) {
  if (left.size() != right.size()) {
    return "node counts differ";
  }
  for (std::size_t index = 0; index < left.size(); ++index) {
    if (left[index] != right[index]) {
      return "node " + std::to_string(index) + ": incremental " +
             std::to_string(left[index].x) + "," + std::to_string(left[index].y) + " " +
             std::to_string(left[index].width) + "x" + std::to_string(left[index].height) +
             "  full " + std::to_string(right[index].x) + "," + std::to_string(right[index].y) +
             " " + std::to_string(right[index].width) + "x" +
             std::to_string(right[index].height);
    }
  }
  return {};
}

struct Pair {
  layout_scene::Scene incremental;
  layout_scene::Scene full;
};

Pair build_pair(const layout_scene::Options& options) {
  return Pair{layout_scene::build(options), layout_scene::build(options)};
}

// Applies the same mutation to both trees, lays one out incrementally and the
// other from scratch, and returns the incremental pass's statistics so a
// caller can also assert on how much work it did.
LayoutStats step(Pair& pair, Mutation mutation, int frame) {
  layout_scene::apply(pair.incremental.tree, pair.incremental.handles, mutation, frame);
  layout_scene::apply(pair.full.tree, pair.full.handles, mutation, frame);
  const LayoutStats stats = pair.incremental.tree.layout();
  pair.full.tree.layout_full();
  return stats;
}

// Returns a bool rather than comparing the two vectors in the assertion:
// doctest stringifies whatever it is given, and a 400x300 framebuffer printed
// byte by byte buries the frame number that is the only useful part.
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

// Returns a bool rather than comparing the two vectors in the assertion:
// doctest stringifies whatever it is given, and a 400x300 framebuffer printed
// byte by byte buries the frame number that is the only useful part.
bool same_pixels(const RasterSurface& left, const RasterSurface& right) {
  return snapshot(left) == snapshot(right);
}

}  // namespace

TEST_CASE("incremental layout equals full layout, node for node") {
  int count = 0;
  const Mutation* mutations = layout_scene::all_mutations(count);
  REQUIRE(count == 6);

  for (int which = 0; which < count; ++which) {
    const Mutation mutation = mutations[which];
    CAPTURE(layout_scene::name_of(mutation));

    Pair pair = build_pair(options_at(kWidth, kHeight));
    for (int frame = 0; frame < kScriptFrames; ++frame) {
      step(pair, mutation, frame);
      const std::string difference =
          first_difference(bounds_of(pair.incremental.tree), bounds_of(pair.full.tree));
      REQUIRE_MESSAGE(difference.empty(), "frame " << frame << ": " << difference);
    }
  }
}

TEST_CASE("the composite script agrees frame for frame") {
  // Every mutation on its own schedule, so most frames change one thing and
  // the occasional frame changes several at once - which is the case where a
  // dirty list that assumes a single root goes wrong.
  Pair pair = build_pair(options_at(kWidth, kHeight));
  for (int frame = 0; frame < kScriptFrames; ++frame) {
    layout_scene::apply_frame(pair.incremental.tree, pair.incremental.handles, frame);
    layout_scene::apply_frame(pair.full.tree, pair.full.handles, frame);
    pair.incremental.tree.layout();
    pair.full.tree.layout_full();
    const std::string difference =
        first_difference(bounds_of(pair.incremental.tree), bounds_of(pair.full.tree));
    REQUIRE_MESSAGE(difference.empty(), "frame " << frame << ": " << difference);
  }
}

TEST_CASE("a resize mid-script does not desynchronise the two paths") {
  Pair pair = build_pair(options_at(kWidth, kHeight));
  const dg::PixelSize sizes[] = {{641, 421}, {900, 300}, {420, 700}, {641, 421}};

  for (const dg::PixelSize& size : sizes) {
    pair.incremental.tree.resize(size);
    pair.full.tree.resize(size);
    for (int frame = 0; frame < 24; ++frame) {
      layout_scene::apply_frame(pair.incremental.tree, pair.incremental.handles, frame);
      layout_scene::apply_frame(pair.full.tree, pair.full.handles, frame);
      pair.incremental.tree.layout();
      pair.full.tree.layout_full();
      const std::string difference =
          first_difference(bounds_of(pair.incremental.tree), bounds_of(pair.full.tree));
      REQUIRE_MESSAGE(difference.empty(), size.width << "x" << size.height << " frame " << frame
                                                     << ": " << difference);
    }
  }
}

TEST_CASE("the identity check is armed") {
  // A comparison that cannot fail proves nothing about the ones that pass.
  // Here the incremental side simply skips a pass, which is the cheapest
  // possible stand-in for a dirty mark that stopped too early.
  Pair pair = build_pair(options_at(kWidth, kHeight));
  layout_scene::apply(pair.incremental.tree, pair.incremental.handles,
                      Mutation::kLeafResizesParent, 40);
  layout_scene::apply(pair.full.tree, pair.full.handles, Mutation::kLeafResizesParent, 40);
  pair.full.tree.layout_full();

  CHECK_FALSE(
      first_difference(bounds_of(pair.incremental.tree), bounds_of(pair.full.tree)).empty());

  pair.incremental.tree.layout();
  CHECK(first_difference(bounds_of(pair.incremental.tree), bounds_of(pair.full.tree)).empty());
}

TEST_CASE("a leaf that resizes its parent moves the parent's later siblings") {
  // The premise the containment cases are measured against. If this node did
  // not actually reach outward, "the change was contained" would be a claim
  // about a change that never went anywhere.
  Pair pair = build_pair(options_at(kWidth, kHeight));
  const NodeId tag = pair.incremental.handles.first_tag;
  const PixelRect before = pair.incremental.tree.bounds(tag);

  const LayoutStats stats = step(pair, Mutation::kLeafResizesParent, 30);
  CHECK(pair.incremental.tree.bounds(tag).x != before.x);
  CHECK(stats.nodes_moved > 1);
  CHECK(stats.damage_area > 0);
}

TEST_CASE("a change inside a pinned card touches nothing outside it") {
  Pair pair = build_pair(options_at(kWidth, kHeight));
  const LayoutTree& tree = pair.incremental.tree;
  const PixelRect card_before = tree.bounds(pair.incremental.handles.card);
  const PixelRect toolbar_before = tree.bounds(pair.incremental.handles.toolbar);
  const PixelRect sidebar_before = tree.bounds(pair.incremental.handles.sidebar_item);

  const LayoutStats stats = step(pair, Mutation::kContainedResize, 7);

  // The card is a flexible, stretched child of a row, so it is constrained
  // tightly on both axes and its size cannot change however its contents
  // rearrange. That is what makes it a relayout boundary, and the boundary is
  // what stops the dirty mark.
  CHECK(tree.bounds(pair.incremental.handles.card) == card_before);
  CHECK(tree.bounds(pair.incremental.handles.toolbar) == toolbar_before);
  CHECK(tree.bounds(pair.incremental.handles.sidebar_item) == sidebar_before);

  CHECK(stats.dirty_roots == 1);
  CHECK(stats.nodes_relaid_out * 6 < stats.nodes_total);
}

TEST_CASE("a change inside a row nested in a column stays inside that row") {
  Pair pair = build_pair(options_at(kWidth, kHeight));
  const LayoutTree& tree = pair.incremental.tree;
  const PixelRect item_before = tree.bounds(pair.incremental.handles.sidebar_item);
  const PixelRect card_before = tree.bounds(pair.incremental.handles.card);

  const LayoutStats stats = step(pair, Mutation::kNestedRowInColumn, 5);

  CHECK(tree.bounds(pair.incremental.handles.sidebar_item) == item_before);
  CHECK(tree.bounds(pair.incremental.handles.card) == card_before);
  CHECK(stats.dirty_roots == 1);
  CHECK(stats.nodes_relaid_out * 6 < stats.nodes_total);
}

TEST_CASE("a change that changes nothing moves nothing and damages nothing") {
  Pair pair = build_pair(options_at(kWidth, kHeight));

  SUBCASE("a box reassigned its own value") {
    const LayoutStats stats = step(pair, Mutation::kNoOp, 11);
    CHECK(stats.nodes_relaid_out > 0);
    CHECK(stats.nodes_moved == 0);
    CHECK(stats.damage_area == 0);
    CHECK(stats.damage_rects == 0);
  }

  SUBCASE("a fill colour, which layout has no opinion about") {
    const LayoutStats stats = step(pair, Mutation::kPaintOnly, 11);
    CHECK(stats.dirty_roots == 0);
    CHECK(stats.nodes_visited == 0);
    CHECK(stats.nodes_relaid_out == 0);
    CHECK(stats.damage_area == 0);
  }
}

TEST_CASE("re-layout scope stays small over a long run") {
  Pair pair = build_pair(options_at(kWidth, kHeight));
  std::size_t worst_relaid_out = 0;
  std::size_t total = 0;

  for (int frame = 1; frame <= 600; ++frame) {
    layout_scene::apply_frame(pair.incremental.tree, pair.incremental.handles, frame);
    const LayoutStats stats = pair.incremental.tree.layout();
    worst_relaid_out = std::max(worst_relaid_out, stats.nodes_relaid_out);
    total = stats.nodes_total;
  }

  // The claim is that changing a leaf in a deep tree does not touch the whole
  // tree. The worst frame in the script changes four things at once, so this
  // is a generous bound and still a small fraction.
  REQUIRE(total > 60);
  CHECK(worst_relaid_out * 2 < total);
}

TEST_CASE("layout-driven frames repaint identically to a full repaint") {
  // Composes with sub-step 1. Layout's output is node movement, movement is
  // damage, and damage is what partial repaint consumes; a layout that got
  // the damage wrong would show up here as a stale pixel even while every
  // rectangle agreed.
  const layout_scene::Options options = options_at(400, 300);
  std::optional<RasterSurface> damaged_surface = RasterSurface::create(400, 300);
  std::optional<RasterSurface> reference_surface = RasterSurface::create(400, 300);
  // Tested with an if rather than a REQUIRE: doctest's macro is opaque to
  // clang-tidy's optional analysis, and the honest fix is a check the
  // compiler and the linter can both see.
  if (!damaged_surface.has_value() || !reference_surface.has_value()) {
    FAIL("could not allocate a raster surface");
    return;
  }
  RasterSurface& damaged = *damaged_surface;
  RasterSurface& reference = *reference_surface;

  Pair pair = build_pair(options);
  for (int frame = 0; frame < 90; ++frame) {
    layout_scene::apply_frame(pair.incremental.tree, pair.incremental.handles, frame);
    layout_scene::apply_frame(pair.full.tree, pair.full.handles, frame);
    pair.incremental.tree.layout();
    pair.full.tree.layout_full();

    pair.incremental.tree.render().repaint(damaged);
    pair.full.tree.render().repaint_full(reference);
    REQUIRE_MESSAGE(same_pixels(damaged, reference), "frame " << frame);
  }
}
