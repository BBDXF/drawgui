#include "focus_window.h"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/theme/token_ids.generated.h"
#include "drawgui/widget/focus.h"
#include "drawgui/window/popup_host.h"
#include "drawgui/window/window_manager.h"

#include "focus_scene.h"
#include "popup_menu.h"

namespace focus_window {
namespace {

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point started) {
  return std::chrono::duration<double, std::milli>(Clock::now() - started).count();
}

dg::Color ring_color(const dg::Theme& theme) {
  return theme.color_value(DG_TOKEN_COLOR_FOCUS_RING, dg::ThemeVariant::kDark)
      .value_or(dg::Color::from_argb(0xFFFF8800));
}

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

}  // namespace

// The pixel size Runner uses for its own first scene/tree/surface build and
// every later resize check - see examples/22_dropdown_menu/
// dropdown_window.cpp's own definition comment for why this is exported
// rather than file-local (--verify-focus pins that Runner actually calls
// this, not settings.size directly).
dg::PixelSize initial_viewport(dg::WindowManager& manager, dg::WindowId window,
                               const Settings& settings) {
  const dg::Expected<dg::PixelSize, dg::WindowError> size = manager.drawable_size(window);
  return size.value_or(settings.size);
}

namespace {

class Runner {
 public:
  Runner(dg::WindowManager& manager, dg::WindowId host_window, const Settings& settings,
         std::ostream& out)
      : settings_(settings),
        out_(&out),
        manager_(&manager),
        host_window_(host_window),
        scene_(focus_scene::build(
            scene_options(settings, initial_viewport(manager, host_window, settings)))),
        popup_host_(manager) {
    const dg::PixelSize pixels = initial_viewport(manager, host_window, settings);
    host_surface_ = dg::RasterSurface::create(pixels.width, pixels.height);
  }

  int run();

 private:
  static focus_scene::Options scene_options(const Settings& settings, dg::PixelSize pixels) {
    focus_scene::Options options;
    options.spec.viewport = pixels;
    options.font_dir = settings.font_dir;
    return options;
  }

  void handle_pointer(const dg::PointerEvent& event);
  void handle_key(const dg::KeyEvent& event);
  void open_popup();
  void close_popup();
  void resize_if_needed();

  Settings settings_;
  std::ostream* out_;
  dg::WindowManager* manager_;
  dg::WindowId host_window_;
  focus_scene::Scene scene_;
  std::optional<dg::RasterSurface> host_surface_;

  dg::PopupHost popup_host_;
  std::optional<dg::PopupHandle> popup_;
  popup_menu::Handles popup_handles_;
};

void Runner::open_popup() {
  const dg::PixelRect anchor = scene_.tree.render().absolute_bounds(scene_.handles.btn_open);
  dg::Expected<dg::PopupHandle, dg::WindowError> shown =
      popup_host_.show(host_window_, scene_.tree.render(), anchor, popup_menu::kSize,
                       dg::PopupPlacement::kBelow, dg::PopupFlags{});
  if (!shown) {
    *out_ << "could not open the popup: " << shown.error().message << "\n";
    return;
  }
  popup_ = shown.value();

  popup_handles_ =
      popup_menu::build(scene_.tree.render(), scene_.widgets, popup_->content_root);
  scene_.focus.enter_scope(popup_->content_root);
}

void Runner::close_popup() {
  if (!popup_.has_value()) {
    return;
  }
  scene_.focus.exit_scope(scene_.tree.render());
  dg::update_focus_ring(scene_.tree.render(), dg::LayoutTree::root(), scene_.ring,
                        scene_.focus.current(), ring_color(scene_.theme));
  popup_host_.close(*popup_);
  popup_.reset();
}

// See examples/22_dropdown_menu/dropdown_window.cpp's own
// Runner::resize_if_needed() for the full argument; identical shape here
// since focus_scene::Scene is layout-driven the same way (dg::LayoutTree).
void Runner::resize_if_needed() {
  const dg::Expected<dg::PixelSize, dg::WindowError> size =
      manager_->drawable_size(host_window_);
  if (!size) {
    scene_.tree.render().damage_all();
    return;
  }
  const dg::PixelSize pixels = size.value();
  if (host_surface_.has_value() && scene_.tree.render().viewport() == pixels) {
    scene_.tree.render().damage_all();
    return;
  }
  scene_.tree.resize(pixels);
  scene_.tree.layout();
  host_surface_ = dg::RasterSurface::create(pixels.width, pixels.height);
}

void Runner::handle_pointer(const dg::PointerEvent& event) {
  if (popup_.has_value() && popup_->open) {
    if (popup_host_.handle_pointer(*popup_, event.window, event, dg::PopupFlags{})) {
      close_popup();
      return;
    }
    // The click already survived handle_pointer()'s outside-dismissal check
    // above, so it is either on the popup's own content or on host chrome
    // still visible around it - either way, ordinary scene dispatch resolves
    // it correctly because the popup's buttons are attached to this SAME
    // scene_.widgets (popup_menu::build()'s own argument).
    focus_scene::dispatch_pointer(scene_, *manager_, host_window_, event);
    return;
  }
  const bool open_requested =
      focus_scene::dispatch_pointer(scene_, *manager_, host_window_, event);
  if (open_requested) {
    open_popup();
  }
}

void Runner::handle_key(const dg::KeyEvent& event) {
  if (popup_.has_value() && popup_->open) {
    if (popup_host_.handle_key(*popup_, event.window, event, dg::PopupFlags{})) {
      close_popup();
      return;
    }
    if (event.action != dg::KeyAction::kDown || event.key != dg::Key::kTab) {
      return;
    }
    if (event.window == host_window_) {
      focus_scene::tab(scene_, *manager_, host_window_,
                       dg::has(event.mods, dg::Modifier::kShift));
    }
    return;
  }
  focus_scene::dispatch_key(scene_, *manager_, host_window_, event);
}

int Runner::run() {
  const Clock::time_point started = Clock::now();
  *out_ << "focus demo\n"
        << "  Tab/Shift-Tab moves focus; click a widget to focus it; click the \"open popup\" "
           "button for an OVERLAY popup; click outside it or press Escape to dismiss it.\n";

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
  spec.title = "drawgui focus";
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
  focus_scene::Options options;
  options.spec.viewport = settings.size;
  options.spec.background.fill = dg::Color::from_argb(0xFF0E1218);
  options.font_dir = settings.font_dir;
  focus_scene::Scene scene = focus_scene::build(options);

  // Focus the first tab stop and render the ring so the still image shows
  // what Tab actually does, not only what a cold scene looks like.
  const dg::Color color = ring_color(scene.theme);
  scene.focus.set(scene.handles.reversed);
  dg::update_focus_ring(scene.tree.render(), dg::LayoutTree::root(), scene.ring,
                        scene.focus.current(), color);

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

}  // namespace focus_window
