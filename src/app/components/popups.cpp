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

// -- Typed results to events ---------------------------------------------------

void menu_result(WinState& window, const dg::MenuResult& result) {
  if (result.event == dg::MenuEvent::kActivated) {
    emit(window.app, dg::events::MenuEvent{window.sdl_window, window.menu_anchor, result.index,
                                           window.menu_id});
  } else if (result.event == dg::MenuEvent::kDismissed) {
    emit(window.app, dg::events::MenuEvent{window.sdl_window, window.menu_anchor, std::nullopt,
                                           window.menu_id});
  }
  if (result.closed()) {
    window.menu_id = 0;
    window.menu_anchor.reset();
  }
}

void dialog_result(WinState& window, const dg::DialogResult& result) {
  if (result.event == dg::DialogEvent::kClosed) {
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

// Dismisses an open menu, reporting the dismissal.
void dismiss_menu(WinState& window, dg::UiContext& ctx) {
  window.menu.close(ctx);
  emit(window.app, dg::events::MenuEvent{window.sdl_window, window.menu_anchor, std::nullopt,
                                         window.menu_id});
}

}  // namespace

namespace components {

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

bool route_pointer(WinState& window, const dg::PointerEvent& event) {
  if (event.action == dg::PointerAction::kLeave) {
    window.pointer.reset();
  } else {
    window.pointer = dg::PixelPoint{event.x, event.y};
  }
  dg::UiContext ctx = ui(window);
  (void)window.tooltip.handle_pointer(ctx, event);
  if (window.dialog.is_open()) {
    dialog_result(window, window.dialog.handle_pointer(ctx, event));
    return true;  // modal: nothing behind it reacts
  }
  if (window.menu.is_open()) {
    const dg::MenuResult result = window.menu.handle_pointer(ctx, event);
    menu_result(window, result);
    if (result.event == dg::MenuEvent::kDismissed && event.action == dg::PointerAction::kDown &&
        event.button == dg::PointerButton::kSecondary) {
      // A right press outside the open menu dismisses it and is still a
      // context click, so the caller can reopen the menu at the new spot.
      return false;
    }
    if (result.event != dg::MenuEvent::kIgnored) {
      return true;
    }
  }
  const std::vector<NodeRecord*> records = records_in_order(window);
  for (NodeRecord* record : records) {
    std::optional<dg::Dropdown>* dropdown = dropdown_in(*record);
    if (dropdown != nullptr && dropdown->has_value() && (*dropdown)->is_open()) {
      const dg::DropdownResult result = (*dropdown)->handle_pointer(ctx, event);
      dropdown_result(window, *record, result);
      if (result.event != dg::DropdownEvent::kIgnored) {
        return true;
      }
    }
  }
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
    dialog_result(window, window.dialog.handle_key(ctx, event));
    return true;
  }
  if (window.menu.is_open()) {
    menu_result(window, window.menu.handle_key(ctx, event));
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
  if (window.dialog.is_open()) {
    return true;
  }
  dg::UiContext ctx = ui(window);
  if (window.menu.is_open()) {
    menu_result(window, window.menu.handle_text(ctx, event));
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
    if (window.menu_anchor == record->id) {
      window.menu_anchor.reset();
    }
  }
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
  dg::MenuSpec spec;
  spec.items.reserve(items.size());
  for (const MenuEntry& entry : items) {
    dg::MenuItem item;
    item.text = entry.text;
    item.separator = entry.separator;
    item.disabled = entry.disabled;
    item.checked = entry.checked;
    spec.items.push_back(std::move(item));
  }
  dg::UiContext ctx = app_detail::components::ui(window);
  window.tooltip.hide(ctx);
  if (window.menu.is_open()) {
    app_detail::dismiss_menu(window, ctx);
  }
  const dg::PixelRect rect = anchor_rect.value_or(dg::PixelRect{at.x, at.y, 0, 0});
  spec.min_width = anchor_rect.has_value() ? anchor_rect->width : 0;
  if (!window.menu.open(ctx, rect, std::move(spec), window.focus.current())) {
    window.menu_id = 0;
    window.menu_anchor.reset();
    return Unexpected(Status::kUnsupported);
  }
  window.menu_id = window.app.next_popup_id++;
  window.menu_anchor = anchor;
  return window.menu_id;
}

void Window::close_menu() {
  WinState& window = *state_;
  dg::UiContext ctx = app_detail::components::ui(window);
  window.menu.close(ctx);
  window.menu_id = 0;
  window.menu_anchor.reset();
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
  spec.default_button = default_button;
  spec.cancel_button = cancel_button;
  dg::UiContext ctx = app_detail::components::ui(window);
  window.tooltip.hide(ctx);
  if (window.menu.is_open()) {
    app_detail::dismiss_menu(window, ctx);
    window.menu_id = 0;
    window.menu_anchor.reset();
  }
  for (NodeRecord* record : app_detail::records_in_order(window)) {
    std::optional<dg::Dropdown>* dropdown = app_detail::dropdown_in(*record);
    if (dropdown != nullptr && dropdown->has_value()) {
      (*dropdown)->close(ctx);
    }
  }
  // Focus as the caller last saw it, restored on close.
  const std::optional<dg::NodeId> restore = window.reported_focus;
  (void)window.focus.set(restore);
  if (!window.dialog.open(ctx, std::move(spec), restore)) {
    return Unexpected(Status::kUnsupported);
  }
  window.dialog_id = window.app.next_popup_id++;
  return window.dialog_id;
}

void Window::close_dialog() {
  WinState& window = *state_;
  dg::UiContext ctx = app_detail::components::ui(window);
  window.dialog.close(ctx);
  window.dialog_id = 0;
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
