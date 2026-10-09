#include "drawgui/ui/tabs.h"

#include "drawgui/widget/focus.h"
#include "drawgui/widget/interaction.h"
#include "drawgui/widget/widget_set.h"
#include "ui/control_visuals.h"

namespace dg {

void Tabs::build(ControlContext& ctx, NodeId parent, const TabsSpec& spec) {
  destroy(ctx);
  control_visuals::TabsNodes nodes = control_visuals::build_tabs(ctx, parent, spec);
  root_ = nodes.root;
  host_ = nodes.host;
  tabs_ = std::move(nodes.tabs);
  labels_ = std::move(nodes.labels);
  indicators_ = std::move(nodes.indicators);
  panels_ = std::move(nodes.panels);
  items_ = spec.tabs;
  built_ = true;
  selected_.reset();
  if (selectable(spec.selected)) {
    selected_ = spec.selected;
  } else {
    selected_ = step_from(-1, 1);
  }
  apply_selection(ctx);
}

void Tabs::destroy(ControlContext& ctx) {
  if (built_ && ctx.tree().is_valid(root_)) {
    remove_subtree(ctx.layout, root_, ctx.tables);
  }
  built_ = false;
  tabs_.clear();
  labels_.clear();
  indicators_.clear();
  panels_.clear();
  items_.clear();
  selected_.reset();
}

NodeId Tabs::panel(int index) const {
  if (index < 0 || static_cast<std::size_t>(index) >= panels_.size()) {
    return RenderTree::root();
  }
  return panels_[static_cast<std::size_t>(index)];
}

std::optional<int> Tabs::tab_index_of(NodeId id) const {
  for (std::size_t i = 0; i < tabs_.size(); ++i) {
    if (tabs_[i] == id) {
      return static_cast<int>(i);
    }
  }
  return std::nullopt;
}

bool Tabs::selectable(int index) const {
  return index >= 0 && static_cast<std::size_t>(index) < items_.size() &&
         !items_[static_cast<std::size_t>(index)].disabled;
}

std::optional<int> Tabs::step_from(int from, int step) const {
  const int count = static_cast<int>(items_.size());
  for (int i = 1; i <= count; ++i) {
    const int index = (((from + (i * step)) % count) + count) % count;
    if (selectable(index)) {
      return index;
    }
  }
  return std::nullopt;
}

bool Tabs::select(ControlContext& ctx, int index) {
  if (!built_ || !selectable(index) || selected_ == index) {
    return false;
  }
  selected_ = index;
  apply_selection(ctx);
  return true;
}

void Tabs::apply_selection(ControlContext& ctx) {
  RenderTree& tree = ctx.tree();
  WidgetSet& widgets = ctx.tables.widgets;
  for (std::size_t i = 0; i < tabs_.size(); ++i) {
    const bool active = selected_ == static_cast<int>(i);
    // Roving tab stop: only the selected tab is in the Tab sequence.
    Widget widget = widgets.at(tabs_[i]);
    const std::optional<int> tab_index = active ? std::nullopt : std::optional<int>(-1);
    if (widget.tab_index != tab_index) {
      widget.tab_index = tab_index;
      widgets.attach(tabs_[i], widget);
    }
    control_visuals::paint_tab(ctx, labels_[i], indicators_[i], items_[i], active);

    const NodeId panel = panels_[i];
    NodeStyle style = tree.style(panel);
    if (style.disabled == active) {
      style.disabled = !active;
      tree.set_style(panel, style);
    }
    if (active) {
      if (!panel_rect_.is_empty() && tree.local_bounds(panel) != panel_rect_) {
        tree.set_local_bounds(panel, panel_rect_);
      }
      widgets.refresh_subtree(tree, panel, ctx.tables.interaction);
    } else if (!tree.local_bounds(panel).is_empty()) {
      tree.set_local_bounds(panel, PixelRect{});
    }
  }
  ctx.tables.focus.blur_if_disabled(tree);
}

void Tabs::sync(ControlContext& ctx) {
  if (!built_) {
    return;
  }
  // Layout gives every panel the same full rect; remember it for the panel
  // a later select() shows, then collapse the hidden ones again.
  for (const NodeId panel : panels_) {
    const PixelRect rect = ctx.tree().local_bounds(panel);
    if (!rect.is_empty()) {
      panel_rect_ = rect;
      break;
    }
  }
  apply_selection(ctx);
}

TabsResult Tabs::handle_pointer(ControlContext& ctx, const PointerEvent& event) {
  if (!built_ || event.action != PointerAction::kDown ||
      event.button != PointerButton::kPrimary) {
    return {};
  }
  const std::optional<NodeId> owner =
      ctx.tables.widgets.widget_at(ctx.tree(), PixelPoint{event.x, event.y});
  const std::optional<int> index = owner.has_value() ? tab_index_of(*owner) : std::nullopt;
  if (!index.has_value() || !selectable(*index)) {
    return {};
  }
  ctx.tables.focus.set_guarded(ctx.tree(), tabs_[static_cast<std::size_t>(*index)]);
  if (select(ctx, *index)) {
    return TabsResult{TabsEvent::kSelected, *index};
  }
  return TabsResult{TabsEvent::kHandled, *index};
}

TabsResult Tabs::handle_key(ControlContext& ctx, const KeyEvent& event) {
  if (!built_ || event.action != KeyAction::kDown) {
    return {};
  }
  const std::optional<NodeId> focused = ctx.tables.focus.current();
  const std::optional<int> current =
      focused.has_value() ? tab_index_of(*focused) : std::nullopt;
  if (!current.has_value()) {
    return {};
  }
  std::optional<int> target;
  switch (event.key) {
    case Key::kLeft:
      target = step_from(*current, -1);
      break;
    case Key::kRight:
      target = step_from(*current, 1);
      break;
    case Key::kHome:
      target = step_from(-1, 1);
      break;
    case Key::kEnd:
      target = step_from(static_cast<int>(items_.size()), -1);
      break;
    case Key::kEnter:
      target = current;
      break;
    default:
      return {};
  }
  if (!target.has_value()) {
    return TabsResult{TabsEvent::kHandled, -1};
  }
  ctx.tables.focus.set_guarded(ctx.tree(), tabs_[static_cast<std::size_t>(*target)]);
  if (select(ctx, *target)) {
    return TabsResult{TabsEvent::kSelected, *target};
  }
  return TabsResult{TabsEvent::kHandled, *target};
}

}  // namespace dg
