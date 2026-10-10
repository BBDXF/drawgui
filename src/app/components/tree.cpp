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
  for (std::size_t i = 0; i < state.items.size(); ++i) {
    state.items[i].expanded = state.tree.is_expanded(static_cast<int>(i));
  }
}

bool control_pointer(WinState& window, NodeRecord& record, TreeState& state,
                     const dg::PointerEvent& event) {
  dg::ControlContext ctx = controls(window);
  const dg::TreeResult r = state.tree.handle_pointer(ctx, event);
  tree_result(window, record, r);
  return r.event != dg::TreeEvent::kIgnored;
}

bool control_key(WinState& window, NodeRecord& record, TreeState& state,
                 const dg::KeyEvent& event) {
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

// Contract stub: tree check boxes land in a later phase.
Expected<bool, Status> TreeRef::row_checked(int /*index*/) const {
  return Unexpected(Status::kUnsupported);
}

}  // namespace dg
