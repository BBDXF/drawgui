#include "drawgui/widget/widget_set.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

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
    case WidgetKind::kSlider:
      break;
  }
  return false;
}

// [min_value, max_value], then snapped to the nearest `step` above
// min_value when it is positive, then clamped again - snapping can push a
// value that was already at a bound slightly past it by rounding.
float clamp_slider_value(const Widget& widget, float value) {
  float clamped = std::clamp(value, widget.min_value, widget.max_value);
  if (widget.step > 0.0F) {
    const float steps = std::round((clamped - widget.min_value) / widget.step);
    clamped = std::clamp(widget.min_value + (steps * widget.step), widget.min_value,
                         widget.max_value);
  }
  return clamped;
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
  if (!widget->group.has_value()) {
    widget->checked = !widget->checked;
    return widget->checked;
  }

  // Radio behaviour: re-selecting the already-checked option in a group is
  // a no-op, matching every desktop toolkit, then every OTHER checkbox in
  // the same group is cleared.
  if (widget->checked) {
    return true;
  }
  widget->checked = true;
  const int group = *widget->group;
  for (std::optional<Widget>& slot : widgets_) {
    if (!slot.has_value() || &(*slot) == widget) {
      continue;
    }
    if (slot->kind == WidgetKind::kCheckbox && slot->group == group) {
      slot->checked = false;
    }
  }
  return true;
}

std::vector<NodeId> WidgetSet::group_members(NodeId id) const {
  std::vector<NodeId> members;
  const Widget* widget = find(id);
  if (widget == nullptr || widget->kind != WidgetKind::kCheckbox ||
      !widget->group.has_value()) {
    return members;
  }
  for (std::size_t i = 0; i < widgets_.size(); ++i) {
    const std::optional<Widget>& slot = widgets_[i];
    if (!slot.has_value() || i == id.value) {
      continue;
    }
    if (slot->kind == WidgetKind::kCheckbox && slot->group == widget->group) {
      members.push_back(NodeId{static_cast<std::uint32_t>(i)});
    }
  }
  return members;
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

std::optional<NodeId> WidgetSet::slidable_owner_of(const RenderTree& tree, NodeId id) const {
  NodeId current = id;
  while (true) {
    const Widget* widget = find(current);
    if (widget != nullptr && widget->kind == WidgetKind::kSlider) {
      return current;
    }
    const NodeId parent = tree.parent(current);
    if (parent == current) {
      return std::nullopt;
    }
    current = parent;
  }
}

void WidgetSet::reposition_slider(RenderTree& tree, NodeId id, const Widget& widget) {
  const PixelRect track = tree.local_bounds(id);
  const PixelRect thumb = tree.local_bounds(widget.thumb);
  const int travel = std::max(0, track.width - thumb.width);
  const float span = widget.max_value - widget.min_value;
  const float fraction = span > 0.0F ? (widget.value - widget.min_value) / span : 0.0F;
  const auto thumb_x = static_cast<int>(
      std::lround(static_cast<double>(fraction) * static_cast<double>(travel)));
  const int thumb_y = (track.height - thumb.height) / 2;
  tree.set_local_origin(widget.thumb, thumb_x, thumb_y);
}

bool WidgetSet::set_slider_value(RenderTree& tree, NodeId id, float value) {
  Widget* widget = find(id);
  if (widget == nullptr || widget->kind != WidgetKind::kSlider) {
    return false;
  }
  const float clamped = clamp_slider_value(*widget, value);
  if (clamped == widget->value) {
    return false;
  }
  widget->value = clamped;
  reposition_slider(tree, id, *widget);
  return true;
}

float WidgetSet::slider_value(NodeId id) const {
  const Widget* widget = find(id);
  return widget != nullptr && widget->kind == WidgetKind::kSlider ? widget->value : 0.0F;
}

float WidgetSet::slider_value_at(const RenderTree& tree, NodeId id, int pointer_x) const {
  const Widget* widget = find(id);
  if (widget == nullptr || widget->kind != WidgetKind::kSlider) {
    return 0.0F;
  }
  const PixelRect track = tree.absolute_bounds(id);
  const PixelRect thumb = tree.local_bounds(widget->thumb);
  const int travel = std::max(0, track.width - thumb.width);
  const float half_thumb = static_cast<float>(thumb.width) / 2.0F;
  const float t = travel > 0
                      ? std::clamp((static_cast<float>(pointer_x - track.x) - half_thumb) /
                                       static_cast<float>(travel),
                                   0.0F, 1.0F)
                      : 0.0F;
  return widget->min_value + (t * (widget->max_value - widget->min_value));
}

void WidgetSet::resync_sliders(RenderTree& tree) const {
  for (std::size_t i = 0; i < widgets_.size(); ++i) {
    const std::optional<Widget>& slot = widgets_[i];
    if (!slot.has_value() || slot->kind != WidgetKind::kSlider) {
      continue;
    }
    reposition_slider(tree, NodeId{static_cast<std::uint32_t>(i)}, *slot);
  }
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
