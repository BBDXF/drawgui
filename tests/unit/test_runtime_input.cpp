// dg::App / dg::Window driven directly (no C ABI): focus traversal,
// disabled presses, context clicks, list selection, and the editors'
// clipboard and undo keys.

#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "runtime_fixture.h"

using runtime_test::of;
using runtime_test::Rt;
namespace ev = dg::events;

TEST_SUITE("runtime input") {
  TEST_CASE("Tab traverses focusable nodes and reports FocusChangedEvent") {
    Rt t;
    const dg::NodeId first = t.add(dg::NodeKind::kButton);
    const dg::NodeId field = t.add(dg::NodeKind::kTextField);
    const dg::NodeId label = t.add(dg::NodeKind::kLabel);
    (void)t.pump(1);
    CHECK_FALSE(t.win->focused().has_value());

    REQUIRE(t.win->focus(field) == dg::Status::kOk);
    std::vector<ev::FocusChangedEvent> focus = of<ev::FocusChangedEvent>(t.app.poll());
    REQUIRE(focus.size() == 1);
    CHECK(focus[0].node == field);
    CHECK(focus[0].window == t.win->id());
    CHECK(t.win->focus(label) == dg::Status::kUnsupported);

    t.key(dg::Key::kTab);
    focus = of<ev::FocusChangedEvent>(t.pump());
    REQUIRE(focus.size() == 1);
    CHECK(focus[0].node == first);  // wrapped around
    t.key(dg::Key::kTab, true);
    (void)t.pump();
    CHECK(t.win->focused() == field);

    t.win->clear_focus();
    focus = of<ev::FocusChangedEvent>(t.pump(1));
    REQUIRE(focus.size() == 1);
    CHECK_FALSE(focus[0].node.has_value());
  }

  TEST_CASE("disabling a node mid-press swallows its ClickEvent") {
    Rt t;
    const dg::NodeId button = t.add(dg::NodeKind::kButton);
    (void)t.pump(1);
    const dg::PixelRect r = t.bounds(button);
    const int x = r.x + (r.width / 2);
    const int y = r.y + (r.height / 2);

    t.wm().warp_pointer(t.win->id(), x, y);
    t.wm().post_pointer_button(t.win->id(), true, x, y);
    (void)t.pump(1);
    REQUIRE(t.win->set_disabled(button, true) == dg::Status::kOk);
    t.wm().post_pointer_button(t.win->id(), false, x, y);
    CHECK(of<ev::ClickEvent>(t.pump()).empty());

    t.press(x, y);
    CHECK(of<ev::ClickEvent>(t.pump()).empty());  // still disabled

    REQUIRE(t.win->set_disabled(button, false) == dg::Status::kOk);
    t.press(x, y);
    const std::vector<ev::ClickEvent> clicks = of<ev::ClickEvent>(t.pump());
    REQUIRE(clicks.size() == 1);
    CHECK(clicks[0].node == button);
  }

  TEST_CASE("a secondary press is a ContextClickEvent on the node under it") {
    Rt t;
    const dg::NodeId button = t.add(dg::NodeKind::kButton);
    (void)t.pump(1);
    t.press(20, 20, dg::PointerButton::kSecondary);
    const std::vector<dg::Event> events = t.pump();
    CHECK(of<ev::ClickEvent>(events).empty());
    const std::vector<ev::ContextClickEvent> context = of<ev::ContextClickEvent>(events);
    REQUIRE(context.size() == 1);
    CHECK(context[0].node == button);
    CHECK(context[0].x == 20);
    CHECK(context[0].y == 20);
  }

  TEST_CASE("a list selects on a press through ListRef") {
    Rt t;
    const dg::NodeId node = t.add(dg::NodeKind::kList);
    dg::Expected<dg::ListRef, dg::Status> list = t.win->list(node);
    REQUIRE(list.has_value());
    CHECK(list.value().node() == node);
    std::vector<std::string> items;
    for (int i = 0; i < 100; ++i) {
      items.push_back("item " + std::to_string(i));
    }
    REQUIRE(list.value().set_items(items, 20) == dg::Status::kOk);
    CHECK(list.value().selected().value() == -1);
    (void)t.pump(1);

    t.press(50, 50);  // row 2
    const std::vector<ev::ListSelectEvent> select = of<ev::ListSelectEvent>(t.pump());
    REQUIRE(select.size() == 1);
    CHECK(select[0].node == node);
    CHECK(select[0].index == 2);
    CHECK(list.value().selected().value() == 2);

    CHECK(list.value().select(100) == dg::Status::kValueOutOfRange);
    CHECK(list.value().select(-1) == dg::Status::kOk);
    CHECK(list.value().set_items({}, -1) == dg::Status::kInvalidArgument);
    CHECK(t.win->list(t.root).error() == dg::Status::kUnsupported);

    REQUIRE(t.win->remove(node) == dg::Status::kOk);
    CHECK(list.value().selected().error() == dg::Status::kInvalidNode);
    CHECK(t.win->list(node).error() == dg::Status::kInvalidNode);
  }

  TEST_CASE("text field: Mod+A/X/V cut and paste through the clipboard") {
    Rt t;
    const dg::NodeId field = t.add(dg::NodeKind::kTextField);
    REQUIRE(t.win->set_text(field, DG_PROP_TEXT, "ab") == dg::Status::kOk);
    REQUIRE(t.win->focus(field) == dg::Status::kOk);
    (void)t.pump(1);
    t.chord(dg::LogicalKey::kA);
    t.chord(dg::LogicalKey::kX);
    CHECK(of<ev::TextChangedEvent>(t.pump()).size() == 1);
    CHECK(t.win->text(field).value().empty());
    CHECK(t.wm().get_clipboard_text() == "ab");
    t.chord(dg::LogicalKey::kV);
    const std::vector<ev::TextChangedEvent> changed = of<ev::TextChangedEvent>(t.pump());
    REQUIRE(changed.size() == 1);
    CHECK(changed[0].node == field);
    CHECK(t.win->text(field).value() == "ab");
  }

  TEST_CASE("text area: typing, Mod+Z undo and Mod+Shift+Z redo") {
    Rt t;
    const dg::NodeId area = t.add(dg::NodeKind::kTextArea);
    REQUIRE(t.win->focus(area) == dg::Status::kOk);
    (void)t.pump(1);
    // One pump each: a pump applies its text input before its keys.
    t.wm().post_text_input(t.win->id(), "x");
    (void)t.pump(1);
    t.key(dg::Key::kEnter);
    (void)t.pump(1);
    t.wm().post_text_input(t.win->id(), "y");
    (void)t.pump();
    CHECK(t.win->text(area).value() == "x\ny");
    t.chord(dg::LogicalKey::kZ);
    (void)t.pump();
    const std::string undone = t.win->text(area).value();
    CHECK(undone != "x\ny");
    t.chord(dg::LogicalKey::kZ, dg::Modifier::kMod | dg::Modifier::kShift);
    (void)t.pump();
    CHECK(t.win->text(area).value() == "x\ny");
  }
}
