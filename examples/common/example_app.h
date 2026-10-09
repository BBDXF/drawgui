// Shared scaffolding of the examples built on dg::App / dg::Window: the
// headless setup their --verify / --dump-png modes use, input injection
// through the window manager (the route the C ABI's dg_debug_* hooks take),
// a PNG writer for Window::frame(), and the "ok / FAIL" check printer.

#pragma once

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <ostream>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "drawgui/app/app.h"
#include "drawgui/app/events.h"
#include "drawgui/app/window.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/window/window_manager.h"

namespace example {

// A whole decimal number, or nothing - command-line parsing that never throws.
inline std::optional<int> parse_int(const std::string& text) {
  int value = 0;
  const char* end = text.data() + text.size();
  const std::from_chars_result parsed = std::from_chars(text.data(), end, value);
  if (parsed.ec != std::errc{} || parsed.ptr != end) {
    return std::nullopt;
  }
  return value;
}

// Offscreen modes run on SDL's dummy video driver: no display needed.
inline void use_headless_driver() { setenv("SDL_VIDEODRIVER", "dummy", 1); }

// An App with `family` from `font_dir` as its default font. A missing font
// directory is not fatal: the scene then draws no text.
inline dg::Expected<dg::App, dg::AppError> make_app(const std::string& font_dir,
                                                    const std::string& family,
                                                    std::ostream& out) {
  dg::Expected<dg::App, dg::AppError> app = dg::App::create();
  if (!app) {
    out << "no app: " << app.error().message << "\n";
    return app;
  }
  if (!app.value().add_font(font_dir, family)) {
    out << "note: font \"" << family << "\" not found in " << font_dir << "\n";
  }
  return app;
}

// Drives one window the way a user would, then pumps so the runtime
// dispatches and repaints.
class Driver {
 public:
  Driver(dg::App& app, dg::Window& window) : app_(app), window_(window) {}

  std::vector<dg::Event> pump(int rounds = 3) {
    std::vector<dg::Event> all;
    for (int i = 0; i < rounds; ++i) {
      std::vector<dg::Event> got = app_.pump(5);
      all.insert(all.end(), got.begin(), got.end());
    }
    return all;
  }

  std::vector<dg::Event> move(int x, int y) {
    app_.window_manager().warp_pointer(window_.id(), x, y);
    return pump();
  }

  std::vector<dg::Event> press(int x, int y, bool down) {
    app_.window_manager().warp_pointer(window_.id(), x, y);
    app_.window_manager().post_pointer_button(window_.id(), down, x, y);
    return pump();
  }

  std::vector<dg::Event> click(int x, int y) {
    app_.window_manager().warp_pointer(window_.id(), x, y);
    app_.window_manager().post_pointer_button(window_.id(), true, x, y);
    app_.window_manager().post_pointer_button(window_.id(), false, x, y);
    return pump();
  }

  std::vector<dg::Event> click(dg::NodeId node) {
    const dg::PixelRect r = bounds(node);
    return click(r.x + (r.width / 2), r.y + (r.height / 2));
  }

  std::vector<dg::Event> key(dg::Key key, bool shift = false) {
    app_.window_manager().post_key(window_.id(), true, key, shift);
    app_.window_manager().post_key(window_.id(), false, key, shift);
    return pump();
  }

  std::vector<dg::Event> wheel(int x, int y, float dy) {
    app_.window_manager().warp_pointer(window_.id(), x, y);
    app_.window_manager().post_wheel(window_.id(), 0.0F, dy);
    return pump();
  }

  [[nodiscard]] dg::PixelRect bounds(dg::NodeId node) const {
    return window_.layout().render().absolute_bounds(node);
  }

 private:
  dg::App& app_;
  dg::Window& window_;
};

template <class E>
[[nodiscard]] std::vector<E> events_of(const std::vector<dg::Event>& events) {
  std::vector<E> out;
  for (const dg::Event& event : events) {
    if (const E* match = dg::event_as<E>(event)) {
      out.push_back(*match);
    }
  }
  return out;
}

inline bool check(bool ok, const std::string& what, std::ostream& out, bool& all) {
  out << (ok ? "  ok   " : "  FAIL ") << what << "\n";
  all = all && ok;
  return ok;
}

// Encodes the frame the last pump() painted.
inline int write_png(const dg::Window& window, const std::string& path, std::ostream& out) {
  const dg::RasterSurface* frame = window.frame();
  if (frame == nullptr) {
    out << "no frame was painted\n";
    return 1;
  }
  const std::vector<std::uint8_t> png = frame->encode_png();
  std::ofstream file(path, std::ios::binary);
  file.write(reinterpret_cast<const char*>(png.data()),  // NOLINT(*-reinterpret-cast)
             static_cast<std::streamsize>(png.size()));
  if (!file) {
    out << "could not write " << path << "\n";
    return 1;
  }
  out << "wrote " << path << "\n";
  return 0;
}

// Closes every window after `ms` (0 = never) - the --run-ms flag.
inline void close_after(dg::App& app, int ms) {
  if (ms > 0) {
    (void)app.add_timer(ms, false);
  }
}

}  // namespace example
