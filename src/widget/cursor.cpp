#include "drawgui/widget/cursor.h"

#include <optional>

namespace dg {

Cursor widget_default_cursor(WidgetKind kind) {
  switch (kind) {
    case WidgetKind::kTextField:
    case WidgetKind::kTextArea:
      return Cursor::kText;
    case WidgetKind::kPanel:
    case WidgetKind::kLabel:
    case WidgetKind::kButton:
    case WidgetKind::kCheckbox:
    case WidgetKind::kScrollView:
    case WidgetKind::kSlider:
    case WidgetKind::kList:
    case WidgetKind::kDropdown:
    case WidgetKind::kScrollbar:
      return Cursor::kDefault;
  }
  return Cursor::kDefault;
}

Cursor resolve_cursor(const RenderTree& tree, const WidgetSet& widgets,
                      std::optional<NodeId> node) {
  if (!node.has_value()) {
    return Cursor::kDefault;
  }
  const bool disabled = is_disabled(tree, *node);
  NodeId current = *node;
  while (true) {
    const Cursor own = tree.style(current).cursor;
    if (own != Cursor::kAuto) {
      return own;
    }
    if (!disabled && widgets.has(current)) {
      const Cursor kind_default = widget_default_cursor(widgets.at(current).kind);
      if (kind_default != Cursor::kDefault) {
        return kind_default;
      }
    }
    const NodeId parent = tree.parent(current);
    if (parent == current) {
      return Cursor::kDefault;
    }
    current = parent;
  }
}

Cursor cursor_at(const RenderTree& tree, const WidgetSet& widgets,
                 const Interaction& interaction, PixelPoint point) {
  if (interaction.holding().has_value()) {
    return resolve_cursor(tree, widgets, interaction.holding());
  }
  return resolve_cursor(tree, widgets, tree.hit_test(point));
}

}  // namespace dg
