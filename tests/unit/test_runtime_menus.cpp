// App-defined shortcuts, the keyboard context menu, the menu bar, menu
// shortcut labels/commands and form dialogs, through the C++ runtime.

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "runtime_fixture.h"

using runtime_test::of;
using runtime_test::Rt;
namespace ev = dg::events;

namespace {

constexpr std::uint16_t kNew = dg::kUserActionBase + 1;
constexpr std::uint16_t kRename = dg::kUserActionBase + 2;
constexpr std::uint16_t kGlobalRename = dg::kUserActionBase + 3;
constexpr std::uint16_t kPage = dg::kUserActionBase + 4;
constexpr std::uint16_t kTyping = dg::kUserActionBase + 5;

std::vector<std::uint16_t> actions(const std::vector<dg::Event>& events) {
  std::vector<std::uint16_t> out;
  for (const ev::ActionEvent& e : of<ev::ActionEvent>(events)) {
    out.push_back(e.action_id);
  }
  return out;
}

dg::MenuEntry entry(std::string text, std::int32_t command, std::string shortcut = {}) {
  dg::MenuEntry e;
  e.text = std::move(text);
  e.command = command;
  e.shortcut_label = std::move(shortcut);
  return e;
}

// A File / Edit menu bar at the top of the root.
struct Bar {
  dg::NodeId node;
  std::vector<dg::NodeId> titles;
};

Bar make_bar(Rt& t) {
  Bar bar;
  bar.node = t.add(dg::NodeKind::kMenuBar);
  dg::MenuBarRef ref = t.win->menu_bar(bar.node).value();
  std::vector<dg::MenuBarMenu> menus(2);
  menus[0].title = "File";
  menus[0].entries = {entry("New", 10, "Ctrl+N"), entry("Open", 11)};
  menus[1].title = "Edit";
  menus[1].entries = {entry("Undo", 20), entry("Redo", 21)};
  REQUIRE(ref.set_menus(menus) == dg::Status::kOk);
  (void)t.pump(2);
  bar.titles = t.win->layout().render().children(bar.node);
  REQUIRE(bar.titles.size() == 2);
  return bar;
}

dg::PixelPoint centre(const Rt& t, dg::NodeId node) {
  const dg::PixelRect r = t.bounds(node);
  return {r.x + (r.width / 2), r.y + (r.height / 2)};
}

void key_chord(Rt& t, dg::LogicalKey key, dg::Modifier mods = dg::Modifier::kNone) {
  t.wm().post_logical_key(t.win->id(), true, key, mods);
  t.wm().post_logical_key(t.win->id(), false, key, mods);
}

bool has_text(const Rt& t, const std::string& text) {
  const dg::RenderTree& tree = t.win->layout().render();
  const std::vector<dg::NodeId> all = dg::subtree_of(tree, dg::RenderTree::root());
  return std::any_of(all.begin(), all.end(),
                     [&](dg::NodeId id) { return tree.style(id).text.text == text; });
}

bool inside(const Rt& t, dg::NodeId ancestor, dg::NodeId node) {
  const dg::RenderTree& tree = t.win->layout().render();
  for (dg::NodeId at = node;; at = tree.parent(at)) {
    if (at == ancestor) {
      return true;
    }
    if (tree.parent(at) == at) {
      return false;
    }
  }
}

}  // namespace

TEST_SUITE("runtime menus") {
  TEST_CASE("app shortcuts: level 4, chord labels, replacement, removal, bad input") {
    Rt t;
    CHECK(t.app.add_shortcut("Ctrl+Shift+N", kNew) == dg::Status::kOk);
    CHECK(t.app.chord_label("ctrl+shift+n").value() == "Ctrl+Shift+N");
    CHECK(t.app.chord_label("Alt+F4").value() == "Alt+F4");
    CHECK(t.app.chord_label("Shift+Ctrl+Delete").value() == "Ctrl+Shift+Delete");
    CHECK(!t.app.chord_label("Ctrl+Nope").has_value());
    CHECK(!t.app.chord_label("Ctrl+").has_value());
    CHECK(t.app.add_shortcut("Ctrl+Bogus", kNew) == dg::Status::kInvalidArgument);
    CHECK(t.app.add_shortcut("Ctrl+K", 5) == dg::Status::kInvalidArgument);

    t.chord(dg::LogicalKey::kN, dg::Modifier::kMod | dg::Modifier::kShift);
    std::vector<dg::Event> got = t.pump();
    REQUIRE(actions(got) == std::vector<std::uint16_t>{kNew});
    CHECK(!of<ev::ActionEvent>(got)[0].node.has_value());

    // Rebinding the chord at app level replaces it.
    CHECK(t.app.add_shortcut("Mod+Shift+N", kRename) == dg::Status::kOk);
    t.chord(dg::LogicalKey::kN, dg::Modifier::kMod | dg::Modifier::kShift);
    CHECK(actions(t.pump()) == std::vector<std::uint16_t>{kRename});

    CHECK(t.app.remove_shortcut("Ctrl+Shift+N") == dg::Status::kOk);
    CHECK(t.app.remove_shortcut("Ctrl+Shift+N") == dg::Status::kUnknownId);
    t.chord(dg::LogicalKey::kN, dg::Modifier::kMod | dg::Modifier::kShift);
    CHECK(actions(t.pump()).empty());
  }

  TEST_CASE("window shortcuts: level 2 inside the scope, shadowing, conflicts") {
    Rt t;
    const dg::NodeId panel = t.add(dg::NodeKind::kBox);
    const dg::NodeId field = t.add(dg::NodeKind::kTextField, panel);
    const dg::NodeId outside = t.add(dg::NodeKind::kTextField);
    (void)t.pump(1);
    REQUIRE(t.win->add_shortcut(panel, "F2", kRename) == dg::Status::kOk);
    CHECK(t.win->add_shortcut(panel, "f2", kGlobalRename) == dg::Status::kAlreadyAttached);
    CHECK(t.win->add_shortcut(panel, "F99", kRename) == dg::Status::kInvalidArgument);
    REQUIRE(t.app.add_shortcut("F2", kGlobalRename) == dg::Status::kOk);

    // F2 is not an editing key: inside the scope the window binding wins...
    REQUIRE(t.win->focus(field) == dg::Status::kOk);
    key_chord(t, dg::LogicalKey::kF2);
    std::vector<dg::Event> got = t.pump();
    REQUIRE(actions(got) == std::vector<std::uint16_t>{kRename});
    CHECK(of<ev::ActionEvent>(got)[0].node == panel);
    // ...outside it the app binding fires.
    REQUIRE(t.win->focus(outside) == dg::Status::kOk);
    key_chord(t, dg::LogicalKey::kF2);
    CHECK(actions(t.pump()) == std::vector<std::uint16_t>{kGlobalRename});
    t.win->clear_focus();
    key_chord(t, dg::LogicalKey::kF2);
    CHECK(actions(t.pump()) == std::vector<std::uint16_t>{kGlobalRename});

    // A user binding shadows the generated PageDown (scroll_page_down).
    key_chord(t, dg::LogicalKey::kPageDown);
    CHECK(actions(t.pump()) == std::vector<std::uint16_t>{DG_ACTION_SCROLL_PAGE_DOWN});
    REQUIRE(t.app.add_shortcut("PageDown", kPage) == dg::Status::kOk);
    key_chord(t, dg::LogicalKey::kPageDown);
    CHECK(actions(t.pump()) == std::vector<std::uint16_t>{kPage});

    // remove_shortcut() drops the window binding too.
    CHECK(t.app.remove_shortcut("F2") == dg::Status::kOk);
    REQUIRE(t.win->focus(field) == dg::Status::kOk);
    key_chord(t, dg::LogicalKey::kF2);
    CHECK(actions(t.pump()).empty());
  }

  TEST_CASE("editor keys win over app shortcuts; typing keys are not shortcuts in an editor") {
    Rt t;
    const dg::NodeId field = t.add(dg::NodeKind::kTextField);
    (void)t.pump(1);
    REQUIRE(t.app.add_shortcut("Delete", kNew) == dg::Status::kOk);
    REQUIRE(t.app.add_shortcut("Ctrl+Z", kRename) == dg::Status::kOk);
    REQUIRE(t.app.add_shortcut("K", kTyping) == dg::Status::kOk);
    REQUIRE(t.win->focus(field) == dg::Status::kOk);
    t.key(dg::Key::kDelete);
    t.chord(dg::LogicalKey::kZ);
    key_chord(t, dg::LogicalKey::kK);
    CHECK(actions(t.pump()).empty());

    t.win->clear_focus();
    t.key(dg::Key::kDelete);
    CHECK(actions(t.pump()) == std::vector<std::uint16_t>{kNew});
    t.chord(dg::LogicalKey::kZ);
    CHECK(actions(t.pump()) == std::vector<std::uint16_t>{kRename});
    key_chord(t, dg::LogicalKey::kK);
    CHECK(actions(t.pump()) == std::vector<std::uint16_t>{kTyping});
  }

  TEST_CASE("keyboard context menu: Shift+F10 on a table names its selected row; Menu key") {
    Rt t;
    const dg::NodeId node = t.add(dg::NodeKind::kTable);
    dg::TableRef table = t.win->table(node).value();
    std::vector<dg::TableColumnSpec> columns(1);
    columns[0].title = "Name";
    REQUIRE(table.set_columns(columns) == dg::Status::kOk);
    REQUIRE(table.set_rows({{"a"}, {"b"}, {"c"}}) == dg::Status::kOk);
    REQUIRE(table.select(1) == dg::Status::kOk);
    (void)t.pump(1);
    REQUIRE(t.win->focus(node) == dg::Status::kOk);
    (void)t.pump(1);
    key_chord(t, dg::LogicalKey::kF10, dg::Modifier::kShift);
    std::vector<ev::ContextClickEvent> context = of<ev::ContextClickEvent>(t.pump());
    REQUIRE(context.size() == 1);
    CHECK(context[0].node == node);
    CHECK(context[0].item == 1);
    CHECK(context[0].clicks == 0);
    const dg::PixelRect bounds = t.bounds(node);
    CHECK(context[0].x == bounds.x);
    CHECK(context[0].y == bounds.y + bounds.height);

    // Nothing focused: the Menu key reports the window root, no item.
    t.win->clear_focus();
    key_chord(t, dg::LogicalKey::kMenu);
    context = of<ev::ContextClickEvent>(t.pump());
    REQUIRE(context.size() == 1);
    CHECK(context[0].node == t.root);
    CHECK(!context[0].item.has_value());
  }

  TEST_CASE("menu bar: click opens, hover switches, a choice reports bar_menu/item/command") {
    Rt t;
    const Bar bar = make_bar(t);
    const int baseline = t.count();
    const dg::PixelPoint file = centre(t, bar.titles[0]);
    const dg::PixelPoint edit = centre(t, bar.titles[1]);
    CHECK(file.x < edit.x);

    t.press(file.x, file.y);
    CHECK(of<ev::ClickEvent>(t.pump()).empty());
    CHECK(t.count() > baseline);
    // The File menu hangs below its title: its first row is "New".
    const dg::PixelRect title = t.bounds(bar.titles[0]);
    t.press(title.x + 20, title.y + title.height + 18);
    std::vector<ev::MenuEvent> menu = of<ev::MenuEvent>(t.pump());
    REQUIRE(menu.size() == 1);
    CHECK(menu[0].anchor == bar.node);
    CHECK(menu[0].bar_menu == 0);
    CHECK(menu[0].item == 0);
    CHECK(menu[0].command == 10);
    CHECK(t.count() == baseline);

    // Open File, hover Edit: Edit replaces it without a dismissal.
    t.press(file.x, file.y);
    (void)t.pump();
    t.wm().warp_pointer(t.win->id(), edit.x, edit.y);
    CHECK(of<ev::MenuEvent>(t.pump()).empty());
    t.key(dg::Key::kEnter);
    menu = of<ev::MenuEvent>(t.pump());
    REQUIRE(menu.size() == 1);
    CHECK(menu[0].bar_menu == 1);
    CHECK(menu[0].command == 20);

    // A press on the open title closes its menu (a dismissal).
    t.press(file.x, file.y);
    (void)t.pump();
    t.press(file.x, file.y);
    menu = of<ev::MenuEvent>(t.pump());
    REQUIRE(menu.size() == 1);
    CHECK(!menu[0].item.has_value());
    CHECK(menu[0].bar_menu == 0);
    CHECK(t.count() == baseline);
  }

  TEST_CASE("menu bar keyboard: F10, Right, Down opens Edit; Left/Right switch; Escape twice") {
    Rt t;
    const Bar bar = make_bar(t);
    key_chord(t, dg::LogicalKey::kF10);
    t.key(dg::Key::kRight);
    t.key(dg::Key::kDown);
    t.key(dg::Key::kDown);  // Redo
    t.key(dg::Key::kEnter);
    std::vector<ev::MenuEvent> menu = of<ev::MenuEvent>(t.pump());
    REQUIRE(menu.size() == 1);
    CHECK(menu[0].bar_menu == 1);
    CHECK(menu[0].item == 1);
    CHECK(menu[0].command == 21);

    // Alt press+release activates too; inside an open menu Right moves on.
    key_chord(t, dg::LogicalKey::kAltKey);
    t.key(dg::Key::kEnter);  // opens File
    t.key(dg::Key::kRight);  // Edit
    t.key(dg::Key::kLeft);   // back to File
    t.key(dg::Key::kEnter);
    menu = of<ev::MenuEvent>(t.pump());
    REQUIRE(menu.size() == 1);
    CHECK(menu[0].bar_menu == 0);
    CHECK(menu[0].command == 10);

    // Escape closes the menu (a dismissal), the bar stays active: Down
    // reopens; Escape twice leaves keyboard mode, so Down then does nothing.
    key_chord(t, dg::LogicalKey::kF10);
    t.key(dg::Key::kDown);
    t.key(dg::Key::kEscape);
    menu = of<ev::MenuEvent>(t.pump());
    REQUIRE(menu.size() == 1);
    CHECK(!menu[0].item.has_value());
    t.key(dg::Key::kDown);
    t.key(dg::Key::kEnter);
    menu = of<ev::MenuEvent>(t.pump());
    REQUIRE(menu.size() == 1);
    CHECK(menu[0].command == 10);
    key_chord(t, dg::LogicalKey::kF10);
    t.key(dg::Key::kEscape);
    t.key(dg::Key::kEscape);
    t.key(dg::Key::kDown);
    t.key(dg::Key::kEnter);
    CHECK(of<ev::MenuEvent>(t.pump()).empty());

    dg::MenuBarRef ref = t.win->menu_bar(bar.node).value();
    CHECK(ref.set_entry(1, 0, entry("Undo typing", 22)) == dg::Status::kOk);
    CHECK(ref.set_entry(2, 0, entry("x", 0)) == dg::Status::kValueOutOfRange);
    CHECK(ref.set_entry(1, 5, entry("x", 0)) == dg::Status::kValueOutOfRange);
    key_chord(t, dg::LogicalKey::kF10);
    t.key(dg::Key::kRight);
    t.key(dg::Key::kEnter);
    t.key(dg::Key::kEnter);
    menu = of<ev::MenuEvent>(t.pump());
    REQUIRE(menu.size() == 1);
    CHECK(menu[0].command == 22);
    CHECK(!t.win->menu_bar(t.root).has_value());
  }

  TEST_CASE("open_menu: shortcut labels are drawn, commands come back") {
    Rt t;
    const std::vector<dg::MenuEntry> entries = {entry("Save", 7, "Ctrl+S"), entry("Quit", 8)};
    const std::int32_t id = t.win->open_menu(std::nullopt, {20, 20}, entries).value();
    (void)t.pump(1);
    CHECK(has_text(t, "Ctrl+S"));
    t.key(dg::Key::kDown);
    t.key(dg::Key::kEnter);
    const std::vector<ev::MenuEvent> menu = of<ev::MenuEvent>(t.pump());
    REQUIRE(menu.size() == 1);
    CHECK(menu[0].menu_id == id);
    CHECK(menu[0].item == 1);
    CHECK(menu[0].command == 8);
    CHECK(!menu[0].bar_menu.has_value());
    CHECK(!has_text(t, "Ctrl+S"));
  }

  TEST_CASE("form dialog: content in the body, focus trap, Enter, disabled default, return") {
    Rt t;
    const dg::NodeId form = t.add(dg::NodeKind::kBox);
    t.set(form, DG_PROP_DIRECTION, dg::PropValue::option(DG_DIRECTION_COLUMN));
    const dg::NodeId field = t.add(dg::NodeKind::kTextField, form);
    t.set(field, DG_PROP_HEIGHT, dg::PropValue::length(32.0F));
    (void)t.pump(1);

    dg::DialogOptions options;
    options.title = "Rename";
    options.body = "New name:";
    options.buttons = {"OK", "Cancel"};
    options.cancel_button = 1;
    options.content = form;
    const std::int32_t id = t.win->open_dialog(options).value();
    (void)t.pump(1);
    const dg::PixelRect at = t.bounds(field);
    CHECK(at.height == 32);
    CHECK(!inside(t, t.root, form));

    // Tab cycles OK -> Cancel -> the field (and stays inside the dialog).
    t.key(dg::Key::kTab);
    t.key(dg::Key::kTab);
    (void)t.pump();
    CHECK(t.win->focused() == field);
    t.wm().post_text_input(t.win->id(), "doc");
    (void)t.pump();
    t.key(dg::Key::kLeft);
    t.key(dg::Key::kBackspace);
    (void)t.pump();
    CHECK(t.win->text(field).value() == "dc");
    // A click in the content edits it, not the dialog.
    t.press(at.x + 4, at.y + 4);
    CHECK(of<ev::DialogResultEvent>(t.pump()).empty());

    // A disabled default button blocks Enter.
    CHECK(t.win->set_dialog_button_disabled(id, 0, true) == dg::Status::kOk);
    CHECK(t.win->set_dialog_button_disabled(id, 2, true) == dg::Status::kValueOutOfRange);
    CHECK(t.win->set_dialog_button_disabled(id + 100, 0, true) == dg::Status::kUnknownId);
    t.key(dg::Key::kEnter);
    CHECK(of<ev::DialogResultEvent>(t.pump()).empty());
    CHECK(t.win->set_dialog_button_disabled(id, 0, false) == dg::Status::kOk);
    REQUIRE(t.win->focus(field) == dg::Status::kOk);
    t.key(dg::Key::kEnter);
    std::vector<ev::DialogResultEvent> result = of<ev::DialogResultEvent>(t.pump());
    REQUIRE(result.size() == 1);
    CHECK(result[0].button == 0);
    CHECK(result[0].dialog_id == id);

    // Returned alive and detached; the app can put it back.
    CHECK(t.win->text(field).value() == "dc");
    CHECK(!inside(t, t.root, form));
    CHECK(t.win->set_dialog_button_disabled(id, 0, true) == dg::Status::kUnknownId);
    CHECK(t.win->move(form, t.root) == dg::Status::kOk);
    (void)t.pump();
    CHECK(inside(t, t.root, field));

    // Re-used: Escape cancels.
    REQUIRE(t.win->open_dialog(options).has_value());
    (void)t.pump(1);
    t.key(dg::Key::kEscape);
    result = of<ev::DialogResultEvent>(t.pump());
    REQUIRE(result.size() == 1);
    CHECK(result[0].button == 1);
  }

  TEST_CASE("form dialog: Enter inside a text area is a newline; close_dialog() returns content") {
    Rt t;
    const dg::NodeId area = t.add(dg::NodeKind::kTextArea);
    (void)t.pump(1);
    dg::DialogOptions options;
    options.title = "Notes";
    options.content = area;
    REQUIRE(t.win->open_dialog(options).has_value());
    (void)t.pump(1);
    REQUIRE(t.win->focus(area) == dg::Status::kOk);
    t.wm().post_text_input(t.win->id(), "a");
    (void)t.pump();
    t.key(dg::Key::kEnter);
    CHECK(of<ev::DialogResultEvent>(t.pump()).empty());
    CHECK(t.win->text(area).value() == "a\n");
    t.win->close_dialog();
    (void)t.pump();
    CHECK(!t.win->has_dialog());
    CHECK(t.win->text(area).value() == "a\n");
    CHECK(!inside(t, t.root, area));
    // The window root itself cannot be dialog content.
    options.content = t.root;
    CHECK(t.win->open_dialog(options).error() == dg::Status::kInvalidArgument);
  }
}
