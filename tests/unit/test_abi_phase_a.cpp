// ABI 0.2 phase A through the C ABI, headless (SDL's dummy driver): box
// containers, text size/alignment, percentage lengths, keyboard focus,
// scroll views and lists, editor max length, click counts and context
// clicks, timers and user events, images, and the color.scrim token.

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/abi/drawgui.h"
#include "drawgui/render/prop_ids.generated.h"
#include "drawgui/theme/theme_loader.h"
#include "drawgui/theme/token_ids.generated.h"
#include "drawgui/ui/ui_context.h"

namespace {

dg_value number(float value) {
  dg_value out{};
  out.size = sizeof(dg_value);
  out.type = DG_VALUE_FLOAT;
  out.number = value;
  return out;
}

dg_value length(float value, std::uint32_t unit = DG_LENGTH_UNIT_PX) {
  dg_value out{};
  out.size = sizeof(dg_value);
  out.type = DG_VALUE_LENGTH;
  out.number = value;
  out.bits = unit;
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

  explicit App(bool fonts = false) {
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    app = dg_app_create(nullptr);
    REQUIRE(app != nullptr);
    if (fonts) {
      REQUIRE(dg_app_add_font(app, DG_TEST_FONT_DIR, "DgTest Latin") == DG_ERR_OK);
    }
    dg_window_opts opts{};
    opts.size = sizeof(opts);
    opts.title = "abi phase a";
    opts.width = 400;
    opts.height = 300;
    window = dg_window_create(app, &opts);
    REQUIRE(window != nullptr);
    root = node(DG_NODE_TYPE_BOX);
    REQUIRE(dg_window_set_root(window, root) == DG_ERR_OK);
    set(root, DG_PROP_WIDTH, length(400));
    set(root, DG_PROP_HEIGHT, length(300));
    set(root, DG_PROP_DIRECTION, option(DG_DIRECTION_COLUMN));
  }
  App(const App&) = delete;
  App& operator=(const App&) = delete;
  App(App&&) = delete;
  App& operator=(App&&) = delete;
  ~App() { dg_app_destroy(app); }

  dg_node_t* node(std::uint16_t type) const {
    dg_node_t* created = dg_node_create(app, type);
    REQUIRE(created != nullptr);
    return created;
  }

  dg_node_t* add(dg_node_t* parent, std::uint16_t type) const {
    dg_node_t* created = node(type);
    REQUIRE(dg_node_insert_before(parent, created, nullptr) == DG_ERR_OK);
    return created;
  }

  static void set(dg_node_t* target, std::uint16_t prop, const dg_value& value) {
    REQUIRE(dg_node_set_prop(target, prop, &value) == DG_ERR_OK);
  }

  // Pumps a few times and returns every event that arrived.
  std::vector<dg_event> pump(int rounds = 3, int timeout_ms = 5) const {
    std::vector<dg_event> all;
    for (int i = 0; i < rounds; ++i) {
      REQUIRE(dg_wait_events(app, timeout_ms) >= 0);
      dg_event events[32];
      const std::int32_t n = dg_poll_events(app, events, 32);
      all.insert(all.end(), events, events + n);
    }
    return all;
  }

  void press(std::int32_t x, std::int32_t y, std::int32_t button = DG_POINTER_BUTTON_PRIMARY,
             std::int32_t clicks = 0) const {
    REQUIRE(dg_debug_warp_pointer(window, x, y) == DG_ERR_OK);
    REQUIRE(dg_debug_post_pointer_button_ex(window, 1, x, y, button, clicks) == DG_ERR_OK);
    REQUIRE(dg_debug_post_pointer_button_ex(window, 0, x, y, button, clicks) == DG_ERR_OK);
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

// "x":N of the index-th child object in dg_dump_layout_tree(parent).
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

}  // namespace

TEST_SUITE("abi phase a") {
  TEST_CASE("DG_PROP_DIRECTION turns a BOX into a row; DG_PROP_WRAP wraps it") {
    App a;
    dg_node_t* row = a.add(a.root, DG_NODE_TYPE_BOX);
    App::set(row, DG_PROP_DIRECTION, option(DG_DIRECTION_ROW));
    App::set(row, DG_PROP_GAP, number(10));
    App::set(row, DG_PROP_WIDTH, length(130));
    for (int i = 0; i < 3; ++i) {
      dg_node_t* chip = a.add(row, DG_NODE_TYPE_BOX);
      App::set(chip, DG_PROP_WIDTH, length(50));
      App::set(chip, DG_PROP_HEIGHT, length(20));
    }
    (void)a.pump(1);
    CHECK(child_field(row, 1, "x") == 60);
    CHECK(child_field(row, 2, "y") == 0);
    App::set(row, DG_PROP_WRAP, option(1, DG_VALUE_BOOL));
    (void)a.pump(1);
    CHECK(child_field(row, 2, "x") == 0);
    CHECK(child_field(row, 2, "y") > 0);

    const dg_value wrap = option(1, DG_VALUE_BOOL);
    dg_node_t* leaf = a.add(a.root, DG_NODE_TYPE_BOX);
    CHECK(dg_node_set_prop(leaf, DG_PROP_WRAP, &wrap) == DG_ERR_NOT_APPLICABLE);
    const dg_value reverse = option(DG_DIRECTION_ROW_REVERSE);
    CHECK(dg_node_set_prop(leaf, DG_PROP_DIRECTION, &reverse) == DG_ERR_UNSUPPORTED);
    // The refused write left it a leaf: justify still does not apply.
    const dg_value justify = option(DG_JUSTIFY_CENTER);
    CHECK(dg_node_set_prop(leaf, DG_PROP_JUSTIFY, &justify) == DG_ERR_NOT_APPLICABLE);
    dg_node_t* button = a.add(a.root, DG_NODE_TYPE_BUTTON);
    const dg_value row_dir = option(DG_DIRECTION_ROW);
    CHECK(dg_node_set_prop(button, DG_PROP_DIRECTION, &row_dir) == DG_ERR_NOT_APPLICABLE);
  }

  TEST_CASE("font_size and text_align on labels, buttons and editors") {
    App a(true);
    dg_node_t* label = a.add(a.root, DG_NODE_TYPE_LABEL);
    dg_node_t* button = a.add(a.root, DG_NODE_TYPE_BUTTON);
    dg_node_t* field = a.add(a.root, DG_NODE_TYPE_TEXT_FIELD);
    dg_node_t* area = a.add(a.root, DG_NODE_TYPE_TEXT_AREA);
    for (dg_node_t* n : {label, button, field, area}) {
      App::set(n, DG_PROP_FONT_SIZE, number(24));
      App::set(n, DG_PROP_TEXT_ALIGN, option(DG_TEXT_ALIGN_RIGHT));
    }
    REQUIRE(dg_node_set_text(button, DG_PROP_TEXT, "OK", 2) == DG_ERR_OK);
    const dg_value zero = number(0);
    CHECK(dg_node_set_prop(label, DG_PROP_FONT_SIZE, &zero) == DG_ERR_VALUE_OUT_OF_RANGE);
    const dg_value bad_align = option(3);
    CHECK(dg_node_set_prop(label, DG_PROP_TEXT_ALIGN, &bad_align) == DG_ERR_VALUE_OUT_OF_RANGE);
    (void)a.pump(1);
  }

  TEST_CASE("DG_LENGTH_UNIT_PERCENT reaches the layout as a percentage") {
    App a;
    dg_node_t* half = a.add(a.root, DG_NODE_TYPE_BOX);
    App::set(half, DG_PROP_WIDTH, length(50, DG_LENGTH_UNIT_PERCENT));
    (void)a.pump(1);
    CHECK(child_field(a.root, 0, "w") == 200);
    const dg_value bad_unit = length(5, 2);
    CHECK(dg_node_set_prop(half, DG_PROP_WIDTH, &bad_unit) == DG_ERR_VALUE_OUT_OF_RANGE);
    const dg_value percent_gap = length(5, DG_LENGTH_UNIT_PERCENT);
    CHECK(dg_node_set_prop(a.root, DG_PROP_GAP, &percent_gap) == DG_ERR_TYPE_MISMATCH);
  }

  TEST_CASE("focus: dg_node_focus, Tab order, tab_index, clear, and removal") {
    App a(true);
    dg_node_t* first = a.add(a.root, DG_NODE_TYPE_BUTTON);
    dg_node_t* field = a.add(a.root, DG_NODE_TYPE_TEXT_FIELD);
    dg_node_t* label = a.add(a.root, DG_NODE_TYPE_LABEL);
    (void)a.pump(1);
    CHECK(dg_window_get_focus(a.window) == nullptr);

    REQUIRE(dg_node_focus(field) == DG_ERR_OK);
    CHECK(dg_window_get_focus(a.window) == field);
    std::vector<dg_event> focus = of_kind(a.pump(1), DG_EVENT_FOCUS_CHANGED);
    REQUIRE(focus.size() == 1);
    CHECK(focus[0].node == field);
    CHECK(dg_node_focus(label) == DG_ERR_UNSUPPORTED);

    REQUIRE(dg_debug_post_text_field_key(a.window, 1, "Tab", 0) == DG_ERR_OK);
    focus = of_kind(a.pump(), DG_EVENT_FOCUS_CHANGED);
    REQUIRE(focus.size() == 1);
    CHECK(focus[0].node == first);  // wrapped around
    REQUIRE(dg_debug_post_text_field_key(a.window, 1, "Tab", 1) == DG_ERR_OK);
    (void)a.pump();
    CHECK(dg_window_get_focus(a.window) == field);

    // A positive tab_index goes first.
    App::set(field, DG_PROP_TAB_INDEX, number(1));
    REQUIRE(dg_window_clear_focus(a.window) == DG_ERR_OK);
    focus = of_kind(a.pump(1), DG_EVENT_FOCUS_CHANGED);
    REQUIRE(focus.size() == 1);
    CHECK(focus[0].node == nullptr);
    REQUIRE(dg_debug_post_text_field_key(a.window, 1, "Tab", 0) == DG_ERR_OK);
    (void)a.pump();
    CHECK(dg_window_get_focus(a.window) == field);
    const dg_value fraction = number(1.5F);
    CHECK(dg_node_set_prop(field, DG_PROP_TAB_INDEX, &fraction) == DG_ERR_VALUE_OUT_OF_RANGE);
    CHECK(dg_node_set_prop(label, DG_PROP_TAB_INDEX, &fraction) == DG_ERR_VALUE_OUT_OF_RANGE);
    const dg_value one = number(1);
    CHECK(dg_node_set_prop(label, DG_PROP_TAB_INDEX, &one) == DG_ERR_NOT_APPLICABLE);
    const dg_value wrong_type = option(1);
    CHECK(dg_node_set_prop(field, DG_PROP_TAB_INDEX, &wrong_type) == DG_ERR_TYPE_MISMATCH);

    REQUIRE(dg_node_remove(field) == DG_ERR_OK);
    focus = of_kind(a.pump(1), DG_EVENT_FOCUS_CHANGED);
    REQUIRE(focus.size() == 1);
    CHECK(focus[0].node == nullptr);
    CHECK(dg_window_get_focus(a.window) == nullptr);
  }

  TEST_CASE("a click focuses and reports the click count; right press is a context click") {
    App a;
    dg_node_t* button = a.add(a.root, DG_NODE_TYPE_BUTTON);
    (void)a.pump(1);
    a.press(20, 20, DG_POINTER_BUTTON_PRIMARY, 2);
    const std::vector<dg_event> events = a.pump();
    const std::vector<dg_event> clicks = of_kind(events, DG_EVENT_CLICK);
    REQUIRE(clicks.size() == 1);
    CHECK(clicks[0].node == button);
    CHECK(clicks[0].clicks == 2);
    CHECK(of_kind(events, DG_EVENT_FOCUS_CHANGED).size() == 1);

    a.press(20, 20, DG_POINTER_BUTTON_SECONDARY);
    const std::vector<dg_event> after = a.pump();
    CHECK(of_kind(after, DG_EVENT_CLICK).empty());
    const std::vector<dg_event> context = of_kind(after, DG_EVENT_CONTEXT_CLICK);
    REQUIRE(context.size() == 1);
    CHECK(context[0].node == button);
    CHECK(context[0].x == 20);
    CHECK(context[0].clicks == 1);
    CHECK(dg_debug_post_pointer_button_ex(a.window, 1, 0, 0, 9, 0) == DG_ERR_INVALID_ARGUMENT);
  }

  TEST_CASE("a scroll view takes children, scrolls on the wheel, and follows its axis") {
    App a;
    dg_node_t* view = a.add(a.root, DG_NODE_TYPE_SCROLL_VIEW);
    App::set(view, DG_PROP_GAP, number(4));
    for (int i = 0; i < 10; ++i) {
      dg_node_t* item = a.add(view, DG_NODE_TYPE_BOX);
      App::set(item, DG_PROP_HEIGHT, length(40));
    }
    (void)a.pump(1);
    std::int32_t x = -1;
    std::int32_t y = -1;
    REQUIRE(dg_node_get_scroll_offset(view, &x, &y) == DG_ERR_OK);
    CHECK(y == 0);
    REQUIRE(dg_debug_warp_pointer(a.window, 50, 50) == DG_ERR_OK);
    REQUIRE(dg_debug_post_wheel(a.window, 0.0F, -1.0F) == DG_ERR_OK);
    (void)a.pump();
    REQUIRE(dg_node_get_scroll_offset(view, &x, &y) == DG_ERR_OK);
    CHECK(x == 0);
    CHECK(y == 40);

    // PageDown with a focused node inside scrolls by a page.
    dg_node_t* inside = a.add(view, DG_NODE_TYPE_BUTTON);
    (void)a.pump(1);
    REQUIRE(dg_node_focus(inside) == DG_ERR_OK);
    REQUIRE(dg_debug_post_key(a.window, 1, "PageDown") == DG_ERR_OK);
    const std::vector<dg_event> actions = of_kind(a.pump(), DG_EVENT_ACTION);
    REQUIRE(actions.size() == 1);
    REQUIRE(dg_node_get_scroll_offset(view, &x, &y) == DG_ERR_OK);
    CHECK(y > 40);
    App::set(view, DG_PROP_SCROLL_AXIS, option(DG_SCROLL_AXIS_HORIZONTAL));
    (void)a.pump(1);
    REQUIRE(dg_node_get_scroll_offset(view, nullptr, &y) == DG_ERR_OK);
    CHECK(y == 0);
    CHECK(dg_node_get_scroll_offset(a.root, nullptr, nullptr) == DG_ERR_UNSUPPORTED);
  }

  TEST_CASE("a list shows pushed items, scrolls, and selects on a press") {
    App a(true);
    dg_node_t* list = a.add(a.root, DG_NODE_TYPE_LIST);
    std::vector<std::string> texts;
    std::vector<const char*> items;
    for (int i = 0; i < 100; ++i) {
      texts.push_back("item " + std::to_string(i));
    }
    for (const std::string& t : texts) {
      items.push_back(t.c_str());
    }
    REQUIRE(dg_list_set_items(list, items.data(), 100, 20) == DG_ERR_OK);
    CHECK(dg_list_get_selected(list) == -1);
    (void)a.pump(1);

    a.press(50, 50, DG_POINTER_BUTTON_PRIMARY, 1);  // row 2 (rows are 20 tall)
    std::vector<dg_event> select = of_kind(a.pump(), DG_EVENT_LIST_SELECT);
    REQUIRE(select.size() == 1);
    CHECK(select[0].node == list);
    CHECK(select[0].x == 2);
    CHECK(select[0].clicks == 1);
    CHECK(dg_list_get_selected(list) == 2);

    REQUIRE(dg_debug_warp_pointer(a.window, 50, 50) == DG_ERR_OK);
    REQUIRE(dg_debug_post_wheel(a.window, 0.0F, -2.0F) == DG_ERR_OK);
    (void)a.pump();
    std::int32_t y = 0;
    REQUIRE(dg_node_get_scroll_offset(list, nullptr, &y) == DG_ERR_OK);
    CHECK(y == 80);
    a.press(50, 50);  // now row 6
    select = of_kind(a.pump(), DG_EVENT_LIST_SELECT);
    REQUIRE(select.size() == 1);
    CHECK(select[0].x == 6);

    CHECK(dg_list_set_selected(list, 100) == DG_ERR_VALUE_OUT_OF_RANGE);
    CHECK(dg_list_set_selected(list, -1) == DG_ERR_OK);
    CHECK(dg_list_get_selected(list) == -1);
    CHECK(dg_list_set_items(list, nullptr, 1, 0) == DG_ERR_INVALID_ARGUMENT);
    CHECK(dg_list_set_items(list, nullptr, 0, 0) == DG_ERR_OK);
    CHECK(dg_list_get_selected(a.root) == DG_ERR_UNSUPPORTED);
    dg_node_t* child = a.node(DG_NODE_TYPE_BOX);
    CHECK(dg_node_insert_before(list, child, nullptr) == DG_ERR_UNSUPPORTED);
    const dg_value horizontal = option(DG_SCROLL_AXIS_HORIZONTAL);
    CHECK(dg_node_set_prop(list, DG_PROP_SCROLL_AXIS, &horizontal) == DG_ERR_UNSUPPORTED);
    App::set(list, DG_PROP_FONT_SIZE, number(12));
  }

  TEST_CASE("DG_PROP_MAX_LENGTH caps typed text") {
    App a(true);
    dg_node_t* field = a.add(a.root, DG_NODE_TYPE_TEXT_FIELD);
    App::set(field, DG_PROP_MAX_LENGTH, number(3));
    REQUIRE(dg_node_focus(field) == DG_ERR_OK);
    (void)a.pump(1);
    REQUIRE(dg_debug_post_text_input(a.window, "abcdef") == DG_ERR_OK);
    (void)a.pump();
    char buf[16] = {};
    std::uint32_t len = 0;
    REQUIRE(dg_node_get_text(field, buf, sizeof(buf), &len) == DG_ERR_OK);
    CHECK(std::string(buf, len) == "abc");
    const dg_value negative = number(-1);
    CHECK(dg_node_set_prop(field, DG_PROP_MAX_LENGTH, &negative) == DG_ERR_VALUE_OUT_OF_RANGE);
    const dg_value three = number(3);
    CHECK(dg_node_set_prop(a.root, DG_PROP_MAX_LENGTH, &three) == DG_ERR_NOT_APPLICABLE);
  }

  TEST_CASE("timers, user events and dg_wake") {
    App a;
    const std::uint32_t once = dg_timer_add(a.app, 0, 0);
    REQUIRE(once != 0);
    const std::uint32_t never = dg_timer_add(a.app, 60000, 0);
    REQUIRE(never != 0);
    CHECK(dg_timer_cancel(a.app, never) == DG_ERR_OK);
    CHECK(dg_timer_cancel(a.app, never) == DG_ERR_INVALID_ARGUMENT);
    std::vector<dg_event> timers = of_kind(a.pump(2, 100), DG_EVENT_TIMER);
    REQUIRE(timers.size() == 1);
    CHECK(static_cast<std::uint32_t>(timers[0].x) == once);
    CHECK(timers[0].window == nullptr);

    // dg_wait_events() with no timeout still returns for a timer.
    const std::uint32_t soon = dg_timer_add(a.app, 30, 1);
    (void)a.pump(1, 0);
    REQUIRE(dg_wait_events(a.app, -1) >= 0);
    dg_event events[32];
    std::int32_t n = dg_poll_events(a.app, events, 32);
    bool fired = false;
    for (std::int32_t i = 0; i < n; ++i) {
      fired = fired || (events[i].kind == DG_EVENT_TIMER &&
                        static_cast<std::uint32_t>(events[i].x) == soon);
    }
    CHECK(fired);
    CHECK(dg_timer_cancel(a.app, soon) == DG_ERR_OK);

    const std::uint64_t payload = 0x123456789ABCDEF0ULL;
    std::thread poster([&] { CHECK(dg_post_user_event(a.app, payload) == DG_ERR_OK); });
    poster.join();
    std::vector<dg_event> user = of_kind(a.pump(2, 100), DG_EVENT_USER);
    REQUIRE(user.size() == 1);
    const std::uint64_t got =
        (static_cast<std::uint64_t>(static_cast<std::uint32_t>(user[0].y)) << 32U) |
        static_cast<std::uint32_t>(user[0].x);
    CHECK(got == payload);

    std::thread waker([&] {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      CHECK(dg_wake(a.app) == DG_ERR_OK);
    });
    const auto start = std::chrono::steady_clock::now();
    REQUIRE(dg_wait_events(a.app, 10000) >= 0);
    waker.join();
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(5));
    n = dg_poll_events(a.app, events, 32);
    CHECK(n >= 0);
  }

  TEST_CASE("images decode from bytes into an IMAGE node; bad data and types are refused") {
    App a;
    dg_node_t* image = a.add(a.root, DG_NODE_TYPE_IMAGE);
    const char* svg =
        "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16'>"
        "<rect width='16' height='16' fill='black'/></svg>";
    CHECK(dg_node_set_image_data(image, svg, static_cast<std::uint32_t>(std::strlen(svg)),
                                 0xFFFF0000U) == DG_ERR_OK);
    (void)a.pump(1);
    const char* png = dg_debug_capture_png(a.window);
    CHECK(png != nullptr);
    const char junk[] = "not an image";
    CHECK(dg_node_set_image_data(image, junk, sizeof(junk), 0) == DG_ERR_INVALID_ARGUMENT);
    CHECK(dg_node_set_image_data(image, nullptr, 0, 0) == DG_ERR_INVALID_ARGUMENT);
    CHECK(dg_node_set_image_file(image, "/nonexistent/x.png", 0) == DG_ERR_INVALID_ARGUMENT);
    dg_node_t* button = a.add(a.root, DG_NODE_TYPE_BUTTON);
    CHECK(dg_node_set_image_data(button, svg, 4, 0) == DG_ERR_UNSUPPORTED);
    const dg_value fit = option(DG_IMAGE_FIT_COVER);
    CHECK(dg_node_set_prop(image, DG_PROP_IMAGE_FIT, &fit) == DG_ERR_OK);
  }

  TEST_CASE("color.scrim is a theme token PopupStyle reads") {
    CHECK(DG_TOKEN_COLOR_SCRIM == 20);
    const auto theme = dg::load_builtin_theme();
    REQUIRE(theme.has_value());
    CHECK(dg::popup_style_from_theme(theme.value(), dg::ThemeVariant::kDark).scrim.argb() ==
          0x99000000U);
    CHECK(dg::popup_style_from_theme(theme.value(), dg::ThemeVariant::kLight).scrim.argb() ==
          0x660F141AU);
  }
}
