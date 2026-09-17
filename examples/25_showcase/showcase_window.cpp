#include "showcase_window.h"

#include <sys/resource.h>
#include <chrono>
#include <cmath>
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
#include "drawgui/widget/tooltip.h"
#include "drawgui/window/popup_host.h"
#include "drawgui/window/window_manager.h"

#include "context_menu.h"
#include "profile_dialog.h"
#include "showcase_scene.h"
#include "sort_options.h"
#include "tooltip_content.h"

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
  (void)manager.present(window, image, tree.painted().rects());
}

enum class PopupKind : std::uint8_t { kNone, kDropdown, kContextMenu };

class Runner {
 public:
  Runner(dg::WindowManager& manager, dg::WindowId host_window, const Settings& settings,
         std::ostream& out)
      : settings_(settings),
        out_(&out),
        manager_(&manager),
        host_window_(host_window),
        scene_(showcase_scene::build(scene_options(settings))),
        popup_host_(manager) {
    host_surface_ = dg::RasterSurface::create(settings.size.width, settings.size.height);
  }

  int run();

 private:
  static showcase_scene::Options scene_options(const Settings& settings) {
    showcase_scene::Options options;
    options.spec.viewport = settings.size;
    options.font_dir = settings.font_dir;
    return options;
  }

  void handle_pointer(const dg::PointerEvent& event);
  void handle_key(const dg::KeyEvent& event);
  void update_tooltip();
  void open_tooltip();
  void close_tooltip();

  void open_popup(PopupKind kind, const dg::PixelRect& anchor, dg::PixelSize size);
  void close_popup(std::optional<int> select_index);
  dg::WidgetSet& active_widgets();
  dg::Focus& active_focus();
  dg::RenderTree& active_tree();
  void refresh_ring();
  std::optional<int> row_index_of(dg::NodeId id) const;

  void open_dialog();
  void close_dialog();
  void handle_pointer_dialog(const dg::PointerEvent& event);
  void handle_key_dialog(const dg::KeyEvent& event);

  int next_pump_timeout_ms(Clock::time_point started) const;
  void dispatch_text_events(const dg::PumpResult& pumped);
  void present_all_surfaces();
  void run_script();

  Settings settings_;
  std::ostream* out_;
  dg::WindowManager* manager_;
  dg::WindowId host_window_;
  showcase_scene::Scene scene_;
  std::optional<dg::RasterSurface> host_surface_;

  dg::PopupHost popup_host_;
  std::optional<dg::PopupHandle> popup_;
  PopupKind popup_kind_ = PopupKind::kNone;
  std::vector<dg::NodeId> popup_rows_;
  std::optional<dg::WidgetSet> popup_native_widgets_;
  std::optional<dg::Focus> popup_native_focus_;
  dg::FocusRing popup_native_ring_;
  std::optional<dg::RasterSurface> popup_native_surface_;

  std::optional<dg::PopupHandle> tooltip_popup_;
  std::optional<dg::RasterSurface> tooltip_native_surface_;

  bool dialog_open_ = false;
  std::optional<profile_dialog::Handles> dialog_handles_;
};

dg::WidgetSet& Runner::active_widgets() {
  if (popup_.has_value() && popup_->is_native && popup_native_widgets_.has_value()) {
    return *popup_native_widgets_;
  }
  return scene_.widgets;
}

dg::Focus& Runner::active_focus() {
  if (popup_.has_value() && popup_->is_native && popup_native_focus_.has_value()) {
    return *popup_native_focus_;
  }
  return scene_.focus;
}

dg::RenderTree& Runner::active_tree() {
  return (popup_.has_value() && popup_->is_native) ? *popup_->tree : scene_.tree.render();
}

std::optional<int> Runner::row_index_of(dg::NodeId id) const {
  for (std::size_t i = 0; i < popup_rows_.size(); ++i) {
    if (popup_rows_[i] == id) {
      return static_cast<int>(i);
    }
  }
  return std::nullopt;
}

void Runner::refresh_ring() {
  if (popup_.has_value() && popup_->is_native) {
    if (popup_native_focus_.has_value()) {
      dg::update_focus_ring(*popup_->tree, popup_->content_root, popup_native_ring_,
                            popup_native_focus_->current(), ring_color(scene_.theme));
    }
    return;
  }
  dg::update_focus_ring(scene_.tree.render(), dg::LayoutTree::root(), scene_.ring,
                        scene_.focus.current(), ring_color(scene_.theme));
}

void Runner::open_popup(PopupKind kind, const dg::PixelRect& anchor, dg::PixelSize size) {
  dg::PlatformCaps caps = dg::WindowManager::platform_caps();
  if (settings_.force_overlay) {
    caps.native_popup = false;
  }
  const dg::Expected<dg::PopupHandle, dg::WindowError> shown =
      popup_host_.show(host_window_, scene_.tree.render(), caps, anchor, size,
                       dg::PopupPlacement::kBelow, dg::PopupFlags{});
  if (!shown) {
    *out_ << "could not open the popup: " << shown.error().message << "\n";
    return;
  }
  popup_ = shown.value();
  popup_kind_ = kind;
  const std::vector<std::string>& items =
      kind == PopupKind::kDropdown ? showcase_scene::kSortOptions : kContextMenuItems;

  if (popup_->is_native) {
    popup_native_widgets_.emplace();
    popup_native_focus_.emplace();
    popup_native_ring_ = dg::FocusRing{};
    popup_rows_ = kind == PopupKind::kDropdown
                      ? sort_options::build(*popup_->tree, *popup_native_widgets_,
                                            popup_->content_root, items, size.width,
                                            scene_.ui_font, scene_.theme, scene_.variant)
                      : context_menu::build(*popup_->tree, *popup_native_widgets_,
                                            popup_->content_root, items, size.width,
                                            scene_.ui_font, scene_.theme, scene_.variant);
    popup_native_surface_ = dg::RasterSurface::create(size.width, size.height);
  } else {
    popup_rows_ = kind == PopupKind::kDropdown
                      ? sort_options::build(scene_.tree.render(), scene_.widgets,
                                            popup_->content_root, items, size.width,
                                            scene_.ui_font, scene_.theme, scene_.variant)
                      : context_menu::build(scene_.tree.render(), scene_.widgets,
                                            popup_->content_root, items, size.width,
                                            scene_.ui_font, scene_.theme, scene_.variant);
    scene_.focus.enter_scope(popup_->content_root);
  }
  active_focus().set(popup_rows_.front());
  refresh_ring();
}

void Runner::close_popup(std::optional<int> select_index) {
  if (!popup_.has_value()) {
    return;
  }
  if (select_index.has_value() && popup_kind_ == PopupKind::kDropdown &&
      scene_.fonts.has_value()) {
    scene_.widgets.dropdown_select(scene_.tree.render(), *scene_.fonts,
                                   scene_.handles.sort_dropdown, *select_index);
  } else if (select_index.has_value() && popup_kind_ == PopupKind::kContextMenu) {
    *out_ << "context menu: \"" << kContextMenuItems[static_cast<std::size_t>(*select_index)]
          << "\" selected\n";
  }
  if (popup_->is_native) {
    popup_native_widgets_.reset();
    popup_native_focus_.reset();
    popup_native_ring_ = dg::FocusRing{};
    popup_native_surface_.reset();
  } else {
    scene_.focus.exit_scope(scene_.tree.render());
  }
  popup_host_.close(*popup_);
  popup_.reset();
  popup_rows_.clear();
  const PopupKind kind = popup_kind_;
  popup_kind_ = PopupKind::kNone;
  if (kind == PopupKind::kDropdown) {
    showcase_scene::apply_focus_change(scene_, *manager_, host_window_,
                                       scene_.handles.sort_dropdown);
  }
}

void Runner::update_tooltip() {
  const std::optional<dg::NodeId> hovered = scene_.interaction.hovered();
  const dg::AnimTime now = dg::steady_anim_time();
  dg::update_hover_timer(scene_.hover_timer, hovered, now);

  const bool over_target = hovered == scene_.handles.help_button;
  if (tooltip_popup_.has_value() && !over_target) {
    close_tooltip();
    return;
  }
  if (!tooltip_popup_.has_value() && over_target &&
      dg::hover_ready(scene_.hover_timer, now, settings_.tooltip_delay_ms)) {
    open_tooltip();
  }
}

void Runner::open_tooltip() {
  dg::PlatformCaps caps = dg::WindowManager::platform_caps();
  if (settings_.force_overlay) {
    caps.native_popup = false;
  }
  const dg::PixelRect anchor = scene_.tree.render().absolute_bounds(scene_.handles.help_button);
  const std::string text = "\u9f9f\u5751\u63d0\u793a\uff1a\u53f3\u952e\u6587\u4ef6\u4fe1\u606f";
  const dg::PixelSize size = tooltip_content::size_for(text, 13);
  const dg::Expected<dg::PopupHandle, dg::WindowError> shown = popup_host_.show(
      host_window_, scene_.tree.render(), caps, anchor, size, dg::PopupPlacement::kBelow,
      dg::PopupFlags{}, dg::PopupWindowKind::kTooltip);
  if (!shown) {
    *out_ << "could not open the tooltip: " << shown.error().message << "\n";
    return;
  }
  tooltip_popup_ = shown.value();
  if (tooltip_popup_->is_native) {
    tooltip_content::build(*tooltip_popup_->tree, tooltip_popup_->content_root, text,
                           scene_.ui_font, 13.0F, size, scene_.theme, scene_.variant);
    tooltip_native_surface_ = dg::RasterSurface::create(size.width, size.height);
  } else {
    tooltip_content::build(scene_.tree.render(), tooltip_popup_->content_root, text,
                           scene_.ui_font, 13.0F, size, scene_.theme, scene_.variant);
  }
}

void Runner::close_tooltip() {
  if (!tooltip_popup_.has_value()) {
    return;
  }
  popup_host_.close(*tooltip_popup_);
  tooltip_popup_.reset();
  tooltip_native_surface_.reset();
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
  scene_.focus.enter_scope(dialog_handles_->panel, /*modal=*/true);
  scene_.focus.set_guarded(scene_.tree.render(), dialog_handles_->name_field);
  refresh_ring();
  dialog_open_ = true;
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
        event.shift ? scene_.focus.focus_previous(scene_.tree.render(), scene_.widgets,
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
  switch (event.key) {
    case dg::Key::kBackspace:
      scene_.widgets.text_field_backspace(tree, fonts, field);
      break;
    case dg::Key::kDelete:
      scene_.widgets.text_field_delete_forward(tree, fonts, field);
      break;
    case dg::Key::kLeft:
      scene_.widgets.text_field_move(tree, fonts, field, dg::TextFieldMove::kCharLeft,
                                     event.shift);
      break;
    case dg::Key::kRight:
      scene_.widgets.text_field_move(tree, fonts, field, dg::TextFieldMove::kCharRight,
                                     event.shift);
      break;
    default:
      break;
  }
}

void Runner::handle_pointer(const dg::PointerEvent& event) {
  if (popup_.has_value() && popup_->open) {
    if (popup_host_.handle_pointer(*popup_, event.window, event, dg::PopupFlags{})) {
      close_popup(std::nullopt);
      return;
    }
    if (event.action != dg::PointerAction::kDown) {
      return;
    }
    if (popup_->is_native && event.window != popup_->window) {
      return;
    }
    if (!popup_->is_native && event.window != host_window_) {
      return;
    }
    const dg::PixelPoint at{event.x, event.y};
    const std::optional<dg::NodeId> hit = active_widgets().widget_at(active_tree(), at);
    const std::optional<int> index = hit.has_value() ? row_index_of(*hit) : std::nullopt;
    if (index.has_value()) {
      close_popup(index);
      return;
    }
    active_focus().set(hit);
    refresh_ring();
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
    const dg::PixelPoint at{event.x, event.y};
    open_popup(PopupKind::kContextMenu, dg::PixelRect{at.x, at.y, 0, 0},
               context_menu::size_for(static_cast<int>(kContextMenuItems.size()), 140));
  }
  if (wants_dropdown) {
    const dg::PixelRect anchor =
        scene_.tree.render().absolute_bounds(scene_.handles.sort_dropdown);
    open_popup(PopupKind::kDropdown, anchor,
               sort_options::size_for(static_cast<int>(showcase_scene::kSortOptions.size()),
                                      anchor.width));
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
  if (popup_.has_value() && popup_->open) {
    dg::PopupHandle& handle = *popup_;
    if (popup_host_.handle_key(handle, event.window, event, dg::PopupFlags{})) {
      close_popup(std::nullopt);
      return;
    }
    if (event.action != dg::KeyAction::kDown) {
      return;
    }
    if (event.key == dg::Key::kUp) {
      active_focus().focus_previous(active_tree(), active_widgets(), handle.content_root);
      refresh_ring();
      return;
    }
    if (event.key == dg::Key::kDown) {
      active_focus().focus_next(active_tree(), active_widgets(), handle.content_root);
      refresh_ring();
      return;
    }
    if (event.key == dg::Key::kEnter) {
      const std::optional<dg::NodeId> highlighted = active_focus().current();
      close_popup(highlighted.has_value() ? row_index_of(*highlighted) : std::nullopt);
      return;
    }
    return;
  }
  if (dialog_open_) {
    handle_key_dialog(event);
    return;
  }
  if (event.action == dg::KeyAction::kDown &&
      (event.key == dg::Key::kDown || event.key == dg::Key::kEnter) &&
      scene_.focus.is_focused(scene_.handles.sort_dropdown)) {
    const dg::PixelRect anchor =
        scene_.tree.render().absolute_bounds(scene_.handles.sort_dropdown);
    open_popup(PopupKind::kDropdown, anchor,
               sort_options::size_for(static_cast<int>(showcase_scene::kSortOptions.size()),
                                      anchor.width));
    return;
  }
  showcase_scene::dispatch_key(scene_, *manager_, host_window_, event);
}

int Runner::next_pump_timeout_ms(Clock::time_point started) const {
  const bool hover_pending =
      scene_.hover_timer.target.has_value() && !tooltip_popup_.has_value();
  if (hover_pending) {
    return kHoverPollMs;
  }
  if (settings_.run_ms > 0) {
    const double remaining = static_cast<double>(settings_.run_ms) - ms_since(started);
    return remaining > 0.0 ? static_cast<int>(std::lround(remaining)) + 10 : 10;
  }
  return -1;
}

void Runner::present_all_surfaces() {
  if (!host_surface_.has_value()) {
    return;
  }
  present_if_damaged(*manager_, host_window_, scene_.tree.render(), *host_surface_);
  if (popup_.has_value() && popup_->open && popup_->is_native &&
      popup_native_surface_.has_value()) {
    present_if_damaged(*manager_, popup_->window, *popup_->tree, *popup_native_surface_);
  }
  if (tooltip_popup_.has_value() && tooltip_popup_->open && tooltip_popup_->is_native &&
      tooltip_native_surface_.has_value()) {
    present_if_damaged(*manager_, tooltip_popup_->window, *tooltip_popup_->tree,
                       *tooltip_native_surface_);
  }
}

int Runner::run() {
  const Clock::time_point started = Clock::now();
  *out_ << "showcase demo\n"
        << "  Tab/Shift-Tab moves focus (including into the recent-files list); click the "
           "sort dropdown or Tab+Down/Enter to open it; right-click \"File Info\" for a "
           "context menu; hover \"Help\" for a tooltip; \"Open Profile\" opens a modal "
           "dialog with a CJK-capable name field; click \"Toggle Theme\" to switch "
           "light/dark. "
        << (settings_.force_overlay ? "OVERLAY" : "NATIVE") << " popup branch.\n";

  if (!host_surface_.has_value()) {
    return 1;
  }
  scene_.tree.render().repaint_full(*host_surface_);
  present_if_damaged(*manager_, host_window_, scene_.tree.render(), *host_surface_);

  if (settings_.script) {
    run_script();
  }

  while (manager_->open_window_count() > 0) {
    if (settings_.run_ms > 0 && ms_since(started) >= settings_.run_ms) {
      manager_->request_close(host_window_);
    }
    const dg::PumpResult pumped = manager_->pump(next_pump_timeout_ms(started));
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
