#include "clip_window.h"

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

#include "clip_scene.h"

namespace clip_window {
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

// Flips every clipping panel to `visible` and back, so that two screenshots
// taken a moment apart differ only in the property. `open` is left alone - it
// is the control, and a control that changes is not one.
void set_clipping(clip_scene::Scene& scene, bool on) {
  const clip_scene::Handles& handles = scene.handles;
  for (const dg::NodeId id :
       {handles.cut, handles.round, handles.nest_outer, handles.nest_inner, handles.gone}) {
    dg::NodeStyle style = scene.tree.render().style(id);
    style.overflow = on ? dg::Overflow::kClip : dg::Overflow::kVisible;
    scene.tree.render().set_style(id, style);
  }
}

void report_layout(const clip_scene::Scene& scene, std::ostream& out) {
  const dg::PixelRect panel = scene.tree.bounds(scene.handles.cut);
  out << "  viewport " << scene.tree.viewport().width << "x" << scene.tree.viewport().height
      << "  panel " << panel.width << " wide  child overruns "
      << clip_scene::overflow_amount(scene) << " px\n";
}

// Opening the window is done BEFORE the Runner exists, so the Runner can hold
// a manager rather than an optional one. An optional member would have to be
// dereferenced on every line of the loop, and clang-tidy is right that a
// dereference guarded only by "run() called open_window() first" is guarded
// by a comment.
class Runner {
 public:
  Runner(dg::WindowManager& manager, dg::WindowId window, const Settings& settings,
         std::ostream& out)
      : settings_(settings), out_(&out), manager_(&manager), window_(window) {}

  int run();

 private:
  bool resize_if_needed();

  // Both take what they work on rather than reading the members run() has
  // already checked, which is the arrangement examples/05_widgets arrived at
  // for the same reason: clang-tidy cannot see that an optional tested in one
  // function is still engaged in another, and silencing the check would
  // silence the one thing that would notice the day that test moves.
  void draw(clip_scene::Scene& scene, dg::RasterSurface& surface);
  void report_hover(const clip_scene::Scene& scene, dg::PixelPoint at);

  Settings settings_;

  // A pointer, not a reference: a reference member deletes assignment and
  // trips cppcoreguidelines-avoid-const-or-ref-data-members. It is never null.
  std::ostream* out_;
  dg::WindowManager* manager_;
  dg::WindowId window_;
  std::optional<clip_scene::Scene> scene_;
  std::optional<dg::RasterSurface> surface_;
  std::string hovered_;
  bool clipping_ = true;
};

bool Runner::resize_if_needed() {
  const dg::Expected<dg::PixelSize, dg::WindowError> size = manager_->drawable_size(window_);
  if (!size) {
    return false;
  }
  const dg::PixelSize pixels = size.value();
  if (scene_.has_value()) {
    clip_scene::Scene& scene = *scene_;
    if (surface_.has_value() && scene.tree.viewport() == pixels) {
      return true;
    }
    scene.tree.resize(pixels);
    scene.tree.layout();
  } else {
    scene_ = clip_scene::build(spec_for(pixels));
  }

  surface_ = dg::RasterSurface::create(pixels.width, pixels.height);
  if (!surface_.has_value() || !scene_.has_value()) {
    return false;
  }
  clip_scene::Scene& built = *scene_;
  set_clipping(built, clipping_);
  report_layout(built, *out_);
  return true;
}

void Runner::draw(clip_scene::Scene& scene, dg::RasterSurface& surface) {
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

// The interactive half of "painting and hit testing agree": the name printed
// as the pointer crosses the boundary of a clipped panel changes at exactly
// the column the picture is cut at, and the child that is clipped away is
// never named at all.
void Runner::report_hover(const clip_scene::Scene& scene, dg::PixelPoint at) {
  const std::optional<dg::NodeId> hit = scene.tree.render().hit_test(at);
  std::string name =
      hit.has_value() ? clip_scene::describe(scene, *hit) : std::string{"<nothing>"};
  if (name == hovered_) {
    return;
  }
  hovered_ = std::move(name);
  *out_ << "  pointer " << at.x << "," << at.y << " -> " << hovered_ << "\n";
}

int Runner::run() {
  const Clock::time_point started = Clock::now();
  Clock::time_point toggled = started;

  *out_ << "clipping demo: resize the window and watch the overflow change; hover a\n"
        << "panel and watch the name change where the picture is cut.\n";

  while (manager_->open_window_count() > 0) {
    if (settings_.run_ms > 0 && ms_since(started) >= settings_.run_ms) {
      manager_->request_close(window_);
    }
    if (settings_.toggle_ms > 0 && ms_since(toggled) >= settings_.toggle_ms &&
        scene_.has_value()) {
      clipping_ = !clipping_;
      set_clipping(scene_.value(), clipping_);
      toggled = Clock::now();
      *out_ << "  overflow = " << (clipping_ ? "clip" : "visible") << "\n";
    }

    const dg::PumpResult pumped = manager_->pump(settings_.toggle_ms > 0 ? 16 : 40);

    // needs_repaint BEFORE the pointer events, which the window manager's
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
    clip_scene::Scene& scene = *scene_;
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
std::optional<clip_scene::Scene> rendered(const Settings& settings,
                                          std::optional<dg::RasterSurface>& surface) {
  surface = dg::RasterSurface::create(settings.size.width, settings.size.height);
  if (!surface.has_value()) {
    return std::nullopt;
  }
  clip_scene::Scene scene = clip_scene::build(spec_for(settings.size));
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
  spec.title = "drawgui clipping";
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
  const std::optional<clip_scene::Scene> scene = rendered(settings, surface);
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
  out << "wrote " << path << " (" << png.size() << " bytes)\n";
  return 0;
}

int probe(const Settings& settings, dg::PixelPoint point, std::ostream& out) {
  std::optional<dg::RasterSurface> surface;
  const std::optional<clip_scene::Scene> scene = rendered(settings, surface);
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
      << (static_cast<unsigned>(view.pixels[offset + 2]) << 16U |
          static_cast<unsigned>(view.pixels[offset + 1]) << 8U |
          static_cast<unsigned>(view.pixels[offset]))
      << std::dec << "  hit "
      << (hit.has_value() ? clip_scene::describe(*scene, *hit) : std::string{"<nothing>"})
      << "\n";
  return 0;
}

}  // namespace clip_window
