#include "form_window.h"

#include <chrono>
#include <fstream>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/widget/interaction.h"
#include "drawgui/window/window_manager.h"

#include "form_scene.h"

namespace form_window {
namespace {

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point started) {
  return std::chrono::duration<double, std::milli>(Clock::now() - started).count();
}

dg::TreeSpec spec_for(dg::PixelSize size) {
  dg::TreeSpec spec;
  spec.viewport = size;
  spec.background.fill = dg::Color::from_argb(0xFF0E1218);
  return spec;
}

void legend(std::ostream& out) {
  out << "  click the checkbox or a radio option; drag either slider's track.\n";
}

void apply_presets(form_scene::Scene& scene, const Settings& settings) {
  // toggle() only flips the widget's stored `checked` bit; it has no
  // RenderTree to repaint the indicator with, exactly like the real click
  // path in form_scene::dispatch() - so a preset has to call refresh()
  // itself, on the widget AND every radio sibling toggle() may have just
  // deselected, the same as dispatch() does for a real click.
  const auto toggle_and_refresh = [&](dg::NodeId id) {
    scene.widgets.toggle(id);
    scene.widgets.refresh(scene.tree.render(), id, dg::PointerState{});
    for (const dg::NodeId sibling : scene.widgets.group_members(id)) {
      scene.widgets.refresh(scene.tree.render(), sibling, dg::PointerState{});
    }
  };

  if (settings.preset_checked_radio_a >= 0 &&
      static_cast<std::size_t>(settings.preset_checked_radio_a) <
          scene.handles.radio_group_a.size()) {
    toggle_and_refresh(
        scene.handles.radio_group_a[static_cast<std::size_t>(settings.preset_checked_radio_a)]);
  }
  if (settings.preset_volume >= 0.0F) {
    scene.widgets.set_slider_value(scene.tree.render(), scene.handles.slider_volume,
                                   settings.preset_volume);
  }
  if (settings.preset_checkbox) {
    toggle_and_refresh(scene.handles.checkbox);
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
  void draw(form_scene::Scene& scene, dg::RasterSurface& surface);
  void handle_down(form_scene::Scene& scene, dg::PixelPoint at);
  void handle_move(form_scene::Scene& scene, dg::PixelPoint at);
  void handle_up(form_scene::Scene& scene, dg::PixelPoint at);
  void handle_leave(form_scene::Scene& scene);

  Settings settings_;
  std::ostream* out_;
  dg::WindowManager* manager_;
  dg::WindowId window_;
  std::optional<form_scene::Scene> scene_;
  std::optional<dg::RasterSurface> surface_;
  dg::Interaction interaction_;

  // Which slider (if any) a drag is moving. A SEPARATE mechanism from
  // dg::Interaction, exactly as examples/10_scrolling's own drag state is -
  // kSlider is not accepts_pointer(), so Interaction never sees a press on
  // one at all, and there is nothing for a drag to compete against
  // (design.md section 5.16.3's gesture arena stays undeclined for the
  // identical reason doc/scrolling.md section 6 already gives).
  std::optional<dg::NodeId> dragging_;
};

bool Runner::resize_if_needed() {
  const dg::Expected<dg::PixelSize, dg::WindowError> size = manager_->drawable_size(window_);
  if (!size) {
    return false;
  }
  const dg::PixelSize pixels = size.value();
  if (scene_.has_value()) {
    form_scene::Scene& scene = *scene_;
    if (surface_.has_value() && scene.tree.viewport() == pixels) {
      return true;
    }
    scene.tree.resize(pixels);
    scene.tree.layout();
    // The track a `grow`-weighted slider sits in may have just changed
    // width; resync_sliders() re-derives every thumb from its stored value
    // against the CURRENT track size - see its own doc comment for why a
    // relayout alone would silently leave the thumb at the wrong pixel.
    scene.widgets.resync_sliders(scene.tree.render());
  } else {
    scene_ = form_scene::build(spec_for(pixels));
    apply_presets(*scene_, settings_);
  }

  surface_ = dg::RasterSurface::create(pixels.width, pixels.height);
  return surface_.has_value() && scene_.has_value();
}

void Runner::draw(form_scene::Scene& scene, dg::RasterSurface& surface) {
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

void Runner::handle_down(form_scene::Scene& scene, dg::PixelPoint at) {
  const std::optional<dg::NodeId> hit = scene.tree.render().hit_test(at);
  const std::optional<dg::NodeId> slider =
      hit.has_value() ? scene.widgets.slidable_owner_of(scene.tree.render(), *hit)
                      : std::nullopt;
  if (slider.has_value()) {
    dragging_ = slider;
    const float value = scene.widgets.slider_value_at(scene.tree.render(), *slider, at.x);
    if (scene.widgets.set_slider_value(scene.tree.render(), *slider, value)) {
      *out_ << "  " << form_scene::describe(scene, *slider) << " -> "
            << scene.widgets.slider_value(*slider) << "\n";
    }
    return;
  }
  form_scene::dispatch(scene, interaction_,
                       dg::PointerEvent{window_, dg::PointerAction::kDown, at.x, at.y});
}

void Runner::handle_move(form_scene::Scene& scene, dg::PixelPoint at) {
  if (dragging_.has_value()) {
    const float value = scene.widgets.slider_value_at(scene.tree.render(), *dragging_, at.x);
    if (scene.widgets.set_slider_value(scene.tree.render(), *dragging_, value)) {
      *out_ << "  " << form_scene::describe(scene, *dragging_) << " -> "
            << scene.widgets.slider_value(*dragging_) << "\n";
    }
    return;
  }
  form_scene::dispatch(scene, interaction_,
                       dg::PointerEvent{window_, dg::PointerAction::kMove, at.x, at.y});
}

void Runner::handle_up(form_scene::Scene& scene, dg::PixelPoint at) {
  if (dragging_.has_value()) {
    dragging_.reset();
    return;
  }
  form_scene::dispatch(scene, interaction_,
                       dg::PointerEvent{window_, dg::PointerAction::kUp, at.x, at.y});
}

void Runner::handle_leave(form_scene::Scene& scene) {
  dragging_.reset();
  form_scene::dispatch(scene, interaction_,
                       dg::PointerEvent{window_, dg::PointerAction::kLeave, 0, 0});
}

int Runner::run() {
  const Clock::time_point started = Clock::now();
  *out_ << "form controls demo: click the checkbox/radios, drag a slider's track.\n";
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
    form_scene::Scene& scene = *scene_;
    dg::RasterSurface& surface = *surface_;

    for (const dg::PointerEvent& event : pumped.pointer) {
      switch (event.action) {
        case dg::PointerAction::kDown:
          handle_down(scene, dg::PixelPoint{event.x, event.y});
          break;
        case dg::PointerAction::kMove:
          handle_move(scene, dg::PixelPoint{event.x, event.y});
          break;
        case dg::PointerAction::kUp:
          handle_up(scene, dg::PixelPoint{event.x, event.y});
          break;
        case dg::PointerAction::kLeave:
          handle_leave(scene);
          break;
        case dg::PointerAction::kWheel:
          break;
      }
    }
    draw(scene, surface);
  }
  return 0;
}

std::optional<form_scene::Scene> rendered(const Settings& settings,
                                          std::optional<dg::RasterSurface>& surface) {
  surface = dg::RasterSurface::create(settings.size.width, settings.size.height);
  if (!surface.has_value()) {
    return std::nullopt;
  }
  form_scene::Scene scene = form_scene::build(spec_for(settings.size));
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
  spec.title = "drawgui form controls";
  spec.width = settings.size.width;
  spec.height = settings.size.height;
  spec.fill = dg::Color::from_argb(0xFF0E1218);
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
  const std::optional<form_scene::Scene> scene = rendered(settings, surface);
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
  out << "volume " << scene->widgets.slider_value(scene->handles.slider_volume)
      << "  brightness " << scene->widgets.slider_value(scene->handles.slider_brightness)
      << "\n";
  out << "wrote " << path << " (" << png.size() << " bytes)\n";
  return 0;
}

int probe(const Settings& settings, dg::PixelPoint point, std::ostream& out) {
  std::optional<dg::RasterSurface> surface;
  const std::optional<form_scene::Scene> scene = rendered(settings, surface);
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
      << (hit.has_value() ? form_scene::describe(*scene, *hit) : std::string{"<nothing>"})
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
  spec.title = "drawgui form controls (scripted)";
  spec.width = settings.size.width;
  spec.height = settings.size.height;
  spec.fill = dg::Color::from_argb(0xFF0E1218);
  dg::Expected<dg::WindowId, dg::WindowError> opened = manager.open(spec);
  if (!opened) {
    out << "could not open a window: " << opened.error().message << "\n";
    return 1;
  }
  const dg::WindowId window = opened.value();

  // One frame so the scene exists and its geometry is known - exactly what
  // the interactive Runner's first iteration does.
  form_scene::Scene scene = form_scene::build(spec_for(settings.size));
  std::optional<dg::RasterSurface> surface =
      dg::RasterSurface::create(settings.size.width, settings.size.height);
  if (!surface.has_value()) {
    out << "could not allocate a surface\n";
    return 1;
  }
  scene.tree.render().repaint_full(*surface);

  const auto centre_of = [&](dg::NodeId id) {
    const dg::PixelRect bounds = scene.tree.bounds(id);
    return dg::PixelPoint{bounds.x + (bounds.width / 2), bounds.y + (bounds.height / 2)};
  };
  const auto click = [&](dg::PixelPoint at, const char* label) {
    manager.warp_pointer(window, at.x, at.y);
    (void)manager.pump(50);
    manager.post_pointer_button(window, true, at.x, at.y);
    manager.post_pointer_button(window, false, at.x, at.y);
    const dg::PumpResult events = manager.pump(200);
    int downs = 0;
    int ups = 0;
    for (const dg::PointerEvent& event : events.pointer) {
      downs += event.action == dg::PointerAction::kDown ? 1 : 0;
      ups += event.action == dg::PointerAction::kUp ? 1 : 0;
    }
    out << "  clicked " << label << " at " << at.x << "," << at.y << ": " << downs
        << " real down event(s), " << ups << " real up event(s) delivered\n";
  };

  // warp_pointer()/post_pointer_button() move the REAL cursor and put real
  // button events on the platform's own queue - the same route
  // examples/05_widgets and examples/10_scrolling use, so this exercises the
  // actual event path rather than calling WidgetSet directly.
  click(centre_of(scene.handles.checkbox), "the checkbox");
  click(centre_of(scene.handles.radio_group_a[1]), "radio a/1");

  const dg::PixelRect track = scene.tree.bounds(scene.handles.slider_volume);
  const dg::PixelPoint drag_start{track.x + 10, track.y + (track.height / 2)};
  const dg::PixelPoint drag_end{track.x + track.width - 10, track.y + (track.height / 2)};
  manager.warp_pointer(window, drag_start.x, drag_start.y);
  (void)manager.pump(50);
  manager.post_pointer_button(window, true, drag_start.x, drag_start.y);
  (void)manager.pump(50);
  manager.warp_pointer(window, drag_end.x, drag_end.y);
  const dg::PumpResult drag_events = manager.pump(200);
  manager.post_pointer_button(window, false, drag_end.x, drag_end.y);
  (void)manager.pump(50);
  int moves = 0;
  for (const dg::PointerEvent& event : drag_events.pointer) {
    moves += event.action == dg::PointerAction::kMove ? 1 : 0;
  }
  out << "  dragged the volume slider from " << drag_start.x << " to " << drag_end.x << ": "
      << moves << " real move event(s) delivered\n";

  out << "wrote nothing; this mode is for interactive/manual verification\n";
  manager.request_close(window);
  (void)manager.pump(50);
  return 0;
}

}  // namespace form_window
