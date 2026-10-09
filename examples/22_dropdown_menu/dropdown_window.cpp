#include "dropdown_window.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "drawgui/anim/animation_engine.h"
#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/render/node_lifecycle.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/shortcuts/action_scopes.h"
#include "drawgui/theme/theme_bindings.h"
#include "drawgui/theme/token_ids.generated.h"
#include "drawgui/ui/dropdown.h"
#include "drawgui/widget/focus.h"
#include "drawgui/window/popup_host.h"
#include "drawgui/window/window_manager.h"

#include "dropdown_scene.h"

namespace dropdown_window {
namespace {

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point started) {
  return std::chrono::duration<double, std::milli>(Clock::now() - started).count();
}

dg::Color ring_color(const dg::Theme& theme) {
  return theme.color_value(DG_TOKEN_COLOR_FOCUS_RING, dg::ThemeVariant::kDark)
      .value_or(dg::Color::from_argb(0xFFFF8800));
}

}  // namespace

// The pixel size Runner uses for its own first scene/tree/surface build and
// every later resize check - real drawable_size() whenever the window
// answers it (which a mapped, resized, or HiDPI-scaled window always does),
// `settings.size` only as the fallback for a query that fails outright
// (window not yet mapped, or a driver that cannot answer it at all). Not
// file-local: --verify-dropdown pins that Runner's constructor actually
// calls this rather than using settings.size directly, without driving the
// full interactive event loop (which never naturally returns under a
// headless driver).
dg::PixelSize initial_viewport(dg::WindowManager& manager, dg::WindowId window,
                               const Settings& settings) {
  const dg::Expected<dg::PixelSize, dg::WindowError> size = manager.drawable_size(window);
  return size.value_or(settings.size);
}

namespace {

void present_if_damaged(dg::WindowManager& manager, dg::WindowId window, dg::RenderTree& tree,
                        dg::RasterSurface& surface) {
  if (tree.damage().is_empty()) {
    return;
  }
  tree.repaint(surface);
  const dg::PixelView view = surface.peek_pixels();
  if (view.pixels == nullptr || !view.is_bgra8888) {
    return;
  }
  const dg::ImageView image{view.pixels, view.width, view.height, view.row_bytes,
                            dg::PixelFormat::kBgra8888};
  const dg::Expected<void, dg::WindowError> presented =
      manager.present(window, image, tree.painted().rects());
  if (!presented) {
    static bool warned = false;
    if (!warned) {
      warned = true;
      std::cerr << "present() failed: " << presented.error().message << "\n";
    }
  }
}

// repaint_full() ends by retiring the tree's damage (RenderTree::repaint_full()'s
// own doc comment: "discarding accumulated damage"), so right after it runs
// tree.damage() is already empty and present_if_damaged()'s guard above would
// drop the frame entirely - the pixels sit rasterized in `surface` with
// nothing ever pushed to the window, which is the exact first-frame black-
// window bug this sibling exists to avoid. It presents unconditionally,
// using tree.painted() - which repaint_full() has just set to the whole
// viewport - because the frame is already painted; there is nothing left to
// repaint.
void present_painted(dg::WindowManager& manager, dg::WindowId window, dg::RenderTree& tree,
                     dg::RasterSurface& surface) {
  const dg::PixelView view = surface.peek_pixels();
  if (view.pixels == nullptr || !view.is_bgra8888) {
    return;
  }
  const dg::ImageView image{view.pixels, view.width, view.height, view.row_bytes,
                            dg::PixelFormat::kBgra8888};
  const dg::Expected<void, dg::WindowError> presented =
      manager.present(window, image, tree.painted().rects());
  if (!presented) {
    static bool warned = false;
    if (!warned) {
      warned = true;
      std::cerr << "present() failed: " << presented.error().message << "\n";
    }
  }
}

class Runner {
 public:
  Runner(dg::WindowManager& manager, dg::WindowId host_window, const Settings& settings,
         std::ostream& out)
      : settings_(settings),
        out_(&out),
        manager_(&manager),
        host_window_(host_window),
        scene_(dropdown_scene::build(
            scene_options(settings, initial_viewport(manager, host_window, settings)))),
        popup_host_(manager) {
    const dg::PixelSize pixels = initial_viewport(manager, host_window, settings);
    host_surface_ = dg::RasterSurface::create(pixels.width, pixels.height);
  }

  int run();

 private:
  static dropdown_scene::Options scene_options(const Settings& settings, dg::PixelSize pixels) {
    dropdown_scene::Options options;
    options.spec.viewport = pixels;
    options.font_dir = settings.font_dir;
    return options;
  }

  void handle_pointer(const dg::PointerEvent& event);
  void handle_key(const dg::KeyEvent& event);
  void after_dropdown(const dg::DropdownResult& result);
  void refresh_ring();
  void resize_if_needed();
  dg::NodeSideTables side_tables();
  dg::UiContext ui();

  Settings settings_;
  std::ostream* out_;
  dg::WindowManager* manager_;
  dg::WindowId host_window_;
  dropdown_scene::Scene scene_;
  std::optional<dg::RasterSurface> host_surface_;

  // Empty: this example binds no theme tokens, runs no animations and
  // scopes no actions, but PopupHost::destroy() needs every NodeId-keyed
  // table by reference (dg::NodeSideTables).
  dg::ThemeBindings theme_bindings_;
  dg::AnimationEngine animation_;
  dg::ActionScopes action_scopes_;

  dg::PopupHost popup_host_;
  // The library component: builds the option Menu over PopupHost on open,
  // destroys it on close, and commits through WidgetSet::dropdown_select().
  dg::Dropdown dropdown_{scene_.handles.dropdown};
};

dg::NodeSideTables Runner::side_tables() {
  return {scene_.widgets, theme_bindings_, scene_.focus,
          animation_,     action_scopes_,  scene_.interaction};
}

dg::UiContext Runner::ui() {
  return dg::UiContext{popup_host_,
                       host_window_,
                       scene_.tree.render(),
                       side_tables(),
                       scene_.fonts.has_value() ? &*scene_.fonts : nullptr,
                       scene_.ui_font,
                       dg::popup_style_from_theme(scene_.theme, dg::ThemeVariant::kDark)};
}

// Runs on the SAME needs_repaint trigger examples/15_list/list_window.cpp's
// own resize_if_needed() uses, not a new poll of its own - a window resize
// is exactly one of the reasons a driver marks a window as needing repaint,
// alongside an ordinary expose. dropdown_scene::Scene is layout-driven
// (dg::LayoutTree, not hand-placed PixelRects - dropdown_scene.cpp's own
// add_button()/add_dropdown() go through BoxStyle/LayoutKind::kRow), so a
// changed viewport gets tree.resize() AND tree.layout() - resize() alone
// would leave the row's children still constrained to the OLD viewport
// width. LayoutTree::resize() already forwards to RenderTree::resize(),
// which damages the whole viewport itself, so no separate damage_all() is
// needed on that path.
void Runner::resize_if_needed() {
  const dg::Expected<dg::PixelSize, dg::WindowError> size =
      manager_->drawable_size(host_window_);
  if (!size) {
    scene_.tree.render().damage_all();
    return;
  }
  const dg::PixelSize pixels = size.value();
  if (host_surface_.has_value() && scene_.tree.render().viewport() == pixels) {
    // An ordinary expose, not a resize: still damage the whole window,
    // matching this branch's own prior (pre-resize-fix) behaviour exactly.
    scene_.tree.render().damage_all();
    return;
  }
  scene_.tree.resize(pixels);
  scene_.tree.layout();
  host_surface_ = dg::RasterSurface::create(pixels.width, pixels.height);
}

void Runner::refresh_ring() {
  // While the option list is open the highlight is the Menu's own row fill;
  // the host ring is collapsed rather than drawn under the overlay.
  dg::update_focus_ring(scene_.tree.render(), dg::LayoutTree::root(), scene_.ring,
                        dropdown_.is_open() ? std::nullopt : scene_.focus.current(),
                        ring_color(scene_.theme));
}

void Runner::after_dropdown(const dg::DropdownResult& result) {
  if (result.event == dg::DropdownEvent::kChanged && result.selected.has_value()) {
    *out_ << "selected \""
          << dropdown_scene::kOptions[static_cast<std::size_t>(*result.selected)] << "\"\n";
  }
  if (result.event != dg::DropdownEvent::kIgnored) {
    refresh_ring();
  }
}

void Runner::handle_pointer(const dg::PointerEvent& event) {
  dg::UiContext ctx = ui();
  if (dropdown_.is_open()) {
    after_dropdown(dropdown_.handle_pointer(ctx, event));
    return;
  }
  const bool open_requested = dropdown_scene::dispatch_pointer(scene_, event);
  if (open_requested) {
    after_dropdown(dropdown_.open(ctx));
  }
}

void Runner::handle_key(const dg::KeyEvent& event) {
  dg::UiContext ctx = ui();
  const dg::DropdownResult result = dropdown_.handle_key(ctx, event);
  if (result.event != dg::DropdownEvent::kIgnored || dropdown_.is_open()) {
    after_dropdown(result);
    return;
  }
  dropdown_scene::dispatch_key(scene_, event);
}

int Runner::run() {
  const Clock::time_point started = Clock::now();
  *out_ << "dropdown demo\n"
        << "  Tab reaches the dropdown; Down/Enter opens it (or click it); Up/Down moves the "
           "highlight; Enter selects; Escape or a click outside closes it without changing the "
           "selection. OVERLAY popup branch.\n";

  if (!host_surface_.has_value()) {
    return 1;
  }
  scene_.tree.render().repaint_full(*host_surface_);
  present_painted(*manager_, host_window_, scene_.tree.render(), *host_surface_);

  while (manager_->open_window_count() > 0) {
    if (settings_.run_ms > 0 && ms_since(started) >= settings_.run_ms) {
      manager_->request_close(host_window_);
    }
    const dg::PumpResult pumped = manager_->pump(40);
    for (const dg::WindowId id : pumped.needs_repaint) {
      if (id == host_window_) {
        resize_if_needed();
      }
    }
    for (const dg::PointerEvent& event : pumped.pointer) {
      handle_pointer(event);
    }
    for (const dg::KeyEvent& event : pumped.key) {
      handle_key(event);
    }
    if (!host_surface_.has_value()) {
      return 1;
    }
    present_if_damaged(*manager_, host_window_, scene_.tree.render(), *host_surface_);
  }
  return 0;
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
  spec.title = "drawgui dropdown";
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
  std::optional<dg::RasterSurface> surface =
      dg::RasterSurface::create(settings.size.width, settings.size.height);
  if (!surface.has_value()) {
    out << "could not allocate a surface\n";
    return 1;
  }
  dropdown_scene::Options options;
  options.spec.viewport = settings.size;
  options.spec.background.fill = dg::Color::from_argb(0xFF0E1218);
  options.font_dir = settings.font_dir;
  dropdown_scene::Scene scene = dropdown_scene::build(options);

  if (scene.fonts.has_value()) {
    scene.widgets.dropdown_select(scene.tree.render(), *scene.fonts, scene.handles.dropdown, 2);
  }
  dropdown_scene::apply_focus_change(scene, scene.handles.dropdown);

  scene.tree.render().repaint_full(*surface);

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
  out << "wrote " << path << " (" << png.size() << " bytes)\n";
  return 0;
}

}  // namespace dropdown_window
