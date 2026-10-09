// ABI 0.2 phase B: the src/ui component layer (dg::Menu, dg::Dropdown,
// dg::Tooltip, dg::Dialog, dg::ProgressBar, dg::Tabs, dg::TreeView,
// dg::Table) behind the C ABI, poll-based. The pump in abi_impl.cpp hands
// every pointer/key/text event to route_*() first (an open dialog, then an
// open menu, then dropdowns, then the controls), ticks tooltips and
// indeterminate progress bars, and calls sync() after a layout pass. Every
// typed result a component returns becomes one dg_event.
//
// A component node is a wrapper the caller holds a handle to; the
// component builds its own composed nodes inside it. A theme switch or a
// DG_PROP_HEIGHT write rebuilds those composed nodes from the data kept in
// abi::Component (the caller-visible tab panels are moved out and back in).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "abi_impl.h"
#include "abi_internal.h"
#include "drawgui/anim/clock.h"
#include "drawgui/render/node_lifecycle.h"
#include "drawgui/ui/control_context.h"
#include "drawgui/ui/ui_context.h"
#include "drawgui/widget/focus.h"
#include "drawgui/widget/widget_set.h"

namespace dg::abi {
namespace {

constexpr int kDropdownWidth = 200;
constexpr int kDropdownHeight = 32;
constexpr int kDropdownInset = 10;
constexpr int kProgressWidth = 200;
constexpr int kProgressHeight = 8;
constexpr int kTabsWidth = 320;
constexpr int kTabsHeight = 240;
constexpr int kTreeWidth = 240;
constexpr int kTreeHeight = 240;
constexpr int kTableWidth = 360;
constexpr int kTableHeight = 240;
constexpr int kFrame = 1;  // control_visuals' tree/table outer border
constexpr int kAnimationFrameMs = 16;
constexpr std::uint32_t kMaxMenuItems = 4096;
constexpr std::uint32_t kMaxDialogButtons = 8;
constexpr std::size_t kMaxTabs = 64;
constexpr std::uint32_t kMaxColumns = 64;
constexpr std::uint32_t kMaxItems = 1U << 24U;
constexpr int kMaxTooltipDelayMs = 60000;

dg::NodeSideTables tables_of(WindowImpl& window) {
  return dg::NodeSideTables{window.widgets,   window.theme_bindings, window.focus,
                            window.animation, window.action_scopes,  window.interaction};
}

const dg::FontCatalog* fonts_of(const AppImpl& impl) {
  return impl.fonts.has_value() ? &*impl.fonts : nullptr;
}

dg::UiContext ui(AppImpl& impl, WindowImpl& window) {
  dg::PopupStyle style;
  const auto [theme, variant] = active_theme_of(impl);
  if (theme != nullptr) {
    style = dg::popup_style_from_theme(*theme, variant);
  }
  if (!window.popups.has_value()) {
    window.popups.emplace(impl.wm);
  }
  return dg::UiContext{*window.popups,
                       window.sdl_window,
                       window.tree.render(),
                       tables_of(window),
                       fonts_of(impl),
                       impl.default_font,
                       style};
}

dg::ControlStyle control_style(const AppImpl& impl) {
  const auto [theme, variant] = active_theme_of(impl);
  return theme != nullptr ? dg::control_style_from_theme(*theme, variant) : dg::ControlStyle{};
}

dg::ControlContext controls(AppImpl& impl, WindowImpl& window) {
  return dg::ControlContext{window.tree, tables_of(window), fonts_of(impl), impl.default_font,
                            control_style(impl)};
}

void queue(AppImpl& impl, WindowImpl& window, std::uint32_t kind, dg_node_t* node,
           std::int32_t x, std::int32_t y, int clicks = 0) {
  dg_event out{};
  out.size = sizeof(dg_event);
  out.kind = kind;
  out.window = window.self_handle;
  out.node = node;
  out.x = x;
  out.y = y;
  out.clicks = static_cast<std::uint16_t>(std::clamp(clicks, 0, 0xFFFF));
  impl.pending_events.push_back(out);
}

// Every live component slot of `window`.
std::vector<NodeSlot*> component_slots(AppImpl& impl, const WindowImpl& window) {
  std::vector<NodeSlot*> out;
  for (NodeSlot& slot : impl.nodes) {
    if (slot.state == NodeSlot::State::kLive && slot.window_index == window.self_index &&
        slot.component != nullptr) {
      out.push_back(&slot);
    }
  }
  return out;
}

bool any_dropdown_open(AppImpl& impl, const WindowImpl& window) {
  return std::ranges::any_of(component_slots(impl, window), [](const NodeSlot* slot) {
    return slot->component->dropdown.has_value() && slot->component->dropdown->is_open();
  });
}

int wrapper_height(const WindowImpl& window, const NodeSlot& slot, int fallback) {
  return window.tree.box(slot.node_id).height.value_or(fallback);
}

// A control rebuild loses the focused composed node; focus moves to the
// rebuilt one without reporting a change (the caller-visible focus did not
// move).
struct FocusKeeper {
  bool had = false;

  FocusKeeper(const WindowImpl& window, dg::NodeId old_root) {
    const std::optional<dg::NodeId> current = window.focus.current();
    had = old_root != dg::NodeId{} && current.has_value() &&
          window.tree.render().is_valid(old_root) &&
          dg::is_within(window.tree.render(), old_root, *current);
  }

  void restore(WindowImpl& window, dg::NodeId new_target) const {
    if (!had) {
      return;
    }
    (void)window.focus.set(new_target);
    window.reported_focus = window.focus.current();
  }
};

// -- Builders (also the rebuild path) -----------------------------------------

void build_progress(AppImpl& impl, WindowImpl& window, NodeSlot& slot) {
  Component& c = *slot.component;
  dg::ControlContext ctx = controls(impl, window);
  const float value = c.progress.value();
  const bool indeterminate = c.progress.indeterminate();
  c.progress.destroy(ctx);
  dg::ProgressBarSpec spec;
  spec.box.percent.width = 100.0F;
  spec.box.height = wrapper_height(window, slot, kProgressHeight);
  spec.value = value;
  spec.indeterminate = indeterminate;
  c.progress.build(ctx, slot.node_id, spec, dg::steady_anim_time());
  window.relaid = true;
}

void build_tabs(AppImpl& impl, WindowImpl& window, NodeSlot& slot) {
  Component& c = *slot.component;
  dg::ControlContext ctx = controls(impl, window);
  const std::optional<int> selected = c.tabs.selected();
  const std::optional<dg::NodeId> focused = window.focus.current();
  const bool focus_on_tab =
      focused.has_value() && std::find(c.tabs.tab_nodes().begin(), c.tabs.tab_nodes().end(),
                                       *focused) != c.tabs.tab_nodes().end();
  // The caller's panels survive the rebuild: parked in the wrapper, then
  // moved into the rebuilt panel areas.
  for (dg_node_t* panel : c.tab_panels) {
    const NodeSlot* panel_slot = slot_of(panel);
    if (panel_slot != nullptr && panel_slot->state == NodeSlot::State::kLive) {
      (void)window.tree.move_child(panel_slot->node_id, slot.node_id, std::nullopt);
    }
  }
  c.tabs.destroy(ctx);
  dg::TabsSpec spec;
  spec.tabs = c.tab_items;
  spec.selected = selected.value_or(0);
  spec.box.percent.width = 100.0F;
  spec.box.percent.height = 100.0F;
  c.tabs.build(ctx, slot.node_id, spec);
  for (std::size_t i = 0; i < c.tab_panels.size(); ++i) {
    const dg::NodeId area = c.tabs.panel(static_cast<int>(i));
    dg::BoxStyle area_box = window.tree.box(area);
    area_box.cross_align = dg::CrossAlign::kStretch;
    window.tree.set_box(area, area_box);
    const NodeSlot* panel_slot = slot_of(c.tab_panels[i]);
    if (panel_slot != nullptr && panel_slot->state == NodeSlot::State::kLive) {
      (void)window.tree.move_child(panel_slot->node_id, area, std::nullopt);
    }
  }
  const std::optional<int> now_selected = c.tabs.selected();
  if (focus_on_tab && now_selected.has_value()) {
    (void)window.focus.set(c.tabs.tab_nodes()[static_cast<std::size_t>(*now_selected)]);
    window.reported_focus = window.focus.current();
  }
  window.relaid = true;
}

void build_tree(AppImpl& impl, WindowImpl& window, NodeSlot& slot,
                std::optional<int> select = std::nullopt) {
  Component& c = *slot.component;
  dg::ControlContext ctx = controls(impl, window);
  const FocusKeeper keeper(window, c.tree.root());
  c.tree.destroy(ctx);
  dg::TreeViewSpec spec;
  spec.items = c.tree_items;
  spec.viewport_height =
      std::max(ctx.style.row_height, wrapper_height(window, slot, kTreeHeight) - (2 * kFrame));
  spec.box.percent.width = 100.0F;
  c.tree.build(ctx, slot.node_id, std::move(spec));
  if (select.has_value()) {
    (void)c.tree.select(ctx, *select);
  }
  keeper.restore(window, c.tree.root());
  window.relaid = true;
}

// The tree's items with their current expansion, for a rebuild.
void capture_tree(Component& c) {
  for (std::size_t i = 0; i < c.tree_items.size(); ++i) {
    c.tree_items[i].expanded = c.tree.is_expanded(static_cast<int>(i));
  }
}

void build_table(AppImpl& impl, WindowImpl& window, NodeSlot& slot,
                 std::optional<int> select = std::nullopt) {
  Component& c = *slot.component;
  dg::ControlContext ctx = controls(impl, window);
  const FocusKeeper keeper(window, c.table.root());
  c.table.destroy(ctx);
  dg::TableSpec spec;
  spec.columns = c.columns;
  spec.rows = c.rows;
  spec.viewport_height =
      std::max(ctx.style.row_height, wrapper_height(window, slot, kTableHeight) - (2 * kFrame) -
                                         ctx.style.row_height);
  spec.box.percent.width = 100.0F;
  c.table.build(ctx, slot.node_id, std::move(spec));
  if (c.sort_column.has_value()) {
    c.table.set_sort_indicator(ctx, c.sort_column, c.sort_ascending);
  }
  if (select.has_value()) {
    (void)c.table.select(ctx, *select);
  }
  keeper.restore(window, c.table.root());
  window.relaid = true;
}

void paint_dropdown(AppImpl& impl, WindowImpl& window, NodeSlot& slot) {
  const dg::ControlStyle style = control_style(impl);
  dg::RenderTree& tree = window.tree.render();
  dg::NodeStyle node_style = tree.style(slot.node_id);
  node_style.border_color = style.border;
  tree.set_style(slot.node_id, node_style);
  dg::Widget widget = window.widgets.at(slot.node_id);
  widget.fill_normal = style.surface;
  widget.fill_hover = style.border;
  widget.fill_pressed = style.border;
  window.widgets.attach(slot.node_id, widget);
  dg::TextStyle text = tree.style(widget.label).text;
  text.color = style.on_surface;
  if (!widget.selected_index.has_value()) {
    text.text = slot.component->placeholder;
  }
  tree.set_text(widget.label, text);
  window.widgets.refresh(tree, slot.node_id, window.interaction.state_of(slot.node_id));
}

// -- Typed results to events ---------------------------------------------------

void menu_result(AppImpl& impl, WindowImpl& window, const dg::MenuResult& result) {
  if (result.event == dg::MenuEvent::kActivated) {
    queue(impl, window, DG_EVENT_MENU_ACTIVATE, window.menu_anchor, result.index,
          window.menu_id);
  } else if (result.event == dg::MenuEvent::kDismissed) {
    queue(impl, window, DG_EVENT_MENU_DISMISS, window.menu_anchor, -1, window.menu_id);
  }
  if (result.closed()) {
    window.menu_id = 0;
    window.menu_anchor = nullptr;
  }
}

void dialog_result(AppImpl& impl, WindowImpl& window, const dg::DialogResult& result) {
  if (result.event == dg::DialogEvent::kClosed) {
    queue(impl, window, DG_EVENT_DIALOG_RESULT, nullptr, result.button.value_or(-1),
          window.dialog_id);
    window.dialog_id = 0;
  }
}

void dropdown_result(AppImpl& impl, WindowImpl& window, NodeSlot& slot,
                     const dg::DropdownResult& result) {
  if (result.event == dg::DropdownEvent::kChanged) {
    queue(impl, window, DG_EVENT_VALUE_CHANGED, slot.self_handle, result.selected.value_or(-1),
          result.previous.value_or(-1));
  }
}

bool control_pointer(AppImpl& impl, WindowImpl& window, NodeSlot& slot,
                     const dg::PointerEvent& event) {
  Component& c = *slot.component;
  dg::ControlContext ctx = controls(impl, window);
  dg_node_t* node = slot.self_handle;
  switch (slot.type_id) {
    case DG_NODE_TYPE_TABS: {
      const dg::TabsResult r = c.tabs.handle_pointer(ctx, event);
      if (r.event == dg::TabsEvent::kSelected) {
        queue(impl, window, DG_EVENT_TAB_SELECT, node, r.index, 0);
      }
      return r.event != dg::TabsEvent::kIgnored;
    }
    case DG_NODE_TYPE_TREE: {
      const dg::TreeResult r = c.tree.handle_pointer(ctx, event);
      if (r.event == dg::TreeEvent::kSelected) {
        queue(impl, window, DG_EVENT_TREE_SELECT, node, r.item, 0);
      } else if (r.event == dg::TreeEvent::kExpanded) {
        queue(impl, window, DG_EVENT_TREE_EXPAND, node, r.item, 0);
      } else if (r.event == dg::TreeEvent::kCollapsed) {
        queue(impl, window, DG_EVENT_TREE_COLLAPSE, node, r.item, 0);
      }
      return r.event != dg::TreeEvent::kIgnored;
    }
    case DG_NODE_TYPE_TABLE: {
      const dg::TableResult r = c.table.handle_pointer(ctx, event);
      if (r.event == dg::TableEvent::kSelected) {
        queue(impl, window, DG_EVENT_TABLE_SELECT, node, r.row, 0, std::max(event.clicks, 1));
      } else if (r.event == dg::TableEvent::kSortRequested) {
        queue(impl, window, DG_EVENT_TABLE_SORT, node, r.column, 0);
      }
      return r.event != dg::TableEvent::kIgnored;
    }
    default:
      return false;
  }
}

bool control_key(AppImpl& impl, WindowImpl& window, NodeSlot& slot, const dg::KeyEvent& event) {
  Component& c = *slot.component;
  dg::ControlContext ctx = controls(impl, window);
  dg_node_t* node = slot.self_handle;
  switch (slot.type_id) {
    case DG_NODE_TYPE_TABS: {
      const dg::TabsResult r = c.tabs.handle_key(ctx, event);
      if (r.event == dg::TabsEvent::kSelected) {
        queue(impl, window, DG_EVENT_TAB_SELECT, node, r.index, 0);
      }
      return r.event != dg::TabsEvent::kIgnored;
    }
    case DG_NODE_TYPE_TREE: {
      const dg::TreeResult r = c.tree.handle_key(ctx, event);
      if (r.event == dg::TreeEvent::kSelected) {
        queue(impl, window, DG_EVENT_TREE_SELECT, node, r.item, 0);
      } else if (r.event == dg::TreeEvent::kExpanded) {
        queue(impl, window, DG_EVENT_TREE_EXPAND, node, r.item, 0);
      } else if (r.event == dg::TreeEvent::kCollapsed) {
        queue(impl, window, DG_EVENT_TREE_COLLAPSE, node, r.item, 0);
      }
      return r.event != dg::TreeEvent::kIgnored;
    }
    case DG_NODE_TYPE_TABLE: {
      const dg::TableResult r = c.table.handle_key(ctx, event);
      if (r.event == dg::TableEvent::kSelected) {
        queue(impl, window, DG_EVENT_TABLE_SELECT, node, r.row, 0);
      } else if (r.event == dg::TableEvent::kSortRequested) {
        queue(impl, window, DG_EVENT_TABLE_SORT, node, r.column, 0);
      }
      return r.event != dg::TableEvent::kIgnored;
    }
    default:
      return false;
  }
}

// The nearest ancestor-or-self of the node under the pointer that has a
// tooltip.
std::optional<dg::NodeId> tooltip_target(AppImpl& impl, WindowImpl& window) {
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
    for (const NodeSlot& slot : impl.nodes) {
      if (slot.state == NodeSlot::State::kLive && slot.window_index == window.self_index &&
          slot.node_id == current && !slot.tooltip.empty()) {
        return current;
      }
    }
    const dg::NodeId parent = tree.parent(current);
    if (parent == current) {
      return std::nullopt;
    }
    current = parent;
  }
}

// A live slot of `type_id`, or the DG_ERR_* saying why not.
struct Resolved {
  NodeSlot* slot = nullptr;
  AppImpl* impl = nullptr;
  WindowImpl* window = nullptr;
  std::int32_t status = DG_ERR_OK;
};

Resolved resolve(dg_node_t* node_h, std::uint16_t type_id) {
  Resolved out;
  out.slot = slot_of(node_h);
  if (out.slot == nullptr) {
    out.status = DG_ERR_INVALID_HANDLE;
    return out;
  }
  if (out.slot->state != NodeSlot::State::kLive) {
    out.status = DG_ERR_NO_WINDOW;
    return out;
  }
  if (out.slot->type_id != type_id || out.slot->component == nullptr) {
    out.status = DG_ERR_UNSUPPORTED;
    return out;
  }
  out.impl = app_of(node_h->app);
  out.window = out.impl->windows[out.slot->window_index].get();
  return out;
}

std::string text_or_empty(const char* text) {
  return text == nullptr ? std::string() : std::string(text);
}

}  // namespace

// -- Hooks called by abi_impl.cpp ---------------------------------------------

namespace components {

bool is_component_type(std::uint16_t type_id) {
  return type_id == DG_NODE_TYPE_DROPDOWN || type_id == DG_NODE_TYPE_PROGRESS ||
         type_id == DG_NODE_TYPE_TABS || type_id == DG_NODE_TYPE_TREE ||
         type_id == DG_NODE_TYPE_TABLE;
}

void defaults(const AppImpl& impl, std::uint16_t type_id, dg::BoxStyle& box,
              dg::NodeStyle& style) {
  if (type_id == DG_NODE_TYPE_DROPDOWN) {
    const dg::ControlStyle colors = control_style(impl);
    box.kind = dg::LayoutKind::kLeaf;
    box.width = kDropdownWidth;
    box.height = kDropdownHeight;
    style.fill = colors.surface;
    style.border_color = colors.border;
    style.border_width = dg::BorderWidths::all(1.0F);
    style.radii = dg::Radii::all(static_cast<float>(colors.radius));
    return;
  }
  box.kind = dg::LayoutKind::kColumn;
  box.cross_align = dg::CrossAlign::kStretch;
  switch (type_id) {
    case DG_NODE_TYPE_PROGRESS:
      box.width = kProgressWidth;
      box.height = kProgressHeight;
      break;
    case DG_NODE_TYPE_TABS:
      box.width = kTabsWidth;
      box.height = kTabsHeight;
      break;
    case DG_NODE_TYPE_TREE:
      box.width = kTreeWidth;
      box.height = kTreeHeight;
      break;
    default:
      box.width = kTableWidth;
      box.height = kTableHeight;
      break;
  }
}

void compose(AppImpl& impl, WindowImpl& window, NodeSlot& slot) {
  slot.component = std::make_shared<Component>();
  switch (slot.type_id) {
    case DG_NODE_TYPE_DROPDOWN: {
      dg::BoxStyle label_box;
      label_box.percent.width = 100.0F;
      label_box.percent.height = 100.0F;
      dg::NodeStyle label_style;
      label_style.text.font = impl.default_font;
      label_style.text.size = default_text_size();
      label_style.text.align = dg::TextAlign::kLeft;
      label_style.text.inset = kDropdownInset;
      dg::Widget widget;
      widget.kind = dg::WidgetKind::kDropdown;
      widget.label = window.tree.add_child(slot.node_id, label_box, label_style);
      window.widgets.attach(slot.node_id, widget);
      slot.component->dropdown.emplace(slot.node_id);
      paint_dropdown(impl, window, slot);
      break;
    }
    case DG_NODE_TYPE_PROGRESS:
      build_progress(impl, window, slot);
      break;
    case DG_NODE_TYPE_TABS:
      build_tabs(impl, window, slot);
      break;
    case DG_NODE_TYPE_TREE:
      build_tree(impl, window, slot);
      break;
    case DG_NODE_TYPE_TABLE:
      build_table(impl, window, slot);
      break;
    default:
      break;
  }
}

void init_window(AppImpl& impl, WindowImpl& window) {
  window.popups.emplace(impl.wm);
  window.tooltip = dg::Tooltip(impl.tooltip_delay_ms);
}

bool route_pointer(AppImpl& impl, WindowImpl& window, const dg::PointerEvent& event) {
  if (event.action == dg::PointerAction::kLeave) {
    window.pointer.reset();
  } else {
    window.pointer = dg::PixelPoint{event.x, event.y};
  }
  dg::UiContext ctx = ui(impl, window);
  (void)window.tooltip.handle_pointer(ctx, event);
  if (window.dialog.is_open()) {
    dialog_result(impl, window, window.dialog.handle_pointer(ctx, event));
    return true;  // modal: nothing behind it reacts
  }
  if (window.menu.is_open()) {
    const dg::MenuResult result = window.menu.handle_pointer(ctx, event);
    menu_result(impl, window, result);
    if (result.event != dg::MenuEvent::kIgnored) {
      return true;
    }
  }
  const std::vector<NodeSlot*> slots = component_slots(impl, window);
  for (NodeSlot* slot : slots) {
    std::optional<dg::Dropdown>& dropdown = slot->component->dropdown;
    if (dropdown.has_value() && dropdown->is_open()) {
      const dg::DropdownResult result = dropdown->handle_pointer(ctx, event);
      dropdown_result(impl, window, *slot, result);
      if (result.event != dg::DropdownEvent::kIgnored) {
        return true;
      }
    }
  }
  for (NodeSlot* slot : slots) {
    std::optional<dg::Dropdown>& dropdown = slot->component->dropdown;
    if (dropdown.has_value() && !dropdown->is_open()) {
      const dg::DropdownResult result = dropdown->handle_pointer(ctx, event);
      if (result.event != dg::DropdownEvent::kIgnored) {
        window.tooltip.hide(ctx);
        return true;
      }
    }
  }
  for (NodeSlot* slot : slots) {
    if (control_pointer(impl, window, *slot, event)) {
      return true;
    }
  }
  return false;
}

bool route_key(AppImpl& impl, WindowImpl& window, const dg::KeyEvent& event) {
  dg::UiContext ctx = ui(impl, window);
  if (window.dialog.is_open()) {
    dialog_result(impl, window, window.dialog.handle_key(ctx, event));
    return true;
  }
  if (window.menu.is_open()) {
    menu_result(impl, window, window.menu.handle_key(ctx, event));
    return true;
  }
  const std::vector<NodeSlot*> slots = component_slots(impl, window);
  for (NodeSlot* slot : slots) {
    std::optional<dg::Dropdown>& dropdown = slot->component->dropdown;
    if (dropdown.has_value() && dropdown->is_open()) {
      dropdown_result(impl, window, *slot, dropdown->handle_key(ctx, event));
      return true;
    }
  }
  for (NodeSlot* slot : slots) {
    std::optional<dg::Dropdown>& dropdown = slot->component->dropdown;
    if (dropdown.has_value() &&
        dropdown->handle_key(ctx, event).event != dg::DropdownEvent::kIgnored) {
      return true;
    }
  }
  if (event.action != dg::KeyAction::kDown) {
    return false;
  }
  for (NodeSlot* slot : slots) {
    if (control_key(impl, window, *slot, event)) {
      return true;
    }
  }
  return false;
}

bool route_text(AppImpl& impl, WindowImpl& window, const dg::TextInputEvent& event) {
  if (window.dialog.is_open()) {
    return true;
  }
  dg::UiContext ctx = ui(impl, window);
  if (window.menu.is_open()) {
    menu_result(impl, window, window.menu.handle_text(ctx, event));
    return true;
  }
  for (NodeSlot* slot : component_slots(impl, window)) {
    std::optional<dg::Dropdown>& dropdown = slot->component->dropdown;
    if (dropdown.has_value() && dropdown->is_open()) {
      dropdown_result(impl, window, *slot, dropdown->handle_text(ctx, event));
      return true;
    }
  }
  return false;
}

void report_focus_drift(AppImpl& impl, WindowImpl& window) {
  if (window.dialog.is_open() || window.menu.is_open() || any_dropdown_open(impl, window)) {
    return;  // popups move focus inside themselves; it comes back on close
  }
  const std::optional<dg::NodeId> current = window.focus.current();
  if (current == window.reported_focus) {
    return;
  }
  dg::FocusChange change;
  change.blurred = window.reported_focus;
  change.focused = current;
  report_focus(impl, window, change);
}

int clamp_timeout(const AppImpl& impl, int timeout_ms) {
  const std::int64_t now = dg::steady_anim_time().ms;
  std::int64_t wait = timeout_ms < 0 ? -1 : timeout_ms;
  const auto cut = [&wait](std::int64_t ms) {
    ms = std::max<std::int64_t>(ms, 0);
    wait = wait < 0 ? ms : std::min(wait, ms);
  };
  for (const std::unique_ptr<WindowImpl>& window : impl.windows) {
    if (window->closed) {
      continue;
    }
    if (const std::optional<dg::AnimTime> deadline = window->tooltip.next_deadline()) {
      cut(deadline->ms - now);
    }
  }
  for (const NodeSlot& slot : impl.nodes) {
    if (slot.state == NodeSlot::State::kLive && slot.type_id == DG_NODE_TYPE_PROGRESS &&
        slot.component != nullptr && slot.component->progress.indeterminate() &&
        !impl.windows[slot.window_index]->closed) {
      cut(kAnimationFrameMs);
    }
  }
  return static_cast<int>(wait);
}

void tick(AppImpl& impl, WindowImpl& window) {
  const dg::AnimTime now = dg::steady_anim_time();
  dg::UiContext ctx = ui(impl, window);
  (void)window.tooltip.tick(ctx, tooltip_target(impl, window), now);
  for (NodeSlot* slot : component_slots(impl, window)) {
    if (slot->type_id == DG_NODE_TYPE_PROGRESS && slot->component->progress.indeterminate()) {
      dg::ControlContext controls_ctx = controls(impl, window);
      (void)slot->component->progress.tick(controls_ctx, now);
    }
  }
}

void sync(AppImpl& impl, WindowImpl& window) {
  for (NodeSlot* slot : component_slots(impl, window)) {
    dg::ControlContext ctx = controls(impl, window);
    Component& c = *slot->component;
    switch (slot->type_id) {
      case DG_NODE_TYPE_PROGRESS:
        c.progress.sync(ctx);
        break;
      case DG_NODE_TYPE_TABS:
        c.tabs.sync(ctx);
        break;
      case DG_NODE_TYPE_TREE:
        c.tree.sync(ctx);
        break;
      case DG_NODE_TYPE_TABLE:
        c.table.sync(ctx);
        break;
      default:
        break;
    }
  }
}

void restyle(AppImpl& impl, WindowImpl& window) {
  for (NodeSlot* slot : component_slots(impl, window)) {
    Component& c = *slot->component;
    switch (slot->type_id) {
      case DG_NODE_TYPE_DROPDOWN:
        paint_dropdown(impl, window, *slot);
        break;
      case DG_NODE_TYPE_PROGRESS:
        build_progress(impl, window, *slot);
        break;
      case DG_NODE_TYPE_TABS:
        build_tabs(impl, window, *slot);
        break;
      case DG_NODE_TYPE_TREE:
        capture_tree(c);
        build_tree(impl, window, *slot, c.tree.selected());
        break;
      case DG_NODE_TYPE_TABLE:
        build_table(impl, window, *slot, c.table.selected());
        break;
      default:
        break;
    }
  }
}

void before_remove(AppImpl& impl, WindowImpl& window, const std::vector<dg::NodeId>& subtree) {
  const auto inside = [&subtree](dg::NodeId id) {
    return std::find(subtree.begin(), subtree.end(), id) != subtree.end();
  };
  dg::UiContext ctx = ui(impl, window);
  const std::optional<dg::NodeId> shown = window.tooltip.shown_for();
  if (shown.has_value() && inside(*shown)) {
    window.tooltip.hide(ctx);
  }
  for (NodeSlot& slot : impl.nodes) {
    if (slot.state != NodeSlot::State::kLive || slot.window_index != window.self_index ||
        !inside(slot.node_id)) {
      continue;
    }
    if (!slot.tooltip.empty()) {
      window.tooltip.unbind(slot.node_id);
    }
    if (slot.component != nullptr && slot.component->dropdown.has_value()) {
      // Its menu lives outside the subtree, under the tree root.
      slot.component->dropdown->close(ctx);
    }
    if (window.menu_anchor == slot.self_handle) {
      window.menu_anchor = nullptr;
    }
  }
}

void after_remove(AppImpl& impl) {
  for (NodeSlot& slot : impl.nodes) {
    if (slot.state == NodeSlot::State::kRemoved) {
      slot.component.reset();
      slot.tooltip.clear();
    }
  }
}

std::optional<std::int32_t> set_prop(AppImpl& impl, WindowImpl& window, NodeSlot& slot,
                                     std::uint16_t prop_id, const dg_value& value) {
  if (slot.type_id != DG_NODE_TYPE_PROGRESS ||
      (prop_id != DG_PROP_VALUE && prop_id != DG_PROP_INDETERMINATE)) {
    return std::nullopt;
  }
  dg::ControlContext ctx = controls(impl, window);
  dg::ProgressBar& bar = slot.component->progress;
  if (prop_id == DG_PROP_VALUE) {
    if (value.type != DG_VALUE_FLOAT) {
      return DG_ERR_TYPE_MISMATCH;
    }
    if (!std::isfinite(value.number) || value.number < 0.0F || value.number > 1.0F) {
      return DG_ERR_VALUE_OUT_OF_RANGE;
    }
    (void)bar.set_value(ctx, value.number);
    return DG_ERR_OK;
  }
  if (value.type != DG_VALUE_BOOL) {
    return DG_ERR_TYPE_MISMATCH;
  }
  if (value.bits > 1U) {
    return DG_ERR_VALUE_OUT_OF_RANGE;
  }
  bar.set_indeterminate(ctx, value.bits == 1U, dg::steady_anim_time());
  return DG_ERR_OK;
}

void after_prop(AppImpl& impl, WindowImpl& window, NodeSlot& slot, std::uint16_t prop_id) {
  if (prop_id != DG_PROP_HEIGHT || slot.component == nullptr) {
    return;
  }
  Component& c = *slot.component;
  if (slot.type_id == DG_NODE_TYPE_PROGRESS) {
    build_progress(impl, window, slot);
  } else if (slot.type_id == DG_NODE_TYPE_TREE) {
    capture_tree(c);
    build_tree(impl, window, slot, c.tree.selected());
  } else if (slot.type_id == DG_NODE_TYPE_TABLE) {
    build_table(impl, window, slot, c.table.selected());
  }
}

std::optional<std::int32_t> set_text(AppImpl& impl, WindowImpl& window, NodeSlot& slot,
                                     std::string_view text) {
  if (slot.type_id != DG_NODE_TYPE_DROPDOWN || slot.component == nullptr) {
    return std::nullopt;
  }
  slot.component->placeholder = std::string(text);
  paint_dropdown(impl, window, slot);
  return DG_ERR_OK;
}

dg::NodeId focus_target(const WindowImpl& window, const NodeSlot& slot) {
  if (slot.component == nullptr) {
    return slot.node_id;
  }
  const Component& c = *slot.component;
  switch (slot.type_id) {
    case DG_NODE_TYPE_TREE:
      return c.tree.root();
    case DG_NODE_TYPE_TABLE:
      return c.table.root();
    case DG_NODE_TYPE_TABS:
      if (c.tabs.selected().has_value()) {
        return c.tabs.tab_nodes()[static_cast<std::size_t>(*c.tabs.selected())];
      }
      return slot.node_id;
    default:
      (void)window;
      return slot.node_id;
  }
}

}  // namespace components

// -- Exported entry points -----------------------------------------------------

std::int32_t menu_open(dg_window_t* window_h, dg_node_t* anchor, std::int32_t x, std::int32_t y,
                       const char* const* labels, const std::uint32_t* flags,
                       std::uint32_t count) {
  WindowImpl* window = window_of(window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  AppImpl& impl = *app_of(window_h->app);
  if (labels == nullptr || count == 0 || count > kMaxMenuItems) {
    set_last_error("dg_menu_open: need 1..4096 labels");
    return DG_ERR_INVALID_ARGUMENT;
  }
  if (window->dialog.is_open()) {
    set_last_error("dg_menu_open: a dialog is open on this window");
    return DG_ERR_UNSUPPORTED;
  }
  std::optional<dg::PixelRect> anchor_rect;
  if (anchor != nullptr) {
    const NodeSlot* slot = slot_of(anchor);
    if (slot == nullptr) {
      return DG_ERR_INVALID_HANDLE;
    }
    if (anchor->app != window_h->app || slot->state != NodeSlot::State::kLive ||
        slot->window_index != window->self_index) {
      set_last_error("dg_menu_open: the anchor is not live in this window");
      return DG_ERR_INVALID_ARGUMENT;
    }
    anchor_rect = window->tree.render().absolute_bounds(slot->node_id);
  }
  dg::MenuSpec spec;
  bool any_selectable = false;
  for (std::uint32_t i = 0; i < count; ++i) {
    const std::uint32_t bits = flags == nullptr ? 0U : flags[i];
    dg::MenuItem item;
    item.text = text_or_empty(labels[i]);
    item.separator = (bits & DG_MENU_ITEM_SEPARATOR) != 0U;
    item.disabled = (bits & DG_MENU_ITEM_DISABLED) != 0U;
    if ((bits & (DG_MENU_ITEM_CHECKED | DG_MENU_ITEM_CHECKABLE)) != 0U) {
      item.checked = (bits & DG_MENU_ITEM_CHECKED) != 0U;
    }
    any_selectable = any_selectable || (!item.separator && !item.disabled);
    spec.items.push_back(std::move(item));
  }
  if (!any_selectable) {
    set_last_error("dg_menu_open: no item can be chosen");
    return DG_ERR_INVALID_ARGUMENT;
  }
  dg::UiContext ctx = ui(impl, *window);
  window->tooltip.hide(ctx);
  if (window->menu.is_open()) {
    window->menu.close(ctx);
    queue(impl, *window, DG_EVENT_MENU_DISMISS, window->menu_anchor, -1, window->menu_id);
  }
  const dg::PixelRect rect = anchor_rect.value_or(dg::PixelRect{x, y, 0, 0});
  spec.min_width = anchor_rect.has_value() ? anchor_rect->width : 0;
  if (!window->menu.open(ctx, rect, std::move(spec), window->focus.current())) {
    window->menu_id = 0;
    window->menu_anchor = nullptr;
    set_last_error("dg_menu_open: the popup could not be shown");
    return DG_ERR_UNSUPPORTED;
  }
  window->menu_id = impl.next_popup_id++;
  window->menu_anchor = anchor;
  return window->menu_id;
}

std::int32_t menu_close(dg_window_t* window_h) {
  WindowImpl* window = window_of(window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  AppImpl& impl = *app_of(window_h->app);
  dg::UiContext ctx = ui(impl, *window);
  window->menu.close(ctx);
  window->menu_id = 0;
  window->menu_anchor = nullptr;
  components::report_focus_drift(impl, *window);
  return DG_ERR_OK;
}

std::int32_t dropdown_set_items(dg_node_t* node_h, const char* const* items,
                                std::uint32_t count) {
  const Resolved r = resolve(node_h, DG_NODE_TYPE_DROPDOWN);
  if (r.status != DG_ERR_OK) {
    return r.status;
  }
  if ((items == nullptr && count > 0) || count > kMaxMenuItems) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  std::vector<std::string> options;
  options.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    options.push_back(text_or_empty(items[i]));
  }
  WindowImpl& window = *r.window;
  dg::UiContext ctx = ui(*r.impl, window);
  if (r.slot->component->dropdown.has_value()) {
    r.slot->component->dropdown->close(ctx);
  }
  dg::Widget widget = window.widgets.at(r.slot->node_id);
  widget.options = options;
  widget.selected_index.reset();
  window.widgets.attach(r.slot->node_id, widget);
  if (r.impl->fonts.has_value()) {
    window.widgets.dropdown_set_options(window.tree.render(), *r.impl->fonts, r.slot->node_id,
                                        std::move(options));
  }
  paint_dropdown(*r.impl, window, *r.slot);
  components::report_focus_drift(*r.impl, window);
  return DG_ERR_OK;
}

std::int32_t dropdown_get_selected(dg_node_t* node_h) {
  const Resolved r = resolve(node_h, DG_NODE_TYPE_DROPDOWN);
  if (r.status != DG_ERR_OK) {
    return r.status;
  }
  return r.window->widgets.dropdown_selected_index(r.slot->node_id).value_or(-1);
}

std::int32_t dropdown_set_selected(dg_node_t* node_h, std::int32_t index) {
  const Resolved r = resolve(node_h, DG_NODE_TYPE_DROPDOWN);
  if (r.status != DG_ERR_OK) {
    return r.status;
  }
  WindowImpl& window = *r.window;
  const dg::NodeId id = r.slot->node_id;
  const auto count = static_cast<std::int32_t>(window.widgets.dropdown_options(id).size());
  if (index < -1 || index >= count) {
    return DG_ERR_VALUE_OUT_OF_RANGE;
  }
  if (index >= 0 && r.impl->fonts.has_value()) {
    (void)window.widgets.dropdown_select(window.tree.render(), *r.impl->fonts, id, index);
    return DG_ERR_OK;
  }
  dg::Widget widget = window.widgets.at(id);
  widget.selected_index = index >= 0 ? std::optional<int>(index) : std::nullopt;
  window.widgets.attach(id, widget);
  paint_dropdown(*r.impl, window, *r.slot);
  return DG_ERR_OK;
}

std::int32_t node_set_tooltip(dg_node_t* node_h, const char* utf8) {
  NodeSlot* slot = slot_of(node_h);
  if (slot == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  if (slot->state != NodeSlot::State::kLive) {
    return DG_ERR_NO_WINDOW;
  }
  AppImpl& impl = *app_of(node_h->app);
  WindowImpl& window = *impl.windows[slot->window_index];
  slot->tooltip = text_or_empty(utf8);
  if (slot->tooltip.empty()) {
    window.tooltip.unbind(slot->node_id);
    if (window.tooltip.shown_for() == slot->node_id) {
      dg::UiContext ctx = ui(impl, window);
      window.tooltip.hide(ctx);
    }
    return DG_ERR_OK;
  }
  window.tooltip.bind(slot->node_id, slot->tooltip);
  return DG_ERR_OK;
}

std::int32_t app_set_tooltip_delay(dg_app_t* app, std::int32_t delay_ms) {
  AppImpl* impl = app_of(app);
  if (impl == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  if (delay_ms < 0 || delay_ms > kMaxTooltipDelayMs) {
    return DG_ERR_VALUE_OUT_OF_RANGE;
  }
  impl->tooltip_delay_ms = delay_ms;
  for (const std::unique_ptr<WindowImpl>& window : impl->windows) {
    if (window->closed || !window->popups.has_value()) {
      continue;
    }
    dg::UiContext ctx = ui(*impl, *window);
    window->tooltip.hide(ctx);
    window->tooltip = dg::Tooltip(delay_ms);
    for (const NodeSlot& slot : impl->nodes) {
      if (slot.state == NodeSlot::State::kLive && slot.window_index == window->self_index &&
          !slot.tooltip.empty()) {
        window->tooltip.bind(slot.node_id, slot.tooltip);
      }
    }
  }
  return DG_ERR_OK;
}

std::int32_t dialog_open(dg_window_t* window_h, const char* title, const char* body,
                         const char* const* buttons, std::uint32_t count,
                         std::int32_t default_button, std::int32_t cancel_button) {
  WindowImpl* window = window_of(window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  AppImpl& impl = *app_of(window_h->app);
  if (title == nullptr || (buttons == nullptr && count > 0) || count > kMaxDialogButtons) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  if (window->dialog.is_open()) {
    set_last_error("dg_dialog_open: a dialog is already open on this window");
    return DG_ERR_UNSUPPORTED;
  }
  dg::DialogSpec spec;
  spec.title = title;
  spec.body = text_or_empty(body);
  if (count > 0) {
    spec.buttons.clear();
    for (std::uint32_t i = 0; i < count; ++i) {
      spec.buttons.push_back(text_or_empty(buttons[i]));
    }
  }
  const auto total = static_cast<std::int32_t>(spec.buttons.size());
  if (default_button < -1 || default_button >= total || cancel_button < -1 ||
      cancel_button >= total) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  spec.default_button = default_button >= 0 ? std::optional<int>(default_button) : std::nullopt;
  spec.cancel_button = cancel_button >= 0 ? std::optional<int>(cancel_button) : std::nullopt;
  dg::UiContext ctx = ui(impl, *window);
  window->tooltip.hide(ctx);
  if (window->menu.is_open()) {
    window->menu.close(ctx);
    queue(impl, *window, DG_EVENT_MENU_DISMISS, window->menu_anchor, -1, window->menu_id);
    window->menu_id = 0;
    window->menu_anchor = nullptr;
  }
  for (NodeSlot* slot : component_slots(impl, *window)) {
    if (slot->component->dropdown.has_value()) {
      slot->component->dropdown->close(ctx);
    }
  }
  // Focus as the caller last saw it, restored on close.
  const std::optional<dg::NodeId> restore = window->reported_focus;
  (void)window->focus.set(restore);
  if (!window->dialog.open(ctx, std::move(spec), restore)) {
    set_last_error("dg_dialog_open: the popup could not be shown");
    return DG_ERR_UNSUPPORTED;
  }
  window->dialog_id = impl.next_popup_id++;
  return window->dialog_id;
}

std::int32_t dialog_close(dg_window_t* window_h) {
  WindowImpl* window = window_of(window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  AppImpl& impl = *app_of(window_h->app);
  dg::UiContext ctx = ui(impl, *window);
  window->dialog.close(ctx);
  window->dialog_id = 0;
  components::report_focus_drift(impl, *window);
  return DG_ERR_OK;
}

dg_node_t* tabs_add(dg_node_t* node_h, const char* label, std::int32_t disabled) {
  const Resolved r = resolve(node_h, DG_NODE_TYPE_TABS);
  if (r.status != DG_ERR_OK) {
    set_last_error("dg_tabs_add: not a live DG_NODE_TYPE_TABS node");
    return nullptr;
  }
  Component& c = *r.slot->component;
  if (c.tab_items.size() >= kMaxTabs) {
    set_last_error("dg_tabs_add: at most 64 tabs");
    return nullptr;
  }
  dg_node_t* panel = node_create(node_h->app, DG_NODE_TYPE_BOX);
  if (panel == nullptr) {
    return nullptr;
  }
  dg::TabItem item;
  item.label = text_or_empty(label);
  item.disabled = disabled != 0;
  c.tab_items.push_back(std::move(item));
  c.tab_panels.push_back(panel);
  // `r.slot` may move when node_create() grows the arena.
  NodeSlot& slot = *slot_of(node_h);
  WindowImpl& window = *r.window;
  build_tabs(*r.impl, window, slot);
  const dg::NodeId area = c.tabs.panel(static_cast<int>(c.tab_panels.size() - 1));
  const std::int32_t status = attach_node(*r.impl, slot.window_index, area, panel->index);
  if (status != DG_ERR_OK) {
    set_last_error("dg_tabs_add: could not attach the panel");
    return nullptr;
  }
  const dg::NodeId panel_id = slot_of(panel)->node_id;
  dg::BoxStyle box;
  box.kind = dg::LayoutKind::kColumn;
  box.grow = 1;
  window.tree.set_box(panel_id, box);
  window.tree.render().set_style(panel_id, dg::NodeStyle{});
  // The panel inherits its tab panel's hidden (disabled) state.
  window.widgets.refresh_subtree(window.tree.render(), panel_id, window.interaction);
  return panel;
}

std::int32_t tabs_get_selected(dg_node_t* node_h) {
  const Resolved r = resolve(node_h, DG_NODE_TYPE_TABS);
  if (r.status != DG_ERR_OK) {
    return r.status;
  }
  return r.slot->component->tabs.selected().value_or(-1);
}

std::int32_t tabs_set_selected(dg_node_t* node_h, std::int32_t index) {
  const Resolved r = resolve(node_h, DG_NODE_TYPE_TABS);
  if (r.status != DG_ERR_OK) {
    return r.status;
  }
  Component& c = *r.slot->component;
  if (index < 0 || static_cast<std::size_t>(index) >= c.tab_items.size() ||
      c.tab_items[static_cast<std::size_t>(index)].disabled) {
    return DG_ERR_VALUE_OUT_OF_RANGE;
  }
  dg::ControlContext ctx = controls(*r.impl, *r.window);
  (void)c.tabs.select(ctx, index);
  components::report_focus_drift(*r.impl, *r.window);
  return DG_ERR_OK;
}

std::int32_t tree_set_items(dg_node_t* node_h, const char* const* labels,
                            const std::int32_t* depths, const std::uint32_t* flags,
                            std::uint32_t count) {
  const Resolved r = resolve(node_h, DG_NODE_TYPE_TREE);
  if (r.status != DG_ERR_OK) {
    return r.status;
  }
  if ((count > 0 && (labels == nullptr || depths == nullptr)) || count > kMaxItems) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  std::vector<dg::TreeItem> items;
  items.reserve(count);
  int previous = -1;
  for (std::uint32_t i = 0; i < count; ++i) {
    const int depth = depths[i];
    if (depth < 0 || depth > previous + 1) {
      set_last_error("dg_tree_set_items: item " + std::to_string(i) +
                     " is more than one level deeper than the item before it");
      return DG_ERR_INVALID_ARGUMENT;
    }
    previous = depth;
    dg::TreeItem item;
    item.label = text_or_empty(labels[i]);
    item.depth = depth;
    item.expanded = flags != nullptr && (flags[i] & DG_TREE_ITEM_EXPANDED) != 0U;
    items.push_back(std::move(item));
  }
  r.slot->component->tree_items = std::move(items);
  build_tree(*r.impl, *r.window, *r.slot);
  return DG_ERR_OK;
}

std::int32_t tree_get_selected(dg_node_t* node_h) {
  const Resolved r = resolve(node_h, DG_NODE_TYPE_TREE);
  if (r.status != DG_ERR_OK) {
    return r.status;
  }
  return r.slot->component->tree.selected().value_or(-1);
}

std::int32_t tree_set_selected(dg_node_t* node_h, std::int32_t index) {
  const Resolved r = resolve(node_h, DG_NODE_TYPE_TREE);
  if (r.status != DG_ERR_OK) {
    return r.status;
  }
  Component& c = *r.slot->component;
  if (index < -1 || index >= static_cast<std::int32_t>(c.tree_items.size())) {
    return DG_ERR_VALUE_OUT_OF_RANGE;
  }
  if (index == -1) {
    // TreeView cannot deselect; a rebuild keeps expansion and drops it.
    capture_tree(c);
    build_tree(*r.impl, *r.window, *r.slot);
    return DG_ERR_OK;
  }
  dg::ControlContext ctx = controls(*r.impl, *r.window);
  (void)c.tree.select(ctx, index);
  return DG_ERR_OK;
}

std::int32_t tree_set_expanded(dg_node_t* node_h, std::int32_t index, std::int32_t expanded) {
  const Resolved r = resolve(node_h, DG_NODE_TYPE_TREE);
  if (r.status != DG_ERR_OK) {
    return r.status;
  }
  Component& c = *r.slot->component;
  if (index < 0 || index >= static_cast<std::int32_t>(c.tree_items.size())) {
    return DG_ERR_VALUE_OUT_OF_RANGE;
  }
  dg::ControlContext ctx = controls(*r.impl, *r.window);
  (void)c.tree.set_expanded(ctx, index, expanded != 0);
  return DG_ERR_OK;
}

std::int32_t table_set_columns(dg_node_t* node_h, const char* const* titles,
                               const float* widths, const std::uint32_t* units,
                               const std::uint32_t* flags, std::uint32_t count) {
  const Resolved r = resolve(node_h, DG_NODE_TYPE_TABLE);
  if (r.status != DG_ERR_OK) {
    return r.status;
  }
  if (titles == nullptr || count == 0 || count > kMaxColumns) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  std::vector<dg::TableColumn> columns;
  for (std::uint32_t i = 0; i < count; ++i) {
    dg::TableColumn column;
    column.title = text_or_empty(titles[i]);
    column.sortable = flags != nullptr && (flags[i] & DG_TABLE_COLUMN_SORTABLE) != 0U;
    const float width = widths == nullptr ? 0.0F : widths[i];
    const std::uint32_t unit = units == nullptr ? DG_LENGTH_UNIT_PX : units[i];
    if (!std::isfinite(width) || unit > DG_LENGTH_UNIT_PERCENT) {
      return DG_ERR_INVALID_ARGUMENT;
    }
    if (width > 0.0F) {
      if (unit == DG_LENGTH_UNIT_PERCENT) {
        column.width_percent = std::min(width, 100.0F);
      } else {
        column.width_px = static_cast<int>(std::lround(std::min(width, 1.0e6F)));
      }
    }
    columns.push_back(std::move(column));
  }
  Component& c = *r.slot->component;
  c.columns = std::move(columns);
  c.rows.clear();
  c.sort_column.reset();
  build_table(*r.impl, *r.window, *r.slot);
  return DG_ERR_OK;
}

std::int32_t table_set_cells(dg_node_t* node_h, const char* const* cells, std::uint32_t rows) {
  const Resolved r = resolve(node_h, DG_NODE_TYPE_TABLE);
  if (r.status != DG_ERR_OK) {
    return r.status;
  }
  Component& c = *r.slot->component;
  const std::size_t columns = c.columns.size();
  if (rows > 0 && (cells == nullptr || columns == 0 ||
                   static_cast<std::uint64_t>(rows) * columns > kMaxItems)) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  std::vector<std::vector<std::string>> data(rows);
  for (std::size_t row = 0; row < rows; ++row) {
    data[row].reserve(columns);
    for (std::size_t col = 0; col < columns; ++col) {
      data[row].push_back(text_or_empty(cells[(row * columns) + col]));
    }
  }
  c.rows = data;
  dg::ControlContext ctx = controls(*r.impl, *r.window);
  c.table.set_rows(ctx, std::move(data));
  return DG_ERR_OK;
}

std::int32_t table_get_selected(dg_node_t* node_h) {
  const Resolved r = resolve(node_h, DG_NODE_TYPE_TABLE);
  if (r.status != DG_ERR_OK) {
    return r.status;
  }
  return r.slot->component->table.selected().value_or(-1);
}

std::int32_t table_set_selected(dg_node_t* node_h, std::int32_t row) {
  const Resolved r = resolve(node_h, DG_NODE_TYPE_TABLE);
  if (r.status != DG_ERR_OK) {
    return r.status;
  }
  Component& c = *r.slot->component;
  if (row < -1 || row >= c.table.row_count()) {
    return DG_ERR_VALUE_OUT_OF_RANGE;
  }
  dg::ControlContext ctx = controls(*r.impl, *r.window);
  if (row == -1) {
    c.table.set_rows(ctx, c.rows);  // clears the selection
  } else {
    (void)c.table.select(ctx, row);
  }
  return DG_ERR_OK;
}

std::int32_t table_set_sort_indicator(dg_node_t* node_h, std::int32_t column,
                                      std::int32_t ascending) {
  const Resolved r = resolve(node_h, DG_NODE_TYPE_TABLE);
  if (r.status != DG_ERR_OK) {
    return r.status;
  }
  Component& c = *r.slot->component;
  if (column < -1 || column >= static_cast<std::int32_t>(c.columns.size())) {
    return DG_ERR_VALUE_OUT_OF_RANGE;
  }
  c.sort_column = column >= 0 ? std::optional<int>(column) : std::nullopt;
  c.sort_ascending = ascending != 0;
  dg::ControlContext ctx = controls(*r.impl, *r.window);
  c.table.set_sort_indicator(ctx, c.sort_column, c.sort_ascending);
  return DG_ERR_OK;
}

std::int32_t debug_node_count(dg_window_t* window_h) {
  WindowImpl* window = window_of(window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  return static_cast<std::int32_t>(
      dg::subtree_of(window->tree.render(), dg::RenderTree::root()).size());
}

}  // namespace dg::abi
