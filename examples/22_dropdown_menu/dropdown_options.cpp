#include "dropdown_options.h"

#include "drawgui/widget/widget_set.h"

namespace dropdown_options {
namespace {

using dg::Color;
using dg::NodeStyle;
using dg::PixelRect;
using dg::TextAlign;
using dg::TextStyle;
using dg::Widget;
using dg::WidgetKind;

constexpr std::uint32_t kBackground = 0xFF2B303B;
constexpr std::uint32_t kRowNormal = 0xFF2B303B;
constexpr std::uint32_t kRowHover = 0xFF3A4152;
constexpr std::uint32_t kRowPressed = 0xFF23272F;
constexpr std::uint32_t kRowText = 0xFFE6E9F0;

}  // namespace

dg::PixelSize size_for(int count, int width) {
  return dg::PixelSize{width, count * kRowHeight};
}

std::vector<dg::NodeId> build(dg::RenderTree& tree, dg::WidgetSet& widgets, dg::NodeId parent,
                              const std::vector<std::string>& options, int width,
                              dg::FontId font, float font_size) {
  NodeStyle background;
  background.fill = Color::from_argb(kBackground);
  tree.add_child(parent, PixelRect{0, 0, width, static_cast<int>(options.size()) * kRowHeight},
                 background);

  std::vector<dg::NodeId> rows;
  rows.reserve(options.size());
  for (std::size_t i = 0; i < options.size(); ++i) {
    NodeStyle row_style;
    row_style.fill = Color::from_argb(kRowNormal);
    const dg::NodeId row = tree.add_child(
        parent, PixelRect{0, static_cast<int>(i) * kRowHeight, width, kRowHeight}, row_style);

    Widget widget;
    widget.kind = WidgetKind::kButton;
    widget.fill_normal = Color::from_argb(kRowNormal);
    widget.fill_hover = Color::from_argb(kRowHover);
    widget.fill_pressed = Color::from_argb(kRowPressed);
    widgets.attach(row, widget);

    NodeStyle label_style;
    label_style.text.text = options[i];
    label_style.text.font = font;
    label_style.text.size = font_size;
    label_style.text.color = Color::from_argb(kRowText);
    label_style.text.align = TextAlign::kLeft;
    label_style.text.inset = kRowPad;
    tree.add_child(row, PixelRect{0, 0, width, kRowHeight}, label_style);

    rows.push_back(row);
  }
  return rows;
}

}  // namespace dropdown_options
