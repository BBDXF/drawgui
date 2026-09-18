#include "menu_window.h"

#include <sys/resource.h>
#include <chrono>
#include <cmath>
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

#include "dialog_panel.h"
#include "menu_rows.h"
#include "menu_scene.h"
#include "tooltip_content.h"

namespace menu_window {
namespace {

using Clock = std::chrono::steady_clock;

// The context menu's own three items - each its own word, matching
// examples/22_dropdown_menu's own "no substring/rotation of another"
// discipline against the "identical options prove nothing" failure mode.
const std::vector<std::string> kMenuItems = {"Copy", "Paste", "Delete"};

// 6-1's own frame-pacer interval (examples/17_animation's kFramePacerMs),
// reused verbatim rather than re-derived: a short poll while something is
// actively being timed (here, a hover toward a tooltip), never a busy loop.
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
        scene_(menu_scene::build(scene_options(settings))),
        popup_host_(manager) {
    host_surface_ = dg::RasterSurface::create(settings.size.width, settings.size.height);
  }

  int run();

 private:
  static menu_scene::Options scene_options(const Settings& settings) {
    menu_scene::Options options;
    options.spec.viewport = settings.size;
    options.font_dir = settings.font_dir;
    return options;
  }

  void handle_pointer(const dg::PointerEvent& event);
  void handle_pointer_menu(const dg::PointerEvent& event);
  void handle_pointer_dialog_overlay(const dg::PointerEvent& event);
  bool handle_pointer_dialog_native(const dg::PointerEvent& event);
  void handle_key(const dg::KeyEvent& event);
  void handle_key_menu(const dg::KeyEvent& event);
  void handle_key_dialog_overlay(const dg::KeyEvent& event);
  bool menu_target_secondary_click(const dg::PointerEvent& event);
  int next_pump_timeout_ms(Clock::time_point started) const;
  void process_close_requests(const dg::PumpResult& pumped);
  void present_all_surfaces();

  void open_context_menu(dg::PixelPoint at);
  void close_context_menu(std::optional<int> select_index);
  dg::WidgetSet& menu_active_widgets();
  dg::Focus& menu_active_focus();
  dg::RenderTree& menu_active_tree();
  void refresh_menu_ring();
  std::optional<int> menu_row_index_of(dg::NodeId id) const;

  void update_tooltip();
  void open_tooltip();
  void close_tooltip();

  void open_dialog();
  void close_dialog_overlay();
  void open_dialog_native();
  void close_dialog_native();

  Settings settings_;
  std::ostream* out_;
  dg::WindowManager* manager_;
  dg::WindowId host_window_;
  menu_scene::Scene scene_;
  std::optional<dg::RasterSurface> host_surface_;

  dg::PopupHost popup_host_;

  // --- Context menu ---
  std::optional<dg::PopupHandle> menu_popup_;
  std::vector<dg::NodeId> menu_rows_;
  std::optional<dg::WidgetSet> menu_native_widgets_;
  std::optional<dg::Focus> menu_native_focus_;
  dg::FocusRing menu_native_ring_;
  std::optional<dg::RasterSurface> menu_native_surface_;

  // --- Tooltip ---
  std::optional<dg::PopupHandle> tooltip_popup_;
  std::optional<dg::RasterSurface> tooltip_native_surface_;

  // --- Dialog, overlay branch: shares the host's own tree/Focus ---
  bool dialog_overlay_open_ = false;
  std::optional<dialog_panel::Handles> dialog_handles_;

  // --- Dialog, native branch: a genuine second OS window, its own
  // RenderTree/WidgetSet/Focus - built directly on WindowManager rather
  // than through PopupHost, because Dialog and Popup are peer window kinds
  // (design.md section 5.2's own table), not one built on the other.
  std::optional<dg::WindowId> dialog_window_;
  std::optional<dg::RenderTree> dialog_tree_;
  std::optional<dg::WidgetSet> dialog_widgets_;
  std::optional<dg::Focus> dialog_focus_;
  dg::FocusRing dialog_ring_;
  std::optional<dg::RasterSurface> dialog_surface_;
  dg::NodeId dialog_close_button_;
  dg::NodeId dialog_veto_button_;
  bool dialog_veto_ = false;
};

dg::WidgetSet& Runner::menu_active_widgets() {
  if (menu_popup_.has_value() && menu_popup_->is_native && menu_native_widgets_.has_value()) {
    return *menu_native_widgets_;
  }
  return scene_.widgets;
}

dg::Focus& Runner::menu_active_focus() {
  if (menu_popup_.has_value() && menu_popup_->is_native && menu_native_focus_.has_value()) {
    return *menu_native_focus_;
  }
  return scene_.focus;
}

dg::RenderTree& Runner::menu_active_tree() {
  return (menu_popup_.has_value() && menu_popup_->is_native) ? *menu_popup_->tree
                                                             : scene_.tree.render();
}

std::optional<int> Runner::menu_row_index_of(dg::NodeId id) const {
  for (std::size_t i = 0; i < menu_rows_.size(); ++i) {
    if (menu_rows_[i] == id) {
      return static_cast<int>(i);
    }
  }
  return std::nullopt;
}

void Runner::refresh_menu_ring() {
  if (menu_popup_.has_value() && menu_popup_->is_native) {
    if (menu_native_focus_.has_value()) {
      dg::update_focus_ring(*menu_popup_->tree, menu_popup_->content_root, menu_native_ring_,
                            menu_native_focus_->current(), ring_color(scene_.theme));
    }
    return;
  }
  dg::update_focus_ring(scene_.tree.render(), dg::LayoutTree::root(), scene_.ring,
                        scene_.focus.current(), ring_color(scene_.theme));
}

void Runner::open_context_menu(dg::PixelPoint at) {
  dg::PlatformCaps caps = dg::WindowManager::platform_caps();
  if (settings_.force_overlay) {
    caps.native_popup = false;
  }
  const dg::PixelRect anchor{at.x, at.y, 0, 0};
  const dg::PixelSize size = menu_rows::size_for(static_cast<int>(kMenuItems.size()), 140);
  const dg::Expected<dg::PopupHandle, dg::WindowError> shown =
      popup_host_.show(host_window_, scene_.tree.render(), caps, anchor, size,
                       dg::PopupPlacement::kBelow, dg::PopupFlags{});
  if (!shown) {
    *out_ << "could not open the context menu: " << shown.error().message << "\n";
    return;
  }
  menu_popup_ = shown.value();

  if (menu_popup_->is_native) {
    menu_native_widgets_.emplace();
    menu_native_focus_.emplace();
    menu_native_ring_ = dg::FocusRing{};
    menu_rows_ =
        menu_rows::build(*menu_popup_->tree, *menu_native_widgets_, menu_popup_->content_root,
                         kMenuItems, size.width, scene_.ui_font, 14.0F);
    menu_native_surface_ = dg::RasterSurface::create(size.width, size.height);
  } else {
    menu_rows_ =
        menu_rows::build(scene_.tree.render(), scene_.widgets, menu_popup_->content_root,
                         kMenuItems, size.width, scene_.ui_font, 14.0F);
    scene_.focus.enter_scope(menu_popup_->content_root);
  }
  menu_active_focus().set(menu_rows_.front());
  refresh_menu_ring();
}

void Runner::close_context_menu(std::optional<int> select_index) {
  if (!menu_popup_.has_value()) {
    return;
  }
  if (select_index.has_value()) {
    *out_ << "context menu: \"" << kMenuItems[static_cast<std::size_t>(*select_index)]
          << "\" selected\n";
  }
  if (menu_popup_->is_native) {
    menu_native_widgets_.reset();
    menu_native_focus_.reset();
    menu_native_ring_ = dg::FocusRing{};
    menu_native_surface_.reset();
  } else {
    scene_.focus.exit_scope(scene_.tree.render());
  }
  popup_host_.close(*menu_popup_);
  menu_popup_.reset();
  menu_rows_.clear();
}

void Runner::update_tooltip() {
  const std::optional<dg::NodeId> hovered = scene_.interaction.hovered();
  const dg::AnimTime now = dg::steady_anim_time();
  dg::update_hover_timer(scene_.hover_timer, hovered, now);

  const bool over_target = hovered == scene_.handles.hover_target;
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
  const dg::PixelRect anchor =
      scene_.tree.render().absolute_bounds(scene_.handles.hover_target);
  const std::string text = "This button opens a tooltip";
  const dg::PixelSize size = tooltip_content::size_for(text, 13);
  // dismiss_on_click_outside/dismiss_on_escape are both irrelevant to a
  // tooltip - it accepts no input on the native branch (SDL_WINDOW_TOOLTIP,
  // doc/popup.md section 1), and dismissal here is driven entirely by
  // update_tooltip()'s own hover-timer state, never by PopupHost::
  // handle_pointer()/handle_key() (menu_window.h's own header comment).
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
                           scene_.ui_font, 13.0F, size);
    tooltip_native_surface_ = dg::RasterSurface::create(size.width, size.height);
  } else {
    tooltip_content::build(scene_.tree.render(), tooltip_popup_->content_root, text,
                           scene_.ui_font, 13.0F, size);
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
  if (settings_.force_overlay) {
    if (dialog_overlay_open_) {
      return;
    }
    const dg::Expected<dg::PixelSize, dg::WindowError> size =
        manager_->drawable_size(host_window_);
    if (!size) {
      *out_ << "could not open the dialog: " << size.error().message << "\n";
      return;
    }
    dialog_handles_ =
        dialog_panel::build(scene_.tree.render(), scene_.widgets, dg::LayoutTree::root(),
                            size.value(), dg::PixelSize{280, 120}, scene_.ui_font);
    scene_.focus.enter_scope(dialog_handles_->panel, /*modal=*/true);
    scene_.focus.set_guarded(scene_.tree.render(), dialog_handles_->close_button);
    refresh_menu_ring();
    dialog_overlay_open_ = true;
    return;
  }
  open_dialog_native();
}

void Runner::close_dialog_overlay() {
  if (!dialog_overlay_open_) {
    return;
  }
  scene_.focus.exit_scope(scene_.tree.render());
  // The panel/backdrop are never removed - RenderTree is append-only
  // (doc/widgets.md); a real "Dialog" would clip them to empty the same
  // way PopupHost's own overlay branch closes (doc/popup.md section 3).
  // Left visible-but-inert here is out of this demo's scope (this branch's
  // whole point is the FOCUS trap, not a second clip-to-close mechanism
  // that would just re-derive PopupHost's own).
  dg::update_focus_ring(scene_.tree.render(), dg::LayoutTree::root(), scene_.ring,
                        scene_.focus.current(), ring_color(scene_.theme));
  dialog_overlay_open_ = false;
}

void Runner::open_dialog_native() {
  if (dialog_window_.has_value()) {
    return;
  }
  dg::WindowSpec spec;
  spec.title = "Dialog";
  spec.width = 300;
  spec.height = 140;
  spec.fill = dg::Color::from_argb(0xFF1C2129);
  spec.cancellable_close = true;
  const dg::Expected<dg::WindowId, dg::WindowError> opened =
      manager_->open_dialog(spec, host_window_);
  if (!opened) {
    *out_ << "could not open the native dialog: " << opened.error().message << "\n";
    return;
  }
  dialog_window_ = opened.value();
  dg::TreeSpec tree_spec;
  tree_spec.viewport = dg::PixelSize{300, 140};
  dialog_tree_.emplace(tree_spec);
  dialog_widgets_.emplace();
  dialog_focus_.emplace();
  dialog_ring_ = dg::FocusRing{};
  dialog_surface_ = dg::RasterSurface::create(300, 140);
  dialog_veto_ = false;

  auto add_button = [&](int x, int y, int w, int h, const std::string& label) {
    dg::NodeStyle style;
    style.fill = dg::Color::from_argb(0xFF2C3644);
    style.border_color = dg::Color::from_argb(0xFF3A4152);
    style.border_width = dg::BorderWidths::all(1.0F);
    style.radii = dg::Radii::all(6.0F);
    const dg::NodeId node =
        dialog_tree_->add_child(dg::RenderTree::root(), dg::PixelRect{x, y, w, h}, style);
    dg::Widget widget;
    widget.kind = dg::WidgetKind::kButton;
    widget.fill_normal = dg::Color::from_argb(0xFF2C3644);
    widget.fill_hover = dg::Color::from_argb(0xFF3B4A5E);
    widget.fill_pressed = dg::Color::from_argb(0xFF1F2731);
    dialog_widgets_->attach(node, widget);
    dg::NodeStyle label_style;
    label_style.text.text = label;
    label_style.text.font = scene_.ui_font;
    label_style.text.size = 13.0F;
    label_style.text.color = dg::Color::from_argb(0xFFE6E9F0);
    label_style.text.align = dg::TextAlign::kCenter;
    dialog_tree_->add_child(node, dg::PixelRect{0, 0, w, h}, label_style);
    return node;
  };
  dialog_close_button_ = add_button(20, 90, 110, 30, "Close");
  dialog_veto_button_ = add_button(160, 90, 120, 30, "Veto: OFF");
}

void Runner::close_dialog_native() {
  if (!dialog_window_.has_value()) {
    return;
  }
  manager_->close_now(*dialog_window_);
  dialog_window_.reset();
  dialog_tree_.reset();
  dialog_widgets_.reset();
  dialog_focus_.reset();
  dialog_ring_ = dg::FocusRing{};
  dialog_surface_.reset();
}

void Runner::handle_pointer(const dg::PointerEvent& event) {
  if (menu_popup_.has_value() && menu_popup_->open) {
    handle_pointer_menu(event);
    return;
  }
  if (dialog_overlay_open_) {
    handle_pointer_dialog_overlay(event);
    return;
  }
  if (handle_pointer_dialog_native(event)) {
    return;
  }

  if (menu_target_secondary_click(event)) {
    return;
  }
  menu_scene::dispatch_pointer(scene_, event);
  if (scene_.interaction.holding() == scene_.handles.open_dialog &&
      event.action == dg::PointerAction::kUp &&
      scene_.widgets.widget_at(scene_.tree.render(), dg::PixelPoint{event.x, event.y}) ==
          scene_.handles.open_dialog) {
    open_dialog();
  }
  update_tooltip();
}

void Runner::handle_pointer_menu(const dg::PointerEvent& event) {
  if (!menu_popup_.has_value()) {
    return;
  }
  dg::PopupHandle& handle = *menu_popup_;
  if (popup_host_.handle_pointer(handle, event.window, event, dg::PopupFlags{})) {
    close_context_menu(std::nullopt);
    return;
  }
  if (event.action != dg::PointerAction::kDown) {
    return;
  }
  if (handle.is_native && event.window != handle.window) {
    return;
  }
  if (!handle.is_native && event.window != host_window_) {
    return;
  }
  const dg::PixelPoint at{event.x, event.y};
  const std::optional<dg::NodeId> hit = menu_active_widgets().widget_at(menu_active_tree(), at);
  const std::optional<int> index = hit.has_value() ? menu_row_index_of(*hit) : std::nullopt;
  close_context_menu(index);
}

void Runner::handle_pointer_dialog_overlay(const dg::PointerEvent& event) {
  if (!dialog_handles_.has_value()) {
    return;
  }
  if (event.window != host_window_ || event.action != dg::PointerAction::kDown) {
    return;
  }
  const dg::PixelPoint at{event.x, event.y};
  const std::optional<dg::NodeId> hit = scene_.widgets.widget_at(scene_.tree.render(), at);
  if (hit.has_value() && *hit == dialog_handles_->close_button) {
    close_dialog_overlay();
    return;
  }
  // Anything else (the backdrop, the panel background, or - were it
  // somehow reachable - `before`/`after` behind it) is REFUSED: the
  // backdrop's own non-interactive climb already stops a click from
  // resolving to a host widget (dialog_panel.h's own argument), and
  // set_guarded() is what stops a DIRECT Focus target the same way -
  // exercised explicitly, not merely relied upon, immediately below.
  scene_.focus.set_guarded(scene_.tree.render(), hit);
}

bool Runner::handle_pointer_dialog_native(const dg::PointerEvent& event) {
  if (!dialog_window_.has_value() || !dialog_tree_.has_value() ||
      !dialog_widgets_.has_value()) {
    return false;
  }
  if (event.window != *dialog_window_) {
    // A click on the host window while the native dialog is open needs no
    // Focus-level refusal at all: it is a genuinely separate OS window
    // with its own separate dg::Focus (7-4's own precedent for a native
    // popup, doc/focus.md section 5) - there is no NodeId in the host's
    // own tree that this dialog's Focus could even name to refuse. Real
    // OS-level modal enforcement (SDL_SetWindowModal, WindowManager::
    // open_dialog()) is the platform's own answer where it is honoured;
    // under a driver that ignores it, the host simply keeps working
    // ordinarily, named here rather than silently assumed protected.
    return false;
  }
  if (event.action == dg::PointerAction::kDown) {
    const dg::PixelPoint at{event.x, event.y};
    const std::optional<dg::NodeId> hit = dialog_widgets_->widget_at(*dialog_tree_, at);
    if (hit.has_value() && *hit == dialog_close_button_) {
      manager_->request_close(*dialog_window_);
    } else if (hit.has_value() && *hit == dialog_veto_button_) {
      dialog_veto_ = !dialog_veto_;
      const dg::NodeId label = dialog_tree_->children(dialog_veto_button_).front();
      dg::TextStyle text;
      text.text = dialog_veto_ ? "Veto: ON" : "Veto: OFF";
      text.font = scene_.ui_font;
      text.size = 13.0F;
      text.color = dg::Color::from_argb(0xFFE6E9F0);
      text.align = dg::TextAlign::kCenter;
      dialog_tree_->set_text(label, text);
    }
  }
  return true;
}

bool Runner::menu_target_secondary_click(const dg::PointerEvent& event) {
  if (event.button != dg::PointerButton::kSecondary ||
      event.action != dg::PointerAction::kDown) {
    return false;
  }
  const dg::PixelPoint at{event.x, event.y};
  const std::optional<dg::NodeId> hit = scene_.widgets.widget_at(scene_.tree.render(), at);
  if (hit != scene_.handles.menu_target) {
    return false;
  }
  open_context_menu(at);
  return true;
}

void Runner::handle_key(const dg::KeyEvent& event) {
  if (menu_popup_.has_value() && menu_popup_->open) {
    handle_key_menu(event);
    return;
  }
  if (dialog_overlay_open_) {
    handle_key_dialog_overlay(event);
    return;
  }
  menu_scene::dispatch_key(scene_, event);
}

void Runner::handle_key_menu(const dg::KeyEvent& event) {
  if (!menu_popup_.has_value()) {
    return;
  }
  dg::PopupHandle& handle = *menu_popup_;
  if (popup_host_.handle_key(handle, event.window, event, dg::PopupFlags{})) {
    close_context_menu(std::nullopt);
    return;
  }
  if (event.action != dg::KeyAction::kDown) {
    return;
  }
  if (event.key == dg::Key::kUp) {
    menu_active_focus().focus_previous(menu_active_tree(), menu_active_widgets(),
                                       handle.content_root);
    refresh_menu_ring();
    return;
  }
  if (event.key == dg::Key::kDown) {
    menu_active_focus().focus_next(menu_active_tree(), menu_active_widgets(),
                                   handle.content_root);
    refresh_menu_ring();
    return;
  }
  if (event.key == dg::Key::kEnter) {
    const std::optional<dg::NodeId> highlighted = menu_active_focus().current();
    const std::optional<int> index =
        highlighted.has_value() ? menu_row_index_of(*highlighted) : std::nullopt;
    close_context_menu(index);
  }
}

void Runner::handle_key_dialog_overlay(const dg::KeyEvent& event) {
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
      refresh_menu_ring();
    }
    return;
  }
  if (event.key == dg::Key::kEscape) {
    close_dialog_overlay();
  }
}

int Runner::run() {
  const Clock::time_point started = Clock::now();
  *out_ << "menu/tooltip/dialog demo\n"
        << "  Right-click \"Right-click me\" for a context menu; hover \"Hover me\" for a "
           "tooltip; \"Open Dialog\" opens a modal dialog. "
        << (settings_.force_overlay ? "OVERLAY" : "NATIVE") << " branch.\n";

  if (!host_surface_.has_value()) {
    return 1;
  }
  scene_.tree.render().repaint_full(*host_surface_);
  present_painted(*manager_, host_window_, scene_.tree.render(), *host_surface_);

  while (manager_->open_window_count() > 0) {
    if (settings_.run_ms > 0 && ms_since(started) >= settings_.run_ms) {
      manager_->request_close(host_window_);
    }

    const dg::PumpResult pumped = manager_->pump(next_pump_timeout_ms(started));

    process_close_requests(pumped);
    for (const dg::PointerEvent& event : pumped.pointer) {
      handle_pointer(event);
    }
    for (const dg::KeyEvent& event : pumped.key) {
      handle_key(event);
    }
    // Always re-checked, not conditioned on the timeout path taken above:
    // a pump() that returned because of a real event (not the hover-poll
    // timeout) still needs this in case the delay elapsed in the meantime,
    // and update_hover_timer()/hover_ready() are cheap, idempotent reads -
    // there is no cost to paying for this every iteration.
    update_tooltip();

    if (!host_surface_.has_value()) {
      return 1;
    }
    present_all_surfaces();
  }
  return 0;
}

int Runner::next_pump_timeout_ms(Clock::time_point started) const {
  // design.md section 5.15.1's idle-vs-active shape, applied to a hover
  // delay for the first time (examples/17_animation's own kFramePacerMs
  // reused verbatim, section 6.2's own argument): -1 (block indefinitely)
  // whenever nothing needs to be timed, a short poll only while
  // scene_.hover_timer names a widget still waiting out its delay.
  // `--run-ms`'s own deadline needs an upper bound on the -1 case too -
  // unlike examples/17_animation, this demo does not always have
  // something animating to keep the loop naturally bounded, so the
  // deadline itself caps the wait exactly the way examples/
  // 01_sdl3_multi_window's own `--auto-close-ms` already polls at a fixed
  // interval rather than blocking indefinitely.
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

void Runner::process_close_requests(const dg::PumpResult& pumped) {
  for (const dg::WindowId id : pumped.close_requested) {
    if (dialog_window_.has_value() && id == *dialog_window_) {
      if (dialog_veto_) {
        *out_ << "dialog close request VETOED (on_close_request handler declined)\n";
      } else {
        close_dialog_native();
      }
    }
  }
}

void Runner::present_all_surfaces() {
  if (!host_surface_.has_value()) {
    return;
  }
  present_if_damaged(*manager_, host_window_, scene_.tree.render(), *host_surface_);
  if (menu_popup_.has_value() && menu_popup_->open && menu_popup_->is_native &&
      menu_native_surface_.has_value()) {
    present_if_damaged(*manager_, menu_popup_->window, *menu_popup_->tree,
                       *menu_native_surface_);
  }
  if (tooltip_popup_.has_value() && tooltip_popup_->open && tooltip_popup_->is_native &&
      tooltip_native_surface_.has_value()) {
    present_if_damaged(*manager_, tooltip_popup_->window, *tooltip_popup_->tree,
                       *tooltip_native_surface_);
  }
  if (dialog_window_.has_value() && dialog_tree_.has_value() && dialog_surface_.has_value()) {
    present_if_damaged(*manager_, *dialog_window_, *dialog_tree_, *dialog_surface_);
  }
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
  spec.title = "drawgui menu/tooltip/dialog";
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
  menu_scene::Options options;
  options.spec.viewport = settings.size;
  options.spec.background.fill = dg::Color::from_argb(0xFF0E1218);
  options.font_dir = settings.font_dir;
  menu_scene::Scene scene = menu_scene::build(options);
  menu_scene::apply_focus_change(scene, scene.handles.menu_target);

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
  spec.title = "drawgui menu/tooltip/dialog (idle probe)";
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

  // Drain the window's own creation/expose events first, then block with
  // NOTHING hovered - the -1 (indefinite) timeout path this file's own
  // hover-pending branch takes when scene_.hover_timer.target is nullopt -
  // matching examples/17_animation's own idle_probe() shape exactly (the
  // task's own instruction is to reuse 6-1's clock/frame-loop technique,
  // not re-derive a second measurement method).
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

  out << "idle probe with a hover timer present but NOTHING hovered (design.md section "
         "5.15.1's \"wait_events() blocks, CPU 0%\", re-checked under 7-5b's own new state):\n"
      << "  requested block:  " << idle_probe_ms << " ms\n"
      << "  actual wall time: " << wall_ms << " ms across " << wakeups << " pump() call(s)\n"
      << "  process CPU time consumed (user+sys) while blocked: " << cpu_ms << " ms\n"
      << "  CPU utilisation over the block: "
      << (wall_ms > 0.0 ? (cpu_ms / wall_ms * 100.0) : 0.0) << "%\n";
  return 0;
}

}  // namespace menu_window
