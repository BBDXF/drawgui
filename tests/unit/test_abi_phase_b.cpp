// ABI 0.2 phase B through the C ABI, headless (SDL's dummy driver): menus,
// dropdowns, tooltips, dialogs, progress bars, tabs, trees and tables -
// event routing through the real pump, the typed results as dg_events, and
// node-count baselines after every close/remove (dg_debug_node_count()).

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/abi/drawgui.h"
#include "drawgui/render/prop_ids.generated.h"

namespace {

dg_value number(float value) {
  dg_value out{};
  out.size = sizeof(dg_value);
  out.type = DG_VALUE_FLOAT;
  out.number = value;
  return out;
}

dg_value length(float value) {
  dg_value out{};
  out.size = sizeof(dg_value);
  out.type = DG_VALUE_LENGTH;
  out.number = value;
  return out;
}

dg_value option(std::uint32_t ordinal, std::uint32_t type = DG_VALUE_ENUM) {
  dg_value out{};
  out.size = sizeof(dg_value);
  out.type = type;
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
    opts.title = "abi phase b";
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

  dg_node_t* add(std::uint16_t type) const {
    dg_node_t* created = dg_node_create(app, type);
    REQUIRE(created != nullptr);
    REQUIRE(dg_node_insert_before(root, created, nullptr) == DG_ERR_OK);
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

  [[nodiscard]] int count() const { return dg_debug_node_count(window); }

  void press(int x, int y, int button = DG_POINTER_BUTTON_PRIMARY) const {
    REQUIRE(dg_debug_warp_pointer(window, x, y) == DG_ERR_OK);
    REQUIRE(dg_debug_post_pointer_button_ex(window, 1, x, y, button, 0) == DG_ERR_OK);
    REQUIRE(dg_debug_post_pointer_button_ex(window, 0, x, y, button, 0) == DG_ERR_OK);
  }

  void key(const char* name, bool shift = false) const {
    REQUIRE(dg_debug_post_text_field_key(window, 1, name, shift ? 1 : 0) == DG_ERR_OK);
    REQUIRE(dg_debug_post_text_field_key(window, 0, name, shift ? 1 : 0) == DG_ERR_OK);
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

// A field of root's index-th child in dg_dump_layout_tree(root) - root sits
// at the window origin, so these are window coordinates.
int child_field(dg_node_t* parent, int index, const char* field) {
  const std::string dump = dg_dump_layout_tree(parent);
  std::size_t at = dump.find("\"children\":[");
  REQUIRE(at != std::string::npos);
  at += 12;
  int depth = 0;
  int seen = -1;
  for (std::size_t i = at; i < dump.size(); ++i) {
    if (dump[i] == '{') {
      if (depth == 0 && ++seen == index) {
        const std::string key = std::string("\"") + field + "\":";
        return std::atoi(dump.c_str() + dump.find(key, i) + key.size());
      }
      ++depth;
    } else if (dump[i] == '}') {
      --depth;
    }
  }
  FAIL("no such child");
  return 0;
}

long long elapsed_ms(std::chrono::steady_clock::time_point since) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now() - since)
      .count();
}

}  // namespace

TEST_SUITE("abi phase b") {
  TEST_CASE("menus: keys, clicks, right-click, outside dismissal, replacement, baselines") {
    App a;
    const int baseline = a.count();
    const char* labels[] = {"Cut", "", "Paste", "Delete"};
    const std::uint32_t flags[] = {0, DG_MENU_ITEM_SEPARATOR, DG_MENU_ITEM_DISABLED,
                                   DG_MENU_ITEM_CHECKABLE | DG_MENU_ITEM_CHECKED};

    const std::int32_t id = dg_menu_open(a.window, nullptr, 40, 40, labels, flags, 4);
    REQUIRE(id > 0);
    (void)a.pump(1);
    CHECK(a.count() > baseline);
    a.key("Down");  // the first item starts highlighted
    a.key("Enter");
    std::vector<dg_event> activated = of_kind(a.pump(), DG_EVENT_MENU_ACTIVATE);
    REQUIRE(activated.size() == 1);
    CHECK(activated[0].x == 3);  // Down skips the separator and the disabled item
    CHECK(activated[0].y == id);
    CHECK(a.count() == baseline);

    // Escape dismisses.
    const std::int32_t second = dg_menu_open(a.window, nullptr, 40, 40, labels, flags, 4);
    REQUIRE(second > id);
    (void)a.pump(1);
    a.key("Escape");
    std::vector<dg_event> dismissed = of_kind(a.pump(), DG_EVENT_MENU_DISMISS);
    REQUIRE(dismissed.size() == 1);
    CHECK(dismissed[0].y == second);
    CHECK(dismissed[0].x == -1);
    CHECK(a.count() == baseline);

    // A right-click reports its position; a menu opened there takes a click.
    dg_node_t* target = a.add(DG_NODE_TYPE_BOX);
    (void)a.pump(1);
    const int with_target = a.count();
    a.press(60, 60, DG_POINTER_BUTTON_SECONDARY);
    const std::vector<dg_event> context = of_kind(a.pump(), DG_EVENT_CONTEXT_CLICK);
    REQUIRE(context.size() == 1);
    CHECK(context[0].node == target);
    const std::int32_t third =
        dg_menu_open(a.window, context[0].node, context[0].x, context[0].y, labels, flags, 4);
    REQUIRE(third > 0);
    (void)a.pump(1);
    // Anchored below `target` (0,0 240x120): the first row starts under it.
    a.press(40, 120 + 8 + 15);
    activated = of_kind(a.pump(), DG_EVENT_MENU_ACTIVATE);
    REQUIRE(activated.size() == 1);
    CHECK(activated[0].x == 0);
    CHECK(activated[0].node == target);
    CHECK(a.count() == with_target);

    // A click outside dismisses and is consumed; opening over an open menu
    // dismisses the first one.
    REQUIRE(dg_menu_open(a.window, nullptr, 10, 10, labels, flags, 4) > 0);
    const std::int32_t replacing = dg_menu_open(a.window, nullptr, 20, 20, labels, flags, 4);
    std::vector<dg_event> events = a.pump(1);
    a.press(470, 350);
    const std::vector<dg_event> later = a.pump();
    events.insert(events.end(), later.begin(), later.end());
    CHECK(of_kind(events, DG_EVENT_MENU_DISMISS).size() == 2);
    CHECK(of_kind(events, DG_EVENT_MENU_DISMISS).back().y == replacing);
    CHECK(of_kind(events, DG_EVENT_CLICK).empty());
    CHECK(a.count() == with_target);

    // dg_menu_close() closes silently.
    REQUIRE(dg_menu_open(a.window, nullptr, 10, 10, labels, flags, 4) > 0);
    CHECK(dg_menu_close(a.window) == DG_ERR_OK);
    CHECK(of_kind(a.pump(), DG_EVENT_MENU_DISMISS).empty());
    CHECK(a.count() == with_target);

    const std::uint32_t none[] = {DG_MENU_ITEM_DISABLED};
    CHECK(dg_menu_open(a.window, nullptr, 0, 0, labels, none, 1) == DG_ERR_INVALID_ARGUMENT);
    CHECK(dg_menu_open(a.window, nullptr, 0, 0, nullptr, nullptr, 1) ==
          DG_ERR_INVALID_ARGUMENT);
  }

  TEST_CASE("dropdown: open, choose, VALUE_CHANGED, set/get, removal while open") {
    App a;
    const int baseline = a.count();
    dg_node_t* dropdown = a.add(DG_NODE_TYPE_DROPDOWN);
    CHECK(dg_dropdown_get_selected(dropdown) == -1);
    const char* items[] = {"apple", "banana", "cherry"};
    REQUIRE(dg_dropdown_set_items(dropdown, items, 3) == DG_ERR_OK);
    REQUIRE(dg_node_set_text(dropdown, DG_PROP_TEXT, "Pick", 4) == DG_ERR_OK);
    (void)a.pump(1);
    const int closed = a.count();
    a.press(100, 16);
    (void)a.pump(1);
    CHECK(a.count() > closed);
    a.key("Down");
    a.key("Down");
    a.key("Enter");
    std::vector<dg_event> changed = of_kind(a.pump(), DG_EVENT_VALUE_CHANGED);
    REQUIRE(changed.size() == 1);
    CHECK(changed[0].node == dropdown);
    CHECK(changed[0].x == dg_dropdown_get_selected(dropdown));
    CHECK(changed[0].y == -1);
    CHECK(a.count() == closed);

    CHECK(dg_dropdown_set_selected(dropdown, 2) == DG_ERR_OK);
    CHECK(dg_dropdown_get_selected(dropdown) == 2);
    CHECK(dg_dropdown_set_selected(dropdown, 3) == DG_ERR_VALUE_OUT_OF_RANGE);
    CHECK(dg_dropdown_set_selected(dropdown, -1) == DG_ERR_OK);
    CHECK(of_kind(a.pump(1), DG_EVENT_VALUE_CHANGED).empty());

    // Down on the focused dropdown opens it too; removing it while open
    // takes the menu with it.
    REQUIRE(dg_node_focus(dropdown) == DG_ERR_OK);
    a.key("Down");
    (void)a.pump(1);
    CHECK(a.count() > closed);
    REQUIRE(dg_node_remove(dropdown) == DG_ERR_OK);
    (void)a.pump(1);
    CHECK(a.count() == baseline);
    CHECK(dg_dropdown_get_selected(dropdown) == DG_ERR_INVALID_HANDLE);
  }

  TEST_CASE("tooltips: the wait wakes for the delay; moving off or removing hides it") {
    App a;
    REQUIRE(dg_app_set_tooltip_delay(a.app, 60) == DG_ERR_OK);
    CHECK(dg_app_set_tooltip_delay(a.app, -1) == DG_ERR_VALUE_OUT_OF_RANGE);
    dg_node_t* box = a.add(DG_NODE_TYPE_BOX);
    dg_node_t* inner = dg_node_create(a.app, DG_NODE_TYPE_LABEL);
    REQUIRE(dg_node_insert_before(box, inner, nullptr) == DG_ERR_OK);
    REQUIRE(dg_node_set_tooltip(box, "A helpful tip") == DG_ERR_OK);
    (void)a.pump(1);
    const int baseline = a.count();

    REQUIRE(dg_debug_warp_pointer(a.window, 20, 10) == DG_ERR_OK);  // over the child label
    const auto start = std::chrono::steady_clock::now();
    while (a.count() == baseline && elapsed_ms(start) < 3000) {
      REQUIRE(dg_wait_events(a.app, 2000) >= 0);
    }
    CHECK(a.count() > baseline);
    CHECK(elapsed_ms(start) < 1500);  // not the 2000 ms timeout

    REQUIRE(dg_debug_warp_pointer(a.window, 400, 300) == DG_ERR_OK);
    (void)a.pump();
    CHECK(a.count() == baseline);

    REQUIRE(dg_debug_warp_pointer(a.window, 20, 10) == DG_ERR_OK);
    const auto again = std::chrono::steady_clock::now();
    while (a.count() == baseline && elapsed_ms(again) < 3000) {
      REQUIRE(dg_wait_events(a.app, 100) >= 0);
    }
    REQUIRE(a.count() > baseline);
    REQUIRE(dg_node_remove(box) == DG_ERR_OK);
    (void)a.pump();
    CHECK(a.count() == baseline - 2);  // the box and its label went too

    dg_node_t* other = a.add(DG_NODE_TYPE_BOX);
    CHECK(dg_node_set_tooltip(other, nullptr) == DG_ERR_OK);
  }

  TEST_CASE("dialogs: modal, Escape cancels, Enter defaults, Tab moves, ids and baselines") {
    App a;
    dg_node_t* button = a.add(DG_NODE_TYPE_BUTTON);
    (void)a.pump(1);
    const int baseline = a.count();
    const char* buttons[] = {"OK", "Cancel"};
    const std::int32_t id =
        dg_dialog_open(a.window, "Delete?", "This cannot be undone.", buttons, 2, 0, 1);
    REQUIRE(id > 0);
    CHECK(dg_dialog_open(a.window, "again", nullptr, nullptr, 0, -1, -1) == DG_ERR_UNSUPPORTED);
    (void)a.pump(1);
    CHECK(a.count() > baseline);
    a.press(20, 20);  // over the button behind the scrim
    std::vector<dg_event> events = a.pump();
    CHECK(of_kind(events, DG_EVENT_CLICK).empty());
    a.key("Escape");
    events = of_kind(a.pump(), DG_EVENT_DIALOG_RESULT);
    REQUIRE(events.size() == 1);
    CHECK(events[0].x == 1);
    CHECK(events[0].y == id);
    CHECK(a.count() == baseline);

    REQUIRE(dg_dialog_open(a.window, "Save?", nullptr, buttons, 2, 0, -1) > 0);
    (void)a.pump(1);
    a.key("Enter");
    events = of_kind(a.pump(), DG_EVENT_DIALOG_RESULT);
    REQUIRE(events.size() == 1);
    CHECK(events[0].x == 0);

    REQUIRE(dg_dialog_open(a.window, "Save?", nullptr, buttons, 2, 0, -1) > 0);
    (void)a.pump(1);
    a.key("Tab");
    a.key("Enter");
    events = of_kind(a.pump(), DG_EVENT_DIALOG_RESULT);
    REQUIRE(events.size() == 1);
    CHECK(events[0].x == 1);

    REQUIRE(dg_dialog_open(a.window, "Info", nullptr, nullptr, 0, -1, -1) > 0);
    (void)a.pump(1);
    a.key("Escape");
    events = of_kind(a.pump(), DG_EVENT_DIALOG_RESULT);
    REQUIRE(events.size() == 1);
    CHECK(events[0].x == -1);

    REQUIRE(dg_dialog_open(a.window, "Info", nullptr, nullptr, 0, -1, -1) > 0);
    CHECK(dg_dialog_close(a.window) == DG_ERR_OK);
    CHECK(of_kind(a.pump(), DG_EVENT_DIALOG_RESULT).empty());
    CHECK(a.count() == baseline);
    CHECK(dg_dialog_open(a.window, "x", nullptr, buttons, 2, 2, -1) == DG_ERR_INVALID_ARGUMENT);

    // With no dialog the button clicks again.
    a.press(20, 20);
    events = of_kind(a.pump(), DG_EVENT_CLICK);
    REQUIRE(events.size() == 1);
    CHECK(events[0].node == button);
  }

  TEST_CASE("progress: value, indeterminate animation wakes the wait, validation") {
    App a;
    const int baseline = a.count();
    dg_node_t* bar = a.add(DG_NODE_TYPE_PROGRESS);
    App::set(bar, DG_PROP_VALUE, number(0.5F));
    const dg_value too_big = number(1.5F);
    CHECK(dg_node_set_prop(bar, DG_PROP_VALUE, &too_big) == DG_ERR_VALUE_OUT_OF_RANGE);
    const dg_value wrong = option(1, DG_VALUE_BOOL);
    CHECK(dg_node_set_prop(bar, DG_PROP_VALUE, &wrong) == DG_ERR_TYPE_MISMATCH);
    dg_node_t* box = a.add(DG_NODE_TYPE_BOX);
    const dg_value half = number(0.5F);
    CHECK(dg_node_set_prop(box, DG_PROP_VALUE, &half) == DG_ERR_UNSUPPORTED);
    (void)a.pump(1);
    CHECK(child_field(a.root, 0, "w") == 200);

    App::set(bar, DG_PROP_INDETERMINATE, option(1, DG_VALUE_BOOL));
    (void)a.pump(1);
    const auto start = std::chrono::steady_clock::now();
    REQUIRE(dg_wait_events(a.app, 2000) >= 0);
    CHECK(elapsed_ms(start) < 1000);
    App::set(bar, DG_PROP_HEIGHT, length(12));
    App::set(bar, DG_PROP_INDETERMINATE, option(0, DG_VALUE_BOOL));
    (void)a.pump(1);
    CHECK(child_field(a.root, 0, "h") == 12);
    REQUIRE(dg_node_remove(bar) == DG_ERR_OK);
    REQUIRE(dg_node_remove(box) == DG_ERR_OK);
    (void)a.pump(1);
    CHECK(a.count() == baseline);
  }

  TEST_CASE("tabs: panels take children, clicks and keys select, disabled tabs skipped") {
    App a;
    const int baseline = a.count();
    dg_node_t* tabs = a.add(DG_NODE_TYPE_TABS);
    dg_node_t* first = dg_tabs_add(tabs, "One", 0);
    dg_node_t* locked = dg_tabs_add(tabs, "Locked", 1);
    dg_node_t* third = dg_tabs_add(tabs, "Three", 0);
    REQUIRE(first != nullptr);
    REQUIRE(locked != nullptr);
    REQUIRE(third != nullptr);
    dg_node_t* label = dg_node_create(a.app, DG_NODE_TYPE_BUTTON);
    REQUIRE(dg_node_insert_before(first, label, nullptr) == DG_ERR_OK);
    dg_node_t* stray = dg_node_create(a.app, DG_NODE_TYPE_BOX);
    CHECK(dg_node_insert_before(tabs, stray, nullptr) == DG_ERR_UNSUPPORTED);
    (void)a.pump(1);
    CHECK(dg_tabs_get_selected(tabs) == 0);

    REQUIRE(dg_node_focus(tabs) == DG_ERR_OK);
    a.key("Right");
    std::vector<dg_event> events = of_kind(a.pump(), DG_EVENT_TAB_SELECT);
    REQUIRE(events.size() == 1);
    CHECK(events[0].x == 2);
    CHECK(events[0].node == tabs);
    CHECK(dg_tabs_set_selected(tabs, 1) == DG_ERR_VALUE_OUT_OF_RANGE);

    a.press(8, 16);  // the first tab, at the strip's left
    events = of_kind(a.pump(), DG_EVENT_TAB_SELECT);
    REQUIRE(events.size() == 1);
    CHECK(events[0].x == 0);
    CHECK(dg_tabs_set_selected(tabs, 2) == DG_ERR_OK);
    CHECK(dg_tabs_get_selected(tabs) == 2);
    CHECK(of_kind(a.pump(1), DG_EVENT_TAB_SELECT).empty());

    REQUIRE(dg_node_remove(third) == DG_ERR_OK);  // the tab stays, its panel empties
    CHECK(dg_tabs_add(tabs, "Four", 0) != nullptr);
    REQUIRE(dg_node_remove(tabs) == DG_ERR_OK);
    (void)a.pump(1);
    CHECK(a.count() == baseline);
    CHECK(dg_node_focus(label) == DG_ERR_INVALID_HANDLE);
    CHECK(dg_tabs_add(tabs, "x", 0) == nullptr);
  }

  TEST_CASE("tree: items, keyboard, clicks, expand/collapse events, validation") {
    App a;
    const int baseline = a.count();
    dg_node_t* tree = a.add(DG_NODE_TYPE_TREE);
    const char* labels[] = {"src", "a.c", "b.c", "doc", "x.md"};
    const std::int32_t depths[] = {0, 1, 1, 0, 1};
    const std::uint32_t flags[] = {DG_TREE_ITEM_EXPANDED, 0, 0, 0, 0};
    REQUIRE(dg_tree_set_items(tree, labels, depths, flags, 5) == DG_ERR_OK);
    const std::int32_t bad[] = {0, 2, 0, 0, 0};
    CHECK(dg_tree_set_items(tree, labels, bad, nullptr, 5) == DG_ERR_INVALID_ARGUMENT);
    (void)a.pump(1);
    const int built = a.count();

    REQUIRE(dg_node_focus(tree) == DG_ERR_OK);
    a.key("Down");
    std::vector<dg_event> events = of_kind(a.pump(), DG_EVENT_TREE_SELECT);
    REQUIRE(!events.empty());
    CHECK(events.back().node == tree);
    CHECK(dg_tree_get_selected(tree) == events.back().x);

    REQUIRE(dg_tree_set_selected(tree, 0) == DG_ERR_OK);
    a.key("Left");
    events = of_kind(a.pump(), DG_EVENT_TREE_COLLAPSE);
    REQUIRE(events.size() == 1);
    CHECK(events[0].x == 0);
    a.key("Right");
    events = of_kind(a.pump(), DG_EVENT_TREE_EXPAND);
    REQUIRE(events.size() == 1);
    CHECK(events[0].x == 0);

    // A click on the third visible row (b.c): frame 1px, rows 26px.
    a.press(80, 1 + (26 * 2) + 13);
    events = of_kind(a.pump(), DG_EVENT_TREE_SELECT);
    REQUIRE(events.size() == 1);
    CHECK(events[0].x == 2);

    REQUIRE(dg_debug_warp_pointer(a.window, 80, 40) == DG_ERR_OK);
    REQUIRE(dg_debug_post_wheel(a.window, 0.0F, -1.0F) == DG_ERR_OK);
    (void)a.pump();
    CHECK(a.count() == built);  // scrolling and expanding recycle the pool

    CHECK(dg_tree_set_selected(tree, 4) == DG_ERR_OK);
    CHECK(dg_tree_get_selected(tree) == 4);
    CHECK(dg_tree_set_selected(tree, -1) == DG_ERR_OK);
    CHECK(dg_tree_get_selected(tree) == -1);
    CHECK(dg_tree_set_selected(tree, 5) == DG_ERR_VALUE_OUT_OF_RANGE);
    CHECK(dg_tree_set_expanded(tree, 3, 1) == DG_ERR_OK);

    REQUIRE(dg_node_remove(tree) == DG_ERR_OK);
    (void)a.pump(1);
    CHECK(a.count() == baseline);
  }

  TEST_CASE("table: header sort request, row select, keys, sort indicator, height") {
    App a;
    const int baseline = a.count();
    dg_node_t* table = a.add(DG_NODE_TYPE_TABLE);
    const char* titles[] = {"Name", "Size", "Flag"};
    const float widths[] = {50.0F, 80.0F, 0.0F};
    const std::uint32_t units[] = {DG_LENGTH_UNIT_PERCENT, DG_LENGTH_UNIT_PX,
                                   DG_LENGTH_UNIT_PX};
    const std::uint32_t flags[] = {DG_TABLE_COLUMN_SORTABLE, DG_TABLE_COLUMN_SORTABLE, 0};
    REQUIRE(dg_table_set_columns(table, titles, widths, units, flags, 3) == DG_ERR_OK);
    std::vector<std::string> storage;
    for (int i = 0; i < 100; ++i) {
      storage.push_back("row " + std::to_string(i));
      storage.push_back(std::to_string(i * 7));
      storage.push_back(i % 2 == 0 ? "yes" : "no");
    }
    std::vector<const char*> cells;
    for (const std::string& cell : storage) {
      cells.push_back(cell.c_str());
    }
    REQUIRE(dg_table_set_cells(table, cells.data(), 100) == DG_ERR_OK);
    (void)a.pump(1);
    const int built = a.count();

    // Header: frame 1px, 26px tall; the Size column starts at half the row.
    a.press(200, 13);
    std::vector<dg_event> events = of_kind(a.pump(), DG_EVENT_TABLE_SORT);
    REQUIRE(events.size() == 1);
    CHECK(events[0].x == 1);
    CHECK(events[0].node == table);
    a.press(340, 13);  // Flag is not sortable
    CHECK(of_kind(a.pump(), DG_EVENT_TABLE_SORT).empty());
    CHECK(dg_table_set_sort_indicator(table, 1, 0) == DG_ERR_OK);
    CHECK(dg_table_set_sort_indicator(table, 3, 0) == DG_ERR_VALUE_OUT_OF_RANGE);

    a.press(60, 1 + 26 + (26 * 3) + 13);  // the fourth row
    events = of_kind(a.pump(), DG_EVENT_TABLE_SELECT);
    REQUIRE(events.size() == 1);
    CHECK(events[0].x == 3);
    CHECK(events[0].clicks >= 1);
    a.key("Down");
    events = of_kind(a.pump(), DG_EVENT_TABLE_SELECT);
    REQUIRE(events.size() == 1);
    CHECK(events[0].x == 4);
    CHECK(dg_table_get_selected(table) == 4);
    CHECK(dg_table_set_selected(table, 99) == DG_ERR_OK);
    CHECK(dg_table_set_selected(table, 100) == DG_ERR_VALUE_OUT_OF_RANGE);
    CHECK(dg_table_set_selected(table, -1) == DG_ERR_OK);
    CHECK(dg_table_get_selected(table) == -1);
    CHECK(a.count() == built);

    App::set(table, DG_PROP_HEIGHT, length(120));
    (void)a.pump(1);
    CHECK(a.count() < built);  // a shorter viewport needs fewer pooled rows
    CHECK(dg_table_set_cells(table, nullptr, 1) == DG_ERR_INVALID_ARGUMENT);
    CHECK(dg_table_set_columns(table, titles, widths, units, flags, 0) ==
          DG_ERR_INVALID_ARGUMENT);
    REQUIRE(dg_node_remove(table) == DG_ERR_OK);
    (void)a.pump(1);
    CHECK(a.count() == baseline);
  }

  TEST_CASE("a theme switch restyles components and keeps their state and node counts") {
    App a;
    dg_theme_t* theme =
        dg_theme_load_dir(a.app, DG_TEST_FIXTURE_DIR "/../../themes/builtin", nullptr);
    REQUIRE(theme != nullptr);
    REQUIRE(dg_app_set_theme(a.app, theme) == DG_ERR_OK);
    dg_node_t* tabs = a.add(DG_NODE_TYPE_TABS);
    dg_node_t* panel = dg_tabs_add(tabs, "One", 0);
    REQUIRE(dg_tabs_add(tabs, "Two", 0) != nullptr);
    dg_node_t* child = dg_node_create(a.app, DG_NODE_TYPE_TREE);
    REQUIRE(dg_node_insert_before(panel, child, nullptr) == DG_ERR_OK);
    const char* labels[] = {"a", "b"};
    const std::int32_t depths[] = {0, 1};
    REQUIRE(dg_tree_set_items(child, labels, depths, nullptr, 2) == DG_ERR_OK);
    REQUIRE(dg_tree_set_selected(child, 1) == DG_ERR_OK);
    dg_node_t* dropdown = a.add(DG_NODE_TYPE_DROPDOWN);
    const char* items[] = {"x", "y"};
    REQUIRE(dg_dropdown_set_items(dropdown, items, 2) == DG_ERR_OK);
    REQUIRE(dg_dropdown_set_selected(dropdown, 1) == DG_ERR_OK);
    REQUIRE(dg_tabs_set_selected(tabs, 1) == DG_ERR_OK);
    (void)a.pump(1);
    const int before = a.count();

    REQUIRE(dg_theme_set_variant(theme, "light") == DG_ERR_OK);
    (void)a.pump(1);
    CHECK(a.count() == before);
    CHECK(dg_tabs_get_selected(tabs) == 1);
    CHECK(dg_tree_get_selected(child) == 1);
    CHECK(dg_dropdown_get_selected(dropdown) == 1);
    CHECK(dg_tree_set_selected(child, 0) == DG_ERR_OK);  // the handles survive
  }

  TEST_CASE("slider: props, a press, keys, SLIDER_CHANGED, get_value, validation") {
    App a;
    const int baseline = a.count();
    dg_node_t* slider = a.add(DG_NODE_TYPE_SLIDER);
    float value = -1.0F;
    REQUIRE(dg_slider_get_value(slider, &value) == DG_ERR_OK);
    CHECK(value == 0.0F);
    CHECK(dg_slider_get_value(slider, nullptr) == DG_ERR_INVALID_ARGUMENT);
    App::set(slider, DG_PROP_MAX_VALUE, number(10.0F));
    App::set(slider, DG_PROP_STEP, number(1.0F));
    App::set(slider, DG_PROP_VALUE, number(3.4F));
    REQUIRE(dg_slider_get_value(slider, &value) == DG_ERR_OK);
    CHECK(value == 3.0F);
    const dg_value bad_min = number(10.0F);
    CHECK(dg_node_set_prop(slider, DG_PROP_MIN_VALUE, &bad_min) == DG_ERR_VALUE_OUT_OF_RANGE);
    const dg_value wrong = option(1, DG_VALUE_BOOL);
    CHECK(dg_node_set_prop(slider, DG_PROP_VALUE, &wrong) == DG_ERR_TYPE_MISMATCH);
    CHECK(dg_node_set_prop(slider, DG_PROP_STEP, &wrong) == DG_ERR_TYPE_MISMATCH);
    CHECK(of_kind(a.pump(1), DG_EVENT_SLIDER_CHANGED).empty());
    CHECK(child_field(a.root, 0, "w") == 200);
    CHECK(child_field(a.root, 0, "h") == 20);
    CHECK(dg_node_get_checked(slider) == DG_ERR_UNSUPPORTED);
    dg_node_t* stray = dg_node_create(a.app, DG_NODE_TYPE_BOX);
    CHECK(dg_node_insert_before(slider, stray, nullptr) == DG_ERR_UNSUPPORTED);

    a.press(195, 10);  // the track's right end
    std::vector<dg_event> events = of_kind(a.pump(), DG_EVENT_SLIDER_CHANGED);
    REQUIRE(events.size() == 1);
    CHECK(events[0].node == slider);
    CHECK(events[0].x == 0);
    REQUIRE(dg_slider_get_value(slider, &value) == DG_ERR_OK);
    CHECK(value == 10.0F);
    CHECK(dg_window_get_focus(a.window) == slider);
    a.key("Left");
    a.key("Home");
    events = of_kind(a.pump(), DG_EVENT_SLIDER_CHANGED);
    CHECK(events.size() == 2);
    REQUIRE(dg_slider_get_value(slider, &value) == DG_ERR_OK);
    CHECK(value == 0.0F);

    dg_node_t* box = a.add(DG_NODE_TYPE_BOX);
    CHECK(dg_slider_get_value(box, &value) == DG_ERR_UNSUPPORTED);
    REQUIRE(dg_node_remove(slider) == DG_ERR_OK);
    REQUIRE(dg_node_remove(box) == DG_ERR_OK);
    (void)a.pump(1);
    CHECK(a.count() == baseline);
    CHECK(dg_slider_get_value(slider, &value) == DG_ERR_INVALID_HANDLE);
  }

  TEST_CASE("radio: groups, CLICK then get_checked, arrow keys, dg_radio_select") {
    App a;
    dg_node_t* first = a.add(DG_NODE_TYPE_RADIO);
    dg_node_t* second = a.add(DG_NODE_TYPE_RADIO);
    dg_node_t* other = a.add(DG_NODE_TYPE_RADIO);
    App::set(other, DG_PROP_RADIO_GROUP, number(2.0F));
    const dg_value half = number(0.5F);
    CHECK(dg_node_set_prop(other, DG_PROP_RADIO_GROUP, &half) == DG_ERR_VALUE_OUT_OF_RANGE);
    dg_node_t* checkbox = a.add(DG_NODE_TYPE_CHECKBOX);
    CHECK(dg_node_set_prop(checkbox, DG_PROP_RADIO_GROUP, &half) == DG_ERR_UNSUPPORTED);
    CHECK(dg_radio_select(checkbox) == DG_ERR_UNSUPPORTED);
    (void)a.pump(1);
    CHECK(dg_node_get_checked(first) == 0);

    a.press(child_field(a.root, 1, "x") + 16, child_field(a.root, 1, "y") + 16);
    std::vector<dg_event> clicks = of_kind(a.pump(), DG_EVENT_CLICK);
    REQUIRE(clicks.size() == 1);
    CHECK(clicks[0].node == second);
    CHECK(clicks[0].clicks == 1);
    CHECK(dg_node_get_checked(second) == 1);

    a.key("Up");
    clicks = of_kind(a.pump(), DG_EVENT_CLICK);
    REQUIRE(clicks.size() == 1);
    CHECK(clicks[0].node == first);
    CHECK(clicks[0].clicks == 0);
    CHECK(dg_node_get_checked(first) == 1);
    CHECK(dg_node_get_checked(second) == 0);
    CHECK(dg_window_get_focus(a.window) == first);

    CHECK(dg_radio_select(other) == DG_ERR_OK);
    CHECK(dg_radio_select(second) == DG_ERR_OK);
    CHECK(of_kind(a.pump(1), DG_EVENT_CLICK).empty());
    CHECK(dg_node_get_checked(first) == 0);
    CHECK(dg_node_get_checked(second) == 1);
    CHECK(dg_node_get_checked(other) == 1);
  }
}
