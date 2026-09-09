#include "sizing_window.h"

#include <chrono>
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

#include "sizing_scene.h"

namespace sizing_window {
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

void report_layout(const sizing_scene::Scene& scene, std::ostream& out) {
  out << "  viewport " << scene.tree.viewport().width << "x" << scene.tree.viewport().height
      << "  buttons";
  for (const dg::NodeId button : scene.handles.buttons) {
    out << " " << scene.tree.bounds(button).width;
  }
  const dg::PixelRect wide = scene.tree.bounds(scene.handles.wide_thumb);
  const dg::PixelRect square = scene.tree.bounds(scene.handles.square_thumb);
  out << "  16:9 " << wide.width << "x" << wide.height << "  1:1 " << square.width << "x"
      << square.height << "  chips";
  for (const dg::NodeId chip : scene.handles.chips) {
    out << " " << scene.tree.bounds(chip).width;
  }
  out << "\n";
  for (const std::string& diagnostic : scene.tree.diagnostics()) {
    out << "  " << diagnostic << "\n";
  }
}

void legend(std::ostream& out) {
  out << "  rows, top to bottom:\n"
      << "    1  three buttons, same base 200, shrink weights 1 / 2 / 3. Wide, they sit\n"
      << "       at 200 and nothing shrinks; narrow, the deficit splits by weight.\n"
      << "    2  two thumbnails with no size of their own. They are stretched to the\n"
      << "       row's height and derive their WIDTH from it - drag the window TALLER.\n"
      << "    3  four declared bases, 220/170/130/90, that stop fitting as you narrow.\n"
      << "    4  a footer whose items reach the right edge only because the row fills\n"
      << "       the main axis instead of hugging its content.\n";
}

class Runner {
 public:
  Runner(dg::WindowManager& manager, dg::WindowId window, const Settings& settings,
         std::ostream& out)
      : settings_(settings), out_(&out), manager_(&manager), window_(window) {}

  int run();

 private:
  bool resize_if_needed();
  void draw(sizing_scene::Scene& scene, dg::RasterSurface& surface);
  void report_hover(const sizing_scene::Scene& scene, dg::PixelPoint at);

  Settings settings_;

  // Pointers, not references: a reference member deletes assignment and trips
  // cppcoreguidelines-avoid-const-or-ref-data-members. Neither is ever null.
  std::ostream* out_;
  dg::WindowManager* manager_;
  dg::WindowId window_;
  std::optional<sizing_scene::Scene> scene_;
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
    sizing_scene::Scene& scene = *scene_;
    if (surface_.has_value() && scene.tree.viewport() == pixels) {
      return true;
    }
    scene.tree.resize(pixels);
    scene.tree.layout();
  } else {
    scene_ = sizing_scene::build(spec_for(pixels));
  }

  surface_ = dg::RasterSurface::create(pixels.width, pixels.height);
  if (!surface_.has_value() || !scene_.has_value()) {
    return false;
  }
  report_layout(*scene_, *out_);
  return true;
}

void Runner::draw(sizing_scene::Scene& scene, dg::RasterSurface& surface) {
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

// Shrinking moves many nodes at once, and a stale hit rectangle is the classic
// way that goes wrong. Printing the node under the pointer as the window is
// dragged is the interactive form of the every-pixel oracle in
// sizing_check.cpp.
void Runner::report_hover(const sizing_scene::Scene& scene, dg::PixelPoint at) {
  const std::optional<dg::NodeId> hit = scene.tree.render().hit_test(at);
  std::string name =
      hit.has_value() ? sizing_scene::describe(scene, *hit) : std::string{"<nothing>"};
  if (name == hovered_) {
    return;
  }
  hovered_ = std::move(name);
  *out_ << "  pointer " << at.x << "," << at.y << " -> " << hovered_ << "\n";
}

int Runner::run() {
  const Clock::time_point started = Clock::now();

  *out_ << "sizing demo: drag the window narrower to watch the toolbar shrink, and taller\n"
           "to watch the thumbnails derive their width from their height.\n";
  legend(*out_);

  while (manager_->open_window_count() > 0) {
    if (settings_.run_ms > 0 && ms_since(started) >= settings_.run_ms) {
      manager_->request_close(window_);
    }
    const dg::PumpResult pumped = manager_->pump(40);

    // needs_repaint BEFORE the pointer events, as the window manager's
    // contract requires: a resize moves every row, and an event from the same
    // pump would otherwise be hit-tested against a layout the window no longer
    // has.
    const bool stale = !pumped.needs_repaint.empty() || !surface_.has_value();
    if (stale && !resize_if_needed()) {
      return 1;
    }
    if (!scene_.has_value() || !surface_.has_value()) {
      return 1;
    }
    sizing_scene::Scene& scene = *scene_;
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
std::optional<sizing_scene::Scene> rendered(const Settings& settings,
                                            std::optional<dg::RasterSurface>& surface) {
  surface = dg::RasterSurface::create(settings.size.width, settings.size.height);
  if (!surface.has_value()) {
    return std::nullopt;
  }
  sizing_scene::Scene scene = sizing_scene::build(spec_for(settings.size));
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
  spec.title = "drawgui sizing";
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
  const std::optional<sizing_scene::Scene> scene = rendered(settings, surface);
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
  const std::optional<sizing_scene::Scene> scene = rendered(settings, surface);
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
      << (hit.has_value() ? sizing_scene::describe(*scene, *hit) : std::string{"<nothing>"})
      << "\n";
  return 0;
}

}  // namespace sizing_window
