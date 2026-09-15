#include "theme_window.h"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/window/window_manager.h"

#include "theme_scene.h"

namespace theme_window {
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
  out << "  four panels, left to right: color.surface (bordered), color.primary,\n"
      << "  color.primary-hover, and a fourth carrying BOTH color.surface AND an\n"
      << "  int token (radius.md) on its four corners - every colour and every\n"
      << "  radius is a $token BINDING (dg::bind_token), never a literal. Click\n"
      << "  anywhere to switch light<->dark at runtime: no widget tree rebuild, no\n"
      << "  new nodes - see doc/theme.md.\n";
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
  std::optional<theme_scene::Scene> scene_;
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
    scene_ = theme_scene::build(spec_for(pixels));
  }
  surface_ = dg::RasterSurface::create(pixels.width, pixels.height);
  return surface_.has_value() && scene_.has_value();
}

void Runner::switch_theme() {
  if (!scene_.has_value()) {
    return;
  }
  const std::size_t unresolved = theme_scene::switch_variant(*scene_);
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

  *out_
      << "theme demo: click anywhere to switch light/dark; resize otherwise leaves it alone.\n";
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

std::optional<theme_scene::Scene> rendered(const Settings& settings,
                                           std::optional<dg::RasterSurface>& surface) {
  surface = dg::RasterSurface::create(settings.size.width, settings.size.height);
  if (!surface.has_value()) {
    return std::nullopt;
  }
  theme_scene::Scene scene = theme_scene::build(spec_for(settings.size));
  scene.tree.render().repaint_full(*surface);
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
  spec.title = "drawgui theme";
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
  const std::optional<theme_scene::Scene> scene = rendered(settings, surface);
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

}  // namespace theme_window
