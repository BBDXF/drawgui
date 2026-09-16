#include "popup_menu.h"

#include <cstdint>

#include "drawgui/widget/widget_set.h"

namespace popup_menu {
namespace {

using dg::Color;
using dg::NodeStyle;
using dg::PixelRect;
using dg::Widget;
using dg::WidgetKind;

constexpr std::uint32_t kBackground = 0xFF2B303B;
constexpr std::uint32_t kButtonNormal = 0xFF3A4152;
constexpr std::uint32_t kButtonHover = 0xFF4A5468;
constexpr std::uint32_t kButtonPressed = 0xFF2C323F;

NodeStyle flat(std::uint32_t argb) {
  NodeStyle style;
  style.fill = Color::from_argb(argb);
  return style;
}

dg::NodeId add_button(dg::RenderTree& tree, dg::WidgetSet& widgets, dg::NodeId parent,
                      const PixelRect& bounds) {
  const dg::NodeId node = tree.add_child(parent, bounds, flat(kButtonNormal));
  Widget widget;
  widget.kind = WidgetKind::kButton;
  widget.fill_normal = Color::from_argb(kButtonNormal);
  widget.fill_hover = Color::from_argb(kButtonHover);
  widget.fill_pressed = Color::from_argb(kButtonPressed);
  widgets.attach(node, widget);
  return node;
}

}  // namespace

Handles build(dg::RenderTree& tree, dg::WidgetSet& widgets, dg::NodeId parent) {
  tree.add_child(parent, PixelRect{0, 0, kSize.width, kSize.height}, flat(kBackground));

  Handles handles;
  handles.btn1 = add_button(tree, widgets, parent, PixelRect{8, 8, kSize.width - 16, 28});
  handles.btn2 = add_button(tree, widgets, parent, PixelRect{8, 44, kSize.width - 16, 28});
  return handles;
}

}  // namespace popup_menu
