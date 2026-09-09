#include "drawgui/widget/widget_set.h"

#include <algorithm>
#include <cstddef>
#include <optional>

namespace dg {
namespace {

bool interactive(WidgetKind kind) {
  switch (kind) {
    case WidgetKind::kButton:
    case WidgetKind::kCheckbox:
      return true;
    case WidgetKind::kPanel:
    case WidgetKind::kLabel:
    case WidgetKind::kScrollView:
      break;
  }
  return false;
}

}  // namespace

void WidgetSet::attach(NodeId id, const Widget& widget) {
  if (widgets_.size() <= id.value) {
    widgets_.resize(static_cast<std::size_t>(id.value) + 1);
  }
  widgets_[id.value] = widget;
}

const Widget* WidgetSet::find(NodeId id) const {
  if (id.value >= widgets_.size()) {
    return nullptr;
  }
  const std::optional<Widget>& slot = widgets_[id.value];
  return slot.has_value() ? &slot.value() : nullptr;
}

Widget* WidgetSet::find(NodeId id) {
  if (id.value >= widgets_.size()) {
    return nullptr;
  }
  std::optional<Widget>& slot = widgets_[id.value];
  return slot.has_value() ? &slot.value() : nullptr;
}

bool WidgetSet::has(NodeId id) const {
  return find(id) != nullptr;
}

std::size_t WidgetSet::count() const {
  std::size_t total = 0;
  for (const std::optional<Widget>& widget : widgets_) {
    total += widget.has_value() ? std::size_t{1} : std::size_t{0};
  }
  return total;
}

const Widget& WidgetSet::at(NodeId id) const {
  return *find(id);
}

bool WidgetSet::accepts_pointer(NodeId id) const {
  const Widget* widget = find(id);
  return widget != nullptr && interactive(widget->kind);
}

bool WidgetSet::is_checked(NodeId id) const {
  const Widget* widget = find(id);
  return widget != nullptr && widget->checked;
}

bool WidgetSet::toggle(NodeId id) {
  Widget* widget = find(id);
  if (widget == nullptr || widget->kind != WidgetKind::kCheckbox) {
    return false;
  }
  widget->checked = !widget->checked;
  return widget->checked;
}

std::optional<NodeId> WidgetSet::owner_of(const RenderTree& tree, NodeId id) const {
  NodeId current = id;
  while (true) {
    if (accepts_pointer(current)) {
      return current;
    }
    const NodeId parent = tree.parent(current);

    // The root is its own parent, so this is the top of the climb. Written as
    // equality rather than as `current.value == 0` so that it keeps working if
    // the root ever stops being index zero.
    if (parent == current) {
      return std::nullopt;
    }
    current = parent;
  }
}

std::optional<NodeId> WidgetSet::widget_at(const RenderTree& tree, PixelPoint point) const {
  const std::optional<NodeId> node = tree.hit_test(point);
  if (!node.has_value()) {
    return std::nullopt;
  }
  return owner_of(tree, *node);
}

std::optional<NodeId> WidgetSet::scrollable_owner_of(const RenderTree& tree, NodeId id) const {
  NodeId current = id;
  while (true) {
    const Widget* widget = find(current);
    if (widget != nullptr && widget->kind == WidgetKind::kScrollView) {
      return current;
    }
    const NodeId parent = tree.parent(current);
    if (parent == current) {
      return std::nullopt;
    }
    current = parent;
  }
}

bool WidgetSet::scroll_by(RenderTree& tree, NodeId id, const PixelRect& viewport_content,
                          int dx, int dy) const {
  const Widget* widget = find(id);
  if (widget == nullptr || widget->kind != WidgetKind::kScrollView) {
    return false;
  }

  // The clamp: the content child's full natural size, less the room the
  // viewport itself offers, floored at zero so a child SMALLER than its
  // viewport (nothing to scroll) clamps to exactly one position rather than a
  // negative range.
  const PixelRect content = tree.local_bounds(widget->scroll_content);
  const int max_x = std::max(0, content.width - viewport_content.width);
  const int max_y = std::max(0, content.height - viewport_content.height);

  PixelPoint offset = tree.scroll_offset(id);
  switch (widget->scroll_axis) {
    case ScrollAxis::kNone:
      return false;
    case ScrollAxis::kVertical:
      offset.y = std::clamp(offset.y + dy, 0, max_y);
      break;
    case ScrollAxis::kHorizontal:
      offset.x = std::clamp(offset.x + dx, 0, max_x);
      break;
  }
  if (offset == tree.scroll_offset(id)) {
    return false;
  }
  tree.set_scroll_offset(id, offset);
  return true;
}

void WidgetSet::refresh(RenderTree& tree, NodeId id, PointerState state) const {
  const Widget* found = find(id);
  if (found == nullptr) {
    return;
  }
  const Widget& widget = *found;

  // Pressed beats hovered: a widget the pointer is held down on is always
  // hovered too, and reporting it as merely hovered would drop the press
  // feedback exactly while the user is asking for it.
  Color fill = widget.fill_normal;
  if (state.pressed) {
    fill = widget.fill_pressed;
  } else if (state.hovered) {
    fill = widget.fill_hover;
  }
  if (tree.style(id).fill != fill) {
    tree.set_fill(id, fill);
  }

  if (widget.kind != WidgetKind::kCheckbox || widget.indicator == RenderTree::root()) {
    return;
  }
  const Color mark = widget.checked ? widget.indicator_on : widget.indicator_off;
  if (tree.style(widget.indicator).fill != mark) {
    tree.set_fill(widget.indicator, mark);
  }
}

}  // namespace dg
