#include "opacity_window.h"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/window/window_manager.h"

#include "opacity_scene.h"

namespace opacity_window {
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

// A triangle wave, so the fade goes out and comes back through every value
// including both endpoints. A sine would spend most of its time near the ends,
// which is where a compositing defect is least visible.
float fade_at(double elapsed_ms, int period_ms) {
  const double phase = std::fmod(elapsed_ms, static_cast<double>(period_ms) * 2.0) /
                       static_cast<double>(period_ms);
  return static_cast<float>(phase <= 1.0 ? 1.0 - phase : phase - 1.0);
}

void report_layout(const opacity_scene::Scene& scene, std::ostream& out) {
  const dg::PixelRect chip = scene.tree.bounds(scene.handles.grouped_chips[0]);
  const dg::PixelRect band = opacity_scene::solo_band(scene, scene.handles.grouped_chips, 1);
  out << "  viewport " << scene.tree.viewport().width << "x" << scene.tree.viewport().height
      << "  chip " << chip.width << " wide  middle chip's solo band " << band.width << " px\n";
}

void legend(std::ostream& out) {
  out << "  panels, left to right:\n"
      << "    1  per-object alpha  - each chip drawn at alpha 128/255\n"
      << "    2  group opacity     - the same chips opaque, the group at 128/255\n"
      << "    3  nested groups     - two fades of 128/255, so the chips are a quarter\n"
      << "    4  a fade            - opacity animated with --fade-ms\n"
      << "  panels 1 and 2 differ ONLY where two chips overlap: the left one\n"
      << "  darkens there, the right one does not.\n";
}

class Runner {
 public:
  Runner(dg::WindowManager& manager, dg::WindowId window, const Settings& settings,
         std::ostream& out)
      : settings_(settings), out_(&out), manager_(&manager), window_(window) {}

  int run();

 private:
  bool resize_if_needed();
  void draw(opacity_scene::Scene& scene, dg::RasterSurface& surface);
  void report_hover(const opacity_scene::Scene& scene, dg::PixelPoint at);

  Settings settings_;

  // Pointers, not references: a reference member deletes assignment and trips
  // cppcoreguidelines-avoid-const-or-ref-data-members. Neither is ever null.
  std::ostream* out_;
  dg::WindowManager* manager_;
  dg::WindowId window_;
  std::optional<opacity_scene::Scene> scene_;
  std::optional<dg::RasterSurface> surface_;
  std::string hovered_;
};

bool Runner::resize_if_needed() {
  const dg::Expected<dg::PixelSize, dg::WindowError> size = manager_->drawable_size(window_);
  if (!size) {
    return false;
  }
  const dg::PixelSize pixels = size.value();
  if (scene_.has_value()) {
    opacity_scene::Scene& scene = *scene_;
    if (surface_.has_value() && scene.tree.viewport() == pixels) {
      return true;
    }
    scene.tree.resize(pixels);
    scene.tree.layout();
  } else {
    scene_ = opacity_scene::build(spec_for(pixels));
  }

  surface_ = dg::RasterSurface::create(pixels.width, pixels.height);
  if (!surface_.has_value() || !scene_.has_value()) {
    return false;
  }

  // Re-applied after every resize, not only at startup: a resize rebuilds the
  // scene, and a window opened to demonstrate "invisible but still clickable"
  // that quietly became visible again when it was dragged would demonstrate
  // the opposite.
  if (settings_.fade_ms == 0 && settings_.freeze_at >= 0.0F) {
    opacity_scene::set_opacity(*scene_, scene_->handles.fading_stage, settings_.freeze_at);
  }
  report_layout(*scene_, *out_);
  return true;
}

void Runner::draw(opacity_scene::Scene& scene, dg::RasterSurface& surface) {
  scene.tree.layout();
  if (scene.tree.render().damage().is_empty()) {
    return;
  }
  scene.tree.render().repaint(surface);

  const dg::PixelView view = surface.peek_pixels();
  if (view.pixels == nullptr || !view.is_bgra8888) {
    return;
  }
  const dg::ImageView image{view.pixels, view.width, view.height, view.row_bytes,
                            dg::PixelFormat::kBgra8888};
  (void)manager_->present(window_, image, scene.tree.render().painted().rects());
}

// The interactive half of "opacity changes no hit-test answer": the name keeps
// being printed as the pointer crosses a chip that has faded to nothing.
void Runner::report_hover(const opacity_scene::Scene& scene, dg::PixelPoint at) {
  const std::optional<dg::NodeId> hit = scene.tree.render().hit_test(at);
  std::string name =
      hit.has_value() ? opacity_scene::describe(scene, *hit) : std::string{"<nothing>"};
  if (name == hovered_) {
    return;
  }
  hovered_ = std::move(name);
  *out_ << "  pointer " << at.x << "," << at.y << " -> " << hovered_ << "\n";
}

int Runner::run() {
  const Clock::time_point started = Clock::now();

  *out_ << "opacity demo: compare the first two panels where the chips overlap.\n";
  legend(*out_);

  while (manager_->open_window_count() > 0) {
    if (settings_.run_ms > 0 && ms_since(started) >= settings_.run_ms) {
      manager_->request_close(window_);
    }
    if (settings_.fade_ms > 0 && scene_.has_value()) {
      opacity_scene::set_opacity(*scene_, scene_->handles.fading_stage,
                                 fade_at(ms_since(started), settings_.fade_ms));
    }

    const dg::PumpResult pumped = manager_->pump(settings_.fade_ms > 0 ? 16 : 40);

    // needs_repaint BEFORE the pointer events, as the window manager's
    // contract requires: a resize moves every panel, and an event from the
    // same pump would otherwise be hit-tested against a layout the window no
    // longer has.
    const bool stale = !pumped.needs_repaint.empty() || !surface_.has_value();
    if (stale && !resize_if_needed()) {
      return 1;
    }
    if (!scene_.has_value() || !surface_.has_value()) {
      return 1;
    }
    opacity_scene::Scene& scene = *scene_;
    dg::RasterSurface& surface = *surface_;

    for (const dg::PointerEvent& event : pumped.pointer) {
      if (event.action != dg::PointerAction::kLeave) {
        report_hover(scene, dg::PixelPoint{event.x, event.y});
      }
    }
    draw(scene, surface);
  }
  return 0;
}

// One offscreen frame, shared by --dump-png and --probe so that what is
// answered is what would have been drawn.
std::optional<opacity_scene::Scene> rendered(const Settings& settings,
                                             std::optional<dg::RasterSurface>& surface) {
  surface = dg::RasterSurface::create(settings.size.width, settings.size.height);
  if (!surface.has_value()) {
    return std::nullopt;
  }
  opacity_scene::Scene scene = opacity_scene::build(spec_for(settings.size));
  if (settings.freeze_at >= 0.0F) {
    opacity_scene::set_opacity(scene, scene.handles.fading_stage, settings.freeze_at);
  }
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
  spec.title = "drawgui opacity";
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
  const std::optional<opacity_scene::Scene> scene = rendered(settings, surface);
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
  report_layout(*scene, out);
  legend(out);
  out << "wrote " << path << " (" << png.size() << " bytes)\n";
  return 0;
}

int probe(const Settings& settings, dg::PixelPoint point, std::ostream& out) {
  std::optional<dg::RasterSurface> surface;
  const std::optional<opacity_scene::Scene> scene = rendered(settings, surface);
  if (!scene.has_value() || !surface.has_value()) {
    out << "could not render a frame\n";
    return 1;
  }
  report_layout(*scene, out);

  const dg::PixelView view = surface->peek_pixels();
  const std::size_t offset = (static_cast<std::size_t>(point.y) * view.row_bytes) +
                             (static_cast<std::size_t>(point.x) * 4);
  const std::optional<dg::NodeId> hit = scene->tree.render().hit_test(point);
  out << "  at " << point.x << "," << point.y << "  pixel #" << std::hex
      << ((static_cast<unsigned>(view.pixels[offset + 2]) << 16U) |
          (static_cast<unsigned>(view.pixels[offset + 1]) << 8U) |
          static_cast<unsigned>(view.pixels[offset]))
      << std::dec << "  hit "
      << (hit.has_value() ? opacity_scene::describe(*scene, *hit) : std::string{"<nothing>"})
      << "\n";
  return 0;
}

}  // namespace opacity_window
