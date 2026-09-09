#include "scroll_window.h"

#include <chrono>
#include <cmath>
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

#include "scroll_scene.h"

namespace scroll_window {
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
  out << "  wheel over either strip scrolls it; click-drag inside one grabs its content.\n"
      << "  vertical:   " << scroll_scene::kVerticalItemCount << " items x "
      << scroll_scene::kVerticalItemHeight << " px in a "
      << scroll_scene::kVerticalViewportWidth << "x" << scroll_scene::kVerticalViewportHeight
      << " px viewport\n"
      << "  horizontal: " << scroll_scene::kHorizontalItemCount << " items x "
      << scroll_scene::kHorizontalItemWidth << " px in a "
      << scroll_scene::kHorizontalViewportHeight << " px tall strip\n";
}

// Rounds a wheel's fractional "notch" count into pixels. Named so both the
// window driver and a headless script apply the exact same arithmetic a
// check derives its expected offsets from.
int notches_to_pixels(float notches) {
  return static_cast<int>(
      std::lround(static_cast<double>(notches) * scroll_scene::kPixelsPerWheelNotch));
}

void apply_presets(scroll_scene::Scene& scene, const Settings& settings) {
  if (settings.preset_vertical != 0) {
    const dg::PixelRect content = scene.tree.content_bounds(scene.handles.vertical_viewport);
    scene.widgets.scroll_by(scene.tree.render(), scene.handles.vertical_viewport, content, 0,
                            settings.preset_vertical);
  }
  if (settings.preset_horizontal != 0) {
    const dg::PixelRect content = scene.tree.content_bounds(scene.handles.horizontal_viewport);
    scene.widgets.scroll_by(scene.tree.render(), scene.handles.horizontal_viewport, content,
                            settings.preset_horizontal, 0);
  }
}

class Runner {
 public:
  Runner(dg::WindowManager& manager, dg::WindowId window, const Settings& settings,
         std::ostream& out)
      : settings_(settings), out_(&out), manager_(&manager), window_(window) {}

  int run();

 private:
  bool resize_if_needed();
  void draw(scroll_scene::Scene& scene, dg::RasterSurface& surface);
  void handle_wheel(scroll_scene::Scene& scene, const dg::PointerEvent& event);
  void handle_down(scroll_scene::Scene& scene, dg::PixelPoint at);
  void handle_move(scroll_scene::Scene& scene, dg::PixelPoint at);
  void handle_up();

  Settings settings_;
  std::ostream* out_;
  dg::WindowManager* manager_;
  dg::WindowId window_;
  std::optional<scroll_scene::Scene> scene_;
  std::optional<dg::RasterSurface> surface_;

  // The drag state: which viewport is being dragged, and where the pointer
  // last was, so a move reports the DELTA rather than the absolute position.
  // No drag threshold and no gesture arena (design.md section 5.16.3) - this
  // scene's list items carry no click semantics of their own, so there is
  // nothing for a drag to compete against. doc/scrolling.md section 3 names
  // this decline.
  std::optional<dg::NodeId> dragging_;
  dg::PixelPoint drag_last_;
};

bool Runner::resize_if_needed() {
  const dg::Expected<dg::PixelSize, dg::WindowError> size = manager_->drawable_size(window_);
  if (!size) {
    return false;
  }
  const dg::PixelSize pixels = size.value();
  if (scene_.has_value()) {
    scroll_scene::Scene& scene = *scene_;
    if (surface_.has_value() && scene.tree.viewport() == pixels) {
      return true;
    }
    scene.tree.resize(pixels);
    scene.tree.layout();
  } else {
    scene_ = scroll_scene::build(spec_for(pixels));
    apply_presets(*scene_, settings_);
  }

  surface_ = dg::RasterSurface::create(pixels.width, pixels.height);
  return surface_.has_value() && scene_.has_value();
}

void Runner::draw(scroll_scene::Scene& scene, dg::RasterSurface& surface) {
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

void Runner::handle_wheel(scroll_scene::Scene& scene, const dg::PointerEvent& event) {
  const dg::PixelPoint at{event.x, event.y};
  const std::optional<dg::NodeId> hit = scene.tree.render().hit_test(at);
  if (!hit.has_value()) {
    return;
  }
  const std::optional<dg::NodeId> target =
      scene.widgets.scrollable_owner_of(scene.tree.render(), *hit);
  if (!target.has_value()) {
    return;
  }
  const dg::PixelRect content = scene.tree.content_bounds(*target);
  const int dx = notches_to_pixels(event.wheel_x);
  const int dy = notches_to_pixels(event.wheel_y);
  // A wheel scrolls DOWN when the reported delta is negative (the platform
  // convention post_wheel() and the SDL backend already normalise to:
  // positive is up/left), so the offset moves opposite the raw notch sign.
  if (scene.widgets.scroll_by(scene.tree.render(), *target, content, -dx, -dy)) {
    *out_ << "  wheel over " << scroll_scene::describe(scene, *target) << " -> offset "
          << scene.tree.render().scroll_offset(*target).x << ","
          << scene.tree.render().scroll_offset(*target).y << "\n";
  }
}

void Runner::handle_down(scroll_scene::Scene& scene, dg::PixelPoint at) {
  const std::optional<dg::NodeId> hit = scene.tree.render().hit_test(at);
  if (!hit.has_value()) {
    return;
  }
  dragging_ = scene.widgets.scrollable_owner_of(scene.tree.render(), *hit);
  drag_last_ = at;
}

void Runner::handle_move(scroll_scene::Scene& scene, dg::PixelPoint at) {
  if (!dragging_.has_value()) {
    return;
  }
  const int dx = at.x - drag_last_.x;
  const int dy = at.y - drag_last_.y;
  drag_last_ = at;
  if (dx == 0 && dy == 0) {
    return;
  }
  // Dragging grabs the content: moving the pointer down should reveal what
  // is ABOVE, so the offset moves opposite the pointer's own delta.
  const dg::PixelRect content = scene.tree.content_bounds(*dragging_);
  scene.widgets.scroll_by(scene.tree.render(), *dragging_, content, -dx, -dy);
}

void Runner::handle_up() {
  dragging_.reset();
}

int Runner::run() {
  const Clock::time_point started = Clock::now();
  *out_ << "scrolling demo: wheel over a strip to scroll it, or click-drag inside one.\n";
  legend(*out_);

  while (manager_->open_window_count() > 0) {
    if (settings_.run_ms > 0 && ms_since(started) >= settings_.run_ms) {
      manager_->request_close(window_);
    }
    const dg::PumpResult pumped = manager_->pump(40);

    const bool stale = !pumped.needs_repaint.empty() || !surface_.has_value();
    if (stale && !resize_if_needed()) {
      return 1;
    }
    if (!scene_.has_value() || !surface_.has_value()) {
      return 1;
    }
    scroll_scene::Scene& scene = *scene_;
    dg::RasterSurface& surface = *surface_;

    for (const dg::PointerEvent& event : pumped.pointer) {
      switch (event.action) {
        case dg::PointerAction::kWheel:
          handle_wheel(scene, event);
          break;
        case dg::PointerAction::kDown:
          handle_down(scene, dg::PixelPoint{event.x, event.y});
          break;
        case dg::PointerAction::kMove:
          handle_move(scene, dg::PixelPoint{event.x, event.y});
          break;
        case dg::PointerAction::kUp:
        case dg::PointerAction::kLeave:
          handle_up();
          break;
      }
    }
    draw(scene, surface);
  }
  return 0;
}

std::optional<scroll_scene::Scene> rendered(const Settings& settings,
                                            std::optional<dg::RasterSurface>& surface) {
  surface = dg::RasterSurface::create(settings.size.width, settings.size.height);
  if (!surface.has_value()) {
    return std::nullopt;
  }
  scroll_scene::Scene scene = scroll_scene::build(spec_for(settings.size));
  apply_presets(scene, settings);
  scene.tree.layout();
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
  spec.title = "drawgui scrolling";
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
  const std::optional<scroll_scene::Scene> scene = rendered(settings, surface);
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
  out << "vertical offset "
      << scene->tree.render().scroll_offset(scene->handles.vertical_viewport).y
      << "  horizontal offset "
      << scene->tree.render().scroll_offset(scene->handles.horizontal_viewport).x << "\n";
  out << "wrote " << path << " (" << png.size() << " bytes)\n";
  return 0;
}

int probe(const Settings& settings, dg::PixelPoint point, std::ostream& out) {
  std::optional<dg::RasterSurface> surface;
  const std::optional<scroll_scene::Scene> scene = rendered(settings, surface);
  if (!scene.has_value() || !surface.has_value()) {
    out << "could not render a frame\n";
    return 1;
  }
  const dg::PixelView view = surface->peek_pixels();
  const std::size_t offset = (static_cast<std::size_t>(point.y) * view.row_bytes) +
                             (static_cast<std::size_t>(point.x) * 4);
  const std::optional<dg::NodeId> hit = scene->tree.render().hit_test(point);
  out << "  at " << point.x << "," << point.y << "  pixel #" << std::hex
      << ((static_cast<unsigned>(view.pixels[offset + 2]) << 16U) |
          (static_cast<unsigned>(view.pixels[offset + 1]) << 8U) |
          static_cast<unsigned>(view.pixels[offset]))
      << std::dec << "  hit "
      << (hit.has_value() ? scroll_scene::describe(*scene, *hit) : std::string{"<nothing>"})
      << "\n";
  return 0;
}

int script(const Settings& settings, std::ostream& out) {
  dg::Expected<dg::WindowManager, dg::WindowError> made = dg::WindowManager::create();
  if (!made) {
    out << "could not start the window system: " << made.error().message << "\n";
    return 1;
  }
  dg::WindowManager manager = std::move(made.value());

  dg::WindowSpec spec;
  spec.title = "drawgui scrolling (scripted)";
  spec.width = settings.size.width;
  spec.height = settings.size.height;
  spec.fill = dg::Color::from_argb(0xFF14171C);
  dg::Expected<dg::WindowId, dg::WindowError> opened = manager.open(spec);
  if (!opened) {
    out << "could not open a window: " << opened.error().message << "\n";
    return 1;
  }
  const dg::WindowId window = opened.value();

  // One resize/pump so the scene exists and the drawable size is known -
  // exactly what the interactive Runner does on its first iteration.
  scroll_scene::Scene scene = scroll_scene::build(spec_for(settings.size));
  std::optional<dg::RasterSurface> surface =
      dg::RasterSurface::create(settings.size.width, settings.size.height);
  if (!surface.has_value()) {
    out << "could not allocate a surface\n";
    return 1;
  }
  scene.tree.render().repaint_full(*surface);

  const dg::PixelPoint over_vertical{
      (scroll_scene::kVerticalViewportWidth / 2) + 20 /* body padding */, 200};

  // warp_pointer() moves the REAL cursor and post_wheel() puts a real
  // SDL_EVENT_MOUSE_WHEEL on the platform's own queue - the same route
  // examples/05_widgets uses for a scripted click, so this exercises the
  // actual event path rather than calling WidgetSet::scroll_by() directly.
  manager.warp_pointer(window, over_vertical.x, over_vertical.y);
  // Pumped once so SDL has actually processed the motion the warp produced -
  // otherwise SDL_GetMouseState() inside post_wheel() would still report the
  // position from before the warp, since nothing has drained the queue yet.
  (void)manager.pump(50);
  manager.post_wheel(window, 0.0F, -3.0F);
  const dg::PumpResult first = manager.pump(200);
  for (const dg::PointerEvent& event : first.pointer) {
    if (event.action == dg::PointerAction::kWheel) {
      out << "  real wheel event delivered: dy=" << event.wheel_y << " at " << event.x << ","
          << event.y << "\n";
    }
  }

  out << "wrote nothing; this mode is for interactive/manual verification\n";
  manager.request_close(window);
  (void)manager.pump(50);
  return 0;
}

}  // namespace scroll_window
