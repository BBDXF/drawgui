#include "focus_window.h"

#include <chrono>
#include <cstdint>
#include <fstream>
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
  (void)manager.present(window, image, tree.painted().rects());
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
  (void)manager.present(window, image, tree.painted().rects());
}

class Runner {
 public:
  Runner(dg::WindowManager& manager, dg::WindowId host_window, const Settings& settings,
         std::ostream& out)
      : settings_(settings),
        out_(&out),
        manager_(&manager),
        host_window_(host_window),
        scene_(focus_scene::build(scene_options(settings))),
        popup_host_(manager) {
    host_surface_ = dg::RasterSurface::create(settings.size.width, settings.size.height);
  }

  int run();

 private:
  static focus_scene::Options scene_options(const Settings& settings) {
    focus_scene::Options options;
    options.spec.viewport = settings.size;
    options.font_dir = settings.font_dir;
    return options;
  }

  void handle_pointer(const dg::PointerEvent& event);
  void handle_key(const dg::KeyEvent& event);
  void open_popup();
  void close_popup();
  void dispatch_popup_native_pointer(const dg::PointerEvent& event);
  void dispatch_popup_native_tab(bool backwards);
  void refresh_native_ring();

  Settings settings_;
  std::ostream* out_;
  dg::WindowManager* manager_;
  dg::WindowId host_window_;
  focus_scene::Scene scene_;
  std::optional<dg::RasterSurface> host_surface_;

  dg::PopupHost popup_host_;
  std::optional<dg::PopupHandle> popup_;
  popup_menu::Handles popup_handles_;

  // Native branch only - a genuinely separate WidgetSet/Focus/ring, because
  // a native popup is a genuinely separate RenderTree/window (doc/popup.md
  // section 3; doc/focus.md section 6).
  std::optional<dg::WidgetSet> popup_native_widgets_;
  std::optional<dg::Focus> popup_native_focus_;
  dg::FocusRing popup_native_ring_;
  std::optional<dg::RasterSurface> popup_native_surface_;
};

void Runner::open_popup() {
  dg::PlatformCaps caps = dg::WindowManager::platform_caps();
  if (settings_.force_overlay) {
    caps.native_popup = false;
  }
  const dg::PixelRect anchor = scene_.tree.render().absolute_bounds(scene_.handles.btn_open);
  dg::Expected<dg::PopupHandle, dg::WindowError> shown =
      popup_host_.show(host_window_, scene_.tree.render(), caps, anchor, popup_menu::kSize,
                       dg::PopupPlacement::kBelow, dg::PopupFlags{});
  if (!shown) {
    *out_ << "could not open the popup: " << shown.error().message << "\n";
    return;
  }
  popup_ = shown.value();

  if (popup_->is_native) {
    popup_native_widgets_.emplace();
    popup_native_focus_.emplace();
    popup_native_ring_ = dg::FocusRing{};
    popup_handles_ =
        popup_menu::build(*popup_->tree, *popup_native_widgets_, popup_->content_root);
    popup_native_surface_ =
        dg::RasterSurface::create(popup_menu::kSize.width, popup_menu::kSize.height);
    // The OS just moved real keyboard focus to the new window; this
    // engine's own model mirrors that with a fresh dg::Focus rather than a
    // scope, because there is nothing to escape FROM - the popup's node
    // ids live in a different RenderTree entirely, so scene_.focus (the
    // host's own) is simply never consulted while this window has OS
    // focus, and stays exactly as it was.
  } else {
    popup_handles_ =
        popup_menu::build(scene_.tree.render(), scene_.widgets, popup_->content_root);
    scene_.focus.enter_scope(popup_->content_root);
  }
}

void Runner::close_popup() {
  if (!popup_.has_value()) {
    return;
  }
  if (popup_->is_native) {
    popup_native_widgets_.reset();
    popup_native_focus_.reset();
    popup_native_ring_ = dg::FocusRing{};
    popup_native_surface_.reset();
  } else {
    scene_.focus.exit_scope(scene_.tree.render());
    dg::update_focus_ring(scene_.tree.render(), dg::LayoutTree::root(), scene_.ring,
                          scene_.focus.current(), ring_color(scene_.theme));
  }
  popup_host_.close(*popup_);
  popup_.reset();
}

void Runner::refresh_native_ring() {
  if (!popup_.has_value() || !popup_native_focus_.has_value()) {
    return;
  }
  dg::update_focus_ring(*popup_->tree, popup_->content_root, popup_native_ring_,
                        popup_native_focus_->current(), ring_color(scene_.theme));
}

void Runner::dispatch_popup_native_pointer(const dg::PointerEvent& event) {
  if (event.action != dg::PointerAction::kDown) {
    return;
  }
  if (!popup_.has_value() || !popup_native_widgets_.has_value() ||
      !popup_native_focus_.has_value()) {
    return;
  }
  const dg::PixelPoint at{event.x, event.y};
  const std::optional<dg::NodeId> hit = popup_native_widgets_->widget_at(*popup_->tree, at);
  const bool focusable =
      hit.has_value() && dg::is_focusable(popup_native_widgets_->at(*hit).kind);
  popup_native_focus_->set(focusable ? hit : std::nullopt);
  refresh_native_ring();
}

void Runner::dispatch_popup_native_tab(bool backwards) {
  if (!popup_.has_value() || !popup_native_widgets_.has_value() ||
      !popup_native_focus_.has_value()) {
    return;
  }
  if (backwards) {
    popup_native_focus_->focus_previous(*popup_->tree, *popup_native_widgets_,
                                        popup_->content_root);
  } else {
    popup_native_focus_->focus_next(*popup_->tree, *popup_native_widgets_,
                                    popup_->content_root);
  }
  refresh_native_ring();
}

void Runner::handle_pointer(const dg::PointerEvent& event) {
  if (popup_.has_value() && popup_->open) {
    if (popup_host_.handle_pointer(*popup_, event.window, event, dg::PopupFlags{})) {
      close_popup();
      return;
    }
    if (popup_->is_native) {
      if (event.window == popup_->window) {
        dispatch_popup_native_pointer(event);
      }
      return;
    }
    // Overlay: the click already survived handle_pointer()'s outside-
    // dismissal check above, so it is either on the popup's own content or
    // on host chrome still visible around it - either way, ordinary scene
    // dispatch resolves it correctly because the popup's buttons are
    // attached to this SAME scene_.widgets (popup_menu::build()'s own
    // argument).
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
    if (popup_->is_native) {
      if (event.window == popup_->window) {
        dispatch_popup_native_tab(dg::has(event.mods, dg::Modifier::kShift));
      }
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
           "button for a "
        << (settings_.force_overlay ? "OVERLAY" : "NATIVE")
        << " popup; click outside it or press Escape to dismiss it.\n";

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
    if (popup_.has_value() && popup_->open && popup_->is_native &&
        popup_native_surface_.has_value()) {
      present_if_damaged(*manager_, popup_->window, *popup_->tree, *popup_native_surface_);
    }
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
