// The per-window popups (menus, dialogs, tooltips) and the event routing in
// front of the core's own handling: an open dialog, then an open menu, then
// dropdowns, then the controls see every event first.

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "app/components/components.h"
#include "app/internal.h"
#include "drawgui/anim/clock.h"
#include "ui/ui_visuals.h"

namespace dg::app_detail {
namespace {

constexpr int kProgressWidth = 200;
constexpr int kProgressHeight = 8;
constexpr int kTabsWidth = 320;
constexpr int kTabsHeight = 240;
constexpr int kTreeWidth = 240;
constexpr int kTreeHeight = 240;
constexpr int kTableWidth = 360;
constexpr int kTableHeight = 240;
constexpr int kAnimationFrameMs = 16;
constexpr int kMaxTooltipDelayMs = 60000;
constexpr int kContentGap = 8;  // dialog body text -> form content
constexpr int kParkingExtent = 1 << 14;

// -- Typed results to events ---------------------------------------------------

// `by_key`: the menu closed on a key (Escape keeps a menu bar in keyboard
// mode).
void menu_result(WinState& window, const dg::MenuResult& result, bool by_key) {
  if (result.event != dg::MenuEvent::kActivated && result.event != dg::MenuEvent::kDismissed) {
    return;
  }
  dg::events::MenuEvent event{window.sdl_window, window.menu_anchor};
  event.menu_id = window.menu_id;
  if (window.menu_bar.has_value()) {
    event.bar_menu = window.bar_menu;
  }
  if (result.event == dg::MenuEvent::kActivated) {
    event.item = result.index;
    if (result.index >= 0 && static_cast<std::size_t>(result.index) < window.menu_commands.size()) {
      event.command = window.menu_commands[static_cast<std::size_t>(result.index)];
    }
  }
  emit(window.app, event);
  if (window.menu_bar.has_value()) {
    components::menubar_menu_closed(window, *window.menu_bar,
                                    by_key && result.event == dg::MenuEvent::kDismissed);
  } else {
    components::clear_menu_owner(window);
  }
}

void release_dialog_content(WinState& window);

void dialog_result(WinState& window, const dg::DialogResult& result) {
  if (result.event == dg::DialogEvent::kClosed) {
    release_dialog_content(window);
    emit(window.app, dg::events::DialogResultEvent{
                         window.sdl_window, result.button.value_or(-1), window.dialog_id});
    window.dialog_id = 0;
  }
}

void dropdown_result(WinState& window, const NodeRecord& record,
                     const dg::DropdownResult& result) {
  if (result.event == dg::DropdownEvent::kChanged) {
    emit(window.app, dg::events::ValueChangedEvent{window.sdl_window, record.id,
                                                   result.selected.value_or(-1),
                                                   result.previous.value_or(-1)});
  }
}

std::optional<dg::Dropdown>* dropdown_in(NodeRecord& record) {
  auto* state = std::get_if<DropdownState>(&record.state);
  return state != nullptr ? &state->dropdown : nullptr;
}

bool focus_in_dialog_content(const WinState& window) {
  const std::optional<dg::NodeId> focused = window.focus.current();
  return window.dialog_content.has_value() && window.dialog_host.has_value() &&
         focused.has_value() &&
         dg::is_within(window.tree.render(), *window.dialog_host, *focused);
}

bool pointer_in_dialog_content(const WinState& window, const dg::PointerEvent& event) {
  if (!window.dialog_content.has_value() || !window.dialog_host.has_value() ||
      event.window != window.sdl_window) {
    return false;
  }
  if (event.action == dg::PointerAction::kUp || event.action == dg::PointerAction::kMove) {
    // A press that began in the content completes there.
    return true;
  }
  return dg::contains(window.tree.render().absolute_bounds(*window.dialog_host),
                      dg::PixelPoint{event.x, event.y});
}

// Whether a key goes to the focused form content rather than the dialog:
// everything but Tab, Escape and Enter - Enter too inside a text area.
bool key_for_dialog_content(const WinState& window, const dg::KeyEvent& event) {
  const std::optional<dg::NodeId> focused = window.focus.current();
  if (!focused.has_value() || !focus_in_dialog_content(window)) {
    return false;
  }
  switch (event.key) {
    case dg::Key::kTab:
    case dg::Key::kEscape:
      return false;
    case dg::Key::kEnter:
      return is_text_area(window, *focused);
    default:
      return true;
  }
}

// The hidden, disabled, zero-size box parked dialog content lives in - and
// the layout parent of an open dialog's content host (an absolute box, so
// the host's layout offset is its offset inside the dialog body).
dg::NodeId parking_of(WinState& window) {
  if (window.parking.has_value() && window.tree.render().is_valid(*window.parking)) {
    return *window.parking;
  }
  dg::BoxStyle box;
  box.kind = dg::LayoutKind::kAbsolute;
  // Room for any form, offscreen below the window root; the negative
  // margin keeps it from overrunning the root column.
  box.width = kParkingExtent;
  box.height = kParkingExtent;
  box.margin.bottom = -kParkingExtent;
  dg::NodeStyle style;
  style.overflow = dg::Overflow::kClip;
  style.disabled = true;
  const dg::NodeId parking = window.tree.add_child(dg::LayoutTree::root(), box, style);
  window.parking = parking;
  return parking;
}

// After the dialog closed: its content goes to the parking box (alive,
// parentless for the app), the host box is removed.
void release_dialog_content(WinState& window) {
  const std::optional<dg::NodeId> content = window.dialog_content;
  window.dialog_content.reset();
  if (content.has_value() && record_of(window, *content) != nullptr) {
    (void)window.tree.move_child(*content, parking_of(window), std::nullopt);
  }
  if (window.dialog_host.has_value() && window.tree.render().is_valid(*window.dialog_host)) {
    (void)dg::remove_subtree(window.tree, *window.dialog_host, components::tables_of(window));
  }
  window.dialog_host.reset();
  window.relaid = true;
}

}  // namespace

namespace components {

dg::MenuSpec menu_spec_of(const std::vector<dg::MenuEntry>& entries) {
  dg::MenuSpec spec;
  spec.items.reserve(entries.size());
  for (const dg::MenuEntry& entry : entries) {
    dg::MenuItem item;
    item.text = entry.text;
    item.separator = entry.separator;
    item.disabled = entry.disabled;
    item.checked = entry.checked;
    item.shortcut = entry.shortcut_label;
    spec.items.push_back(std::move(item));
  }
  return spec;
}

std::vector<std::int32_t> commands_of(const std::vector<dg::MenuEntry>& entries) {
  std::vector<std::int32_t> commands;
  commands.reserve(entries.size());
  for (const dg::MenuEntry& entry : entries) {
    commands.push_back(entry.command);
  }
  return commands;
}

void clear_menu_owner(WinState& window) {
  window.menu_id = 0;
  window.menu_anchor.reset();
  window.menu_commands.clear();
  window.menu_bar.reset();
  window.bar_menu = -1;
}

void dismiss_open_menu(WinState& window) {
  if (!window.menu.is_open()) {
    return;
  }
  dg::UiContext ctx = ui(window);
  window.menu.close(ctx);
  menu_result(window, dg::MenuResult{dg::MenuEvent::kDismissed, -1}, false);
}

void control_defaults(NodeKind kind, dg::BoxStyle& box) {
  box.kind = dg::LayoutKind::kColumn;
  box.cross_align = dg::CrossAlign::kStretch;
  switch (kind) {
    case NodeKind::kProgress:
      box.width = kProgressWidth;
      box.height = kProgressHeight;
      break;
    case NodeKind::kTabs:
      box.width = kTabsWidth;
      box.height = kTabsHeight;
      break;
    case NodeKind::kTree:
      box.width = kTreeWidth;
      box.height = kTreeHeight;
      break;
    default:
      box.width = kTableWidth;
      box.height = kTableHeight;
      break;
  }
}

void init_window(WinState& window) {
  window.popups.emplace(window.app.wm);
  window.tooltip = dg::Tooltip(window.app.tooltip_delay_ms);
}

bool any_dropdown_open(WinState& window) {
  return std::ranges::any_of(records_in_order(window), [](NodeRecord* record) {
    const std::optional<dg::Dropdown>* dropdown = dropdown_in(*record);
    return dropdown != nullptr && dropdown->has_value() && (*dropdown)->is_open();
  });
}

std::optional<dg::NodeId> tooltip_target(WinState& window) {
  if (!window.pointer.has_value()) {
    return std::nullopt;
  }
  const dg::RenderTree& tree = window.tree.render();
  const std::optional<dg::NodeId> hit = tree.hit_test(*window.pointer);
  if (!hit.has_value()) {
    return std::nullopt;
  }
  dg::NodeId current = *hit;
  while (true) {
    const NodeRecord* record = record_of(window, current);
    if (record != nullptr && !record->tooltip.empty()) {
      return current;
    }
    const dg::NodeId parent = tree.parent(current);
    if (parent == current) {
      return std::nullopt;
    }
    current = parent;
  }
}

namespace {

// The open dropdown list, if any, takes a pointer event first. True when
// it used the event.
bool route_open_dropdown_pointer(WinState& window, const dg::PointerEvent& event) {
  dg::UiContext ctx = ui(window);
  for (NodeRecord* record : records_in_order(window)) {
    std::optional<dg::Dropdown>* dropdown = dropdown_in(*record);
    if (dropdown != nullptr && dropdown->has_value() && (*dropdown)->is_open()) {
      const dg::DropdownResult result = (*dropdown)->handle_pointer(ctx, event);
      dropdown_result(window, *record, result);
      if (result.event != dg::DropdownEvent::kIgnored) {
        return true;
      }
    }
  }
  return false;
}

// An open dropdown list (above any dialog), the open dialog, then the open
// menu, see a pointer event first. nullopt = none took a position on it
// (dialog content included: its components see it next).
std::optional<bool> route_pointer_to_popups(WinState& window, const dg::PointerEvent& event) {
  dg::UiContext ctx = ui(window);
  if (window.dialog.is_open()) {
    // A dropdown list opened from the dialog's content sits above it.
    if (route_open_dropdown_pointer(window, event)) {
      return true;
    }
    const std::optional<dg::NodeId> before = window.focus.current();
    if (pointer_in_dialog_content(window, event)) {
      // Form content: the dialog focuses it, the core then clicks/edits it.
      if (event.action == dg::PointerAction::kDown) {
        (void)window.dialog.handle_pointer(ctx, event);
        sync_editor_focus(window, before);
      }
      // Then the content's own dropdowns and components, then the core.
      return std::nullopt;
    }
    dialog_result(window, window.dialog.handle_pointer(ctx, event));
    sync_editor_focus(window, before);
    return true;  // modal: nothing behind it reacts
  }
  if (!window.menu.is_open()) {
    return std::nullopt;
  }
  if (menubar_menu_pointer(window, event)) {
    return true;
  }
  const dg::MenuResult result = window.menu.handle_pointer(ctx, event);
  menu_result(window, result, false);
  if (result.event == dg::MenuEvent::kDismissed && event.action == dg::PointerAction::kDown &&
      event.button == dg::PointerButton::kSecondary) {
    // A right press outside the open menu dismisses it and is still a
    // context click, so the caller can reopen the menu at the new spot.
    return false;
  }
  if (result.event != dg::MenuEvent::kIgnored) {
    return true;
  }
  return std::nullopt;
}

}  // namespace

bool route_pointer(WinState& window, const dg::PointerEvent& event) {
  if (event.action == dg::PointerAction::kLeave) {
    window.pointer.reset();
  } else {
    window.pointer = dg::PixelPoint{event.x, event.y};
  }
  dg::UiContext ctx = ui(window);
  (void)window.tooltip.handle_pointer(ctx, event);
  if (const std::optional<bool> popup = route_pointer_to_popups(window, event)) {
    return *popup;
  }
  if (route_open_dropdown_pointer(window, event)) {
    return true;
  }
  const std::vector<NodeRecord*> records = records_in_order(window);
  for (NodeRecord* record : records) {
    std::optional<dg::Dropdown>* dropdown = dropdown_in(*record);
    if (dropdown != nullptr && dropdown->has_value() && !(*dropdown)->is_open()) {
      const dg::DropdownResult result = (*dropdown)->handle_pointer(ctx, event);
      if (result.event != dg::DropdownEvent::kIgnored) {
        window.tooltip.hide(ctx);
        return true;
      }
    }
  }
  for (NodeRecord* record : records) {
    const bool consumed =
        std::visit([&](auto& state) { return control_pointer(window, *record, state, event); },
                   record->state);
    if (consumed) {
      return true;
    }
  }
  return false;
}

bool route_key(WinState& window, const dg::KeyEvent& event) {
  dg::UiContext ctx = ui(window);
  if (window.dialog.is_open()) {
    // An open dropdown list above the dialog takes every key (Escape
    // closes the list only); a closed one in the content opens on its keys.
    for (NodeRecord* record : records_in_order(window)) {
      std::optional<dg::Dropdown>* dropdown = dropdown_in(*record);
      if (dropdown != nullptr && dropdown->has_value() &&
          ((*dropdown)->is_open() || focus_in_dialog_content(window))) {
        const dg::DropdownResult result = (*dropdown)->handle_key(ctx, event);
        dropdown_result(window, *record, result);
        if ((*dropdown)->is_open() || result.event != dg::DropdownEvent::kIgnored) {
          return true;
        }
      }
    }
    if (key_for_dialog_content(window, event)) {
      return false;
    }
    const std::optional<dg::NodeId> before = window.focus.current();
    dialog_result(window, window.dialog.handle_key(ctx, event));
    sync_editor_focus(window, before);
    return true;
  }
  if (window.menu.is_open()) {
    if (!menubar_menu_key(window, event)) {
      menu_result(window, window.menu.handle_key(ctx, event), true);
    }
    return true;
  }
  const std::vector<NodeRecord*> records = records_in_order(window);
  for (NodeRecord* record : records) {
    std::optional<dg::Dropdown>* dropdown = dropdown_in(*record);
    if (dropdown != nullptr && dropdown->has_value() && (*dropdown)->is_open()) {
      dropdown_result(window, *record, (*dropdown)->handle_key(ctx, event));
      return true;
    }
  }
  for (NodeRecord* record : records) {
    std::optional<dg::Dropdown>* dropdown = dropdown_in(*record);
    if (dropdown != nullptr && dropdown->has_value() &&
        (*dropdown)->handle_key(ctx, event).event != dg::DropdownEvent::kIgnored) {
      return true;
    }
  }
  if (menubar_route_key(window, event)) {
    return true;
  }
  if (event.action != dg::KeyAction::kDown) {
    return false;
  }
  for (NodeRecord* record : records) {
    const bool consumed = std::visit(
        [&](auto& state) { return control_key(window, *record, state, event); }, record->state);
    if (consumed) {
      return true;
    }
  }
  return false;
}

bool route_text(WinState& window, const dg::TextInputEvent& event) {
  dg::UiContext ctx = ui(window);
  if (window.dialog.is_open()) {
    for (NodeRecord* record : records_in_order(window)) {
      std::optional<dg::Dropdown>* dropdown = dropdown_in(*record);
      if (dropdown != nullptr && dropdown->has_value() && (*dropdown)->is_open()) {
        dropdown_result(window, *record, (*dropdown)->handle_text(ctx, event));
        return true;
      }
    }
    return !focus_in_dialog_content(window);
  }
  if (window.menu.is_open()) {
    menu_result(window, window.menu.handle_text(ctx, event), true);
    return true;
  }
  for (NodeRecord* record : records_in_order(window)) {
    std::optional<dg::Dropdown>* dropdown = dropdown_in(*record);
    if (dropdown != nullptr && dropdown->has_value() && (*dropdown)->is_open()) {
      dropdown_result(window, *record, (*dropdown)->handle_text(ctx, event));
      return true;
    }
  }
  return false;
}

int clamp_timeout(const AppState& app, int timeout_ms) {
  const std::int64_t now = dg::steady_anim_time().ms;
  std::int64_t wait = timeout_ms < 0 ? -1 : timeout_ms;
  const auto cut = [&wait](std::int64_t ms) {
    ms = std::max<std::int64_t>(ms, 0);
    wait = wait < 0 ? ms : std::min(wait, ms);
  };
  for (const std::unique_ptr<dg::Window>& window : app.windows) {
    if (window->impl().closed) {
      continue;
    }
    if (const std::optional<dg::AnimTime> deadline = window->impl().tooltip.next_deadline()) {
      cut(deadline->ms - now);
    }
  }
  for (const std::unique_ptr<dg::Window>& window : app.windows) {
    if (window->impl().closed) {
      continue;
    }
    for (const auto& [id, record] : window->impl().records) {
      if (std::visit([](const auto& state) { return animating(state); }, record.state)) {
        cut(kAnimationFrameMs);
      }
    }
  }
  return static_cast<int>(wait);
}

void before_remove(WinState& window, const std::vector<dg::NodeId>& subtree) {
  const auto inside = [&subtree](dg::NodeId id) {
    return std::find(subtree.begin(), subtree.end(), id) != subtree.end();
  };
  dg::UiContext ctx = ui(window);
  const std::optional<dg::NodeId> shown = window.tooltip.shown_for();
  if (shown.has_value() && inside(*shown)) {
    window.tooltip.hide(ctx);
  }
  for (NodeRecord* record : records_in_order(window)) {
    if (!inside(record->id)) {
      continue;
    }
    if (!record->tooltip.empty()) {
      window.tooltip.unbind(record->id);
    }
    std::optional<dg::Dropdown>* dropdown = dropdown_in(*record);
    if (dropdown != nullptr && dropdown->has_value()) {
      // Its menu lives outside the subtree, under the tree root.
      (*dropdown)->close(ctx);
    }
    if (record->kind == NodeKind::kMenuBar) {
      menubar_forget(window, *record);
    }
    if (window.menu_anchor == record->id) {
      window.menu_anchor.reset();
    }
    if (window.dialog_content == record->id) {
      window.dialog_content.reset();
    }
  }
  std::erase_if(window.user_shortcuts, [&](const dg::UserShortcut& entry) {
    return entry.scope.has_value() && inside(*entry.scope);
  });
}

}  // namespace components
}  // namespace dg::app_detail

namespace dg {

using app_detail::NodeRecord;
using app_detail::WinState;

bool Window::has_dialog() const {
  return state_->dialog.is_open();
}

Expected<std::int32_t, Status> Window::open_menu(std::optional<NodeId> anchor, PixelPoint at,
                                                 const std::vector<MenuEntry>& items) {
  WinState& window = *state_;
  const bool any_selectable = std::any_of(items.begin(), items.end(), [](const MenuEntry& e) {
    return !e.separator && !e.disabled;
  });
  if (items.size() > limits::kMaxMenuItems || !any_selectable) {
    return Unexpected(Status::kInvalidArgument);
  }
  if (window.dialog.is_open()) {
    return Unexpected(Status::kUnsupported);
  }
  std::optional<dg::PixelRect> anchor_rect;
  if (anchor.has_value()) {
    if (app_detail::record_of(window, *anchor) == nullptr) {
      return Unexpected(Status::kInvalidNode);
    }
    anchor_rect = window.tree.render().absolute_bounds(*anchor);
  }
  dg::MenuSpec spec = app_detail::components::menu_spec_of(items);
  dg::UiContext ctx = app_detail::components::ui(window);
  window.tooltip.hide(ctx);
  app_detail::components::dismiss_open_menu(window);
  const dg::PixelRect rect = anchor_rect.value_or(dg::PixelRect{at.x, at.y, 0, 0});
  spec.min_width = anchor_rect.has_value() ? anchor_rect->width : 0;
  if (!window.menu.open(ctx, rect, std::move(spec), window.focus.current())) {
    app_detail::components::clear_menu_owner(window);
    return Unexpected(Status::kUnsupported);
  }
  window.menu_id = window.app.next_popup_id++;
  window.menu_anchor = anchor;
  window.menu_commands = app_detail::components::commands_of(items);
  return window.menu_id;
}

void Window::close_menu() {
  WinState& window = *state_;
  dg::UiContext ctx = app_detail::components::ui(window);
  window.menu.close(ctx);
  if (window.menu_bar.has_value()) {
    app_detail::components::menubar_menu_closed(window, *window.menu_bar, false);
  } else {
    app_detail::components::clear_menu_owner(window);
  }
  app_detail::report_focus_drift(window);
}

Expected<std::int32_t, Status> Window::open_dialog(DialogOptions options) {
  WinState& window = *state_;
  if (options.buttons.size() > limits::kMaxDialogButtons) {
    return Unexpected(Status::kInvalidArgument);
  }
  if (window.dialog.is_open()) {
    return Unexpected(Status::kUnsupported);
  }
  const std::optional<int> default_button = options.default_button;
  const std::optional<int> cancel_button = options.cancel_button;
  dg::DialogSpec spec;
  spec.title = std::move(options.title);
  spec.body = std::move(options.body);
  if (!options.buttons.empty()) {
    spec.buttons = std::move(options.buttons);
  }
  const auto total = static_cast<int>(spec.buttons.size());
  const auto in_range = [total](std::optional<int> button) {
    return !button.has_value() || (*button >= 0 && *button < total);
  };
  if (!in_range(default_button) || !in_range(cancel_button)) {
    return Unexpected(Status::kInvalidArgument);
  }
  if (options.content.has_value()) {
    if (app_detail::record_of(window, *options.content) == nullptr) {
      return Unexpected(Status::kInvalidNode);
    }
    if (window.root == options.content) {
      return Unexpected(Status::kInvalidArgument);
    }
  }
  spec.default_button = default_button;
  spec.cancel_button = cancel_button;
  dg::UiContext ctx = app_detail::components::ui(window);
  window.tooltip.hide(ctx);
  app_detail::components::dismiss_open_menu(window);
  for (NodeRecord* record : app_detail::records_in_order(window)) {
    std::optional<dg::Dropdown>* dropdown = app_detail::dropdown_in(*record);
    if (dropdown != nullptr && dropdown->has_value()) {
      (*dropdown)->close(ctx);
    }
  }
  const std::optional<dg::NodeId> before = window.focus.current();
  // Focus as the caller last saw it, restored on close.
  const std::optional<dg::NodeId> restore = window.reported_focus;
  (void)window.focus.set(restore);
  // Form content: measured in a host box as wide as the dialog body, which
  // then hangs (render side) inside the body, below the body text.
  int content_top = 0;
  if (options.content.has_value()) {
    dg::BoxStyle box;
    box.kind = dg::LayoutKind::kColumn;
    box.cross_align = dg::CrossAlign::kStretch;
    box.width = dg::ui_visuals::dialog_inner_width(ctx, window.tree.viewport(), spec);
    box.left = 0;
    box.top = 0;
    const dg::NodeId host =
        window.tree.add_child(app_detail::parking_of(window), box, dg::NodeStyle{});
    window.dialog_host = host;
    (void)window.tree.move_child(*options.content, host, std::nullopt);
    window.dialog_content = options.content;
    (void)window.tree.layout();
    content_top = spec.body.empty() ? 0 : app_detail::kContentGap;
    spec.body_height = window.tree.render().local_bounds(host).height + content_top;
  }
  if (!window.dialog.open(ctx, std::move(spec), restore)) {
    app_detail::release_dialog_content(window);
    app_detail::sync_editor_focus(window, before);
    return Unexpected(Status::kUnsupported);
  }
  app_detail::sync_editor_focus(window, before);
  if (window.dialog_content.has_value() && window.dialog_host.has_value()) {
    const dg::NodeId host = *window.dialog_host;
    dg::BoxStyle box = window.tree.box(host);
    box.top = window.dialog.body_content_top() + content_top;
    window.tree.set_box(host, box);
    window.dialog.adopt(ctx, host);
    (void)window.tree.layout();
    window.relaid = true;
  }
  window.dialog_id = window.app.next_popup_id++;
  return window.dialog_id;
}

Status Window::set_dialog_button_disabled(std::int32_t dialog_id, int button, bool disabled) {
  WinState& window = *state_;
  if (!window.dialog.is_open() || dialog_id != window.dialog_id) {
    return Status::kUnknownId;
  }
  if (button < 0 || static_cast<std::size_t>(button) >= window.dialog.buttons().size()) {
    return Status::kValueOutOfRange;
  }
  dg::UiContext ctx = app_detail::components::ui(window);
  const std::optional<dg::NodeId> before = window.focus.current();
  window.dialog.set_button_disabled(ctx, button, disabled);
  app_detail::sync_editor_focus(window, before);
  app_detail::report_focus_drift(window);
  return Status::kOk;
}

void Window::close_dialog() {
  WinState& window = *state_;
  dg::UiContext ctx = app_detail::components::ui(window);
  const std::optional<dg::NodeId> before = window.focus.current();
  window.dialog.close(ctx);
  app_detail::sync_editor_focus(window, before);
  window.dialog_id = 0;
  app_detail::release_dialog_content(window);
  app_detail::report_focus_drift(window);
}

Status Window::set_tooltip(NodeId node, std::string text) {
  WinState& window = *state_;
  NodeRecord* record = app_detail::record_of(window, node);
  if (record == nullptr) {
    return Status::kInvalidNode;
  }
  record->tooltip = std::move(text);
  if (record->tooltip.empty()) {
    window.tooltip.unbind(record->id);
    if (window.tooltip.shown_for() == record->id) {
      dg::UiContext ctx = app_detail::components::ui(window);
      window.tooltip.hide(ctx);
    }
    return Status::kOk;
  }
  window.tooltip.bind(record->id, record->tooltip);
  return Status::kOk;
}

Status App::set_tooltip_delay(int delay_ms) {
  app_detail::AppState& app = *state_;
  if (delay_ms < 0 || delay_ms > app_detail::kMaxTooltipDelayMs) {
    return Status::kValueOutOfRange;
  }
  app.tooltip_delay_ms = delay_ms;
  for (const std::unique_ptr<Window>& handle : app.windows) {
    WinState& window = handle->impl();
    if (window.closed || !window.popups.has_value()) {
      continue;
    }
    dg::UiContext ctx = app_detail::components::ui(window);
    window.tooltip.hide(ctx);
    window.tooltip = dg::Tooltip(delay_ms);
    for (NodeRecord* record : app_detail::records_in_order(window)) {
      if (!record->tooltip.empty()) {
        window.tooltip.bind(record->id, record->tooltip);
      }
    }
  }
  return Status::kOk;
}

}  // namespace dg
