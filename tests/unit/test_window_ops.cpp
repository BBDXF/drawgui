// dg::WindowManager's window operations - title, size, position, min/max
// size, fullscreen, resizable, icon, state - and the WindowEvents pump()
// reports for them, against a real WindowManager under
// SDL_VIDEODRIVER=dummy (the precedent test_expose_repaint.cpp sets).
//
// What the dummy driver can and cannot show, measured rather than assumed:
// title, size, position, min/max size and fullscreen all take effect and
// produce their real events. minimize/maximize/restore and set_icon are
// refused by the driver, so for those this file checks that the refusal is
// reported, and drives the minimize/maximize/restore/focus event dispatch
// through post_window_event() instead.

#include <array>
#include <clocale>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/types.h"
#include "drawgui/window/window_manager.h"

namespace {

using dg::Expected;
using dg::ImageView;
using dg::PixelFormat;
using dg::PixelSize;
using dg::PumpResult;
using dg::WindowError;
using dg::WindowEvent;
using dg::WindowEventKind;
using dg::WindowId;
using dg::WindowManager;
using dg::WindowSpec;

WindowManager make_headless_manager() {
  setenv("SDL_VIDEODRIVER", "dummy", 1);
  Expected<WindowManager, WindowError> made = WindowManager::create();
  REQUIRE(made.has_value());
  return std::move(made.value());
}

WindowSpec spec_of(int width, int height) {
  WindowSpec spec;
  spec.title = "window ops";
  spec.width = width;
  spec.height = height;
  spec.fill = dg::Color::from_argb(0xFF202020);
  return spec;
}

WindowId open_window(WindowManager& manager, const WindowSpec& spec) {
  const Expected<WindowId, WindowError> opened = manager.open(spec);
  REQUIRE(opened.has_value());
  return opened.value();
}

std::optional<WindowEvent> find_event(const PumpResult& pumped, WindowEventKind kind) {
  for (const WindowEvent& event : pumped.window_events) {
    if (event.kind == kind) {
      return event;
    }
  }
  return std::nullopt;
}

}  // namespace

TEST_SUITE("window operations") {
  TEST_CASE("title round-trips, including UTF-8") {
    WindowManager manager = make_headless_manager();
    const WindowId window = open_window(manager, spec_of(120, 80));
    CHECK(manager.title(window).value() == "window ops");
    REQUIRE(manager.set_title(window, "标题 - renamed").has_value());
    CHECK(manager.title(window).value() == "标题 - renamed");
  }

  TEST_CASE("set_title cycles non-Latin-1 titles and leaves LC_CTYPE as it was") {
    WindowManager manager = make_headless_manager();
    const WindowId window = open_window(manager, spec_of(120, 80));
    const std::string ctype_before = std::setlocale(LC_CTYPE, nullptr);
    for (const char* title : {"标题改好了 ✓", "drawgui showcase — 第三个标题", "a – b", "组件展示"}) {
      REQUIRE(manager.set_title(window, title).has_value());
      CHECK(manager.title(window).value() == title);
      CHECK(std::string{std::setlocale(LC_CTYPE, nullptr)} == ctype_before);
    }
  }

  TEST_CASE("set_size changes size, drawable size, and reports kResized once") {
    WindowManager manager = make_headless_manager();
    const WindowId window = open_window(manager, spec_of(120, 80));
    const PumpResult opened = manager.pump(0);
    CHECK_FALSE(find_event(opened, WindowEventKind::kResized).has_value());

    REQUIRE(manager.set_size(window, 200, 150).has_value());
    CHECK(manager.size(window).value() == PixelSize{200, 150});
    const PumpResult pumped = manager.pump(0);
    const std::optional<WindowEvent> resized = find_event(pumped, WindowEventKind::kResized);
    REQUIRE(resized.has_value());
    CHECK(resized->window == window);
    CHECK(resized->width == 200);
    CHECK(resized->height == 150);
    CHECK(manager.drawable_size(window).value() == PixelSize{200, 150});
    bool repaint = false;
    for (const WindowId id : pumped.needs_repaint) {
      repaint = repaint || id == window;
    }
    CHECK(repaint);

    // Same size again: no new kResized.
    REQUIRE(manager.set_size(window, 200, 150).has_value());
    CHECK_FALSE(find_event(manager.pump(0), WindowEventKind::kResized).has_value());
  }

  TEST_CASE("set_size rejects non-positive sizes") {
    WindowManager manager = make_headless_manager();
    const WindowId window = open_window(manager, spec_of(120, 80));
    CHECK_FALSE(manager.set_size(window, 0, 10).has_value());
    CHECK_FALSE(manager.set_size(window, 10, -1).has_value());
    CHECK(manager.size(window).value() == PixelSize{120, 80});
  }

  TEST_CASE("set_position moves the window and reports kMoved") {
    WindowManager manager = make_headless_manager();
    const WindowId window = open_window(manager, spec_of(120, 80));
    (void)manager.pump(0);
    REQUIRE(manager.set_position(window, 40, 50).has_value());
    CHECK(manager.position(window).value() == dg::PixelPoint{40, 50});
    const std::optional<WindowEvent> moved =
        find_event(manager.pump(0), WindowEventKind::kMoved);
    REQUIRE(moved.has_value());
    CHECK(moved->x == 40);
    CHECK(moved->y == 50);
  }

  TEST_CASE("min/max size clamp set_size; contradictory limits fail") {
    WindowManager manager = make_headless_manager();
    const WindowId window = open_window(manager, spec_of(120, 80));
    REQUIRE(manager.set_min_size(window, 100, 70).has_value());
    REQUIRE(manager.set_max_size(window, 300, 200).has_value());
    REQUIRE(manager.set_size(window, 10, 10).has_value());
    CHECK(manager.size(window).value() == PixelSize{100, 70});
    REQUIRE(manager.set_size(window, 1000, 1000).has_value());
    CHECK(manager.size(window).value() == PixelSize{300, 200});
    CHECK_FALSE(manager.set_min_size(window, 400, 70).has_value());
    CHECK_FALSE(manager.set_max_size(window, 50, 200).has_value());
    CHECK_FALSE(manager.set_min_size(window, -1, 0).has_value());
    // 0 removes a limit.
    REQUIRE(manager.set_max_size(window, 0, 0).has_value());
    REQUIRE(manager.set_size(window, 500, 400).has_value());
    CHECK(manager.size(window).value() == PixelSize{500, 400});
  }

  TEST_CASE("WindowSpec limits apply at open") {
    WindowManager manager = make_headless_manager();
    WindowSpec spec = spec_of(120, 80);
    spec.resizable = true;
    spec.min_width = 100;
    spec.min_height = 60;
    spec.max_width = 150;
    spec.max_height = 90;
    const WindowId window = open_window(manager, spec);
    REQUIRE(manager.set_size(window, 400, 400).has_value());
    CHECK(manager.size(window).value() == PixelSize{150, 90});

    WindowSpec bad = spec_of(120, 80);
    bad.min_width = 200;
    bad.max_width = 100;
    CHECK_FALSE(manager.open(bad).has_value());
    CHECK(manager.open_window_count() == 1);
  }

  TEST_CASE("fullscreen round-trip reports enter/leave and the state flag") {
    WindowManager manager = make_headless_manager();
    const WindowId window = open_window(manager, spec_of(120, 80));
    (void)manager.pump(0);
    CHECK_FALSE(manager.state(window).value().fullscreen);
    REQUIRE(manager.set_fullscreen(window, true).has_value());
    CHECK(manager.state(window).value().fullscreen);
    CHECK(find_event(manager.pump(0), WindowEventKind::kEnteredFullscreen).has_value());
    REQUIRE(manager.set_fullscreen(window, false).has_value());
    CHECK_FALSE(manager.state(window).value().fullscreen);
    const PumpResult left = manager.pump(0);
    CHECK(find_event(left, WindowEventKind::kLeftFullscreen).has_value());
    // Back at its windowed size, which differs from the fullscreen one.
    const std::optional<WindowEvent> resized = find_event(left, WindowEventKind::kResized);
    REQUIRE(resized.has_value());
    CHECK(resized->width == 120);
    CHECK(resized->height == 80);
  }

  TEST_CASE("minimize/maximize/restore: the dummy driver's refusal is reported") {
    WindowManager manager = make_headless_manager();
    const WindowId window = open_window(manager, spec_of(120, 80));
    // Not CHECK_FALSE: a real display server accepts these. What must hold
    // on every driver is that a refusal says so and leaves state consistent.
    const Expected<void, WindowError> minimized = manager.minimize(window);
    if (!minimized) {
      CHECK(minimized.error().message.find("SDL_MinimizeWindow") != std::string::npos);
      CHECK_FALSE(manager.state(window).value().minimized);
    }
    const Expected<void, WindowError> maximized = manager.maximize(window);
    if (!maximized) {
      CHECK(maximized.error().message.find("SDL_MaximizeWindow") != std::string::npos);
    }
    (void)manager.restore(window);
    CHECK(manager.set_resizable(window, true).has_value());
  }

  TEST_CASE("state events are dispatched in order") {
    WindowManager manager = make_headless_manager();
    const WindowId window = open_window(manager, spec_of(120, 80));
    (void)manager.pump(0);
    const std::array kinds{WindowEventKind::kMinimized, WindowEventKind::kRestored,
                           WindowEventKind::kMaximized, WindowEventKind::kFocusLost,
                           WindowEventKind::kFocusGained};
    for (const WindowEventKind kind : kinds) {
      manager.post_window_event(window, kind);
    }
    const PumpResult pumped = manager.pump(0);
    REQUIRE(pumped.window_events.size() == kinds.size());
    for (std::size_t i = 0; i < kinds.size(); ++i) {
      CHECK(pumped.window_events[i].kind == kinds[i]);
      CHECK(pumped.window_events[i].window == window);
    }
  }

  TEST_CASE("the dummy driver reports a new window as focused") {
    WindowManager manager = make_headless_manager();
    const WindowId window = open_window(manager, spec_of(120, 80));
    CHECK(find_event(manager.pump(0), WindowEventKind::kFocusGained).has_value());
    CHECK(manager.state(window).value().focused);
  }

  TEST_CASE("set_icon validates the image before the platform sees it") {
    WindowManager manager = make_headless_manager();
    const WindowId window = open_window(manager, spec_of(120, 80));
    const std::vector<std::uint8_t> pixels(16 * 16 * 4, 0xFF);

    CHECK(manager.set_icon(window, ImageView{}).error().message.find("empty") !=
          std::string::npos);
    const ImageView narrow_stride{pixels.data(), 16, 16, 60, PixelFormat::kBgra8888};
    CHECK(manager.set_icon(window, narrow_stride).error().message.find("row_bytes") !=
          std::string::npos);

    // A valid image reaches the platform. The dummy driver has no icons and
    // refuses it; a real one accepts it. Either way it got past validation.
    for (const PixelFormat format : {PixelFormat::kBgra8888, PixelFormat::kRgba8888}) {
      const ImageView icon{pixels.data(), 16, 16, 16 * 4, format};
      const Expected<void, WindowError> set = manager.set_icon(window, icon);
      if (!set) {
        CHECK(set.error().message.find("SDL_SetWindowIcon") != std::string::npos);
      }
    }
  }

  TEST_CASE("present() refuses RGBA rather than swapping channels") {
    WindowManager manager = make_headless_manager();
    const WindowId window = open_window(manager, spec_of(16, 16));
    const std::vector<std::uint8_t> pixels(16 * 16 * 4, 0);
    const ImageView image{pixels.data(), 16, 16, 16 * 4, PixelFormat::kRgba8888};
    CHECK_FALSE(manager.present(window, image, dg::PixelRect{0, 0, 16, 16}).has_value());
  }

  TEST_CASE("every operation on an unknown window fails with no such window") {
    WindowManager manager = make_headless_manager();
    const WindowId window = open_window(manager, spec_of(120, 80));
    manager.close_now(window);
    const std::vector<std::uint8_t> pixels(4, 0);
    const std::array results{
        manager.set_title(window, "x"),
        manager.set_size(window, 10, 10),
        manager.set_position(window, 1, 1),
        manager.minimize(window),
        manager.maximize(window),
        manager.restore(window),
        manager.set_fullscreen(window, true),
        manager.set_resizable(window, true),
        manager.set_min_size(window, 1, 1),
        manager.set_max_size(window, 1, 1),
        manager.set_icon(window, ImageView{pixels.data(), 1, 1, 4, PixelFormat::kBgra8888})};
    for (const Expected<void, WindowError>& result : results) {
      REQUIRE_FALSE(result.has_value());
      CHECK(result.error().message == "no such window");
    }
    CHECK_FALSE(manager.title(window).has_value());
    CHECK_FALSE(manager.size(window).has_value());
    CHECK_FALSE(manager.position(window).has_value());
    CHECK_FALSE(manager.state(window).has_value());
    manager.post_window_event(window, WindowEventKind::kMinimized);
    CHECK(manager.pump(0).window_events.empty());
  }
}
