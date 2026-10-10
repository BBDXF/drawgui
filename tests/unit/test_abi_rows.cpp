// ABI 0.2 rich rows through the C ABI: dg_list_set_rows(),
// dg_tree_set_items_ex(), dg_table_set_cells_ex(), the *_checked() readers,
// the row-key setters and dg_event's item/column/item_key.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
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
    opts.title = "abi rows";
    opts.width = 480;
    opts.height = 360;
    window = dg_window_create(app, &opts);
    REQUIRE(window != nullptr);
    root = dg_node_create(app, DG_NODE_TYPE_BOX);
    REQUIRE(dg_window_set_root(window, root) == DG_ERR_OK);
    const dg_value w = length(480);
    const dg_value h = length(360);
    const dg_value column = option(DG_DIRECTION_COLUMN);
    REQUIRE(dg_node_set_prop(root, DG_PROP_WIDTH, &w) == DG_ERR_OK);
    REQUIRE(dg_node_set_prop(root, DG_PROP_HEIGHT, &h) == DG_ERR_OK);
    REQUIRE(dg_node_set_prop(root, DG_PROP_DIRECTION, &column) == DG_ERR_OK);
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

  void press(int x, int y, int button = DG_POINTER_BUTTON_PRIMARY) const {
    REQUIRE(dg_debug_warp_pointer(window, x, y) == DG_ERR_OK);
    REQUIRE(dg_debug_post_pointer_button_ex(window, 1, x, y, button, 0) == DG_ERR_OK);
    REQUIRE(dg_debug_post_pointer_button_ex(window, 0, x, y, button, 0) == DG_ERR_OK);
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

std::int32_t load_icon(dg_app_t* app) {
  const char* svg =
      "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16'>"
      "<rect width='16' height='16' fill='red'/></svg>";
  const std::int32_t id =
      dg_app_load_image(app, svg, static_cast<std::uint32_t>(std::strlen(svg)), 0);
  REQUIRE(id > 0);
  return id;
}

}  // namespace

TEST_SUITE("abi rows") {
  TEST_CASE("dg_event carries item_key after command") {
    CHECK(offsetof(dg_event, item_key) > offsetof(dg_event, command));
    if (sizeof(void*) == 8) {
      CHECK(sizeof(dg_event) == 56);
    }
  }

  TEST_CASE("dg_list_set_rows: checks, keys, row-aware clicks") {
    App a;
    const auto icon = static_cast<std::uint32_t>(load_icon(a.app));
    dg_node_t* list = a.add(DG_NODE_TYPE_LIST);
    const char* texts[] = {"alpha", "beta", "gamma"};
    const char* secondaries[] = {"1", nullptr, "3"};
    const std::uint32_t icons[] = {icon, 0, icon};
    const std::uint32_t flags[] = {DG_ROW_CHECKABLE | DG_ROW_CHECKED, 0, DG_ROW_CHECKABLE};
    const std::uint32_t actions[] = {1, 0, 9};
    CHECK(dg_list_set_rows(list, texts, secondaries, icons, flags, actions, 3, 28) ==
          DG_ERR_VALUE_OUT_OF_RANGE);
    const std::uint32_t ok_actions[] = {1, 0, 0};
    REQUIRE(dg_list_set_rows(list, texts, secondaries, icons, flags, ok_actions, 3, 28) ==
            DG_ERR_OK);
    const std::uint32_t action_icons[] = {icon};
    const char* tips[] = {"Remove"};
    REQUIRE(dg_list_set_row_actions(list, action_icons, tips, 1) == DG_ERR_OK);
    CHECK(dg_list_row_checked(list, 0) == 1);
    CHECK(dg_list_row_checked(list, 2) == 0);
    CHECK(dg_list_row_checked(list, 1) == DG_ERR_NOT_APPLICABLE);
    const std::uint64_t keys[] = {0xA0000000000001ULL, 0xB2, 0xC3};
    CHECK(dg_list_set_row_keys(list, keys, 2) == DG_ERR_INVALID_ARGUMENT);
    REQUIRE(dg_list_set_row_keys(list, keys, 3) == DG_ERR_OK);
    (void)a.pump();

    a.press(150, 1 + 28 + 14);
    std::vector<dg_event> got = a.pump();
    const auto clicks = of_kind(got, DG_EVENT_CLICK);
    REQUIRE(clicks.size() == 1);
    CHECK(clicks[0].item == 1);
    CHECK(clicks[0].item_key == 0xB2U);
    CHECK(dg_list_get_selected(list) == 1);

    a.press(150, 1 + 14, DG_POINTER_BUTTON_SECONDARY);
    got = a.pump();
    REQUIRE(of_kind(got, DG_EVENT_LIST_SELECT).size() == 1);
    const auto contexts = of_kind(got, DG_EVENT_CONTEXT_CLICK);
    REQUIRE(contexts.size() == 1);
    CHECK(contexts[0].item == 0);
    CHECK(contexts[0].item_key == 0xA0000000000001ULL);
  }

  TEST_CASE("dg_node_focus accepts a LIST; Down/End/Space drive it") {
    App a;
    dg_node_t* list = a.add(DG_NODE_TYPE_LIST);
    const char* texts[] = {"alpha", "beta", "gamma"};
    const std::uint32_t flags[] = {0, DG_ROW_CHECKABLE, 0};
    REQUIRE(dg_list_set_rows(list, texts, nullptr, nullptr, flags, nullptr, 3, 28) == DG_ERR_OK);
    (void)a.pump();
    REQUIRE(dg_node_focus(list) == DG_ERR_OK);
    CHECK(dg_window_get_focus(a.window) == list);
    const auto key = [&a](const char* name) {
      REQUIRE(dg_debug_post_key(a.window, 1, name) == DG_ERR_OK);
      REQUIRE(dg_debug_post_key(a.window, 0, name) == DG_ERR_OK);
    };
    key("Down");
    key("Down");
    std::vector<dg_event> got = a.pump();
    const auto selects = of_kind(got, DG_EVENT_LIST_SELECT);
    REQUIRE(selects.size() == 2);
    CHECK(selects[1].x == 1);
    CHECK(selects[1].clicks == 0);
    CHECK(dg_list_get_selected(list) == 1);
    key("Space");
    got = a.pump();
    const auto parts = of_kind(got, DG_EVENT_ROW_PART);
    REQUIRE(parts.size() == 1);
    CHECK(parts[0].x == DG_ROW_PART_CHECK);
    CHECK(parts[0].item == 1);
    CHECK(dg_list_row_checked(list, 1) == 1);
    key("End");
    (void)a.pump();
    CHECK(dg_list_get_selected(list) == 2);
  }

  TEST_CASE("dg_node_set_user_data/get: pending and live, never in events") {
    App a;
    dg_node_t* node = dg_node_create(a.app, DG_NODE_TYPE_BUTTON);
    REQUIRE(node != nullptr);
    std::uint64_t value = 7;
    CHECK(dg_node_get_user_data(node, &value) == DG_ERR_OK);
    CHECK(value == 0);
    CHECK(dg_node_get_user_data(node, nullptr) == DG_ERR_INVALID_ARGUMENT);
    CHECK(dg_node_set_user_data(node, 0xFEEDFACECAFEBEEFULL) == DG_ERR_OK);
    CHECK(dg_node_get_user_data(node, &value) == DG_ERR_OK);
    CHECK(value == 0xFEEDFACECAFEBEEFULL);
    // Kept when the pending node goes live.
    REQUIRE(dg_node_insert_before(a.root, node, nullptr) == DG_ERR_OK);
    value = 0;
    CHECK(dg_node_get_user_data(node, &value) == DG_ERR_OK);
    CHECK(value == 0xFEEDFACECAFEBEEFULL);
    CHECK(dg_node_set_user_data(node, 42) == DG_ERR_OK);
    CHECK(dg_node_get_user_data(node, &value) == DG_ERR_OK);
    CHECK(value == 42);
    // A sibling is independent.
    dg_node_t* other = a.add(DG_NODE_TYPE_LABEL);
    CHECK(dg_node_get_user_data(other, &value) == DG_ERR_OK);
    CHECK(value == 0);
    // Gone with the node.
    REQUIRE(dg_node_remove(node) == DG_ERR_OK);
    CHECK(dg_node_get_user_data(node, &value) == DG_ERR_INVALID_HANDLE);
    CHECK(dg_node_set_user_data(node, 1) == DG_ERR_INVALID_HANDLE);
    CHECK(dg_node_set_user_data(nullptr, 1) == DG_ERR_INVALID_HANDLE);
  }

  TEST_CASE("dg_tree_set_items_ex and dg_table_set_cells_ex: checks and keys") {
    App a;
    const auto icon = static_cast<std::uint32_t>(load_icon(a.app));
    dg_node_t* tree = a.add(DG_NODE_TYPE_TREE);
    const char* labels[] = {"root", "leaf"};
    const std::int32_t depths[] = {0, 1};
    const std::uint32_t tree_flags[] = {DG_TREE_ITEM_EXPANDED | DG_TREE_ITEM_CHECKABLE,
                                        DG_TREE_ITEM_CHECKABLE | DG_TREE_ITEM_CHECKED};
    const std::uint32_t tree_icons[] = {icon, 0};
    const char* tree_secondaries[] = {"2", nullptr};
    REQUIRE(dg_tree_set_items_ex(tree, labels, depths, tree_flags, tree_icons, tree_secondaries,
                                 2) == DG_ERR_OK);
    CHECK(dg_tree_row_checked(tree, 0) == 0);
    CHECK(dg_tree_row_checked(tree, 1) == 1);
    const std::uint64_t tree_keys[] = {5, 6};
    REQUIRE(dg_tree_set_item_keys(tree, tree_keys, 2) == DG_ERR_OK);
    CHECK(dg_tree_row_checked(tree, 1) == 1);

    dg_node_t* table = a.add(DG_NODE_TYPE_TABLE);
    const char* titles[] = {"On", "Name"};
    REQUIRE(dg_table_set_columns(table, titles, nullptr, nullptr, nullptr, 2) == DG_ERR_OK);
    const char* cells[] = {"", "one", "", "two"};
    const std::uint32_t cell_icons[] = {0, icon, 0, icon};
    const std::uint32_t cell_flags[] = {DG_ROW_CHECKABLE, 0, DG_ROW_CHECKABLE | DG_ROW_CHECKED,
                                        0};
    REQUIRE(dg_table_set_cells_ex(table, cells, cell_icons, cell_flags, 2) == DG_ERR_OK);
    CHECK(dg_table_cell_checked(table, 0, 0) == 0);
    CHECK(dg_table_cell_checked(table, 1, 0) == 1);
    CHECK(dg_table_cell_checked(table, 1, 1) == DG_ERR_NOT_APPLICABLE);
    const std::uint64_t table_keys[] = {70, 80};
    REQUIRE(dg_table_set_row_keys(table, table_keys, 2) == DG_ERR_OK);
    CHECK(dg_table_set_row_keys(table, table_keys, 1) == DG_ERR_INVALID_ARGUMENT);
    (void)a.pump();
  }
}
