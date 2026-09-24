#include "dropdown_window.h"

#include <chrono>
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

#include "dropdown_options.h"
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

// The selected row's own label colour (dropdown_options::build()'s
// `selected_text_color`) - resolved from the theme the same way
// ring_color() resolves the focus ring's, with the same "just a colour,
// not a whole node" cost this slice's decision settled on.
dg::Color selected_row_color(const dg::Theme& theme) {
  return theme.color_value(DG_TOKEN_COLOR_PRIMARY, dg::ThemeVariant::kDark)
      .value_or(dg::Color::from_argb(0xFF6699FF));
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
  void open_dropdown();
  void close_dropdown(std::optional<int> select_index);
  void refresh_ring();
  void resize_if_needed();
  std::optional<int> row_index_of(dg::NodeId id) const;

  Settings settings_;
  std::ostream* out_;
  dg::WindowManager* manager_;
  dg::WindowId host_window_;
  dropdown_scene::Scene scene_;
  std::optional<dg::RasterSurface> host_surface_;

  dg::PopupHost popup_host_;
  std::optional<dg::PopupHandle> popup_;
  std::vector<dg::NodeId> rows_;
  // The popup's own focus ring, distinct from scene_.ring (the host
  // window's). scene_.ring's four strip nodes are already children of
  // LayoutTree::root() by the time a popup ever opens - RenderTree is
  // append-only, so they can never be reparented under
  // popup_->content_root. A fresh dg::FocusRing{} is created in
  // open_dropdown() instead, growing its own four nodes under the new
  // content_root the same way scene_.ring grew its own under the host
  // root - matching the native-popup-era `popup_native_ring_` this
  // replaces.
  dg::FocusRing popup_ring_;
};

std::optional<int> Runner::row_index_of(dg::NodeId id) const {
  for (std::size_t i = 0; i < rows_.size(); ++i) {
    if (rows_[i] == id) {
      return static_cast<int>(i);
    }
  }
  return std::nullopt;
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
  // The ring lives wherever the currently-focused node lives: the host
  // root while no popup is open, popup_->content_root while one is - see
  // popup_ring_'s own declaration comment for why this cannot be a single
  // shared FocusRing.
  if (popup_.has_value() && popup_->open) {
    dg::update_focus_ring(scene_.tree.render(), popup_->content_root, popup_ring_,
                          scene_.focus.current(), ring_color(scene_.theme));
    return;
  }
  dg::update_focus_ring(scene_.tree.render(), dg::LayoutTree::root(), scene_.ring,
                        scene_.focus.current(), ring_color(scene_.theme));
}

void Runner::open_dropdown() {
  const dg::PixelRect anchor = scene_.tree.render().absolute_bounds(scene_.handles.dropdown);
  const dg::PixelSize size = dropdown_options::size_for(
      static_cast<int>(dropdown_scene::kOptions.size()), anchor.width);
  const dg::Expected<dg::PopupHandle, dg::WindowError> shown =
      popup_host_.show(host_window_, scene_.tree.render(), anchor, size,
                       dg::PopupPlacement::kBelow, dg::PopupFlags{});
  if (!shown) {
    *out_ << "could not open the dropdown: " << shown.error().message << "\n";
    return;
  }
  popup_ = shown.value();
  // Reset rather than reuse: the previous popup_ring_ (if any) still has
  // its four strip nodes under a NOW-CLOSED, permanently-empty
  // content_root (PopupHost::close()'s own append-only cost). A fresh
  // dg::FocusRing{} grows four new nodes under the fresh content_root
  // this show() call just created.
  popup_ring_ = dg::FocusRing{};
  const std::optional<int> selected =
      scene_.widgets.dropdown_selected_index(scene_.handles.dropdown);

  rows_ =
      dropdown_options::build(scene_.tree.render(), scene_.widgets, popup_->content_root,
                              dropdown_scene::kOptions, size.width, scene_.ui_font, 15.0F,
                              selected, selected_row_color(scene_.theme));
  scene_.focus.enter_scope(popup_->content_root);

  const int start = selected.value_or(0);
  if (start >= 0 && static_cast<std::size_t>(start) < rows_.size()) {
    scene_.focus.set(rows_[static_cast<std::size_t>(start)]);
  }
  refresh_ring();
}

void Runner::close_dropdown(std::optional<int> select_index) {
  if (!popup_.has_value()) {
    return;
  }
  if (select_index.has_value() && scene_.fonts.has_value()) {
    scene_.widgets.dropdown_select(scene_.tree.render(), *scene_.fonts, scene_.handles.dropdown,
                                   *select_index);
  }
  scene_.focus.exit_scope(scene_.tree.render());
  popup_host_.close(*popup_);
  popup_.reset();
  rows_.clear();
  // Conventional dropdown behaviour: closing (by selection, Escape, or a
  // click outside) returns focus to the anchor that opened it, never
  // leaves the window with nothing focused.
  dropdown_scene::apply_focus_change(scene_, scene_.handles.dropdown);
}

void Runner::handle_pointer(const dg::PointerEvent& event) {
  if (popup_.has_value() && popup_->open) {
    dg::PopupHandle& handle = *popup_;
    if (popup_host_.handle_pointer(handle, event.window, event, dg::PopupFlags{})) {
      close_dropdown(std::nullopt);
      return;
    }
    if (event.action != dg::PointerAction::kDown) {
      return;
    }
    if (event.window != host_window_) {
      return;
    }
    const dg::PixelPoint at{event.x, event.y};
    const std::optional<dg::NodeId> hit = scene_.widgets.widget_at(scene_.tree.render(), at);
    if (!hit.has_value()) {
      return;
    }
    const std::optional<int> index = row_index_of(*hit);
    if (index.has_value()) {
      close_dropdown(index);
      return;
    }
    scene_.focus.set(hit);
    refresh_ring();
    return;
  }
  const bool open_requested = dropdown_scene::dispatch_pointer(scene_, event);
  if (open_requested) {
    open_dropdown();
  }
}

void Runner::handle_key(const dg::KeyEvent& event) {
  if (popup_.has_value() && popup_->open) {
    dg::PopupHandle& handle = *popup_;
    if (popup_host_.handle_key(handle, event.window, event, dg::PopupFlags{})) {
      close_dropdown(std::nullopt);
      return;
    }
    if (event.action != dg::KeyAction::kDown) {
      return;
    }
    if (event.key == dg::Key::kUp) {
      scene_.focus.focus_previous(scene_.tree.render(), scene_.widgets, handle.content_root);
      refresh_ring();
      return;
    }
    if (event.key == dg::Key::kDown) {
      scene_.focus.focus_next(scene_.tree.render(), scene_.widgets, handle.content_root);
      refresh_ring();
      return;
    }
    if (event.key == dg::Key::kEnter) {
      const std::optional<dg::NodeId> highlighted = scene_.focus.current();
      const std::optional<int> index =
          highlighted.has_value() ? row_index_of(*highlighted) : std::nullopt;
      close_dropdown(index);
      return;
    }
    return;
  }
  if (event.action != dg::KeyAction::kDown || event.window != host_window_) {
    dropdown_scene::dispatch_key(scene_, event);
    return;
  }
  if ((event.key == dg::Key::kDown || event.key == dg::Key::kEnter) &&
      scene_.focus.is_focused(scene_.handles.dropdown)) {
    open_dropdown();
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
