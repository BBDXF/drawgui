#include "ui/row_list.h"

#include <algorithm>
#include <cmath>

#include "drawgui/widget/focus.h"
#include "drawgui/widget/interaction.h"
#include "drawgui/widget/widget_set.h"

namespace dg::row_list {

int viewport(const ControlContext& ctx, NodeId list) {
  return ctx.tree().local_bounds(list).height;
}

std::optional<int> row_at(const ControlContext& ctx, NodeId list, PixelPoint point, int count) {
  const RenderTree& tree = ctx.tree();
  const PixelRect bounds = tree.absolute_bounds(list);
  const int gutter = ctx.tables.widgets.scrollbar_style().size;
  if (point.x < bounds.x || point.x >= bounds.x + bounds.width - gutter || point.y < bounds.y ||
      point.y >= bounds.y + bounds.height) {
    return std::nullopt;
  }
  const int extent = std::max(1, ctx.style.row_height);
  const int row = (point.y - bounds.y + tree.scroll_offset(list).y) / extent;
  if (row < 0 || row >= count) {
    return std::nullopt;
  }
  return row;
}

bool scroll_pointer(ControlContext& ctx, NodeId root, NodeId list, const PointerEvent& event,
                    bool& dragging, bool& scrolled) {
  RenderTree& tree = ctx.tree();
  WidgetSet& widgets = ctx.tables.widgets;
  const int extent = viewport(ctx, list);
  const PixelPoint at{event.x, event.y};
  scrolled = false;

  if (dragging) {
    if (event.action == PointerAction::kMove) {
      scrolled =
          widgets.scrollbar_drag(tree, widgets.at(list).scrollbar_thumb, extent, at).moved;
      return true;
    }
    if (event.action == PointerAction::kUp || event.action == PointerAction::kLeave) {
      widgets.scrollbar_release();
      dragging = false;
      return true;
    }
  }

  if (event.action == PointerAction::kWheel) {
    const std::optional<NodeId> hit = tree.hit_test(at);
    if (!hit.has_value() || !is_within(tree, root, *hit) || is_disabled(tree, root)) {
      return false;
    }
    const int notches = static_cast<int>(std::lround(event.wheel_y * 3.0F));
    const int dy = -notches * std::max(1, ctx.style.row_height);
    const int before = tree.scroll_offset(list).y;
    widgets.list_scroll_by(tree, list, extent, 0, dy);
    scrolled = tree.scroll_offset(list).y != before;
    return true;
  }

  if (event.action == PointerAction::kDown && event.button == PointerButton::kPrimary) {
    const std::optional<NodeId> owner = widgets.widget_at(tree, at);
    if (!owner.has_value() || widgets.scrollbar_owner_of(*owner) != list) {
      return false;
    }
    const int before = tree.scroll_offset(list).y;
    widgets.scrollbar_press(tree, *owner, extent, at);
    dragging = widgets.is_scrollbar_thumb(*owner);
    scrolled = tree.scroll_offset(list).y != before;
    return true;
  }
  return false;
}

bool reveal(ControlContext& ctx, NodeId list, int row) {
  RenderTree& tree = ctx.tree();
  const int extent = std::max(1, ctx.style.row_height);
  const int height = viewport(ctx, list);
  const int offset = tree.scroll_offset(list).y;
  const int top = row * extent;
  int target = offset;
  if (top < offset) {
    target = top;
  } else if (top + extent > offset + height) {
    target = top + extent - height;
  }
  if (target == offset) {
    return false;
  }
  ctx.tables.widgets.list_scroll_by(tree, list, height, 0, target - offset);
  return tree.scroll_offset(list).y != offset;
}

std::optional<int> nav_target(const ControlContext& ctx, NodeId list, Key key,
                              std::optional<int> current, int count) {
  if (count <= 0) {
    return std::nullopt;
  }
  const int page = std::max(1, viewport(ctx, list) / std::max(1, ctx.style.row_height));
  const int last = count - 1;
  switch (key) {
    case Key::kUp:
      return current.has_value() ? std::max(0, *current - 1) : last;
    case Key::kDown:
      return current.has_value() ? std::min(last, *current + 1) : 0;
    case Key::kHome:
      return 0;
    case Key::kEnd:
      return last;
    case Key::kPageUp:
      return std::max(0, current.value_or(0) - page);
    case Key::kPageDown:
      return std::min(last, current.value_or(0) + page);
    default:
      return std::nullopt;
  }
}

bool owns_key(const ControlContext& ctx, NodeId root, const KeyEvent& event) {
  return event.action == KeyAction::kDown && ctx.tables.focus.is_focused(root) &&
         !is_disabled(ctx.tree(), root);
}

}  // namespace dg::row_list
