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

dg::Status tree_of(WinState& window, dg::NodeId id, NodeRecord*& out) {
  out = record_of(window, id);
  if (out == nullptr) {
    return dg::Status::kInvalidNode;
  }
  if (out->kind != NodeKind::kTree) {
    return dg::Status::kUnsupported;
  }
  return dg::Status::kOk;
}

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
  const FocusKeeper keeper(window, c.tree.root());
  c.tree.destroy(ctx);
  dg::TreeViewSpec spec;
  spec.items = c.items;
  spec.viewport_height = std::max(ctx.style.row_height,
                                  wrapper_height(window, record, kTreeHeight) - (2 * kFrame));
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

void sync_state(WinState& window, NodeRecord& /*record*/, TreeState& state) {
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

using app_detail::NodeRecord;
using app_detail::TreeState;

Status Window::tree_set_items(NodeId tree, std::vector<TreeItem> items) {
  app_detail::WinState& window = *state_;
  NodeRecord* record = nullptr;
  if (const Status status = app_detail::tree_of(window, tree, record); status != Status::kOk) {
    return status;
  }
  std::get<TreeState>(record->state).items = std::move(items);
  app_detail::components::build_tree(window, *record);
  return Status::kOk;
}

Expected<int, Status> Window::tree_selected(NodeId tree) const {
  const NodeRecord* record = app_detail::record_of(*state_, tree);
  if (record == nullptr) {
    return Unexpected(Status::kInvalidNode);
  }
  if (record->kind != NodeKind::kTree) {
    return Unexpected(Status::kUnsupported);
  }
  return std::get<TreeState>(record->state).tree.selected().value_or(-1);
}

Status Window::tree_select(NodeId tree, int index) {
  app_detail::WinState& window = *state_;
  NodeRecord* record = nullptr;
  if (const Status status = app_detail::tree_of(window, tree, record); status != Status::kOk) {
    return status;
  }
  TreeState& c = std::get<TreeState>(record->state);
  if (index < -1 || index >= static_cast<int>(c.items.size())) {
    return Status::kValueOutOfRange;
  }
  if (index == -1) {
    // TreeView cannot deselect; a rebuild keeps expansion and drops it.
    app_detail::components::capture_tree(c);
    app_detail::components::build_tree(window, *record);
    return Status::kOk;
  }
  dg::ControlContext ctx = app_detail::components::controls(window);
  (void)c.tree.select(ctx, index);
  return Status::kOk;
}

Status Window::tree_set_expanded(NodeId tree, int index, bool expanded) {
  app_detail::WinState& window = *state_;
  NodeRecord* record = nullptr;
  if (const Status status = app_detail::tree_of(window, tree, record); status != Status::kOk) {
    return status;
  }
  TreeState& c = std::get<TreeState>(record->state);
  if (index < 0 || index >= static_cast<int>(c.items.size())) {
    return Status::kValueOutOfRange;
  }
  dg::ControlContext ctx = app_detail::components::controls(window);
  (void)c.tree.set_expanded(ctx, index, expanded);
  return Status::kOk;
}

}  // namespace dg
