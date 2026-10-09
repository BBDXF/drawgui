// NodeKind::kSlider and kRadio through dg::Window (no C ABI): the slider's
// range/step/value, a track press, a drag, the keys, disabling and a theme
// switch; radio groups by click, arrow keys and RadioRef.

#include <cmath>
#include <memory>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/theme/theme_package.h"
#include "runtime_fixture.h"

using runtime_test::of;
using runtime_test::Rt;
namespace ev = dg::events;

namespace {

// The thumb's x inside its track, measured from the frame's own tree.
int thumb_x(const Rt& t, dg::NodeId slider) {
  const dg::RenderTree& tree = t.win->layout().render();
  const std::vector<dg::NodeId> kids = tree.children(slider);
  REQUIRE(kids.size() == 1);
  return tree.local_bounds(kids.front()).x;
}

dg::Color thumb_fill(const Rt& t, dg::NodeId slider) {
  const dg::RenderTree& tree = t.win->layout().render();
  return tree.style(tree.children(slider).front()).fill;
}

dg::Theme builtin_theme() {
  dg::Expected<dg::ThemePackage, dg::ThemeLoadError> package =
      dg::ThemePackage::open(DG_TEST_FIXTURE_DIR "/../../themes/builtin");
  REQUIRE(package.has_value());
  dg::Expected<dg::Theme, dg::ThemeLoadError> theme = package.value().load_theme_json();
  REQUIRE(theme.has_value());
  return std::move(theme).value();
}

}  // namespace

TEST_SUITE("runtime form controls") {
  TEST_CASE("slider: defaults, range, clamping, snapping and the thumb position") {
    Rt t;
    const dg::NodeId node = t.add(dg::NodeKind::kSlider);
    dg::SliderRef slider = t.win->slider(node).value();
    (void)t.pump(1);
    CHECK(t.bounds(node).width == 200);
    CHECK(t.bounds(node).height == 20);
    CHECK(slider.min_value().value() == 0.0F);
    CHECK(slider.max_value().value() == 100.0F);
    CHECK(slider.step().value() == 0.0F);
    CHECK(slider.value().value() == 0.0F);
    CHECK(thumb_x(t, node) == 0);

    CHECK(slider.set_value(50.0F) == dg::Status::kOk);
    CHECK(of<ev::SliderEvent>(t.pump(1)).empty());
    CHECK(thumb_x(t, node) == 90);  // half of the 180px travel
    CHECK(slider.set_value(1000.0F) == dg::Status::kOk);
    CHECK(slider.value().value() == 100.0F);
    CHECK(slider.set_value(NAN) == dg::Status::kValueOutOfRange);

    CHECK(slider.set_range(0.0F, 10.0F, 1.0F) == dg::Status::kOk);
    CHECK(slider.value().value() == 10.0F);
    CHECK(slider.set_value(4.4F) == dg::Status::kOk);
    CHECK(slider.value().value() == 4.0F);
    CHECK(slider.set_value(4.6F) == dg::Status::kOk);
    CHECK(slider.value().value() == 5.0F);
    CHECK(slider.set_range(5.0F, 5.0F) == dg::Status::kValueOutOfRange);
    CHECK(slider.set_range(0.0F, 1.0F, -1.0F) == dg::Status::kValueOutOfRange);

    // The props are the same values.
    t.set(node, DG_PROP_MAX_VALUE, dg::PropValue::number(20.0F));
    CHECK(slider.max_value().value() == 20.0F);
    CHECK(t.win->set_prop(node, DG_PROP_MIN_VALUE, dg::PropValue::number(30.0F)) ==
          dg::Status::kValueOutOfRange);
    CHECK(t.win->set_prop(node, DG_PROP_STEP, dg::PropValue::number(-1.0F)) ==
          dg::Status::kValueOutOfRange);
    CHECK(t.win->set_prop(node, DG_PROP_VALUE, dg::PropValue::boolean(true)) ==
          dg::Status::kTypeMismatch);
    t.set(node, DG_PROP_VALUE, dg::PropValue::number(10.0F));
    (void)t.pump(1);
    CHECK(slider.value().value() == 10.0F);
    CHECK(thumb_x(t, node) == 90);

    // Not a container, not a checkbox, and a box takes none of its props.
    CHECK(t.win->create(dg::NodeKind::kBox, node).error() == dg::Status::kUnsupported);
    CHECK(t.win->checked(node).error() == dg::Status::kUnsupported);
    const dg::NodeId box = t.add(dg::NodeKind::kBox);
    CHECK(t.win->set_prop(box, DG_PROP_MIN_VALUE, dg::PropValue::number(1.0F)) ==
          dg::Status::kUnsupported);
    CHECK(t.win->set_prop(box, DG_PROP_RADIO_GROUP, dg::PropValue::number(1.0F)) ==
          dg::Status::kUnsupported);
    CHECK(t.win->slider(box).error() == dg::Status::kUnsupported);

    // DG_PROP_HEIGHT keeps the thumb a circle as tall as the track.
    t.set(node, DG_PROP_HEIGHT, dg::PropValue::length(30.0F));
    (void)t.pump(1);
    const dg::RenderTree& tree = t.win->layout().render();
    CHECK(tree.local_bounds(tree.children(node).front()).height == 30);
    CHECK(thumb_x(t, node) == 85);  // half of the 170px travel
  }

  TEST_CASE("slider: a track press, a drag, the keys and SliderEvent") {
    Rt t;
    const dg::NodeId node = t.add(dg::NodeKind::kSlider);
    dg::SliderRef slider = t.win->slider(node).value();
    (void)t.pump(1);
    const dg::PixelRect track = t.bounds(node);
    const int y = track.y + (track.height / 2);

    // A press jumps there (the thumb centred under the pointer) and focuses.
    t.press(track.x + 10 + 90, y);
    std::vector<dg::Event> events = t.pump();
    std::vector<ev::SliderEvent> moved = of<ev::SliderEvent>(events);
    REQUIRE(moved.size() == 1);
    CHECK(moved[0].node == node);
    CHECK(moved[0].previous == 0.0F);
    CHECK(moved[0].value == doctest::Approx(50.0F));
    CHECK(slider.value().value() == doctest::Approx(50.0F));
    CHECK(t.win->focused() == node);
    CHECK(of<ev::ClickEvent>(events).empty());

    // A drag follows the pointer until the release, past the track's end too.
    t.wm().warp_pointer(t.win->id(), track.x + 10, y);
    t.wm().post_pointer_button(t.win->id(), true, track.x + 10, y);
    (void)t.pump();
    t.wm().warp_pointer(t.win->id(), track.x + 400, y);
    moved = of<ev::SliderEvent>(t.pump());
    REQUIRE(!moved.empty());
    CHECK(moved.back().value == 100.0F);
    t.wm().post_pointer_button(t.win->id(), false, track.x + 400, y);
    (void)t.pump();
    t.wm().warp_pointer(t.win->id(), track.x + 10, y);
    CHECK(of<ev::SliderEvent>(t.pump()).empty());
    CHECK(slider.value().value() == 100.0F);

    // Keys while focused: a step (1/100 of the range when continuous),
    // a tenth of the range, the ends.
    REQUIRE(slider.set_range(0.0F, 10.0F, 1.0F) == dg::Status::kOk);
    REQUIRE(slider.set_value(5.0F) == dg::Status::kOk);
    t.key(dg::Key::kRight);
    moved = of<ev::SliderEvent>(t.pump());
    REQUIRE(moved.size() == 1);
    CHECK(moved[0].value == 6.0F);
    CHECK(moved[0].previous == 5.0F);
    t.key(dg::Key::kLeft);
    t.key(dg::Key::kLeft);
    (void)t.pump();
    CHECK(slider.value().value() == 4.0F);
    t.key(dg::Key::kEnd);
    (void)t.pump();
    CHECK(slider.value().value() == 10.0F);
    t.key(dg::Key::kEnd);
    CHECK(of<ev::SliderEvent>(t.pump()).empty());  // already there
    t.key(dg::Key::kHome);
    (void)t.pump();
    CHECK(slider.value().value() == 0.0F);
    t.key(dg::Key::kPageUp);
    (void)t.pump();
    CHECK(slider.value().value() == 1.0F);
    REQUIRE(slider.set_range(0.0F, 200.0F) == dg::Status::kOk);
    t.key(dg::Key::kPageUp);
    t.key(dg::Key::kUp);
    (void)t.pump();
    CHECK(slider.value().value() == doctest::Approx(23.0F));
    t.key(dg::Key::kPageDown);
    t.key(dg::Key::kDown);
    (void)t.pump();
    CHECK(slider.value().value() == doctest::Approx(1.0F));

    // Tab still leaves it.
    const dg::NodeId after = t.add(dg::NodeKind::kButton);
    (void)t.pump(1);
    t.key(dg::Key::kTab);
    (void)t.pump();
    CHECK(t.win->focused() == after);
  }

  TEST_CASE("slider: disabled takes no press or key; a theme switch restyles it") {
    Rt t;
    t.app.set_theme(std::make_shared<dg::Theme>(builtin_theme()), dg::ThemeVariant::kLight);
    const dg::NodeId node = t.add(dg::NodeKind::kSlider);
    dg::SliderRef slider = t.win->slider(node).value();
    (void)t.pump(1);
    const dg::Color light = thumb_fill(t, node);
    REQUIRE(t.app.set_theme_variant(dg::ThemeVariant::kDark) == dg::Status::kOk);
    (void)t.pump(1);
    const dg::Color dark = thumb_fill(t, node);
    CHECK(dark != light);

    REQUIRE(t.win->set_disabled(node, true) == dg::Status::kOk);
    (void)t.pump(1);
    CHECK(thumb_fill(t, node) != dark);
    const dg::PixelRect track = t.bounds(node);
    t.press(track.x + track.width - 5, track.y + 5);
    CHECK(of<ev::SliderEvent>(t.pump()).empty());
    CHECK(slider.value().value() == 0.0F);
    CHECK(t.win->focus(node) != dg::Status::kOk);
    t.key(dg::Key::kEnd);
    CHECK(of<ev::SliderEvent>(t.pump()).empty());

    REQUIRE(t.win->set_disabled(node, false) == dg::Status::kOk);
    (void)t.pump(1);
    CHECK(thumb_fill(t, node) == dark);
    t.press(track.x + track.width - 5, track.y + 5);
    CHECK(of<ev::SliderEvent>(t.pump()).size() == 1);
  }

  TEST_CASE("radio: groups are exclusive; clicks, arrow keys and RadioRef") {
    Rt t;
    std::vector<dg::NodeId> a;
    for (int i = 0; i < 3; ++i) {
      a.push_back(t.add(dg::NodeKind::kRadio));
    }
    std::vector<dg::NodeId> b;
    for (int i = 0; i < 2; ++i) {
      b.push_back(t.add(dg::NodeKind::kRadio));
      REQUIRE(t.win->radio(b.back()).value().set_group(7) == dg::Status::kOk);
    }
    (void)t.pump(1);
    const auto checked = [&](dg::NodeId id) { return t.win->checked(id).value(); };
    for (const dg::NodeId id : a) {
      CHECK_FALSE(checked(id));
      CHECK(t.win->radio(id).value().group().value() == 0);
    }
    CHECK(t.bounds(a[0]).width == 32);

    // A click selects and reports a ClickEvent; re-clicking keeps it.
    const auto click = [&](dg::NodeId id) {
      const dg::PixelRect r = t.bounds(id);
      t.press(r.x + (r.width / 2), r.y + (r.height / 2));
      return t.pump();
    };
    std::vector<ev::ClickEvent> clicks = of<ev::ClickEvent>(click(a[0]));
    REQUIRE(clicks.size() == 1);
    CHECK(clicks[0].node == a[0]);
    CHECK(checked(a[0]));
    (void)click(a[2]);
    CHECK(checked(a[2]));
    CHECK_FALSE(checked(a[0]));
    (void)click(a[2]);
    CHECK(checked(a[2]));
    (void)click(b[1]);
    CHECK(checked(b[1]));
    CHECK(checked(a[2]));

    // The deselected sibling's dot is repainted too.
    const dg::RenderTree& tree = t.win->layout().render();
    const auto dot = [&](dg::NodeId id) { return tree.style(tree.children(id).front()).fill; };
    CHECK(dot(a[0]) == dot(a[1]));
    CHECK(dot(a[2]) != dot(a[1]));

    // Arrow keys move focus and the selection through the group, wrapping,
    // skipping a disabled member; the ClickEvent carries clicks = 0.
    REQUIRE(t.win->focused() == b[1]);
    REQUIRE(t.win->focus(a[2]) == dg::Status::kOk);
    t.key(dg::Key::kRight);
    clicks = of<ev::ClickEvent>(t.pump());
    REQUIRE(clicks.size() == 1);
    CHECK(clicks[0].node == a[0]);
    CHECK(clicks[0].clicks == 0);
    CHECK(t.win->focused() == a[0]);
    CHECK(checked(a[0]));
    CHECK_FALSE(checked(a[2]));
    CHECK(checked(b[1]));
    REQUIRE(t.win->set_disabled(a[1], true) == dg::Status::kOk);
    t.key(dg::Key::kDown);
    (void)t.pump();
    CHECK(t.win->focused() == a[2]);
    CHECK(checked(a[2]));
    t.key(dg::Key::kUp);
    (void)t.pump();
    CHECK(t.win->focused() == a[0]);
    REQUIRE(t.win->set_disabled(a[1], false) == dg::Status::kOk);
    t.key(dg::Key::kLeft);
    (void)t.pump();
    CHECK(t.win->focused() == a[2]);
    t.key(dg::Key::kLeft);
    (void)t.pump();
    CHECK(t.win->focused() == a[1]);
    CHECK(checked(a[1]));

    // RadioRef::select() reports nothing; a selected radio joining a group
    // that has a selection gives up its own.
    REQUIRE(t.win->radio(a[0]).value().select() == dg::Status::kOk);
    CHECK(of<ev::ClickEvent>(t.pump(1)).empty());
    CHECK(checked(a[0]));
    CHECK_FALSE(checked(a[1]));
    REQUIRE(t.win->radio(a[0]).value().set_group(7) == dg::Status::kOk);
    CHECK_FALSE(checked(a[0]));
    CHECK(checked(b[1]));
    CHECK(t.win->radio(a[0]).value().set_group(1 << 25) == dg::Status::kValueOutOfRange);
    CHECK(t.win->set_prop(a[1], DG_PROP_RADIO_GROUP, dg::PropValue::number(1.5F)) ==
          dg::Status::kValueOutOfRange);
    CHECK(t.win->radio(t.root).error() == dg::Status::kUnsupported);
  }
}
