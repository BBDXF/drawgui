// NodeKind::kTabs: a dg::Tabs strip whose panel areas each hold one
// caller-visible panel BOX created by Window::tabs_add().

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "app/components/components.h"
#include "app/internal.h"

namespace dg::app_detail {
namespace {

void tabs_result(WinState& window, const NodeRecord& record, const dg::TabsResult& r) {
  if (r.event == dg::TabsEvent::kSelected) {
    emit(window.app, dg::events::TabSelectEvent{window.sdl_window, record.id, r.index});
  }
}

}  // namespace

namespace components {

void build_tabs(WinState& window, NodeRecord& record) {
  TabsState& c = std::get<TabsState>(record.state);
  dg::ControlContext ctx = controls(window);
  const std::optional<int> selected = c.tabs.selected();
  const std::optional<dg::NodeId> focused = window.focus.current();
  const bool focus_on_tab =
      focused.has_value() && std::find(c.tabs.tab_nodes().begin(), c.tabs.tab_nodes().end(),
                                       *focused) != c.tabs.tab_nodes().end();
  // The caller's panels survive the rebuild: parked in the wrapper, then
  // moved into the rebuilt panel areas.
  for (const dg::NodeId panel : c.panels) {
    if (record_of(window, panel) != nullptr) {
      (void)window.tree.move_child(panel, record.id, std::nullopt);
    }
  }
  c.tabs.destroy(ctx);
  dg::TabsSpec spec;
  spec.tabs = c.items;
  spec.selected = selected.value_or(0);
  spec.box.percent.width = 100.0F;
  spec.box.percent.height = 100.0F;
  c.tabs.build(ctx, record.id, spec);
  for (std::size_t i = 0; i < c.panels.size(); ++i) {
    const dg::NodeId area = c.tabs.panel(static_cast<int>(i));
    dg::BoxStyle area_box = window.tree.box(area);
    area_box.cross_align = dg::CrossAlign::kStretch;
    window.tree.set_box(area, area_box);
    if (record_of(window, c.panels[i]) != nullptr) {
      (void)window.tree.move_child(c.panels[i], area, std::nullopt);
    }
  }
  const std::optional<int> now_selected = c.tabs.selected();
  if (focus_on_tab && now_selected.has_value()) {
    (void)window.focus.set(c.tabs.tab_nodes()[static_cast<std::size_t>(*now_selected)]);
    window.reported_focus = window.focus.current();
  }
  window.relaid = true;
}

bool control_pointer(WinState& window, NodeRecord& record, TabsState& state,
                     const dg::PointerEvent& event) {
  dg::ControlContext ctx = controls(window);
  const dg::TabsResult r = state.tabs.handle_pointer(ctx, event);
  tabs_result(window, record, r);
  return r.event != dg::TabsEvent::kIgnored;
}

bool control_key(WinState& window, NodeRecord& record, TabsState& state,
                 const dg::KeyEvent& event) {
  dg::ControlContext ctx = controls(window);
  const dg::TabsResult r = state.tabs.handle_key(ctx, event);
  tabs_result(window, record, r);
  return r.event != dg::TabsEvent::kIgnored;
}

void sync_state(WinState& window, NodeRecord& /*record*/, TabsState& state) {
  dg::ControlContext ctx = controls(window);
  state.tabs.sync(ctx);
}

void restyle_state(WinState& window, NodeRecord& record, TabsState& /*state*/) {
  build_tabs(window, record);
}

dg::NodeId focus_target_state(const NodeRecord& record, const TabsState& state) {
  if (state.tabs.selected().has_value()) {
    return state.tabs.tab_nodes()[static_cast<std::size_t>(*state.tabs.selected())];
  }
  return record.id;
}

}  // namespace components
}  // namespace dg::app_detail

namespace dg {

using app_detail::TabsState;
using app_detail::components::component_of;

Expected<NodeId, Status> TabsRef::add(std::string label, bool disabled) {
  app_detail::WinState& win = window();
  const auto found = component_of<TabsState>(win, node());
  if (found.status != Status::kOk) {
    return Unexpected(found.status);
  }
  TabsState& c = *found.state;
  if (c.items.size() >= limits::kMaxTabs) {
    return Unexpected(Status::kValueOutOfRange);
  }
  dg::TabItem item;
  item.label = std::move(label);
  item.disabled = disabled;
  c.items.push_back(std::move(item));
  c.panels.push_back(dg::NodeId{});
  app_detail::components::build_tabs(win, *found.record);
  const dg::NodeId area = c.tabs.panel(static_cast<int>(c.panels.size() - 1));
  const dg::NodeId panel_id = app_detail::attach_new(win, NodeKind::kBox, area);
  c.panels.back() = panel_id;
  dg::BoxStyle box;
  box.kind = dg::LayoutKind::kColumn;
  box.grow = 1;
  win.tree.set_box(panel_id, box);
  win.tree.render().set_style(panel_id, dg::NodeStyle{});
  // The panel inherits its tab panel's hidden (disabled) state.
  win.widgets.refresh_subtree(win.tree.render(), panel_id, win.interaction);
  return panel_id;
}

Expected<int, Status> TabsRef::selected() const {
  const auto found = component_of<TabsState>(std::as_const(window()), node());
  if (found.status != Status::kOk) {
    return Unexpected(found.status);
  }
  return found.state->tabs.selected().value_or(-1);
}

Status TabsRef::select(int index) {
  app_detail::WinState& win = window();
  const auto found = component_of<TabsState>(win, node());
  if (found.status != Status::kOk) {
    return found.status;
  }
  TabsState& c = *found.state;
  if (index < 0 || static_cast<std::size_t>(index) >= c.items.size() ||
      c.items[static_cast<std::size_t>(index)].disabled) {
    return Status::kValueOutOfRange;
  }
  dg::ControlContext ctx = app_detail::components::controls(win);
  (void)c.tabs.select(ctx, index);
  app_detail::report_focus_drift(win);
  return Status::kOk;
}

}  // namespace dg
