#include "drawgui/widget/focus.h"

#include <algorithm>
#include <iterator>

namespace dg {
namespace {

void collect_focus_order(const RenderTree& tree, const WidgetSet& widgets, NodeId id,
                         std::vector<NodeId>& out) {
  if (widgets.has(id)) {
    const Widget& widget = widgets.at(id);
    if (is_focusable(widget.kind) && !(widget.tab_index.has_value() && *widget.tab_index < 0) &&
        !tree.absolute_bounds(id).is_empty()) {
      out.push_back(id);
    }
  }
  for (const NodeId child : tree.children(id)) {
    collect_focus_order(tree, widgets, child, out);
  }
}

// The comparison focus_order() sorts by: positive-tab_index widgets first
// (ascending), then everything else, ties broken by leaving std::stable_sort
// to preserve the tree-order collect_focus_order() already produced.
bool tab_index_precedes(const WidgetSet& widgets, NodeId a, NodeId b) {
  const std::optional<int> ta = widgets.at(a).tab_index;
  const std::optional<int> tb = widgets.at(b).tab_index;
  const bool positive_a = ta.has_value() && *ta > 0;
  const bool positive_b = tb.has_value() && *tb > 0;
  if (positive_a != positive_b) {
    return positive_a;
  }
  if (positive_a && positive_b) {
    return *ta < *tb;
  }
  return false;
}

void collapse_ring(RenderTree& tree, const FocusRing& ring) {
  const PixelRect empty{};
  tree.set_local_bounds(ring.top, empty);
  tree.set_local_bounds(ring.bottom, empty);
  tree.set_local_bounds(ring.left, empty);
  tree.set_local_bounds(ring.right, empty);
}

}  // namespace

bool is_within(const RenderTree& tree, NodeId scope_root, NodeId id) {
  NodeId current = id;
  while (true) {
    if (current == scope_root) {
      return true;
    }
    const NodeId parent = tree.parent(current);
    if (parent == current) {
      // Reached the root (RenderTree::parent()'s own "the root is its own
      // parent" sentinel) without ever matching scope_root.
      return false;
    }
    current = parent;
  }
}

std::vector<NodeId> focus_order(const RenderTree& tree, const WidgetSet& widgets,
                                NodeId scope_root) {
  std::vector<NodeId> ids;
  collect_focus_order(tree, widgets, scope_root, ids);
  std::stable_sort(ids.begin(), ids.end(), [&widgets](NodeId a, NodeId b) {
    return tab_index_precedes(widgets, a, b);
  });
  return ids;
}

FocusChange Focus::set(std::optional<NodeId> target) {
  FocusChange change;
  if (focused_ == target) {
    return change;
  }
  change.blurred = focused_;
  change.focused = target;
  focused_ = target;
  return change;
}

FocusChange Focus::focus_next(const RenderTree& tree, const WidgetSet& widgets,
                              NodeId default_root) {
  const std::vector<NodeId> order = focus_order(tree, widgets, scope_root(default_root));
  if (order.empty()) {
    return set(std::nullopt);
  }
  if (!focused_.has_value()) {
    return set(order.front());
  }
  const auto it = std::find(order.begin(), order.end(), *focused_);
  if (it == order.end()) {
    return set(order.front());
  }
  auto next_it = std::next(it);
  if (next_it == order.end()) {
    next_it = order.begin();
  }
  return set(*next_it);
}

FocusChange Focus::focus_previous(const RenderTree& tree, const WidgetSet& widgets,
                                  NodeId default_root) {
  const std::vector<NodeId> order = focus_order(tree, widgets, scope_root(default_root));
  if (order.empty()) {
    return set(std::nullopt);
  }
  if (!focused_.has_value()) {
    return set(order.back());
  }
  const auto it = std::find(order.begin(), order.end(), *focused_);
  if (it == order.end()) {
    return set(order.back());
  }
  auto prev_it = it == order.begin() ? std::prev(order.end()) : std::prev(it);
  return set(*prev_it);
}

void Focus::enter_scope(NodeId root) { scope_root_ = root; }

FocusChange Focus::exit_scope(const RenderTree& tree) {
  if (!scope_root_.has_value()) {
    return FocusChange{};
  }
  const NodeId root = *scope_root_;
  scope_root_.reset();
  if (focused_.has_value() && is_within(tree, root, *focused_)) {
    return set(std::nullopt);
  }
  return FocusChange{};
}

FocusChange Focus::blur_if_any_of(std::span<const NodeId> recycled) {
  if (!focused_.has_value()) {
    return FocusChange{};
  }
  for (const NodeId id : recycled) {
    if (id == *focused_) {
      return set(std::nullopt);
    }
  }
  return FocusChange{};
}

void update_focus_ring(RenderTree& tree, NodeId parent, FocusRing& ring,
                       std::optional<NodeId> target, Color ring_color, int thickness, int gap) {
  if (!ring.created) {
    NodeStyle style;
    style.fill = ring_color;
    const PixelRect empty{};
    ring.top = tree.add_child(parent, empty, style);
    ring.bottom = tree.add_child(parent, empty, style);
    ring.left = tree.add_child(parent, empty, style);
    ring.right = tree.add_child(parent, empty, style);
    ring.created = true;
  }

  if (!target.has_value()) {
    collapse_ring(tree, ring);
    return;
  }

  const PixelRect bounds = tree.absolute_bounds(*target);
  if (bounds.is_empty()) {
    collapse_ring(tree, ring);
    return;
  }

  const PixelRect outer = bounds.inflated_by(gap + thickness);
  const PixelRect inner = bounds.inflated_by(gap);

  tree.set_local_bounds(
      ring.top, PixelRect::from_edges(outer.left(), outer.top(), outer.right(), inner.top()));
  tree.set_local_bounds(ring.bottom, PixelRect::from_edges(outer.left(), inner.bottom(),
                                                           outer.right(), outer.bottom()));
  tree.set_local_bounds(
      ring.left, PixelRect::from_edges(outer.left(), inner.top(), inner.left(), inner.bottom()));
  tree.set_local_bounds(ring.right, PixelRect::from_edges(inner.right(), inner.top(),
                                                          outer.right(), inner.bottom()));
}

}  // namespace dg
