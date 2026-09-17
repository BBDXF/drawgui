#include "theme_package_window.h"

#include <sys/resource.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/window/window_manager.h"

#include "theme_package_scene.h"

namespace theme_package_window {
namespace {

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point started) {
  return std::chrono::duration<double, std::milli>(Clock::now() - started).count();
}

dg::TreeSpec spec_for(dg::PixelSize size) {
  dg::TreeSpec spec;
  spec.viewport = size;
  spec.background.fill = dg::Color::from_argb(0xFF14171C);
  return spec;
}

void legend(std::ostream& out) {
  out << "  two panels loaded from an EXTERNAL theme package directory (see --package-dir) - "
      << "surface_panel and primary_panel, both $token-bound. Click anywhere to switch "
      << "light<->dark. Editing the package's own theme.json on disk and calling this "
      << "engine's reload takes effect with no widget-tree rebuild - see "
         "--verify-theme-package "
      << "for the measured, deterministic version of that claim.\n";
}

class Runner {
 public:
  Runner(dg::WindowManager& manager, dg::WindowId window, const Settings& settings,
         std::ostream& out)
      : settings_(settings), out_(&out), manager_(&manager), window_(window) {}

  int run();

 private:
  bool resize_if_needed();
  void draw();
  void switch_theme();

  Settings settings_;
  std::ostream* out_;
  dg::WindowManager* manager_;
  dg::WindowId window_;
  std::optional<theme_package_scene::Scene> scene_;
  std::optional<dg::RasterSurface> surface_;
};

bool Runner::resize_if_needed() {
  const dg::Expected<dg::PixelSize, dg::WindowError> size = manager_->drawable_size(window_);
  if (!size) {
    return false;
  }
  const dg::PixelSize pixels = size.value();
  if (scene_.has_value()) {
    if (surface_.has_value() && scene_->tree.viewport() == pixels) {
      return true;
    }
    scene_->tree.resize(pixels);
    scene_->tree.layout();
  } else {
    std::string error;
    scene_ = theme_package_scene::build(spec_for(pixels), settings_.package_dir, error);
    if (!scene_.has_value()) {
      *out_ << "  could not load the theme package: " << error << "\n";
      return false;
    }
  }
  surface_ = dg::RasterSurface::create(pixels.width, pixels.height);
  return surface_.has_value() && scene_.has_value();
}

void Runner::switch_theme() {
  if (!scene_.has_value()) {
    return;
  }
  scene_->variant = scene_->variant == dg::ThemeVariant::kLight ? dg::ThemeVariant::kDark
                                                                : dg::ThemeVariant::kLight;
  const std::size_t unresolved =
      scene_->bindings.apply(scene_->tree, scene_->theme, scene_->variant);
  *out_ << "  switched to " << (scene_->variant == dg::ThemeVariant::kLight ? "light" : "dark")
        << " (" << unresolved << " unresolved binding(s))\n";
}

void Runner::draw() {
  if (!scene_.has_value() || !surface_.has_value()) {
    return;
  }
  scene_->tree.layout();
  if (scene_->tree.render().damage().is_empty()) {
    return;
  }
  scene_->tree.render().repaint(*surface_);

  const dg::PixelView view = surface_->peek_pixels();
  if (view.pixels == nullptr || !view.is_bgra8888) {
    return;
  }
  const dg::ImageView image{view.pixels, view.width, view.height, view.row_bytes,
                            dg::PixelFormat::kBgra8888};
  (void)manager_->present(window_, image, scene_->tree.render().painted().rects());
}

int Runner::run() {
  const Clock::time_point started = Clock::now();

  *out_ << "theme package demo: click anywhere to switch light/dark.\n";
  legend(*out_);

  while (manager_->open_window_count() > 0) {
    if (settings_.run_ms > 0 && ms_since(started) >= settings_.run_ms) {
      manager_->request_close(window_);
    }
    const dg::PumpResult pumped = manager_->pump(40);
    for (const dg::PointerEvent& pointer : pumped.pointer) {
      if (pointer.action == dg::PointerAction::kDown) {
        switch_theme();
      }
    }
    const bool stale = !pumped.needs_repaint.empty() || !surface_.has_value();
    if (stale && !resize_if_needed()) {
      return 1;
    }
    draw();
  }
  return 0;
}

std::optional<theme_package_scene::Scene> rendered(const Settings& settings,
                                                   std::optional<dg::RasterSurface>& surface,
                                                   std::ostream& out) {
  surface = dg::RasterSurface::create(settings.size.width, settings.size.height);
  if (!surface.has_value()) {
    return std::nullopt;
  }
  std::string error;
  std::optional<theme_package_scene::Scene> scene =
      theme_package_scene::build(spec_for(settings.size), settings.package_dir, error);
  if (!scene.has_value()) {
    out << "  could not load the theme package: " << error << "\n";
    return std::nullopt;
  }
  scene->tree.render().repaint_full(*surface);
  return scene;
}

}  // namespace

int run(const Settings& settings, std::ostream& out) {
  dg::Expected<dg::WindowManager, dg::WindowError> made = dg::WindowManager::create();
  if (!made) {
    out << "could not start the window system: " << made.error().message << "\n";
    return 1;
  }
  dg::WindowManager manager = std::move(made.value());

  dg::WindowSpec spec;
  spec.title = "drawgui theme package";
  spec.width = settings.size.width;
  spec.height = settings.size.height;
  spec.fill = dg::Color::from_argb(0xFF14171C);
  dg::Expected<dg::WindowId, dg::WindowError> opened = manager.open(spec);
  if (!opened) {
    out << "could not open a window: " << opened.error().message << "\n";
    return 1;
  }

  Runner runner{manager, opened.value(), settings, out};
  return runner.run();
}

int dump_png(const Settings& settings, const std::string& path, std::ostream& out) {
  std::optional<dg::RasterSurface> surface;
  const std::optional<theme_package_scene::Scene> scene = rendered(settings, surface, out);
  if (!scene.has_value() || !surface.has_value()) {
    out << "could not render a frame\n";
    return 1;
  }
  const std::vector<std::uint8_t> png = surface->encode_png();
  if (png.empty()) {
    out << "could not encode the frame\n";
    return 2;
  }
  std::ofstream file(path, std::ios::binary);
  file.write(reinterpret_cast<const char*>(png.data()),
             static_cast<std::streamsize>(png.size()));
  if (!file) {
    out << "could not write " << path << "\n";
    return 3;
  }
  legend(out);
  out << "wrote " << path << " (" << png.size() << " bytes)\n";
  return 0;
}

int idle_probe(const Settings& settings, int idle_probe_ms, std::ostream& out) {
  dg::Expected<dg::WindowManager, dg::WindowError> made = dg::WindowManager::create();
  if (!made) {
    out << "error: " << made.error().message << "\n";
    return 1;
  }
  dg::WindowManager manager = std::move(made.value());

  dg::WindowSpec spec;
  spec.title = "drawgui theme package (idle probe)";
  spec.width = settings.size.width;
  spec.height = settings.size.height;
  spec.fill = dg::Color::from_argb(0xFF14171C);
  const dg::Expected<dg::WindowId, dg::WindowError> window = manager.open(spec);
  if (!window) {
    out << "error: " << window.error().message << "\n";
    return 1;
  }

  // The package is loaded (so the mechanism this probe is measuring is
  // genuinely PRESENT, not merely absent from the process), but nothing
  // ever calls reload() during the probe - no timer, no watcher, no poll:
  // "explicit call" is this slice's own settled file-change-detection
  // decision, and this is what that decision buys structurally rather
  // than by measurement alone.
  std::string error;
  std::optional<theme_package_scene::Scene> scene =
      theme_package_scene::build(spec_for(settings.size), settings.package_dir, error);
  if (!scene.has_value()) {
    out << "error: could not load the theme package: " << error << "\n";
    return 1;
  }

  const auto to_ms = [](const timeval& tv) {
    return (static_cast<double>(tv.tv_sec) * 1000.0) +
           (static_cast<double>(tv.tv_usec) / 1000.0);
  };

  (void)manager.pump(200);

  struct rusage before {};
  getrusage(RUSAGE_SELF, &before);
  const Clock::time_point wall_start = Clock::now();

  int wakeups = 0;
  double remaining_ms = static_cast<double>(idle_probe_ms);
  while (remaining_ms > 0.0) {
    (void)manager.pump(static_cast<int>(std::lround(remaining_ms)));
    ++wakeups;
    remaining_ms = static_cast<double>(idle_probe_ms) - ms_since(wall_start);
  }

  const double wall_ms = ms_since(wall_start);
  struct rusage after {};
  getrusage(RUSAGE_SELF, &after);
  const double cpu_ms = (to_ms(after.ru_utime) - to_ms(before.ru_utime)) +
                        (to_ms(after.ru_stime) - to_ms(before.ru_stime));

  out << "idle probe (theme package present, no reload happening):\n"
      << "  requested block:  " << idle_probe_ms << " ms\n"
      << "  actual wall time: " << wall_ms << " ms across " << wakeups << " pump() call(s)\n"
      << "  process CPU time consumed (user+sys) while blocked: " << cpu_ms << " ms\n"
      << "  CPU utilisation over the block: "
      << (wall_ms > 0.0 ? (cpu_ms / wall_ms * 100.0) : 0.0) << "%\n";
  return 0;
}

}  // namespace theme_package_window
