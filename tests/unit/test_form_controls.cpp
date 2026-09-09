// Radio and slider: "checkbox plus one field", and a thumb whose position is
// paint-time arithmetic over an existing primitive - not a new WidgetKind for
// radio, and not a new RenderObject kind for slider (design.md section 5.6
// line 622's own acceptance bar for exactly this control).
//
// Same acceptance shape as every other slice: hand-derived geometry for what
// a value MEANS, pinned directly against WidgetSet rather than through a
// scene - `examples/11_form_controls`'s own headless check
// (`form_check.cpp`) is where the byte-identity pair and the on-screen claims
// live; this file is where the arithmetic that feeds them is pinned in
// isolation.

#include <cstdint>
#include <optional>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/types.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/widget/widget_set.h"

namespace {

using dg::Color;
using dg::NodeId;
using dg::NodeStyle;
using dg::PixelPoint;
using dg::PixelRect;
using dg::PixelSize;
using dg::RenderTree;
using dg::TreeSpec;
using dg::Widget;
using dg::WidgetKind;
using dg::WidgetSet;

TreeSpec spec_for(PixelSize size = PixelSize{400, 300}) {
  TreeSpec spec;
  spec.viewport = size;
  spec.background.fill = Color::from_argb(0xFF14171C);
  return spec;
}

NodeStyle flat(std::uint32_t argb) {
  NodeStyle style;
  style.fill = Color::from_argb(argb);
  return style;
}

// --------------------------------------------------------------------------
// Radio: three checkboxes sharing group 1, one sharing group 2 - built
// directly on RenderTree, mirroring test_scroll.cpp's "test the primitive
// without a layout pass in the way".
// --------------------------------------------------------------------------

struct RadioScene {
  RenderTree tree;
  WidgetSet widgets;
  std::vector<NodeId> group_a;
  NodeId group_b_one;
};

RadioScene build_radio_scene() {
  RenderTree tree{spec_for()};
  WidgetSet widgets;
  std::vector<NodeId> group_a;

  for (int i = 0; i < 3; ++i) {
    const NodeId node =
        tree.add_child(RenderTree::root(), PixelRect{i * 40, 0, 32, 32}, flat(0xFF2C3644));
    const NodeId indicator = tree.add_child(node, PixelRect{6, 6, 20, 20}, flat(0xFF10151C));
    Widget widget;
    widget.kind = WidgetKind::kCheckbox;
    widget.indicator = indicator;
    widget.indicator_on = Color::from_argb(0xFF2E86DE);
    widget.indicator_off = Color::from_argb(0xFF10151C);
    widget.group = 1;
    widgets.attach(node, widget);
    widgets.attach(indicator, Widget{});
    group_a.push_back(node);
  }

  const NodeId other_group_node =
      tree.add_child(RenderTree::root(), PixelRect{200, 0, 32, 32}, flat(0xFF2C3644));
  const NodeId other_indicator =
      tree.add_child(other_group_node, PixelRect{6, 6, 20, 20}, flat(0xFF10151C));
  Widget other_widget;
  other_widget.kind = WidgetKind::kCheckbox;
  other_widget.indicator = other_indicator;
  other_widget.indicator_on = Color::from_argb(0xFF2E86DE);
  other_widget.indicator_off = Color::from_argb(0xFF10151C);
  other_widget.group = 2;
  widgets.attach(other_group_node, other_widget);
  widgets.attach(other_indicator, Widget{});

  return RadioScene{std::move(tree), std::move(widgets), std::move(group_a), other_group_node};
}

TEST_CASE("selecting one radio option checks it and unchecks its siblings") {
  RadioScene scene = build_radio_scene();

  CHECK(scene.widgets.toggle(scene.group_a[0]));
  CHECK(scene.widgets.is_checked(scene.group_a[0]));
  CHECK_FALSE(scene.widgets.is_checked(scene.group_a[1]));
  CHECK_FALSE(scene.widgets.is_checked(scene.group_a[2]));

  CHECK(scene.widgets.toggle(scene.group_a[2]));
  CHECK(scene.widgets.is_checked(scene.group_a[2]));
  CHECK_FALSE(scene.widgets.is_checked(scene.group_a[0]));
  CHECK_FALSE(scene.widgets.is_checked(scene.group_a[1]));
}

TEST_CASE("clicking the already-selected radio option is a no-op, not a toggle-off") {
  RadioScene scene = build_radio_scene();
  scene.widgets.toggle(scene.group_a[1]);
  REQUIRE(scene.widgets.is_checked(scene.group_a[1]));

  const bool result = scene.widgets.toggle(scene.group_a[1]);
  CHECK(result);
  CHECK(scene.widgets.is_checked(scene.group_a[1]));
}

TEST_CASE("two radio groups are independent") {
  RadioScene scene = build_radio_scene();
  scene.widgets.toggle(scene.group_a[0]);
  scene.widgets.toggle(scene.group_b_one);

  CHECK(scene.widgets.is_checked(scene.group_a[0]));
  CHECK(scene.widgets.is_checked(scene.group_b_one));

  scene.widgets.toggle(scene.group_a[1]);
  CHECK_FALSE(scene.widgets.is_checked(scene.group_a[0]));
  CHECK(scene.widgets.is_checked(scene.group_a[1]));
  // Selecting a different option in group 1 must not touch group 2 at all.
  CHECK(scene.widgets.is_checked(scene.group_b_one));
}

TEST_CASE("group_members reports the other members and never the widget itself") {
  RadioScene scene = build_radio_scene();
  const std::vector<NodeId> members = scene.widgets.group_members(scene.group_a[0]);
  REQUIRE(members.size() == 2);
  CHECK(members[0] != scene.group_a[0]);
  CHECK(members[1] != scene.group_a[0]);
  CHECK(scene.widgets.group_members(scene.group_b_one).empty());
}

TEST_CASE("an ungrouped checkbox still toggles freely - the pre-existing behaviour") {
  RenderTree tree{spec_for()};
  WidgetSet widgets;
  const NodeId node =
      tree.add_child(RenderTree::root(), PixelRect{0, 0, 32, 32}, flat(0xFF2C3644));
  Widget widget;
  widget.kind = WidgetKind::kCheckbox;
  widgets.attach(node, widget);

  CHECK(widgets.toggle(node));
  CHECK(widgets.is_checked(node));
  CHECK_FALSE(widgets.toggle(node));
  CHECK_FALSE(widgets.is_checked(node));
  CHECK(widgets.group_members(node).empty());
}

// --------------------------------------------------------------------------
// Slider: a track (200x20) and a 20x20 thumb, built directly on RenderTree.
// --------------------------------------------------------------------------

struct SliderScene {
  RenderTree tree;
  WidgetSet widgets;
  NodeId track;
  NodeId thumb;
};

constexpr int kTrackWidth = 200;
constexpr int kTrackHeight = 20;
constexpr int kThumbSize = 20;

SliderScene build_slider_scene(float min_value, float max_value, float step) {
  RenderTree tree{spec_for()};
  WidgetSet widgets;
  const NodeId track = tree.add_child(
      RenderTree::root(), PixelRect{10, 10, kTrackWidth, kTrackHeight}, flat(0xFF20262F));
  const NodeId thumb =
      tree.add_child(track, PixelRect{0, 0, kThumbSize, kThumbSize}, flat(0xFF2E86DE));
  Widget widget;
  widget.kind = WidgetKind::kSlider;
  widget.thumb = thumb;
  widget.min_value = min_value;
  widget.max_value = max_value;
  widget.step = step;
  widgets.attach(track, widget);
  widgets.attach(thumb, Widget{});
  return SliderScene{std::move(tree), std::move(widgets), track, thumb};
}

// travel = track width (200) - thumb width (20) = 180, so value 50 of
// [0,100] should land the thumb's local x at exactly 90 (fraction 0.5 * 180).
TEST_CASE("set_slider_value positions the thumb at the value's fraction of travel") {
  SliderScene scene = build_slider_scene(0.0F, 100.0F, 0.0F);
  CHECK(scene.widgets.set_slider_value(scene.tree, scene.track, 50.0F));
  CHECK(scene.tree.local_bounds(scene.thumb).x == 90);
  CHECK(scene.tree.local_bounds(scene.thumb).y == 0);  // (20 - 20) / 2 == 0

  CHECK(scene.widgets.set_slider_value(scene.tree, scene.track, 0.0F));
  CHECK(scene.tree.local_bounds(scene.thumb).x == 0);

  CHECK(scene.widgets.set_slider_value(scene.tree, scene.track, 100.0F));
  CHECK(scene.tree.local_bounds(scene.thumb).x == 180);
}

TEST_CASE("set_slider_value clamps to [min_value, max_value]") {
  SliderScene scene = build_slider_scene(10.0F, 20.0F, 0.0F);
  scene.widgets.set_slider_value(scene.tree, scene.track, -500.0F);
  CHECK(scene.widgets.slider_value(scene.track) == 10.0F);
  CHECK(scene.tree.local_bounds(scene.thumb).x == 0);

  scene.widgets.set_slider_value(scene.tree, scene.track, 500.0F);
  CHECK(scene.widgets.slider_value(scene.track) == 20.0F);
  CHECK(scene.tree.local_bounds(scene.thumb).x == 180);
}

TEST_CASE("set_slider_value snaps to the nearest step") {
  // [0, 10] step 1: travel 180 px over 10 steps == 18 px per step.
  SliderScene scene = build_slider_scene(0.0F, 10.0F, 1.0F);
  scene.widgets.set_slider_value(scene.tree, scene.track, 4.4F);
  CHECK(scene.widgets.slider_value(scene.track) == 4.0F);
  CHECK(scene.tree.local_bounds(scene.thumb).x == 72);  // 4/10 * 180

  scene.widgets.set_slider_value(scene.tree, scene.track, 4.6F);
  CHECK(scene.widgets.slider_value(scene.track) == 5.0F);
  CHECK(scene.tree.local_bounds(scene.thumb).x == 90);  // 5/10 * 180
}

TEST_CASE("set_slider_value is a no-op when the clamped/snapped value is unchanged") {
  SliderScene scene = build_slider_scene(0.0F, 100.0F, 0.0F);
  CHECK(scene.widgets.set_slider_value(scene.tree, scene.track, 50.0F));
  CHECK_FALSE(scene.widgets.set_slider_value(scene.tree, scene.track, 50.0F));
  // Past the top clamp repeatedly: the second call must report no change.
  CHECK(scene.widgets.set_slider_value(scene.tree, scene.track, 500.0F));
  CHECK_FALSE(scene.widgets.set_slider_value(scene.tree, scene.track, 600.0F));
}

TEST_CASE("slider_value_at maps an absolute pointer position to a value, hand-derived") {
  SliderScene scene = build_slider_scene(0.0F, 100.0F, 0.0F);
  // Track's absolute bounds start at x=10 (see build_slider_scene). A
  // pointer at the thumb's half-width (10 + 10 = 20) is fraction 0, and one
  // at the far edge (10 + 200 - 10 = 200) is fraction 1.
  CHECK(static_cast<double>(scene.widgets.slider_value_at(scene.tree, scene.track, 20)) ==
        doctest::Approx(0.0));
  CHECK(static_cast<double>(scene.widgets.slider_value_at(scene.tree, scene.track, 200)) ==
        doctest::Approx(100.0));
  // Dead centre of the track (10 + 100 = 110) is fraction 0.5.
  CHECK(static_cast<double>(scene.widgets.slider_value_at(scene.tree, scene.track, 110)) ==
        doctest::Approx(50.0));
  // Past either edge clamps rather than extrapolating.
  CHECK(static_cast<double>(scene.widgets.slider_value_at(scene.tree, scene.track, -1000)) ==
        doctest::Approx(0.0));
  CHECK(static_cast<double>(scene.widgets.slider_value_at(scene.tree, scene.track, 1000)) ==
        doctest::Approx(100.0));
}

TEST_CASE("slidable_owner_of climbs from the thumb to the slider that owns it") {
  SliderScene scene = build_slider_scene(0.0F, 100.0F, 0.0F);
  CHECK(scene.widgets.slidable_owner_of(scene.tree, scene.thumb) == scene.track);
  CHECK(scene.widgets.slidable_owner_of(scene.tree, scene.track) == scene.track);
  CHECK_FALSE(scene.widgets.slidable_owner_of(scene.tree, RenderTree::root()).has_value());
}

TEST_CASE("resync_sliders repositions every slider from its stored value, unconditionally") {
  SliderScene scene = build_slider_scene(0.0F, 100.0F, 0.0F);
  scene.widgets.set_slider_value(scene.tree, scene.track, 25.0F);
  REQUIRE(scene.tree.local_bounds(scene.thumb).x == 45);  // 0.25 * 180

  // Simulate a resize that widened the track (as layout() would): the
  // thumb's local origin is what a leaf-child relayout would stomp back to
  // its default - here forced directly, since this file tests the
  // primitive without a LayoutTree.
  scene.tree.set_local_bounds(scene.track, PixelRect{10, 10, 380, kTrackHeight});
  scene.tree.set_local_origin(scene.thumb, 0, 0);
  REQUIRE(scene.tree.local_bounds(scene.thumb).x == 0);

  // The stored value (25) did not change, so set_slider_value() would
  // report no-op and skip repositioning - resync_sliders() must reposition
  // anyway, against the NEW, wider track.
  scene.widgets.resync_sliders(scene.tree);
  // travel = 380 - 20 = 360; 0.25 * 360 = 90.
  CHECK(scene.tree.local_bounds(scene.thumb).x == 90);
}

}  // namespace
