// What a property change costs, and what it must leave on screen.
//
// design.md section 5.15.2 separates invalidation into levels by cost, and the
// two this engine has are repaint and relayout. The whole value of the property
// system reaching layout and paint SEPARATELY is that a colour change does not
// pay for a relayout - so if the two were wired together, everything would
// still look right and the design claim would be quietly false. Nothing on
// screen would say so. That is what these tests are for.
//
// Three things are checked, and they fail for different reasons:
//
//   the CLASS   - a paint property must not relayout; a layout property must.
//   the PIXELS  - a damage repaint after a property change must be
//                 byte-identical to a full repaint. That is slice 1's
//                 acceptance technique, reused on a new way of mutating.
//   the EXTENT  - when a property MOVES a node, the pixels it vacated and the
//                 pixels it now occupies must both be damaged. Damaging only
//                 the destination leaves a trail, and a still frame does not
//                 show it.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/graphics/types.h"
#include "drawgui/layout/box.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/props/node_props.h"
#include "drawgui/render/prop_ids.generated.h"
#include "drawgui/render/render_tree.h"

namespace {

using dg::BoxStyle;
using dg::Color;
using dg::CrossAlign;
using dg::EdgeInsets;
using dg::LayoutKind;
using dg::LayoutStats;
using dg::LayoutTree;
using dg::NodeId;
using dg::NodeStyle;
using dg::PixelRect;
using dg::PixelSize;
using dg::PropValue;
using dg::RasterSurface;

constexpr int kWidth = 317;
constexpr int kHeight = 223;

dg::TreeSpec spec_of() {
  dg::TreeSpec spec;
  spec.viewport = PixelSize{kWidth, kHeight};
  spec.background.fill = Color::from_argb(0xFF14171C);
  return spec;
}

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

// A row of three children, the middle one flexible.
struct Scene {
  LayoutTree tree{spec_of()};
  NodeId row;
  NodeId first;
  NodeId middle;
  NodeId last;

  Scene() {
    BoxStyle root = tree.box(LayoutTree::root());
    root.kind = LayoutKind::kColumn;
    root.padding = EdgeInsets::all(8);
    root.cross_align = CrossAlign::kStretch;
    tree.set_box(LayoutTree::root(), root);

    BoxStyle row_box;
    row_box.kind = LayoutKind::kRow;
    row_box.height = 90;
    row_box.gap = 6;
    row_box.cross_align = CrossAlign::kStretch;
    row = tree.add_child(LayoutTree::root(), row_box, NodeStyle{});

    BoxStyle fixed;
    fixed.width = 70;
    NodeStyle first_style;
    first_style.fill = Color::from_argb(0xFF4C6EF5);
    first = tree.add_child(row, fixed, first_style);

    BoxStyle flexible;
    flexible.grow = 1;
    NodeStyle middle_style;
    middle_style.fill = Color::from_argb(0xFF2A3140);
    middle = tree.add_child(row, flexible, middle_style);

    NodeStyle last_style;
    last_style.fill = Color::from_argb(0xFFF59F00);
    last = tree.add_child(row, fixed, last_style);

    tree.layout();
  }
};

// Repaints fully, so the damage list starts empty and whatever a property
// write adds to it afterwards is attributable to that write alone.
void settle(LayoutTree& tree, RasterSurface& surface) {
  tree.layout();
  tree.render().repaint_full(surface);
}

// The same, for the cases that only need the damage list cleared and never
// compare pixels. Owning the surface here rather than in the test case keeps
// the fallible allocation - and the branch that reports it - out of the case's
// cognitive-complexity budget.
[[nodiscard]] bool settle_scene(Scene& scene) {
  std::optional<RasterSurface> surface = RasterSurface::create(kWidth, kHeight);
  if (!surface.has_value()) {
    FAIL("could not allocate a raster surface");
    return false;
  }
  settle(scene.tree, *surface);
  return true;
}

bool region_covers(const dg::DamageRegion& region, const PixelRect& rect) {
  return std::ranges::any_of(
      region.rects(), [&rect](const PixelRect& damaged) { return contains(damaged, rect); });
}

// Assertions are grouped into helpers rather than written flat in the cases.
// clang-tidy budgets cognitive complexity per function and doctest's assertion
// macros are branch-heavy, so a wall of twelve in one case is over the
// threshold; grouping them by what they MEAN is both cheaper and clearer.
void check_no_relayout(const LayoutStats& stats) {
  CHECK(stats.dirty_roots == 0);
  CHECK(stats.nodes_relaid_out == 0);
  CHECK(stats.nodes_moved == 0);
}

void check_relaid_out(const LayoutStats& stats) {
  CHECK(stats.dirty_roots >= 1);
  CHECK(stats.nodes_relaid_out >= 1);
}

void check_covers_both(const dg::DamageRegion& damage, const PixelRect& before,
                       const PixelRect& after) {
  CHECK(region_covers(damage, before));
  CHECK(region_covers(damage, after));
}

PixelRect moved_right(const PixelRect& rect, int distance) {
  return PixelRect{rect.x + distance, rect.y, rect.width, rect.height};
}

// Same size, shifted by exactly `distance`, and both boxes damaged. Bundled
// because the three assertions only mean anything together: the geometry check
// is what establishes this is a pure translation, and the damage check is what
// the translation was set up to test.
void check_translated_and_damaged(const LayoutTree& tree, NodeId node, const PixelRect& before,
                                  int distance) {
  CHECK(tree.bounds(node) == moved_right(before, distance));
  check_covers_both(tree.render().damage(), before, tree.bounds(node));
}

// One step of a property script, applied to both trees and compared.
void apply_and_compare(LayoutTree& damaged, NodeId damaged_node, LayoutTree& reference,
                       NodeId reference_node, dg_prop_id prop, const PropValue& value,
                       RasterSurface& damaged_surface, RasterSurface& reference_surface) {
  INFO("prop id ", prop);
  CHECK(dg::set_prop(damaged, damaged_node, prop, value).ok());
  CHECK(dg::set_prop(reference, reference_node, prop, value).ok());

  damaged.layout();
  reference.layout();
  damaged.render().repaint(damaged_surface);
  reference.render().repaint_full(reference_surface);

  CHECK(snapshot(damaged_surface) == snapshot(reference_surface));
}

}  // namespace

TEST_CASE("a paint property repaints without relaying anything out") {
  Scene scene;
  if (!settle_scene(scene)) {
    return;
  }
  const PixelRect before = scene.tree.bounds(scene.first);

  CHECK(dg::set_prop(scene.tree, scene.first, DG_PROP_BACKGROUND_COLOR,
                     PropValue::color(Color::from_argb(0xFF12B886)))
            .ok());

  // The damage exists before any layout pass runs, which is the point: the
  // write went straight to the render tree.
  CHECK_FALSE(scene.tree.render().damage().is_empty());
  CHECK(region_covers(scene.tree.render().damage(), before));

  check_no_relayout(scene.tree.layout());
  CHECK(scene.tree.bounds(scene.first) == before);
}

TEST_CASE("every wired paint property leaves layout alone") {
  std::optional<RasterSurface> surface = RasterSurface::create(kWidth, kHeight);
  if (!surface.has_value()) {
    FAIL("could not allocate a raster surface");
    return;
  }
  Scene scene;

  const std::vector<std::pair<dg_prop_id, PropValue>> paint_properties{
      {DG_PROP_BACKGROUND_COLOR, PropValue::color(Color::from_argb(0xFF12B886))},
      {DG_PROP_BORDER_COLOR, PropValue::color(Color::from_argb(0xFFFFFFFF))},
      {DG_PROP_BORDER_RADIUS_TL, PropValue::number(6)},
      {DG_PROP_BORDER_RADIUS_TR, PropValue::number(6)},
      {DG_PROP_BORDER_RADIUS_BR, PropValue::number(6)},
      {DG_PROP_BORDER_RADIUS_BL, PropValue::number(6)},
  };

  for (const auto& [prop, value] : paint_properties) {
    settle(scene.tree, *surface);
    INFO("prop id ", prop);
    CHECK(dg::set_prop(scene.tree, scene.first, prop, value).ok());
    check_no_relayout(scene.tree.layout());
  }
}

TEST_CASE("a layout property relays out and damages where the node was and is") {
  Scene scene;
  if (!settle_scene(scene)) {
    return;
  }

  const PixelRect first_before = scene.tree.bounds(scene.first);
  const PixelRect middle_before = scene.tree.bounds(scene.middle);

  CHECK(dg::set_prop(scene.tree, scene.first, DG_PROP_WIDTH, PropValue::length(140)).ok());
  check_relaid_out(scene.tree.layout());
  CHECK(scene.tree.bounds(scene.first).width == 140);

  // The middle child carries the weight, so it absorbs the change by RESIZING
  // and the last child does not move at all. That is the flex protocol working,
  // and it is why this case cannot be the one that proves vacated pixels are
  // damaged: a node that changes size damages its new box for that reason
  // alone. The next case builds the shape that can.
  check_covers_both(scene.tree.render().damage(), first_before, scene.tree.bounds(scene.first));
  check_covers_both(scene.tree.render().damage(), middle_before,
                    scene.tree.bounds(scene.middle));
}

// The pure-translation case, and the scene is built by hand for it.
//
// The three-child row cannot produce one on its own: its middle child is
// flexible, so every main-axis change is absorbed by a resize. The shape that
// catches a damage-only-the-destination defect is a node that MOVES while
// keeping its size, so the weight is taken off the middle child first.
TEST_CASE("a property that moves a node damages the box it vacated") {
  Scene scene;
  CHECK(dg::set_prop(scene.tree, scene.middle, DG_PROP_GROW, PropValue::number(0)).ok());
  CHECK(dg::set_prop(scene.tree, scene.middle, DG_PROP_WIDTH, PropValue::length(70)).ok());
  if (!settle_scene(scene)) {
    return;
  }

  const PixelRect middle_before = scene.tree.bounds(scene.middle);
  const PixelRect last_before = scene.tree.bounds(scene.last);

  CHECK(dg::set_prop(scene.tree, scene.first, DG_PROP_MARGIN_R, PropValue::number(40)).ok());
  scene.tree.layout();

  // Same size, moved right by exactly the margin: the vacated pixels are now
  // the only reason the old boxes could be damaged.
  check_translated_and_damaged(scene.tree, scene.middle, middle_before, 40);
  check_translated_and_damaged(scene.tree, scene.last, last_before, 40);
}

TEST_CASE("border width is both a layout and a paint property, and pays for both") {
  Scene scene;
  if (!settle_scene(scene)) {
    return;
  }

  CHECK(dg::set_prop(scene.tree, scene.middle, DG_PROP_BORDER_WIDTH_L, PropValue::number(5))
            .ok());
  CHECK_FALSE(scene.tree.render().damage().is_empty());
  check_relaid_out(scene.tree.layout());
  CHECK(scene.tree.box(scene.middle).border.left == 5);
}

// The byte-identity gate, on a scene mutated only through properties.
//
// doc/damage-repaint.md is right that this cannot prove the pixels are the ones
// anyone wanted - the parity test next door is what pins that - but it is
// exactly the right instrument for "did the write invalidate everything it
// changed", which is the question a still frame cannot answer.
TEST_CASE("a property-driven frame repaints partially the same as it does fully") {
  std::optional<RasterSurface> damaged_surface = RasterSurface::create(kWidth, kHeight);
  std::optional<RasterSurface> reference_surface = RasterSurface::create(kWidth, kHeight);
  if (!damaged_surface.has_value() || !reference_surface.has_value()) {
    FAIL("could not allocate a raster surface");
    return;
  }

  Scene damaged;
  Scene reference;
  settle(damaged.tree, *damaged_surface);
  settle(reference.tree, *reference_surface);

  struct Write {
    NodeId Scene::* node;
    dg_prop_id prop;
    PropValue value;
  };

  const std::vector<Write> script{
      {&Scene::first, DG_PROP_BACKGROUND_COLOR, PropValue::color(Color::from_argb(0xFF12B886))},
      {&Scene::first, DG_PROP_WIDTH, PropValue::length(120)},
      {&Scene::last, DG_PROP_BORDER_RADIUS_TL, PropValue::number(10)},
      {&Scene::last, DG_PROP_BORDER_RADIUS_BR, PropValue::number(10)},
      {&Scene::last, DG_PROP_BORDER_COLOR, PropValue::color(Color::from_argb(0xFFE9ECEF))},
      {&Scene::last, DG_PROP_BORDER_WIDTH_T, PropValue::number(3)},
      {&Scene::first, DG_PROP_MARGIN_R, PropValue::number(24)},
      {&Scene::row, DG_PROP_GAP, PropValue::number(18)},

      // The second sizing stage, composed with damage rather than checked on
      // its own: a declared base, a deficit split between two children, a
      // container that starts filling its main axis, and a ratio that derives
      // one axis from the other. Each one moves several nodes at once, which
      // is the shape a damage rectangle computed from a single node gets wrong.
      {&Scene::first, DG_PROP_BASIS, PropValue::number(80)},
      {&Scene::first, DG_PROP_SHRINK, PropValue::number(2)},
      {&Scene::middle, DG_PROP_SHRINK, PropValue::number(1)},
      {&Scene::row, DG_PROP_MAIN_SIZE, PropValue::option(DG_MAIN_SIZE_MAX)},
      {&Scene::last, DG_PROP_ASPECT_RATIO, PropValue::number(1.5F)},

      {&Scene::row, DG_PROP_JUSTIFY, PropValue::option(DG_JUSTIFY_CENTER)},
      {&Scene::middle, DG_PROP_MIN_WIDTH, PropValue::length(40)},
      {&Scene::first, DG_PROP_WIDTH, PropValue::length(60)},
      {&Scene::row, DG_PROP_DIRECTION, PropValue::option(DG_DIRECTION_COLUMN)},
      {&Scene::row, DG_PROP_ALIGN, PropValue::option(DG_ALIGN_CENTER)},
      {&Scene::first, DG_PROP_PADDING_L, PropValue::number(9)},
  };

  for (const Write& step : script) {
    apply_and_compare(damaged.tree, damaged.*step.node, reference.tree, reference.*step.node,
                      step.prop, step.value, *damaged_surface, *reference_surface);
  }
}
