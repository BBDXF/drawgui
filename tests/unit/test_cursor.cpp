// Mouse cursor shapes (DG_PROP_CURSOR): the property write, dg::resolve_cursor()
// / cursor_at() (inheritance, widget defaults, disabled, held press),
// WindowManager's per-window cursor with its override and "only switch on
// change" rule, and the C ABI path that keeps the cursor in step with the
// pointer. Everything runs under SDL_VIDEODRIVER=dummy, which has no cursors
// at all: what is checked is WindowManager::current_cursor() /
// cursor_switch_count(), never the screen.

#include <cstdint>
#include <cstdlib>
#include <optional>
#include <utility>

#include <doctest/doctest.h>

#include "drawgui/abi/drawgui.h"
#include "drawgui/base/cursor.h"
#include "drawgui/base/expected.h"
#include "drawgui/base/pixel_geometry.h"
#include "drawgui/layout/box.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/props/node_props.h"
#include "drawgui/render/prop_ids.generated.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/widget/cursor.h"
#include "drawgui/widget/interaction.h"
#include "drawgui/widget/widget_set.h"
#include "drawgui/window/window_manager.h"

namespace {

using dg::Cursor;
using dg::Interaction;
using dg::NodeId;
using dg::NodeStyle;
using dg::PixelPoint;
using dg::PixelRect;
using dg::RenderTree;
using dg::Widget;
using dg::WidgetKind;
using dg::WidgetSet;

dg::TreeSpec spec_of(int width, int height) {
  dg::TreeSpec spec;
  spec.viewport = dg::PixelSize{width, height};
  return spec;
}

//   root
//    |- panel (kPanel)                 (0, 0, 400, 100)
//    |    |- button (kButton)           (10, 10, 80, 30)
//    |    |    |- label (kLabel)        (5, 5, 40, 20)
//    |    |- field (kTextField)         (100, 10, 80, 30)
//    |    |    |- content               (0, 0, 80, 30)
//    |    |- slider (kSlider)           (200, 10, 80, 30)
//    |- plain (no widget)               (0, 120, 100, 50)
struct Scene {
  RenderTree tree{spec_of(400, 200)};
  WidgetSet widgets;
  NodeId panel, button, label, field, content, slider, plain;
};

Widget make(WidgetKind kind) {
  Widget w;
  w.kind = kind;
  return w;
}

Scene build() {
  Scene s;
  const NodeId root = RenderTree::root();
  s.panel = s.tree.add_child(root, PixelRect{0, 0, 400, 100}, NodeStyle{});
  s.button = s.tree.add_child(s.panel, PixelRect{10, 10, 80, 30}, NodeStyle{});
  s.label = s.tree.add_child(s.button, PixelRect{5, 5, 40, 20}, NodeStyle{});
  s.field = s.tree.add_child(s.panel, PixelRect{100, 10, 80, 30}, NodeStyle{});
  s.content = s.tree.add_child(s.field, PixelRect{0, 0, 80, 30}, NodeStyle{});
  s.slider = s.tree.add_child(s.panel, PixelRect{200, 10, 80, 30}, NodeStyle{});
  s.plain = s.tree.add_child(root, PixelRect{0, 120, 100, 50}, NodeStyle{});
  s.widgets.attach(s.panel, make(WidgetKind::kPanel));
  s.widgets.attach(s.button, make(WidgetKind::kButton));
  s.widgets.attach(s.label, make(WidgetKind::kLabel));
  s.widgets.attach(s.field, make(WidgetKind::kTextField));
  s.widgets.attach(s.slider, make(WidgetKind::kSlider));
  return s;
}

void set_cursor(RenderTree& tree, NodeId id, Cursor cursor) {
  NodeStyle style = tree.style(id);
  style.cursor = cursor;
  tree.set_style(id, style);
}

void set_disabled(RenderTree& tree, NodeId id, bool disabled) {
  NodeStyle style = tree.style(id);
  style.disabled = disabled;
  tree.set_style(id, style);
}

Cursor at(const Scene& s, NodeId id) {
  return dg::resolve_cursor(s.tree, s.widgets, id);
}

dg::WindowManager make_headless_manager() {
  setenv("SDL_VIDEODRIVER", "dummy", 1);
  dg::Expected<dg::WindowManager, dg::WindowError> made = dg::WindowManager::create();
  REQUIRE(made.has_value());
  return std::move(made.value());
}

dg::WindowId open_window(dg::WindowManager& manager) {
  dg::WindowSpec spec;
  spec.title = "cursor";
  spec.width = 100;
  spec.height = 80;
  const dg::Expected<dg::WindowId, dg::WindowError> opened = manager.open(spec);
  REQUIRE(opened.has_value());
  return opened.value();
}

Cursor current(const dg::WindowManager& manager, dg::WindowId id) {
  const dg::Expected<Cursor, dg::WindowError> cursor = manager.current_cursor(id);
  REQUIRE(cursor.has_value());
  return cursor.value();
}

}  // namespace

TEST_SUITE("cursor property") {
  TEST_CASE("DG_PROP_CURSOR writes NodeStyle::cursor and rejects unknown ordinals") {
    dg::LayoutTree tree(spec_of(100, 100));
    const NodeId node = tree.add_child(dg::LayoutTree::root(), dg::BoxStyle{}, NodeStyle{});
    CHECK(tree.render().style(node).cursor == Cursor::kAuto);
    CHECK(dg::prop_type(DG_PROP_CURSOR) == dg::PropType::k_enum);

    CHECK(dg::set_prop(tree, node, DG_PROP_CURSOR, dg::PropValue::option(DG_CURSOR_TEXT)).ok());
    CHECK(tree.render().style(node).cursor == Cursor::kText);
    CHECK(dg::set_prop(tree, node, DG_PROP_CURSOR, dg::PropValue::option(DG_CURSOR_RESIZE_NESW))
              .ok());
    CHECK(tree.render().style(node).cursor == Cursor::kResizeNesw);

    const dg::PropWrite bad =
        dg::set_prop(tree, node, DG_PROP_CURSOR, dg::PropValue::option(dg::kCursorCount));
    CHECK_FALSE(bad.ok());
    CHECK(tree.render().style(node).cursor == Cursor::kResizeNesw);
  }
}

TEST_SUITE("cursor resolution") {
  TEST_CASE("widget defaults: only a text field changes the cursor") {
    const Scene s = build();
    CHECK(dg::widget_default_cursor(WidgetKind::kTextField) == Cursor::kText);
    CHECK(dg::widget_default_cursor(WidgetKind::kButton) == Cursor::kDefault);
    CHECK(dg::widget_default_cursor(WidgetKind::kCheckbox) == Cursor::kDefault);
    CHECK(dg::widget_default_cursor(WidgetKind::kDropdown) == Cursor::kDefault);
    CHECK(dg::widget_default_cursor(WidgetKind::kSlider) == Cursor::kDefault);
    CHECK(dg::widget_default_cursor(WidgetKind::kList) == Cursor::kDefault);

    CHECK(at(s, s.field) == Cursor::kText);
    CHECK(at(s, s.content) == Cursor::kText);  // a field's own child climbs to it
    CHECK(at(s, s.button) == Cursor::kDefault);
    CHECK(at(s, s.slider) == Cursor::kDefault);
    CHECK(at(s, s.plain) == Cursor::kDefault);
    CHECK(dg::resolve_cursor(s.tree, s.widgets, std::nullopt) == Cursor::kDefault);
  }

  TEST_CASE("an explicit cursor inherits down the subtree; the nearest one wins") {
    Scene s = build();
    set_cursor(s.tree, s.panel, Cursor::kCrosshair);
    CHECK(at(s, s.button) == Cursor::kCrosshair);  // buttons have no opinion
    CHECK(at(s, s.label) == Cursor::kCrosshair);
    CHECK(at(s, s.slider) == Cursor::kCrosshair);
    CHECK(at(s, s.field) == Cursor::kText);  // nearer widget default wins
    CHECK(at(s, s.plain) == Cursor::kDefault);

    set_cursor(s.tree, s.button, Cursor::kPointer);
    CHECK(at(s, s.label) == Cursor::kPointer);
    set_cursor(s.tree, s.field, Cursor::kMove);
    CHECK(at(s, s.content) == Cursor::kMove);
    set_cursor(s.tree, s.content, Cursor::kWait);
    CHECK(at(s, s.content) == Cursor::kWait);

    set_cursor(s.tree, s.button, Cursor::kAuto);
    CHECK(at(s, s.label) == Cursor::kCrosshair);
  }

  TEST_CASE("disabled widgets lose their kind default but keep explicit cursors") {
    Scene s = build();
    set_disabled(s.tree, s.field, true);
    CHECK(at(s, s.field) == Cursor::kDefault);
    CHECK(at(s, s.content) == Cursor::kDefault);

    set_disabled(s.tree, s.field, false);
    set_disabled(s.tree, s.panel, true);  // inherited
    CHECK(at(s, s.content) == Cursor::kDefault);

    set_cursor(s.tree, s.field, Cursor::kNotAllowed);
    CHECK(at(s, s.content) == Cursor::kNotAllowed);
  }

  TEST_CASE("cursor_at hit-tests, and a held press keeps the held widget's cursor") {
    Scene s = build();
    Interaction interaction;
    CHECK(dg::cursor_at(s.tree, s.widgets, interaction, PixelPoint{120, 20}) == Cursor::kText);
    CHECK(dg::cursor_at(s.tree, s.widgets, interaction, PixelPoint{20, 20}) ==
          Cursor::kDefault);
    CHECK(dg::cursor_at(s.tree, s.widgets, interaction, PixelPoint{399, 199}) ==
          Cursor::kDefault);

    (void)interaction.pressed_on(s.widgets.widget_at(s.tree, PixelPoint{120, 20}));
    REQUIRE(interaction.holding() == s.field);
    // Dragged out of the field over the button: still the I-beam.
    CHECK(dg::cursor_at(s.tree, s.widgets, interaction, PixelPoint{20, 20}) == Cursor::kText);
    (void)interaction.released_on(s.widgets.widget_at(s.tree, PixelPoint{20, 20}));
    CHECK(dg::cursor_at(s.tree, s.widgets, interaction, PixelPoint{20, 20}) ==
          Cursor::kDefault);
  }
}

TEST_SUITE("window cursor") {
  TEST_CASE("set_cursor stores per window and only switches on change") {
    dg::WindowManager manager = make_headless_manager();
    const dg::WindowId id = open_window(manager);
    CHECK(current(manager, id) == Cursor::kDefault);
    const std::uint64_t start = manager.cursor_switch_count();

    manager.set_cursor(id, Cursor::kText);
    CHECK(current(manager, id) == Cursor::kText);
    const std::uint64_t after_first = manager.cursor_switch_count();
    CHECK(after_first == start + 1);

    for (int i = 0; i < 5; ++i) {
      manager.set_cursor(id, Cursor::kText);
    }
    CHECK(manager.cursor_switch_count() == after_first);

    manager.set_cursor(id, Cursor::kAuto);  // shown as the arrow
    CHECK(current(manager, id) == Cursor::kDefault);
    CHECK(manager.cursor_switch_count() == after_first + 1);
  }

  TEST_CASE("an override wins until cleared") {
    dg::WindowManager manager = make_headless_manager();
    const dg::WindowId id = open_window(manager);
    manager.set_cursor(id, Cursor::kText);
    manager.set_cursor_override(id, Cursor::kResizeEw);
    CHECK(current(manager, id) == Cursor::kResizeEw);

    const std::uint64_t held = manager.cursor_switch_count();
    manager.set_cursor(id, Cursor::kDefault);  // the resolver keeps running
    manager.set_cursor(id, Cursor::kPointer);
    CHECK(current(manager, id) == Cursor::kResizeEw);
    CHECK(manager.cursor_switch_count() == held);

    manager.set_cursor_override(id, std::nullopt);
    CHECK(current(manager, id) == Cursor::kPointer);
    CHECK(manager.cursor_switch_count() == held + 1);
  }

  TEST_CASE("unknown windows are ignored or reported") {
    dg::WindowManager manager = make_headless_manager();
    const dg::WindowId id = open_window(manager);
    manager.close_now(id);
    manager.set_cursor(id, Cursor::kText);
    manager.set_cursor_override(id, Cursor::kText);
    CHECK_FALSE(manager.current_cursor(id).has_value());
  }
}

namespace {

dg_value enum_value(std::uint32_t ordinal) {
  dg_value value{};
  value.size = sizeof(dg_value);
  value.type = DG_VALUE_ENUM;
  value.bits = ordinal;
  return value;
}

dg_value length(float number) {
  dg_value value{};
  value.size = sizeof(dg_value);
  value.type = DG_VALUE_LENGTH;
  value.number = number;
  return value;
}

void set_size(dg_node_t* node, float width, float height) {
  const dg_value w = length(width);
  const dg_value h = length(height);
  REQUIRE(dg_node_set_prop(node, DG_PROP_WIDTH, &w) == DG_ERR_OK);
  REQUIRE(dg_node_set_prop(node, DG_PROP_HEIGHT, &h) == DG_ERR_OK);
}

struct AbiFixture {
  dg_app_t* app = nullptr;
  dg_window_t* window = nullptr;
  dg_node_t* root = nullptr;

  AbiFixture() {
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    app = dg_app_create(nullptr);
    REQUIRE(app != nullptr);
    dg_window_opts opts{};
    opts.size = sizeof(opts);
    opts.title = "abi cursor";
    opts.width = 200;
    opts.height = 100;
    window = dg_window_create(app, &opts);
    REQUIRE(window != nullptr);
    root = dg_node_create(app, DG_NODE_TYPE_BOX);
    REQUIRE(dg_window_set_root(window, root) == DG_ERR_OK);
  }
  AbiFixture(const AbiFixture&) = delete;
  AbiFixture& operator=(const AbiFixture&) = delete;
  AbiFixture(AbiFixture&&) = delete;
  AbiFixture& operator=(AbiFixture&&) = delete;
  ~AbiFixture() { dg_app_destroy(app); }

  dg_node_t* child(std::uint16_t type) const {
    dg_node_t* node = dg_node_create(app, type);
    REQUIRE(node != nullptr);
    REQUIRE(dg_node_insert_before(root, node, nullptr) == DG_ERR_OK);
    set_size(node, 100, 40);
    return node;
  }

  // A real press + release through the real SDL queue, then the pump.
  void click(std::int32_t x, std::int32_t y) const {
    REQUIRE(dg_wait_events(app, 0) >= 0);  // lay out first
    REQUIRE(dg_debug_warp_pointer(window, x, y) == DG_ERR_OK);
    REQUIRE(dg_debug_post_pointer_button(window, 1, x, y) == DG_ERR_OK);
    REQUIRE(dg_debug_post_pointer_button(window, 0, x, y) == DG_ERR_OK);
    for (int i = 0; i < 5; ++i) {
      REQUIRE(dg_wait_events(app, 10) >= 0);
      dg_event events[8];
      (void)dg_poll_events(app, events, 8);
    }
  }
};

}  // namespace

TEST_SUITE("abi cursor") {
  TEST_CASE("DG_CURSOR_* match dg::Cursor") {
    CHECK(DG_CURSOR_AUTO == static_cast<std::uint32_t>(Cursor::kAuto));
    CHECK(DG_CURSOR_TEXT == static_cast<std::uint32_t>(Cursor::kText));
    CHECK(DG_CURSOR_RESIZE_NESW == static_cast<std::uint32_t>(Cursor::kResizeNesw));
  }

  TEST_CASE("the event pump follows the pointer: text field, explicit, inherited") {
    AbiFixture f;
    CHECK(dg_debug_get_cursor(f.window) == static_cast<std::int32_t>(DG_CURSOR_DEFAULT));

    dg_node_t* field = f.child(DG_NODE_TYPE_TEXT_FIELD);
    f.click(10, 10);
    CHECK(dg_debug_get_cursor(f.window) == static_cast<std::int32_t>(DG_CURSOR_TEXT));

    const dg_value pointer = enum_value(DG_CURSOR_POINTER);
    REQUIRE(dg_node_set_prop(field, DG_PROP_CURSOR, &pointer) == DG_ERR_OK);
    f.click(10, 10);
    CHECK(dg_debug_get_cursor(f.window) == static_cast<std::int32_t>(DG_CURSOR_POINTER));

    const dg_value automatic = enum_value(DG_CURSOR_AUTO);
    REQUIRE(dg_node_set_prop(field, DG_PROP_CURSOR, &automatic) == DG_ERR_OK);
    const dg_value crosshair = enum_value(DG_CURSOR_CROSSHAIR);
    REQUIRE(dg_node_set_prop(f.root, DG_PROP_CURSOR, &crosshair) == DG_ERR_OK);
    f.click(10, 10);
    CHECK(dg_debug_get_cursor(f.window) == static_cast<std::int32_t>(DG_CURSOR_TEXT));
  }

  TEST_CASE("buttons keep the arrow unless told otherwise") {
    AbiFixture f;
    dg_node_t* button = f.child(DG_NODE_TYPE_BUTTON);
    f.click(10, 10);
    CHECK(dg_debug_get_cursor(f.window) == static_cast<std::int32_t>(DG_CURSOR_DEFAULT));

    const dg_value move = enum_value(DG_CURSOR_MOVE);
    REQUIRE(dg_node_set_prop(f.root, DG_PROP_CURSOR, &move) == DG_ERR_OK);
    f.click(10, 10);
    CHECK(dg_debug_get_cursor(f.window) == static_cast<std::int32_t>(DG_CURSOR_MOVE));

    const dg_value bad = enum_value(99);
    CHECK(dg_node_set_prop(button, DG_PROP_CURSOR, &bad) == DG_ERR_VALUE_OUT_OF_RANGE);
    CHECK(dg_debug_get_cursor(nullptr) == DG_ERR_INVALID_HANDLE);
  }
}
