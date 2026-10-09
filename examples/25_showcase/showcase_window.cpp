#include "showcase_window.h"

#include <sys/resource.h>
#include <chrono>
#include <cmath>
#include <cstdint>
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
#include "drawgui/shortcuts/clipboard_actions.h"
#include "drawgui/theme/token_ids.generated.h"
#include "drawgui/ui/dropdown.h"
#include "drawgui/ui/menu.h"
#include "drawgui/ui/tooltip_popup.h"
#include "drawgui/widget/focus.h"
#include "drawgui/widget/tooltip.h"
#include "drawgui/window/popup_host.h"
#include "drawgui/window/window_manager.h"

#include "profile_dialog.h"
#include "showcase_scene.h"

namespace showcase_window {
namespace {

using Clock = std::chrono::steady_clock;

const std::vector<std::string> kContextMenuItems = {"\u91cd\u547d\u540d", "\u5220\u9664"};
constexpr int kHoverPollMs = 16;

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
// rather than file-local (--verify-showcase pins that Runner actually
// calls this, not settings.size directly).
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
        scene_(showcase_scene::build(
            scene_options(settings, initial_viewport(manager, host_window, settings)))),
        popup_host_(manager),
        tooltip_(settings.tooltip_delay_ms) {
    tooltip_.bind(scene_.handles.help_button,
                  "\u9f9f\u5751\u63d0\u793a\uff1a\u53f3\u952e\u6587\u4ef6\u4fe1\u606f");
    const dg::PixelSize pixels = initial_viewport(manager, host_window, settings);
    host_surface_ = dg::RasterSurface::create(pixels.width, pixels.height);
  }

  int run();

 private:
  static showcase_scene::Options scene_options(const Settings& settings, dg::PixelSize pixels) {
    showcase_scene::Options options;
    options.spec.viewport = pixels;
    options.font_dir = settings.font_dir;
    return options;
  }

  void handle_pointer(const dg::PointerEvent& event);
  void handle_key(const dg::KeyEvent& event);
  void update_tooltip();
  void after_popup();
  void refresh_ring();
  void resize_host_if_needed();
  dg::NodeSideTables side_tables();
  dg::UiContext ui();

  void open_dialog();
  void close_dialog();
  void handle_pointer_dialog(const dg::PointerEvent& event);
  void handle_key_dialog(const dg::KeyEvent& event);

  int next_pump_timeout_ms(Clock::time_point started) const;
  void dispatch_text_events(const dg::PumpResult& pumped);
  void present_all_surfaces();
  void apply_needs_repaint(const dg::PumpResult& pumped);
  void run_script();

  Settings settings_;
  std::ostream* out_;
  dg::WindowManager* manager_;
  dg::WindowId host_window_;
  showcase_scene::Scene scene_;
  std::optional<dg::RasterSurface> host_surface_;

  // Empty: this window runs no animations and scopes no actions, but
  // PopupHost::destroy() needs every NodeId-keyed table by reference
  // (dg::NodeSideTables).
  dg::AnimationEngine animation_;
  dg::ActionScopes action_scopes_;

  dg::PopupHost popup_host_;
  // The library popup components (include/drawgui/ui/): each builds its
  // overlay through popup_host_ and destroy()s it on close.
  dg::Dropdown sort_dropdown_{scene_.handles.sort_dropdown};
  dg::Menu context_menu_;
  dg::Tooltip tooltip_;

  bool dialog_open_ = false;
  std::optional<profile_dialog::Handles> dialog_handles_;
  // The dialog panel's own ring: dialog_handles_->panel is appended
  // to the host tree AFTER scene_.ring's nodes already exist, so reusing
  // scene_.ring here would put the ring under the panel in paint order.
  dg::FocusRing dialog_ring_;
};

dg::NodeSideTables Runner::side_tables() {
  return {scene_.widgets, scene_.bindings, scene_.focus,
          animation_,     action_scopes_,  scene_.interaction};
}

dg::UiContext Runner::ui() {
  return dg::UiContext{popup_host_,
                       host_window_,
                       scene_.tree.render(),
                       side_tables(),
                       scene_.fonts.has_value() ? &*scene_.fonts : nullptr,
                       scene_.ui_font,
                       dg::popup_style_from_theme(scene_.theme, scene_.variant)};
}

void Runner::refresh_ring() {
  const dg::Color color = ring_color(scene_.theme);
  // A dropdown/context menu paints its own highlight row; the host ring is
  // collapsed while one is open. The dialog panel keeps its own ring - see
  // dialog_ring_'s declaration comment.
  if (sort_dropdown_.is_open() || context_menu_.is_open()) {
    dg::update_focus_ring(scene_.tree.render(), dg::LayoutTree::root(), scene_.ring,
                          std::nullopt, color);
    return;
  }
  if (dialog_open_ && dialog_handles_.has_value()) {
    dg::update_focus_ring(scene_.tree.render(), dialog_handles_->panel, dialog_ring_,
                          scene_.focus.current(), color);
    return;
  }
  dg::update_focus_ring(scene_.tree.render(), dg::LayoutTree::root(), scene_.ring,
                        scene_.focus.current(), color);
}

void Runner::after_popup() {
  refresh_ring();
}

void Runner::update_tooltip() {
  dg::UiContext ctx = ui();
  (void)tooltip_.tick(ctx, scene_.interaction.hovered(), dg::steady_anim_time());
}

void Runner::open_dialog() {
  if (dialog_open_) {
    return;
  }
  const dg::Expected<dg::PixelSize, dg::WindowError> size =
      manager_->drawable_size(host_window_);
  if (!size) {
    *out_ << "could not open the dialog: " << size.error().message << "\n";
    return;
  }
  dialog_handles_ = profile_dialog::build(
      scene_.tree.render(), scene_.widgets, dg::LayoutTree::root(), size.value(),
      dg::PixelSize{320, 150}, scene_.ui_font, scene_.theme, scene_.variant);
  // Reset before enter_scope/refresh_ring() below: a previous dialog_ring_
  // (if any) has its four strip nodes under a now-orphaned earlier panel
  // (dialog_handles_ is never cleared), and dialog_open_ must already be
  // true by the time refresh_ring() runs or it falls through to
  // scene_.ring/root instead of this branch's own ring.
  dialog_ring_ = dg::FocusRing{};
  dialog_open_ = true;
  scene_.focus.enter_scope(dialog_handles_->panel, /*modal=*/true);
  scene_.focus.set_guarded(scene_.tree.render(), dialog_handles_->name_field);
  refresh_ring();
}

void Runner::close_dialog() {
  if (!dialog_open_) {
    return;
  }
  scene_.focus.exit_scope(scene_.tree.render());
  refresh_ring();
  dialog_open_ = false;
}

void Runner::handle_pointer_dialog(const dg::PointerEvent& event) {
  if (!dialog_handles_.has_value() || event.window != host_window_ ||
      event.action != dg::PointerAction::kDown) {
    return;
  }
  const dg::PixelPoint at{event.x, event.y};
  const std::optional<dg::NodeId> hit = scene_.widgets.widget_at(scene_.tree.render(), at);
  if (hit.has_value() && *hit == dialog_handles_->close_button) {
    close_dialog();
    return;
  }
  scene_.focus.set_guarded(scene_.tree.render(), hit);
}

void Runner::handle_key_dialog(const dg::KeyEvent& event) {
  if (!dialog_handles_.has_value() || event.action != dg::KeyAction::kDown) {
    return;
  }
  if (event.key == dg::Key::kTab) {
    const dg::FocusChange change =
        dg::has(event.mods, dg::Modifier::kShift)
            ? scene_.focus.focus_previous(scene_.tree.render(), scene_.widgets,
                                          dialog_handles_->panel)
            : scene_.focus.focus_next(scene_.tree.render(), scene_.widgets,
                                      dialog_handles_->panel);
    if (change.any()) {
      refresh_ring();
    }
    return;
  }
  if (event.key == dg::Key::kEscape) {
    if (scene_.fonts.has_value() &&
        scene_.widgets.text_field_is_composing(dialog_handles_->name_field)) {
      scene_.widgets.text_field_cancel_composition(scene_.tree.render(), *scene_.fonts,
                                                   dialog_handles_->name_field);
      manager_->clear_composition(host_window_);
      return;
    }
    close_dialog();
    return;
  }
  if (!scene_.focus.is_focused(dialog_handles_->name_field) || !scene_.fonts.has_value()) {
    return;
  }
  const dg::FontCatalog& fonts = *scene_.fonts;
  dg::RenderTree& tree = scene_.tree.render();
  const dg::NodeId field = dialog_handles_->name_field;
  if (dg::apply_clipboard_key(
          event, field, dg::TextFieldEditContext{tree, fonts, scene_.widgets, *manager_})) {
    return;
  }
  switch (event.key) {
    case dg::Key::kBackspace:
      scene_.widgets.text_field_backspace(tree, fonts, field);
      break;
    case dg::Key::kDelete:
      scene_.widgets.text_field_delete_forward(tree, fonts, field);
      break;
    case dg::Key::kLeft:
      scene_.widgets.text_field_move(tree, fonts, field, dg::TextFieldMove::kCharLeft,
                                     dg::has(event.mods, dg::Modifier::kShift));
      break;
    case dg::Key::kRight:
      scene_.widgets.text_field_move(tree, fonts, field, dg::TextFieldMove::kCharRight,
                                     dg::has(event.mods, dg::Modifier::kShift));
      break;
    default:
      break;
  }
}

void Runner::handle_pointer(const dg::PointerEvent& event) {
  dg::UiContext ctx = ui();
  (void)tooltip_.handle_pointer(ctx, event);
  if (sort_dropdown_.is_open()) {
    const dg::DropdownResult result = sort_dropdown_.handle_pointer(ctx, event);
    if (result.event != dg::DropdownEvent::kHandled) {
      after_popup();
    }
    return;
  }
  if (context_menu_.is_open()) {
    const dg::MenuResult result = context_menu_.handle_pointer(ctx, event);
    if (result.event == dg::MenuEvent::kActivated) {
      *out_ << "context menu: \"" << kContextMenuItems[static_cast<std::size_t>(result.index)]
            << "\" selected\n";
    }
    if (result.closed()) {
      after_popup();
    }
    return;
  }
  if (dialog_open_) {
    handle_pointer_dialog(event);
    return;
  }

  bool wants_context_menu = false;
  bool wants_dropdown = false;
  showcase_scene::dispatch_pointer(scene_, *manager_, host_window_, event, &wants_context_menu,
                                   &wants_dropdown);
  if (wants_context_menu) {
    dg::MenuSpec spec;
    for (const std::string& item : kContextMenuItems) {
      spec.items.push_back(dg::menu_item(item));
    }
    spec.min_width = 140;
    (void)context_menu_.open(ctx, dg::PixelRect{event.x, event.y, 0, 0}, spec,
                             scene_.focus.current());
    after_popup();
  }
  if (wants_dropdown) {
    (void)sort_dropdown_.open(ctx);
    after_popup();
  }
  if (event.action == dg::PointerAction::kUp &&
      scene_.widgets.widget_at(scene_.tree.render(), dg::PixelPoint{event.x, event.y}) ==
          scene_.handles.profile_button) {
    open_dialog();
  }
  if (event.action == dg::PointerAction::kUp &&
      scene_.widgets.widget_at(scene_.tree.render(), dg::PixelPoint{event.x, event.y}) ==
          scene_.handles.theme_toggle) {
    (void)showcase_scene::switch_theme_variant(scene_);
  }
  update_tooltip();
}

void Runner::handle_key(const dg::KeyEvent& event) {
  dg::UiContext ctx = ui();
  if (sort_dropdown_.is_open()) {
    (void)sort_dropdown_.handle_key(ctx, event);
    after_popup();
    return;
  }
  if (context_menu_.is_open()) {
    const dg::MenuResult result = context_menu_.handle_key(ctx, event);
    if (result.event == dg::MenuEvent::kActivated) {
      *out_ << "context menu: \"" << kContextMenuItems[static_cast<std::size_t>(result.index)]
            << "\" selected\n";
    }
    after_popup();
    return;
  }
  if (dialog_open_) {
    handle_key_dialog(event);
    return;
  }
  if (sort_dropdown_.handle_key(ctx, event).event != dg::DropdownEvent::kIgnored) {
    after_popup();
    return;
  }
  showcase_scene::dispatch_key(scene_, *manager_, host_window_, event);
}

int Runner::next_pump_timeout_ms(Clock::time_point started) const {
  const bool hover_pending = tooltip_.next_deadline().has_value();
  if (hover_pending) {
    return kHoverPollMs;
  }
  if (settings_.run_ms > 0) {
    const double remaining = static_cast<double>(settings_.run_ms) - ms_since(started);
    return remaining > 0.0 ? static_cast<int>(std::lround(remaining)) + 10 : 10;
  }
  return -1;
}

// See examples/22_dropdown_menu/dropdown_window.cpp's own
// Runner::resize_if_needed() for the full argument; identical shape here
// since showcase_scene::Scene is layout-driven the same way (dg::LayoutTree).
void Runner::resize_host_if_needed() {
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

void Runner::apply_needs_repaint(const dg::PumpResult& pumped) {
  for (const dg::WindowId id : pumped.needs_repaint) {
    if (id == host_window_) {
      resize_host_if_needed();
    }
  }
}

void Runner::present_all_surfaces() {
  if (!host_surface_.has_value()) {
    return;
  }
  present_if_damaged(*manager_, host_window_, scene_.tree.render(), *host_surface_);
}

int Runner::run() {
  const Clock::time_point started = Clock::now();
  *out_ << "showcase demo\n"
        << "  Tab/Shift-Tab moves focus (including into the recent-files list); click the "
           "sort dropdown or Tab+Down/Enter to open it; right-click \"File Info\" for a "
           "context menu; hover \"Help\" for a tooltip; \"Open Profile\" opens a modal "
           "dialog with a CJK-capable name field; click \"Toggle Theme\" to switch "
           "light/dark. OVERLAY popup branch.\n";

  if (!host_surface_.has_value()) {
    return 1;
  }
  scene_.tree.render().repaint_full(*host_surface_);
  present_painted(*manager_, host_window_, scene_.tree.render(), *host_surface_);

  if (settings_.script) {
    run_script();
  }

  while (manager_->open_window_count() > 0) {
    if (settings_.run_ms > 0 && ms_since(started) >= settings_.run_ms) {
      manager_->request_close(host_window_);
    }
    const dg::PumpResult pumped = manager_->pump(next_pump_timeout_ms(started));
    apply_needs_repaint(pumped);
    for (const dg::PointerEvent& event : pumped.pointer) {
      handle_pointer(event);
    }
    for (const dg::KeyEvent& event : pumped.key) {
      handle_key(event);
    }
    dispatch_text_events(pumped);
    update_tooltip();
    if (!host_surface_.has_value()) {
      return 1;
    }
    present_all_surfaces();
  }
  return 0;
}

void Runner::dispatch_text_events(const dg::PumpResult& pumped) {
  if (!scene_.fonts.has_value()) {
    return;
  }
  for (const dg::TextInputEvent& event : pumped.text_input) {
    if (scene_.focus.is_focused(scene_.handles.search_field)) {
      scene_.widgets.text_field_insert(scene_.tree.render(), *scene_.fonts,
                                       scene_.handles.search_field, event.text);
    }
    if (dialog_open_ && dialog_handles_.has_value() &&
        scene_.focus.is_focused(dialog_handles_->name_field)) {
      scene_.widgets.text_field_insert(scene_.tree.render(), *scene_.fonts,
                                       dialog_handles_->name_field, event.text);
    }
  }
  for (const dg::TextEditingEvent& event : pumped.text_editing) {
    if (dialog_open_ && dialog_handles_.has_value() &&
        scene_.focus.is_focused(dialog_handles_->name_field)) {
      scene_.widgets.text_field_composition_update(scene_.tree.render(), *scene_.fonts,
                                                   dialog_handles_->name_field, event.text,
                                                   event.start, event.length);
    }
  }
}

void Runner::run_script() {
  *out_ << "--script: driving real platform events end to end\n";
  auto pump_drain = [&] {
    for (int i = 0; i < 3; ++i) {
      const dg::PumpResult pumped = manager_->pump(20);
      for (const dg::PointerEvent& event : pumped.pointer) {
        handle_pointer(event);
      }
      for (const dg::KeyEvent& event : pumped.key) {
        handle_key(event);
      }
      for (const dg::TextInputEvent& event : pumped.text_input) {
        if (scene_.fonts.has_value() && scene_.focus.is_focused(scene_.handles.search_field)) {
          scene_.widgets.text_field_insert(scene_.tree.render(), *scene_.fonts,
                                           scene_.handles.search_field, event.text);
        }
      }
      present_all_surfaces();
    }
  };

  // Tab into the window, then Tab a few times (crosses several WidgetKinds).
  manager_->post_key(host_window_, /*down=*/true, dg::Key::kTab, false);
  manager_->post_key(host_window_, /*down=*/false, dg::Key::kTab, false);
  pump_drain();
  for (int i = 0; i < 4; ++i) {
    manager_->post_key(host_window_, true, dg::Key::kTab, false);
    manager_->post_key(host_window_, false, dg::Key::kTab, false);
    pump_drain();
  }

  // Click the search field, type CJK, then a real committed-text event.
  const dg::PixelRect field = scene_.tree.render().absolute_bounds(scene_.handles.search_field);
  manager_->post_pointer_button(host_window_, true, field.left() + 5, field.top() + 5);
  manager_->post_pointer_button(host_window_, false, field.left() + 5, field.top() + 5);
  pump_drain();
  manager_->post_text_input(host_window_, "\u4e2d\u6587");
  pump_drain();

  // Right-click "File Info" for the context menu, then Escape.
  const dg::PixelRect info = scene_.tree.render().absolute_bounds(scene_.handles.info_button);
  manager_->post_pointer_button(host_window_, true, info.left() + 5, info.top() + 5,
                                dg::PointerButton::kSecondary);
  manager_->post_pointer_button(host_window_, false, info.left() + 5, info.top() + 5,
                                dg::PointerButton::kSecondary);
  pump_drain();
  manager_->post_key(host_window_, true, dg::Key::kEscape, false);
  manager_->post_key(host_window_, false, dg::Key::kEscape, false);
  pump_drain();

  // Open the profile dialog, drive a synthesized IME composition into the
  // name field, then Escape twice (cancel composition, then close).
  const dg::PixelRect profile =
      scene_.tree.render().absolute_bounds(scene_.handles.profile_button);
  manager_->post_pointer_button(host_window_, true, profile.left() + 5, profile.top() + 5);
  manager_->post_pointer_button(host_window_, false, profile.left() + 5, profile.top() + 5);
  pump_drain();
  manager_->post_text_editing(host_window_, "\u4f60\u597d", 0, 2);
  pump_drain();
  manager_->post_key(host_window_, true, dg::Key::kEscape, false);
  manager_->post_key(host_window_, false, dg::Key::kEscape, false);
  pump_drain();
  manager_->post_key(host_window_, true, dg::Key::kEscape, false);
  manager_->post_key(host_window_, false, dg::Key::kEscape, false);
  pump_drain();

  // Toggle the theme.
  const dg::PixelRect toggle =
      scene_.tree.render().absolute_bounds(scene_.handles.theme_toggle);
  manager_->post_pointer_button(host_window_, true, toggle.left() + 5, toggle.top() + 5);
  manager_->post_pointer_button(host_window_, false, toggle.left() + 5, toggle.top() + 5);
  pump_drain();

  *out_ << "--script: done\n";
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
  spec.title = "drawgui showcase";
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
  showcase_scene::Options options;
  options.spec.viewport = settings.size;
  options.spec.background.fill = dg::Color::from_argb(0xFF0E1218);
  options.font_dir = settings.font_dir;
  showcase_scene::Scene scene = showcase_scene::build(options);

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

int idle_probe(const Settings& settings, int idle_probe_ms, std::ostream& out) {
  dg::Expected<dg::WindowManager, dg::WindowError> made = dg::WindowManager::create();
  if (!made) {
    out << "error: " << made.error().message << "\n";
    return 1;
  }
  dg::WindowManager manager = std::move(made.value());

  dg::WindowSpec spec;
  spec.title = "drawgui showcase (idle probe)";
  spec.width = settings.size.width;
  spec.height = settings.size.height;
  spec.fill = dg::Color::from_argb(0xFF0E1218);
  const dg::Expected<dg::WindowId, dg::WindowError> window = manager.open(spec);
  if (!window) {
    out << "error: " << window.error().message << "\n";
    return 1;
  }

  const auto to_ms = [](const timeval& tv) {
    return (static_cast<double>(tv.tv_sec) * 1000.0) +
           (static_cast<double>(tv.tv_usec) / 1000.0);
  };

  (void)manager.pump(200);

  struct rusage before {};
  getrusage(RUSAGE_SELF, &before);
  const Clock::time_point wall_start = Clock::now();

  int wakeups = 0;
  double remaining_ms = static_cast<double>(idle_probe_ms);
  while (remaining_ms > 0.0) {
    (void)manager.pump(static_cast<int>(std::lround(remaining_ms)));
    ++wakeups;
    remaining_ms = static_cast<double>(idle_probe_ms) - ms_since(wall_start);
  }

  const double wall_ms = ms_since(wall_start);
  struct rusage after {};
  getrusage(RUSAGE_SELF, &after);
  const double cpu_ms = (to_ms(after.ru_utime) - to_ms(before.ru_utime)) +
                        (to_ms(after.ru_stime) - to_ms(before.ru_stime));

  out << "idle probe (dense showcase scene, nothing hovered/animating):\n"
      << "  requested block:  " << idle_probe_ms << " ms\n"
      << "  actual wall time: " << wall_ms << " ms across " << wakeups << " pump() call(s)\n"
      << "  process CPU time consumed (user+sys) while blocked: " << cpu_ms << " ms\n"
      << "  CPU utilisation over the block: "
      << (wall_ms > 0.0 ? (cpu_ms / wall_ms * 100.0) : 0.0) << "%\n";
  return 0;
}

}  // namespace showcase_window
