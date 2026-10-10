// NodeKind::kTree: a dg::TreeView built from the items
// Window::tree_set_items() copied, rebuilt with its expansion kept.

#include <algorithm>
#include <cstddef>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include "app/components/components.h"
#include "app/components/focus_keeper.h"
#include "app/internal.h"

namespace dg::app_detail {
namespace {

constexpr int kTreeHeight = 240;
constexpr int kFrame = 1;  // control_visuals' tree/table outer border

void tree_result(WinState& window, const NodeRecord& record, const dg::TreeResult& r) {
  using Kind = dg::events::TreeEvent::Kind;
  if (r.event == dg::TreeEvent::kSelected) {
    emit(window.app,
         dg::events::TreeEvent{Kind::kSelect, window.sdl_window, record.id, r.item});
  } else if (r.event == dg::TreeEvent::kExpanded) {
    emit(window.app,
         dg::events::TreeEvent{Kind::kExpand, window.sdl_window, record.id, r.item});
  } else if (r.event == dg::TreeEvent::kCollapsed) {
    emit(window.app,
         dg::events::TreeEvent{Kind::kCollapse, window.sdl_window, record.id, r.item});
  }
}

std::uint64_t key_at(const TreeState& state, int item) {
  const std::vector<dg::TreeItem>& items = state.tree.items();
  return item >= 0 && static_cast<std::size_t>(item) < items.size()
             ? items[static_cast<std::size_t>(item)].key
             : 0;
}

}  // namespace

namespace components {

void build_tree(WinState& window, NodeRecord& record, std::optional<int> select) {
  TreeState& c = std::get<TreeState>(record.state);
  dg::ControlContext ctx = controls(window);
  c.built_height = laid_out_height(window, record, kTreeHeight);
  const FocusKeeper keeper(window, c.tree.root());
  c.tree.destroy(ctx);
  dg::TreeViewSpec spec;
  spec.items = c.items;
  spec.viewport_height = std::max(ctx.style.row_height,
                                  c.built_height - (2 * kFrame));
  spec.box.percent.width = 100.0F;
  c.tree.build(ctx, record.id, std::move(spec));
  if (select.has_value()) {
    (void)c.tree.select(ctx, *select);
  }
  keeper.restore(window, c.tree.root());
  window.relaid = true;
}

void capture_tree(TreeState& state) {
  const std::vector<dg::TreeItem>& live = state.tree.items();
  for (std::size_t i = 0; i < state.items.size(); ++i) {
    state.items[i].expanded = state.tree.is_expanded(static_cast<int>(i));
    if (i < live.size()) {
      state.items[i].check = live[i].check;
    }
  }
}

bool control_pointer(WinState& window, NodeRecord& record, TreeState& state,
                     const dg::PointerEvent& event) {
  dg::ControlContext ctx = controls(window);
  const bool press =
      event.action == dg::PointerAction::kDown && event.button == dg::PointerButton::kPrimary;
  const dg::PixelPoint at{event.x, event.y};
  if (press && !dg::is_disabled(ctx.tree(), record.id) &&
      ctx.tables.widgets.widget_at(ctx.tree(), at) == state.tree.root()) {
    // A check box beats the row: it toggles without selecting.
    const std::optional<dg::TreeView::Hit> hit = state.tree.hit(ctx, at);
    if (hit.has_value() && hit->check && state.tree.toggle_check(ctx, hit->item)) {
      ctx.tables.focus.set_guarded(ctx.tree(), state.tree.root());
      emit(window.app, dg::events::RowPartEvent{window.sdl_window, record.id, hit->item, -1,
                                                dg::events::RowPartEvent::Part::kCheck, -1,
                                                key_at(state, hit->item)});
      return true;
    }
  }
  const dg::TreeResult r = state.tree.handle_pointer(ctx, event);
  tree_result(window, record, r);
  if (press && r.item >= 0) {
    emit(window.app, dg::events::ClickEvent{window.sdl_window, record.id, event.x, event.y,
                                            std::max(event.clicks, 1), r.item, std::nullopt,
                                            key_at(state, r.item)});
  }
  return r.event != dg::TreeEvent::kIgnored;
}

bool control_key(WinState& window, NodeRecord& record, TreeState& state,
                 const dg::KeyEvent& event) {
  // Space toggles the selected row's check box.
  if (event.logical_key == dg::LogicalKey::kSpace && event.mods == dg::Modifier::kNone &&
      event.action == dg::KeyAction::kDown && !dg::is_disabled(window.tree.render(), record.id) &&
      toggle_tree_row_check(window, record)) {
    return true;
  }
  dg::ControlContext ctx = controls(window);
  const dg::TreeResult r = state.tree.handle_key(ctx, event);
  tree_result(window, record, r);
  return r.event != dg::TreeEvent::kIgnored;
}

void sync_state(WinState& window, NodeRecord& record, TreeState& state) {
  if (laid_out_height(window, record, kTreeHeight) != state.built_height) {
    capture_tree(state);
    build_tree(window, record, state.tree.selected());
    return;
  }
  dg::ControlContext ctx = controls(window);
  state.tree.sync(ctx);
}

void restyle_state(WinState& window, NodeRecord& record, TreeState& state) {
  capture_tree(state);
  build_tree(window, record, state.tree.selected());
}

void after_prop_state(WinState& window, NodeRecord& record, TreeState& state,
                      std::uint16_t prop_id) {
  if (prop_id == DG_PROP_HEIGHT) {
    capture_tree(state);
    build_tree(window, record, state.tree.selected());
  }
}

dg::NodeId focus_target_state(const NodeRecord& /*record*/, const TreeState& state) {
  return state.tree.root();
}

}  // namespace components

RowHit tree_context_row(WinState& window, NodeRecord& record, dg::PixelPoint at) {
  TreeState* state = std::get_if<TreeState>(&record.state);
  if (state == nullptr) {
    return {};
  }
  dg::ControlContext ctx = components::controls(window);
  const std::optional<dg::TreeView::Hit> hit = state->tree.hit(ctx, at);
  if (!hit.has_value()) {
    return {};
  }
  if (state->tree.select(ctx, hit->item)) {
    tree_result(window, record, dg::TreeResult{dg::TreeEvent::kSelected, hit->item});
  }
  return RowHit{hit->item, std::nullopt, key_at(*state, hit->item)};
}

namespace {

bool toggle_tree_selected(WinState& window, NodeRecord& record, TreeState& state) {
  dg::ControlContext ctx = components::controls(window);
  const std::optional<int> item = state.tree.selected();
  if (!item.has_value() || window.focus.current() != state.tree.root() ||
      !state.tree.toggle_check(ctx, *item)) {
    return false;
  }
  emit(window.app, dg::events::RowPartEvent{window.sdl_window, record.id, *item, -1,
                                            dg::events::RowPartEvent::Part::kCheck, -1,
                                            key_at(state, *item)});
  return true;
}

}  // namespace

bool toggle_tree_row_check(WinState& window, NodeRecord& record) {
  TreeState* state = std::get_if<TreeState>(&record.state);
  return state != nullptr && toggle_tree_selected(window, record, *state);
}

}  // namespace dg::app_detail

namespace dg {

using app_detail::TreeState;
using app_detail::components::component_of;

Status TreeRef::set_items(std::vector<TreeEntry> items) {
  app_detail::WinState& win = window();
  const auto found = component_of<TreeState>(win, node());
  if (found.status != Status::kOk) {
    return found.status;
  }
  if (items.size() > limits::kMaxDataItems) {
    return Status::kInvalidArgument;
  }
  std::vector<dg::TreeItem> converted;
  converted.reserve(items.size());
  int previous = -1;
  for (TreeEntry& entry : items) {
    if (entry.depth < 0 || entry.depth > previous + 1) {
      return Status::kInvalidArgument;
    }
    previous = entry.depth;
    dg::TreeItem item;
    item.label = std::move(entry.label);
    item.depth = entry.depth;
    item.expanded = entry.expanded;
    item.icon = entry.icon;
    item.secondary = std::move(entry.secondary);
    item.check = entry.check;
    item.key = entry.key;
    converted.push_back(std::move(item));
  }
  found.state->items = std::move(converted);
  app_detail::components::build_tree(win, *found.record);
  return Status::kOk;
}

Expected<int, Status> TreeRef::selected() const {
  const auto found = component_of<TreeState>(std::as_const(window()), node());
  if (found.status != Status::kOk) {
    return Unexpected(found.status);
  }
  return found.state->tree.selected().value_or(-1);
}

Status TreeRef::select(int index) {
  app_detail::WinState& win = window();
  const auto found = component_of<TreeState>(win, node());
  if (found.status != Status::kOk) {
    return found.status;
  }
  TreeState& c = *found.state;
  if (index < -1 || index >= static_cast<int>(c.items.size())) {
    return Status::kValueOutOfRange;
  }
  if (index == -1) {
    // TreeView cannot deselect; a rebuild keeps expansion and drops it.
    app_detail::components::capture_tree(c);
    app_detail::components::build_tree(win, *found.record);
    return Status::kOk;
  }
  dg::ControlContext ctx = app_detail::components::controls(win);
  (void)c.tree.select(ctx, index);
  return Status::kOk;
}

Status TreeRef::set_expanded(int index, bool expanded) {
  app_detail::WinState& win = window();
  const auto found = component_of<TreeState>(win, node());
  if (found.status != Status::kOk) {
    return found.status;
  }
  TreeState& c = *found.state;
  if (index < 0 || index >= static_cast<int>(c.items.size())) {
    return Status::kValueOutOfRange;
  }
  dg::ControlContext ctx = app_detail::components::controls(win);
  (void)c.tree.set_expanded(ctx, index, expanded);
  return Status::kOk;
}

}  // namespace dg

namespace dg {

Expected<bool, Status> TreeRef::row_checked(int index) const {
  const auto found = component_of<TreeState>(std::as_const(window()), node());
  if (found.status != Status::kOk) {
    return Unexpected(found.status);
  }
  const std::vector<dg::TreeItem>& items = found.state->tree.items();
  if (index < 0 || static_cast<std::size_t>(index) >= items.size()) {
    return Unexpected(Status::kValueOutOfRange);
  }
  const std::optional<bool> check = items[static_cast<std::size_t>(index)].check;
  if (!check.has_value()) {
    return Unexpected(Status::kNotApplicable);
  }
  return *check;
}

}  // namespace dg

namespace dg {

Expected<std::uint64_t, Status> TreeRef::key_of(int index) const {
  const auto found = component_of<TreeState>(std::as_const(window()), node());
  if (found.status != Status::kOk) {
    return Unexpected(found.status);
  }
  if (index < 0 || static_cast<std::size_t>(index) >= found.state->tree.items().size()) {
    return Unexpected(Status::kValueOutOfRange);
  }
  return found.state->tree.items()[static_cast<std::size_t>(index)].key;
}

Expected<int, Status> TreeRef::index_of(std::uint64_t key) const {
  const auto found = component_of<TreeState>(std::as_const(window()), node());
  if (found.status != Status::kOk) {
    return Unexpected(found.status);
  }
  if (key == 0) {
    return Unexpected(Status::kInvalidArgument);
  }
  const std::vector<dg::TreeItem>& items = found.state->tree.items();
  for (std::size_t i = 0; i < items.size(); ++i) {
    if (items[i].key == key) {
      return static_cast<int>(i);
    }
  }
  return Unexpected(Status::kUnknownId);
}

Status TreeRef::set_row_keys(std::vector<std::uint64_t> keys) {
  app_detail::WinState& win = window();
  const auto found = component_of<TreeState>(win, node());
  if (found.status != Status::kOk) {
    return found.status;
  }
  TreeState& c = *found.state;
  if (keys.size() != c.items.size()) {
    return Status::kInvalidArgument;
  }
  app_detail::components::capture_tree(c);
  for (std::size_t i = 0; i < keys.size(); ++i) {
    c.items[i].key = keys[i];
  }
  app_detail::components::build_tree(win, *found.record, c.tree.selected());
  return Status::kOk;
}

Status TreeRef::select_key(std::uint64_t key) {
  const Expected<int, Status> index = index_of(key);
  return index ? select(index.value()) : index.error();
}

}  // namespace dg
