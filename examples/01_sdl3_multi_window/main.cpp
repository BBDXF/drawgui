// Opens several windows at once, closes them one at a time, and exits when
// the last one is gone.
//
// This is the acceptance criterion for the first step of the restarted plan
// in executable form: proof that drawgui can put windows on screen and manage
// more than one of them, before anything is abstracted over that ability.
//
// The interesting part is not that a window appears. It is that closing one
// leaves the others alive and responsive, and that the process then exits 0
// on its own. Run it and close the windows in any order.
//
// --auto-close-ms N drives the same sequence without a human: every N
// milliseconds it asks for the next window to close. It goes through
// WindowManager::request_close, which pushes a real close event onto the real
// queue, so the scripted run exercises exactly the path the close button
// does.

#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "drawgui/graphics/types.h"
#include "drawgui/window/window_manager.h"

namespace {

// A frame's worth of waiting. Long enough not to spin, short enough that a
// scripted deadline is honoured on the millisecond it falls due.
constexpr int kPumpTimeoutMs = 16;

// Distinct sizes as well as distinct colours: window managers are free to
// stack new windows on top of each other, and two identically sized
// rectangles of different colours are harder to count in a screenshot than
// three obviously different ones.
std::vector<dg::WindowSpec> demo_windows() {
  return {
      dg::WindowSpec{"drawgui 1 - crimson", 480, 320, dg::Color::rgba(0xC0, 0x39, 0x2B)},
      dg::WindowSpec{"drawgui 2 - emerald", 400, 260, dg::Color::rgba(0x27, 0xAE, 0x60)},
      dg::WindowSpec{"drawgui 3 - azure", 320, 200, dg::Color::rgba(0x2E, 0x86, 0xDE)},
  };
}

struct DemoWindow {
  dg::WindowId id;
  std::string title;
};

std::string_view title_of(const std::vector<DemoWindow>& windows, dg::WindowId id) {
  for (const DemoWindow& window : windows) {
    if (window.id == id) {
      return window.title;
    }
  }
  return "<unknown window>";
}

std::optional<int> parse_positive_int(const std::string& text) {
  int value = 0;
  const char* end = text.data() + text.size();
  const std::from_chars_result parsed = std::from_chars(text.data(), end, value);
  if (parsed.ec != std::errc{} || parsed.ptr != end || value <= 0) {
    return std::nullopt;
  }
  return value;
}

// nullopt means the command line was wrong; a nullopt *inside* the value
// means it was fine and no script was asked for.
std::optional<std::optional<int>> parse_auto_close_ms(int argc, char** argv) {
  std::optional<int> auto_close_ms;
  const std::vector<std::string> args{argv + 1, argv + argc};

  for (std::size_t i = 0; i < args.size(); ++i) {
    if (args[i] != "--auto-close-ms" || i + 1 >= args.size()) {
      return std::nullopt;
    }
    ++i;
    auto_close_ms = parse_positive_int(args[i]);
    if (!auto_close_ms.has_value()) {
      return std::nullopt;
    }
  }
  return auto_close_ms;
}

void run_until_all_closed(dg::WindowManager& manager, const std::vector<DemoWindow>& windows,
                          std::optional<int> auto_close_ms) {
  const auto started = std::chrono::steady_clock::now();
  std::size_t requested = 0;

  while (manager.open_window_count() > 0) {
    if (auto_close_ms.has_value()) {
      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - started);
      const std::int64_t due = elapsed.count() / *auto_close_ms;

      while (static_cast<std::int64_t>(requested) < due && requested < windows.size()) {
        std::cout << "requesting close of \"" << windows[requested].title << "\"\n";
        manager.request_close(windows[requested].id);
        ++requested;
      }
    }

    for (const dg::WindowId closed : manager.pump(kPumpTimeoutMs).closed) {
      std::cout << "closed \"" << title_of(windows, closed) << "\" (id " << closed.value
                << "); " << manager.open_window_count() << " window(s) still open\n";
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  const std::optional<std::optional<int>> auto_close_ms = parse_auto_close_ms(argc, argv);
  if (!auto_close_ms.has_value()) {
    std::cerr << "usage: drawgui_multi_window [--auto-close-ms N]\n"
              << "  --auto-close-ms N  request one window's close every N milliseconds\n";
    return 2;
  }

  dg::Expected<dg::WindowManager, dg::WindowError> created = dg::WindowManager::create();
  if (!created) {
    std::cerr << "cannot start the window manager: " << created.error().message << "\n";
    return 1;
  }
  dg::WindowManager manager = std::move(created).value();

  std::vector<DemoWindow> windows;
  for (const dg::WindowSpec& spec : demo_windows()) {
    dg::Expected<dg::WindowId, dg::WindowError> opened = manager.open(spec);
    if (!opened) {
      std::cerr << "cannot open \"" << spec.title << "\": " << opened.error().message << "\n";
      return 1;
    }
    windows.push_back(DemoWindow{opened.value(), spec.title});
    std::cout << "opened \"" << spec.title << "\" (id " << opened.value().value << ", "
              << spec.width << "x" << spec.height << ")\n";
  }

  std::cout << manager.open_window_count()
            << " windows open at once - close them one at a time\n";

  run_until_all_closed(manager, windows, *auto_close_ms);

  std::cout << "every window closed; exiting cleanly\n";
  return 0;
}
