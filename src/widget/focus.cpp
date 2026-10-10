#include "drawgui/widget/focus.h"

#include <algorithm>
#include <cstdio>
#include <iterator>

namespace dg {
namespace {

void collect_focus_order(const RenderTree& tree, const WidgetSet& widgets, NodeId id,
                         std::vector<NodeId>& out) {
  // A disabled node takes its whole subtree out of Tab order (is_disabled()
  // inheritance), so the walk stops here rather than re-climbing per child.
  if (tree.style(id).disabled) {
    return;
  }
  if (widgets.has(id)) {
    const Widget& widget = widgets.at(id);
    if (takes_focus(widget) &&
        (!widget.tab_index.has_value() || *widget.tab_index >= 0) &&
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

// Degenerate case: `bounds` is too small for the requested gap+thickness on
// one or both axes (see update_focus_ring()'s own comment for the exact
// threshold). Rather than let from_edges() silently collapse only ONE strip
// while the other three go stale/inverted, every strip but `ring.top` is
// collapsed to empty and `ring.top` is set to the WHOLE of `local_bounds` -
// the target is entirely covered in ring color. This is chosen over
// collapsing all four (making a focused-but-tiny widget invisible) because a
// visible-but-not-annular indicator is still strictly more useful feedback
// than none; a caller that dislikes the full-cover look is free to widen the
// widget instead.
void fill_ring(RenderTree& tree, const FocusRing& ring, const PixelRect& local_bounds) {
  tree.set_local_bounds(ring.top, local_bounds);
  const PixelRect empty{};
  tree.set_local_bounds(ring.bottom, empty);
  tree.set_local_bounds(ring.left, empty);
  tree.set_local_bounds(ring.right, empty);
}

// One-shot stderr warning (a diagnostic, not a correction):
// a strip that lands entirely outside `tree`'s own viewport is a silent
// "drew nothing" bug the way the outward-ring geometry this replaced always
// was for a flush-edge target. This only OBSERVES - it never clamps a
// strip's bounds, because clamping would make the ring inconsistent (partly
// inside, partly clipped) depending on which side overflowed, which is worse
// than a uniformly-wrong-but-diagnosable ring.
void warn_if_ring_strip_outside_viewport(const RenderTree& tree, const PixelRect& strip_local,
                                         int local_to_absolute_dx, int local_to_absolute_dy) {
  static bool warned = false;
  if (warned || strip_local.is_empty()) {
    return;
  }
  const PixelRect strip_absolute =
      strip_local.offset_by(local_to_absolute_dx, local_to_absolute_dy);
  const PixelRect viewport_rect{0, 0, tree.viewport().width, tree.viewport().height};
  if (!intersects(strip_absolute, viewport_rect)) {
    std::fprintf(stderr,
                 "drawgui: focus ring strip at absolute (%d,%d,%d,%d) falls entirely outside "
                 "the tree viewport (%dx%d) - it will not paint any pixels\n",
                 strip_absolute.x, strip_absolute.y, strip_absolute.width, strip_absolute.height,
                 tree.viewport().width, tree.viewport().height);
    warned = true;
  }
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
  if (is_disabled(tree, scope_root)) {
    return ids;
  }
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

void Focus::enter_scope(NodeId root, bool modal) {
  scope_root_ = root;
  scope_modal_ = modal;
}

FocusChange Focus::exit_scope(const RenderTree& tree) {
  if (!scope_root_.has_value()) {
    return FocusChange{};
  }
  const NodeId root = *scope_root_;
  scope_root_.reset();
  scope_modal_ = false;
  if (focused_.has_value() && is_within(tree, root, *focused_)) {
    return set(std::nullopt);
  }
  return FocusChange{};
}

FocusChange Focus::set_guarded(const RenderTree& tree, std::optional<NodeId> target) {
  if (scope_modal_ && scope_root_.has_value() && target.has_value() &&
      !is_within(tree, *scope_root_, *target)) {
    // Refused: a modal
    // scope refuses a target OUTSIDE it rather than merely leaving it
    // unreachable by Tab (enter_scope()'s own non-modal shape, unchanged
    // above). Blurring to nothing (target == std::nullopt) is NOT refused -
    // see this method's own header comment for why.
    return FocusChange{};
  }
  if (target.has_value() && is_disabled(tree, *target)) {
    return FocusChange{};
  }
  return set(target);
}

FocusChange Focus::blur_if_disabled(const RenderTree& tree) {
  if (focused_.has_value() && is_disabled(tree, *focused_)) {
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

  // absolute -> parent-local: the exact inverse of RenderTree::Impl::
  // reposition()'s own forward transform (child.absolute = child.local.
  // offset_by(parent.absolute.xy - parent.scroll_offset.xy)). `parent` need
  // not be the tree root - an overlay popup's content_root
  // (src/window/popup_host.cpp) sits at a non-zero absolute origin, and a
  // scrollable parent has a non-zero scroll_offset too; both must be
  // undone here or the ring drifts by exactly that amount. Every existing
  // call site today passes a parent whose absolute origin and scroll_offset
  // are both (0, 0), making this a no-op - but it is not a no-op in
  // general, and that is the point.
  const PixelRect parent_absolute = tree.absolute_bounds(parent);
  const PixelPoint parent_scroll = tree.scroll_offset(parent);
  const int dx = parent_scroll.x - parent_absolute.x;
  const int dy = parent_scroll.y - parent_absolute.y;

  // Inward ring geometry ("why not outward"):
  // the ring's outermost edge is inset `gap` pixels from `bounds`'s own
  // edge, and occupies `thickness` more pixels further in. Unlike the
  // outward geometry this replaced - which drew the ring OUTSIDE `bounds`
  // and therefore depended on unstated margin existing around the target -
  // this geometry is self-contained: it only ever touches pixels inside
  // `bounds`, so it is visible in full regardless of what (if anything)
  // surrounds the target.
  const int inset = gap + thickness;
  if (bounds.width < 2 * inset || bounds.height < 2 * inset) {
    // Degenerate: the two insets would meet or cross on at least one axis.
    // See fill_ring()'s own comment for why this fills rather than
    // collapses.
    PixelRect local = bounds.offset_by(dx, dy);
    fill_ring(tree, ring, local);
    return;
  }

  const PixelRect outer = bounds.inflated_by(-gap);
  const PixelRect inner = bounds.inflated_by(-inset);

  const PixelRect top_abs =
      PixelRect::from_edges(outer.left(), outer.top(), outer.right(), inner.top());
  const PixelRect bottom_abs =
      PixelRect::from_edges(outer.left(), inner.bottom(), outer.right(), outer.bottom());
  const PixelRect left_abs =
      PixelRect::from_edges(outer.left(), inner.top(), inner.left(), inner.bottom());
  const PixelRect right_abs =
      PixelRect::from_edges(inner.right(), inner.top(), outer.right(), inner.bottom());

  const PixelRect top_local = top_abs.offset_by(dx, dy);
  const PixelRect bottom_local = bottom_abs.offset_by(dx, dy);
  const PixelRect left_local = left_abs.offset_by(dx, dy);
  const PixelRect right_local = right_abs.offset_by(dx, dy);

  tree.set_local_bounds(ring.top, top_local);
  tree.set_local_bounds(ring.bottom, bottom_local);
  tree.set_local_bounds(ring.left, left_local);
  tree.set_local_bounds(ring.right, right_local);

  warn_if_ring_strip_outside_viewport(tree, top_local, -dx, -dy);
  warn_if_ring_strip_outside_viewport(tree, bottom_local, -dx, -dy);
  warn_if_ring_strip_outside_viewport(tree, left_local, -dx, -dy);
  warn_if_ring_strip_outside_viewport(tree, right_local, -dx, -dy);
}

}  // namespace dg
