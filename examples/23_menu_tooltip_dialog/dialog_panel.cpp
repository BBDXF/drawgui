#include "dialog_panel.h"

namespace dialog_panel {
namespace {

using dg::Color;
using dg::NodeStyle;
using dg::PixelRect;
using dg::TextAlign;
using dg::Widget;
using dg::WidgetKind;

constexpr std::uint32_t kBackdrop = 0x80000000;  // translucent black scrim
constexpr std::uint32_t kPanelFill = 0xFF1C2129;
constexpr std::uint32_t kPanelBorder = 0xFF3A4152;
constexpr std::uint32_t kButtonNormal = 0xFF2C3644;
constexpr std::uint32_t kButtonHover = 0xFF3B4A5E;
constexpr std::uint32_t kButtonPressed = 0xFF1F2731;
constexpr std::uint32_t kText = 0xFFE6E9F0;
constexpr int kButtonWidth = 90;
constexpr int kButtonHeight = 30;
constexpr int kButtonMargin = 16;

}  // namespace

Handles build(dg::RenderTree& tree, dg::WidgetSet& widgets, dg::NodeId parent,
              dg::PixelSize window_size, dg::PixelSize panel_size, dg::FontId font) {
  Handles handles;

  NodeStyle backdrop_style;
  backdrop_style.fill = Color::from_argb(kBackdrop);
  handles.backdrop = tree.add_child(
      parent, PixelRect{0, 0, window_size.width, window_size.height}, backdrop_style);

  const int panel_x = (window_size.width - panel_size.width) / 2;
  const int panel_y = (window_size.height - panel_size.height) / 2;
  NodeStyle panel_style;
  panel_style.fill = Color::from_argb(kPanelFill);
  panel_style.border_color = Color::from_argb(kPanelBorder);
  panel_style.border_width = dg::BorderWidths::all(1.0F);
  handles.panel = tree.add_child(
      parent, PixelRect{panel_x, panel_y, panel_size.width, panel_size.height}, panel_style);

  NodeStyle title_style;
  title_style.text.text = "Modal dialog - Tab/click cannot escape";
  title_style.text.font = font;
  title_style.text.size = 14.0F;
  title_style.text.color = Color::from_argb(kText);
  title_style.text.align = TextAlign::kCenter;
  tree.add_child(handles.panel, PixelRect{0, 16, panel_size.width, 20}, title_style);

  const int close_x = (panel_size.width - kButtonWidth) / 2;
  const int close_y = panel_size.height - kButtonHeight - kButtonMargin;
  NodeStyle close_style;
  close_style.fill = Color::from_argb(kButtonNormal);
  close_style.border_color = Color::from_argb(kPanelBorder);
  close_style.border_width = dg::BorderWidths::all(1.0F);
  close_style.radii = dg::Radii::all(6.0F);
  handles.close_button = tree.add_child(
      handles.panel, PixelRect{close_x, close_y, kButtonWidth, kButtonHeight}, close_style);

  Widget close_widget;
  close_widget.kind = WidgetKind::kButton;
  close_widget.fill_normal = Color::from_argb(kButtonNormal);
  close_widget.fill_hover = Color::from_argb(kButtonHover);
  close_widget.fill_pressed = Color::from_argb(kButtonPressed);
  widgets.attach(handles.close_button, close_widget);

  NodeStyle close_label;
  close_label.text.text = "Close";
  close_label.text.font = font;
  close_label.text.size = 14.0F;
  close_label.text.color = Color::from_argb(kText);
  close_label.text.align = TextAlign::kCenter;
  tree.add_child(handles.close_button, PixelRect{0, 0, kButtonWidth, kButtonHeight},
                 close_label);

  return handles;
}

}  // namespace dialog_panel
