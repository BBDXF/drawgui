// App shortcuts, the menu bar, menu shortcut labels/commands and form
// dialogs through the C ABI, headless (SDL's dummy driver).

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/abi/drawgui.h"
#include "drawgui/render/prop_ids.generated.h"

namespace {

dg_value length(float value) {
  dg_value out{};
  out.size = sizeof(dg_value);
  out.type = DG_VALUE_LENGTH;
  out.number = value;
  return out;
}

dg_value option(std::uint32_t ordinal) {
  dg_value out{};
  out.size = sizeof(dg_value);
  out.type = DG_VALUE_ENUM;
  out.bits = ordinal;
  return out;
}

struct App {
  dg_app_t* app = nullptr;
  dg_window_t* window = nullptr;
  dg_node_t* root = nullptr;

  App() {
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    app = dg_app_create(nullptr);
    REQUIRE(app != nullptr);
    REQUIRE(dg_app_add_font(app, DG_TEST_FONT_DIR, "DgTest Latin") == DG_ERR_OK);
    dg_window_opts opts{};
    opts.size = sizeof(opts);
    opts.title = "abi menus";
    opts.width = 480;
    opts.height = 360;
    window = dg_window_create(app, &opts);
    REQUIRE(window != nullptr);
    root = dg_node_create(app, DG_NODE_TYPE_BOX);
    REQUIRE(dg_window_set_root(window, root) == DG_ERR_OK);
    set(root, DG_PROP_WIDTH, length(480));
    set(root, DG_PROP_HEIGHT, length(360));
    set(root, DG_PROP_DIRECTION, option(DG_DIRECTION_COLUMN));
    (void)pump(2);
  }
  App(const App&) = delete;
  App& operator=(const App&) = delete;
  App(App&&) = delete;
  App& operator=(App&&) = delete;
  ~App() { dg_app_destroy(app); }

  dg_node_t* add(std::uint16_t type, dg_node_t* parent = nullptr) const {
    dg_node_t* created = dg_node_create(app, type);
    REQUIRE(created != nullptr);
    REQUIRE(dg_node_insert_before(parent != nullptr ? parent : root, created, nullptr) ==
            DG_ERR_OK);
    return created;
  }

  static void set(dg_node_t* target, std::uint16_t prop, const dg_value& value) {
    REQUIRE(dg_node_set_prop(target, prop, &value) == DG_ERR_OK);
  }

  std::vector<dg_event> pump(int rounds = 3) const {
    std::vector<dg_event> all;
    for (int i = 0; i < rounds; ++i) {
      REQUIRE(dg_wait_events(app, 5) >= 0);
      dg_event events[64];
      const std::int32_t n = dg_poll_events(app, events, 64);
      all.insert(all.end(), events, events + n);
    }
    return all;
  }

  void press(int x, int y) const {
    REQUIRE(dg_debug_warp_pointer(window, x, y) == DG_ERR_OK);
    REQUIRE(dg_debug_post_pointer_button(window, 1, x, y) == DG_ERR_OK);
    REQUIRE(dg_debug_post_pointer_button(window, 0, x, y) == DG_ERR_OK);
  }

  void key(const char* name) const {
    REQUIRE(dg_debug_post_text_field_key(window, 1, name, 0) == DG_ERR_OK);
    REQUIRE(dg_debug_post_text_field_key(window, 0, name, 0) == DG_ERR_OK);
  }

  void chord(const char* chord) const {
    REQUIRE(dg_debug_post_key(window, 1, chord) == DG_ERR_OK);
    REQUIRE(dg_debug_post_key(window, 0, chord) == DG_ERR_OK);
  }
};

std::vector<dg_event> of_kind(const std::vector<dg_event>& events, std::uint32_t kind) {
  std::vector<dg_event> out;
  for (const dg_event& event : events) {
    if (event.kind == kind) {
      out.push_back(event);
    }
  }
  return out;
}

std::string text_of(dg_node_t* node) {
  char buf[64] = {};
  std::uint32_t len = 0;
  REQUIRE(dg_node_get_text(node, buf, sizeof(buf), &len) == DG_ERR_OK);
  return std::string(buf, len);
}

}  // namespace

TEST_SUITE("abi menus") {
  TEST_CASE("app and window shortcuts, chord labels") {
    App a;
    CHECK(dg_app_add_shortcut(a.app, "Ctrl+Shift+N", DG_USER_ACTION_BASE + 1) == DG_ERR_OK);
    CHECK(dg_app_add_shortcut(a.app, "Ctrl+N", 3) == DG_ERR_INVALID_ARGUMENT);
    CHECK(dg_app_add_shortcut(a.app, "Ctrl+Nope", DG_USER_ACTION_BASE) ==
          DG_ERR_INVALID_ARGUMENT);
    CHECK(std::strcmp(dg_chord_label(a.app, "shift+ctrl+n"), "Ctrl+Shift+N") == 0);
    CHECK(dg_chord_label(a.app, "Ctrl+") == nullptr);

    a.chord("Mod+Shift+N");
    std::vector<dg_event> action = of_kind(a.pump(), DG_EVENT_ACTION);
    REQUIRE(action.size() == 1);
    CHECK(action[0].action_id == DG_USER_ACTION_BASE + 1);
    CHECK(action[0].node == nullptr);

    dg_node_t* panel = a.add(DG_NODE_TYPE_BOX);
    dg_node_t* field = a.add(DG_NODE_TYPE_TEXT_FIELD, panel);
    (void)a.pump(1);
    CHECK(dg_window_add_shortcut(panel, "F2", DG_USER_ACTION_BASE + 2) == DG_ERR_OK);
    CHECK(dg_window_add_shortcut(panel, "F2", DG_USER_ACTION_BASE + 3) != DG_ERR_OK);
    REQUIRE(dg_node_focus(field) == DG_ERR_OK);
    a.chord("F2");
    action = of_kind(a.pump(), DG_EVENT_ACTION);
    REQUIRE(action.size() == 1);
    CHECK(action[0].action_id == DG_USER_ACTION_BASE + 2);
    CHECK(action[0].node == panel);

    CHECK(dg_app_remove_shortcut(a.app, "F2") == DG_ERR_OK);
    CHECK(dg_app_remove_shortcut(a.app, "F2") == DG_ERR_UNKNOWN_ID);
  }

  TEST_CASE("menu bar and dg_menu_open_ex: commands and shortcut labels") {
    App a;
    dg_node_t* bar = a.add(DG_NODE_TYPE_MENUBAR);
    const char* titles[] = {"File", "Edit"};
    const std::uint32_t counts[] = {2, 1};
    const char* labels[] = {"New", "Open", "Undo"};
    const char* shortcuts[] = {"Ctrl+N", nullptr, "Ctrl+Z"};
    const std::int32_t commands[] = {10, 11, 20};
    REQUIRE(dg_menubar_set_menus(bar, titles, counts, 2, labels, shortcuts, nullptr, commands,
                                 3) == DG_ERR_OK);
    CHECK(dg_menubar_set_menus(bar, titles, counts, 2, labels, nullptr, nullptr, nullptr, 2) ==
          DG_ERR_INVALID_ARGUMENT);
    (void)a.pump(2);

    // Keyboard: F10, Right, Down opens Edit; Enter chooses Undo.
    a.chord("F10");
    a.key("Right");
    a.key("Down");
    a.key("Enter");
    std::vector<dg_event> menu = of_kind(a.pump(), DG_EVENT_MENU_ACTIVATE);
    REQUIRE(menu.size() == 1);
    CHECK(menu[0].node == bar);
    CHECK(menu[0].column == 1);
    CHECK(menu[0].item == 0);
    CHECK(menu[0].command == 20);

    // Mouse: the File title sits at the bar's left edge.
    a.press(12, 14);
    (void)a.pump();
    a.key("Down");
    a.key("Enter");
    menu = of_kind(a.pump(), DG_EVENT_MENU_ACTIVATE);
    REQUIRE(menu.size() == 1);
    CHECK(menu[0].column == 0);
    CHECK(menu[0].command == 11);

    CHECK(dg_menubar_set_entry(bar, 0, 1, "Open...", "Ctrl+O", DG_MENU_ITEM_DISABLED, 12) ==
          DG_ERR_OK);
    CHECK(dg_menubar_set_entry(bar, 3, 0, "x", nullptr, 0, 0) == DG_ERR_VALUE_OUT_OF_RANGE);

    const char* items[] = {"Save", "Quit"};
    const char* accel[] = {"Ctrl+S", "Ctrl+Q"};
    const std::int32_t ids[] = {7, 8};
    const std::int32_t id =
        dg_menu_open_ex(a.window, nullptr, 40, 80, items, nullptr, accel, ids, 2);
    REQUIRE(id > 0);
    (void)a.pump(1);
    a.key("Down");
    a.key("Enter");
    menu = of_kind(a.pump(), DG_EVENT_MENU_ACTIVATE);
    REQUIRE(menu.size() == 1);
    CHECK(menu[0].y == id);
    CHECK(menu[0].command == 8);
    CHECK(menu[0].column == -1);
  }

  TEST_CASE("form dialog: dg_dialog_open_ex content, disabled default button") {
    App a;
    dg_node_t* field = a.add(DG_NODE_TYPE_TEXT_FIELD);
    (void)a.pump(1);
    const char* buttons[] = {"OK", "Cancel"};
    const std::int32_t id =
        dg_dialog_open_ex(a.window, "Rename", "Name:", buttons, 2, 0, 1, field);
    REQUIRE(id > 0);
    (void)a.pump(1);
    CHECK(dg_dialog_set_button_disabled(a.window, id, 0, 1) == DG_ERR_OK);
    CHECK(dg_dialog_set_button_disabled(a.window, id, 9, 1) == DG_ERR_VALUE_OUT_OF_RANGE);
    CHECK(dg_dialog_set_button_disabled(a.window, id + 50, 0, 1) == DG_ERR_UNKNOWN_ID);
    REQUIRE(dg_node_focus(field) == DG_ERR_OK);
    REQUIRE(dg_debug_post_text_input(a.window, "ok") == DG_ERR_OK);
    a.key("Enter");
    CHECK(of_kind(a.pump(), DG_EVENT_DIALOG_RESULT).empty());
    CHECK(text_of(field) == "ok");
    CHECK(dg_dialog_set_button_disabled(a.window, id, 0, 0) == DG_ERR_OK);
    a.key("Enter");
    const std::vector<dg_event> result = of_kind(a.pump(), DG_EVENT_DIALOG_RESULT);
    REQUIRE(result.size() == 1);
    CHECK(result[0].x == 0);
    CHECK(result[0].y == id);
    CHECK(text_of(field) == "ok");
  }
}
