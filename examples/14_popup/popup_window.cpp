#include "popup_window.h"

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
#include "drawgui/window/popup_host.h"
#include "drawgui/window/window_manager.h"

#include "popup_scene.h"

namespace popup_window {
namespace {

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point started) {
  return std::chrono::duration<double, std::milli>(Clock::now() - started).count();
}

constexpr dg::PixelRect kMenuButton{40, 40, 100, 24};

dg::TreeSpec host_spec(dg::PixelSize size) {
  dg::TreeSpec spec;
  spec.viewport = size;
  spec.background.fill = dg::Color::from_argb(0xFF14171C);
  return spec;
}

void legend(std::ostream& out, bool native) {
  out << "  click the button to open a "
      << (native ? "NATIVE (real SDL popup window)" : "OVERLAY (in-window layer)")
      << " popup; click outside it or press Escape to dismiss it.\n";
}

// Presents whichever RenderTree/RasterSurface pair changed, through the
// window id it belongs to - the same three-line shape every prior example's
// window loop already repeats for its own single window, applied here to
// two independent windows.
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
        host_tree_(host_spec(settings.size)),
        popup_host_(manager) {
    dg::NodeStyle button_style;
    button_style.fill = dg::Color::from_argb(0xFF3A4152);
    host_tree_.add_child(dg::RenderTree::root(), kMenuButton, button_style);
    host_surface_ = dg::RasterSurface::create(settings.size.width, settings.size.height);
  }

  int run();

 private:
  void handle_pointer(const dg::PointerEvent& event);
  void handle_key(const dg::KeyEvent& event);
  void open_popup();

  Settings settings_;
  std::ostream* out_;
  dg::WindowManager* manager_;
  dg::WindowId host_window_;
  dg::RenderTree host_tree_;
  std::optional<dg::RasterSurface> host_surface_;
  dg::PopupHost popup_host_;
  std::optional<dg::PopupHandle> popup_;
  std::optional<dg::RasterSurface> popup_surface_;
};

void Runner::open_popup() {
  dg::PlatformCaps caps = dg::WindowManager::platform_caps();
  if (settings_.force_overlay) {
    caps.native_popup = false;
  }
  dg::Expected<dg::PopupHandle, dg::WindowError> shown =
      popup_host_.show(host_window_, host_tree_, caps, kMenuButton, popup_scene::kContentSize,
                       dg::PopupPlacement::kBelow, dg::PopupFlags{});
  if (!shown) {
    *out_ << "could not open the popup: " << shown.error().message << "\n";
    return;
  }
  popup_ = shown.value();
  popup_scene::build_menu_content(*popup_->tree, popup_->content_root,
                                  popup_scene::kContentSize);
  if (popup_->is_native) {
    popup_surface_ = dg::RasterSurface::create(popup_scene::kContentSize.width,
                                               popup_scene::kContentSize.height);
  }
}

void Runner::handle_pointer(const dg::PointerEvent& event) {
  if (popup_.has_value() && popup_->open) {
    if (popup_host_.handle_pointer(*popup_, event.window, event, dg::PopupFlags{})) {
      popup_.reset();
      popup_surface_.reset();
      return;
    }
  }
  if (!popup_.has_value() && event.window == host_window_ &&
      event.action == dg::PointerAction::kDown &&
      contains(kMenuButton, dg::PixelPoint{event.x, event.y})) {
    open_popup();
  }
}

void Runner::handle_key(const dg::KeyEvent& event) {
  if (popup_.has_value() && popup_->open) {
    if (popup_host_.handle_key(*popup_, event.window, event, dg::PopupFlags{})) {
      popup_.reset();
      popup_surface_.reset();
    }
  }
}

int Runner::run() {
  const Clock::time_point started = Clock::now();
  *out_ << "popup demo\n";
  legend(*out_, !settings_.force_overlay);

  if (!host_surface_.has_value()) {
    return 1;
  }
  host_tree_.repaint_full(host_surface_.value());
  present_painted(*manager_, host_window_, host_tree_, host_surface_.value());

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
    present_if_damaged(*manager_, host_window_, host_tree_, host_surface_.value());
    if (popup_.has_value() && popup_->open && popup_->is_native && popup_surface_.has_value()) {
      present_if_damaged(*manager_, popup_->window, *popup_->tree, *popup_surface_);
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
  spec.title = "drawgui popup";
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
  std::optional<dg::RasterSurface> surface =
      dg::RasterSurface::create(settings.size.width, settings.size.height);
  if (!surface.has_value()) {
    out << "could not allocate a surface\n";
    return 1;
  }
  dg::RenderTree tree{host_spec(settings.size)};
  dg::NodeStyle button_style;
  button_style.fill = dg::Color::from_argb(0xFF3A4152);
  tree.add_child(dg::RenderTree::root(), kMenuButton, button_style);
  tree.repaint_full(*surface);

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

}  // namespace popup_window
