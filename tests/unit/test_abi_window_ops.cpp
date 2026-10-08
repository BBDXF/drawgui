// The ABI 0.2 window operations (dg_window_set_title() ..
// dg_window_get_state()) and the DG_EVENT_WINDOW_* events, driven through
// the real C ABI on a real dg_window_t under SDL_VIDEODRIVER=dummy. See
// test_window_ops.cpp for what the dummy driver can and cannot show.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/abi/drawgui.h"

namespace {

dg_app_t* create_headless_app() {
  setenv("SDL_VIDEODRIVER", "dummy", 1);
  return dg_app_create(nullptr);
}

dg_window_opts default_opts() {
  dg_window_opts opts{};
  opts.size = sizeof(opts);
  opts.title = "abi window ops";
  opts.width = 160;
  opts.height = 120;
  opts.background_argb = 0xFF10141CU;
  return opts;
}

struct Fixture {
  dg_app_t* app = create_headless_app();
  dg_window_t* window = nullptr;

  explicit Fixture(const dg_window_opts& opts = default_opts()) {
    REQUIRE(app != nullptr);
    window = dg_window_create(app, &opts);
    REQUIRE(window != nullptr);
  }
  Fixture(const Fixture&) = delete;
  Fixture& operator=(const Fixture&) = delete;
  Fixture(Fixture&&) = delete;
  Fixture& operator=(Fixture&&) = delete;
  ~Fixture() { dg_app_destroy(app); }

  std::vector<dg_event> drain() const {
    REQUIRE(dg_wait_events(app, 0) >= 0);
    std::vector<dg_event> events(64);
    const std::int32_t count = dg_poll_events(app, events.data(), 64);
    REQUIRE(count >= 0);
    events.resize(static_cast<std::size_t>(count));
    return events;
  }

  std::string title() const {
    std::uint32_t len = 0;
    REQUIRE(dg_window_get_title(window, nullptr, 0, &len) == DG_ERR_OK);
    std::string text(len, '\0');
    REQUIRE(dg_window_get_title(window, text.data(), len, nullptr) == DG_ERR_OK);
    return text;
  }
};

const dg_event* find_kind(const std::vector<dg_event>& events, std::uint32_t kind) {
  for (const dg_event& event : events) {
    if (event.kind == kind) {
      return &event;
    }
  }
  return nullptr;
}

}  // namespace

TEST_SUITE("abi window operations") {
  TEST_CASE("dg_abi_version reports 0.2") {
    CHECK(DG_ABI_VERSION_MAJOR == 0U);
    CHECK(DG_ABI_VERSION_MINOR == 2U);
    CHECK(dg_abi_version() == ((0U << 16) | 2U));
  }

  TEST_CASE("title round-trip") {
    const Fixture f;
    CHECK(f.title() == "abi window ops");
    REQUIRE(dg_window_set_title(f.window, "新的标题") == DG_ERR_OK);
    CHECK(f.title() == "新的标题");
    CHECK(dg_window_set_title(f.window, nullptr) == DG_ERR_INVALID_ARGUMENT);
    CHECK(dg_window_get_title(f.window, nullptr, 4, nullptr) == DG_ERR_INVALID_ARGUMENT);

    // Truncated read: at most cap bytes, the rest untouched.
    char buf[4] = {'x', 'x', 'x', 'x'};
    std::uint32_t len = 0;
    REQUIRE(dg_window_set_title(f.window, "abcdef") == DG_ERR_OK);
    REQUIRE(dg_window_get_title(f.window, buf, 3, &len) == DG_ERR_OK);
    CHECK(len == 6U);
    CHECK(std::string(buf, 4) == "abcx");
  }

  TEST_CASE("set_size: queries, DG_EVENT_WINDOW_RESIZED, and argument checks") {
    const Fixture f;
    const std::vector<dg_event> initial = f.drain();
    CHECK(find_kind(initial, DG_EVENT_WINDOW_RESIZED) == nullptr);

    REQUIRE(dg_window_set_size(f.window, 240, 180) == DG_ERR_OK);
    std::int32_t width = 0;
    std::int32_t height = 0;
    REQUIRE(dg_window_get_size(f.window, &width, &height) == DG_ERR_OK);
    CHECK(width == 240);
    CHECK(height == 180);
    const std::vector<dg_event> events = f.drain();
    const dg_event* resized = find_kind(events, DG_EVENT_WINDOW_RESIZED);
    REQUIRE(resized != nullptr);
    CHECK(resized->window == f.window);
    CHECK(resized->x == 240);
    CHECK(resized->y == 180);
    CHECK(resized->node == nullptr);

    CHECK(dg_window_set_size(f.window, 0, 10) == DG_ERR_INVALID_ARGUMENT);
    CHECK(dg_window_get_size(f.window, nullptr, nullptr) == DG_ERR_OK);
  }

  TEST_CASE("position round-trip and DG_EVENT_WINDOW_MOVED") {
    const Fixture f;
    (void)f.drain();
    REQUIRE(dg_window_set_position(f.window, 30, 40) == DG_ERR_OK);
    std::int32_t x = -1;
    std::int32_t y = -1;
    REQUIRE(dg_window_get_position(f.window, &x, &y) == DG_ERR_OK);
    CHECK(x == 30);
    CHECK(y == 40);
    const std::vector<dg_event> events = f.drain();
    const dg_event* moved = find_kind(events, DG_EVENT_WINDOW_MOVED);
    REQUIRE(moved != nullptr);
    CHECK(moved->x == 30);
    CHECK(moved->y == 40);
  }

  TEST_CASE("min/max size from dg_window_opts and from the setters") {
    dg_window_opts opts = default_opts();
    opts.resizable = 1;
    opts.min_width = 100;
    opts.min_height = 80;
    opts.max_width = 200;
    opts.max_height = 150;
    const Fixture f(opts);
    std::int32_t width = 0;
    std::int32_t height = 0;
    REQUIRE(dg_window_set_size(f.window, 10, 10) == DG_ERR_OK);
    REQUIRE(dg_window_get_size(f.window, &width, &height) == DG_ERR_OK);
    CHECK(width == 100);
    CHECK(height == 80);

    REQUIRE(dg_window_set_max_size(f.window, 0, 0) == DG_ERR_OK);
    REQUIRE(dg_window_set_min_size(f.window, 0, 0) == DG_ERR_OK);
    REQUIRE(dg_window_set_size(f.window, 500, 400) == DG_ERR_OK);
    REQUIRE(dg_window_get_size(f.window, &width, &height) == DG_ERR_OK);
    CHECK(width == 500);

    REQUIRE(dg_window_set_max_size(f.window, 300, 300) == DG_ERR_OK);
    CHECK(dg_window_set_min_size(f.window, 400, 10) == DG_ERR_UNSUPPORTED);
    CHECK(std::string(dg_last_error()).find("dg_window_set_min_size") == 0);
  }

  TEST_CASE("an ABI 0.1-sized dg_window_opts ignores the appended fields") {
    dg_window_opts opts = default_opts();
    opts.size = static_cast<std::uint32_t>(offsetof(dg_window_opts, resizable));
    opts.min_width = 1000;  // would make the 160-wide window impossible
    const Fixture f(opts);
    std::int32_t width = 0;
    REQUIRE(dg_window_get_size(f.window, &width, nullptr) == DG_ERR_OK);
    CHECK(width == 160);
  }

  TEST_CASE("fullscreen: state bit and enter/leave events") {
    const Fixture f;
    (void)f.drain();
    CHECK((dg_window_get_state(f.window) & DG_WINDOW_STATE_FULLSCREEN) == 0);
    REQUIRE(dg_window_set_fullscreen(f.window, 1) == DG_ERR_OK);
    CHECK((dg_window_get_state(f.window) & DG_WINDOW_STATE_FULLSCREEN) != 0);
    CHECK(find_kind(f.drain(), DG_EVENT_WINDOW_FULLSCREEN_ENTERED) != nullptr);
    REQUIRE(dg_window_set_fullscreen(f.window, 0) == DG_ERR_OK);
    CHECK((dg_window_get_state(f.window) & DG_WINDOW_STATE_FULLSCREEN) == 0);
    CHECK(find_kind(f.drain(), DG_EVENT_WINDOW_FULLSCREEN_LEFT) != nullptr);
  }

  TEST_CASE("focus is reported at creation and in the state bits") {
    const Fixture f;
    CHECK(find_kind(f.drain(), DG_EVENT_WINDOW_FOCUS_GAINED) != nullptr);
    const std::int32_t state = dg_window_get_state(f.window);
    REQUIRE(state >= 0);
    CHECK((state & DG_WINDOW_STATE_FOCUSED) != 0);
    CHECK((state & DG_WINDOW_STATE_MINIMIZED) == 0);
  }

  TEST_CASE("minimize/maximize/restore: OK or a reported refusal") {
    const Fixture f;
    for (const std::int32_t status :
         {dg_window_minimize(f.window), dg_window_maximize(f.window),
          dg_window_restore(f.window)}) {
      CHECK((status == DG_ERR_OK || status == DG_ERR_UNSUPPORTED));
    }
    CHECK(dg_window_set_resizable(f.window, 1) == DG_ERR_OK);
  }

  TEST_CASE("set_icon argument checks") {
    const Fixture f;
    const std::vector<std::uint8_t> pixels(8 * 8 * 4, 0x80);
    CHECK(dg_window_set_icon(f.window, nullptr, 8, 8, 32, DG_PIXEL_FORMAT_BGRA8888) ==
          DG_ERR_INVALID_ARGUMENT);
    CHECK(dg_window_set_icon(f.window, pixels.data(), 0, 8, 32, DG_PIXEL_FORMAT_BGRA8888) ==
          DG_ERR_INVALID_ARGUMENT);
    CHECK(dg_window_set_icon(f.window, pixels.data(), 8, 8, 31, DG_PIXEL_FORMAT_BGRA8888) ==
          DG_ERR_INVALID_ARGUMENT);
    CHECK(dg_window_set_icon(f.window, pixels.data(), 8, 8, 32, 99U) ==
          DG_ERR_INVALID_ARGUMENT);
    for (const std::uint32_t format : {DG_PIXEL_FORMAT_BGRA8888, DG_PIXEL_FORMAT_RGBA8888}) {
      const std::int32_t status = dg_window_set_icon(f.window, pixels.data(), 8, 8, 32, format);
      CHECK((status == DG_ERR_OK || status == DG_ERR_UNSUPPORTED));
    }
  }

  TEST_CASE("null and stale windows are DG_ERR_INVALID_HANDLE") {
    dg_window_t* const none = nullptr;
    std::uint8_t pixel[4] = {};
    std::int32_t value = 0;
    CHECK(dg_window_set_title(none, "x") == DG_ERR_INVALID_HANDLE);
    CHECK(dg_window_get_title(none, nullptr, 0, nullptr) == DG_ERR_INVALID_HANDLE);
    CHECK(dg_window_set_size(none, 1, 1) == DG_ERR_INVALID_HANDLE);
    CHECK(dg_window_get_size(none, &value, &value) == DG_ERR_INVALID_HANDLE);
    CHECK(dg_window_set_position(none, 1, 1) == DG_ERR_INVALID_HANDLE);
    CHECK(dg_window_get_position(none, &value, &value) == DG_ERR_INVALID_HANDLE);
    CHECK(dg_window_minimize(none) == DG_ERR_INVALID_HANDLE);
    CHECK(dg_window_maximize(none) == DG_ERR_INVALID_HANDLE);
    CHECK(dg_window_restore(none) == DG_ERR_INVALID_HANDLE);
    CHECK(dg_window_set_fullscreen(none, 1) == DG_ERR_INVALID_HANDLE);
    CHECK(dg_window_set_resizable(none, 1) == DG_ERR_INVALID_HANDLE);
    CHECK(dg_window_set_min_size(none, 1, 1) == DG_ERR_INVALID_HANDLE);
    CHECK(dg_window_set_max_size(none, 1, 1) == DG_ERR_INVALID_HANDLE);
    CHECK(dg_window_set_icon(none, pixel, 1, 1, 4, DG_PIXEL_FORMAT_BGRA8888) ==
          DG_ERR_INVALID_HANDLE);
    CHECK(dg_window_get_state(none) == DG_ERR_INVALID_HANDLE);

    // A window whose app is gone: the handle stays readable but names nothing.
    dg_app_t* app = create_headless_app();
    REQUIRE(app != nullptr);
    const dg_window_opts opts = default_opts();
    dg_window_t* stale = dg_window_create(app, &opts);
    REQUIRE(stale != nullptr);
    dg_app_destroy(app);
    CHECK(dg_window_set_title(stale, "x") == DG_ERR_INVALID_HANDLE);
    CHECK(dg_window_set_size(stale, 10, 10) == DG_ERR_INVALID_HANDLE);
    CHECK(dg_window_get_state(stale) == DG_ERR_INVALID_HANDLE);
    CHECK(std::string(dg_last_error()).find("invalid window handle") != std::string::npos);
  }
}
