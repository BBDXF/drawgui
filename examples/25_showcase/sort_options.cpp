#include "sort_options.h"

#include "drawgui/theme/token_ids.generated.h"

namespace sort_options {
namespace {

using dg::Color;
using dg::NodeStyle;
using dg::PixelRect;
using dg::TextAlign;
using dg::Widget;
using dg::WidgetKind;

Color tok(const dg::Theme& theme, dg::ThemeVariant variant, dg_token_id id,
          std::uint32_t fallback) {
  return theme.color_value(id, variant).value_or(Color::from_argb(fallback));
}

}  // namespace

dg::PixelSize size_for(int count, int width) {
  return dg::PixelSize{width, count * kRowHeight};
}

std::vector<dg::NodeId> build(dg::RenderTree& tree, dg::WidgetSet& widgets, dg::NodeId parent,
                              const std::vector<std::string>& options, int width,
                              dg::FontId font, const dg::Theme& theme,
                              dg::ThemeVariant variant) {
  const Color surface = tok(theme, variant, DG_TOKEN_COLOR_SURFACE, 0xFF2B303B);
  const Color hover = tok(theme, variant, DG_TOKEN_COLOR_PRIMARY_HOVER, 0xFF3A4152);
  const Color pressed = tok(theme, variant, DG_TOKEN_COLOR_PRIMARY_PRESSED, 0xFF23272F);
  const Color text_color = tok(theme, variant, DG_TOKEN_COLOR_ON_SURFACE, 0xFFE6E9F0);

  NodeStyle background;
  background.fill = surface;
  tree.add_child(parent, PixelRect{0, 0, width, static_cast<int>(options.size()) * kRowHeight},
                 background);

  std::vector<dg::NodeId> rows;
  rows.reserve(options.size());
  for (std::size_t i = 0; i < options.size(); ++i) {
    NodeStyle row_style;
    row_style.fill = surface;
    const dg::NodeId row = tree.add_child(
        parent, PixelRect{0, static_cast<int>(i) * kRowHeight, width, kRowHeight}, row_style);

    Widget widget;
    widget.kind = WidgetKind::kButton;
    widget.fill_normal = surface;
    widget.fill_hover = hover;
    widget.fill_pressed = pressed;
    widgets.attach(row, widget);

    NodeStyle label_style;
    label_style.text.text = options[i];
    label_style.text.font = font;
    label_style.text.size = 14.0F;
    label_style.text.color = text_color;
    label_style.text.align = TextAlign::kLeft;
    label_style.text.inset = 10;
    tree.add_child(row, PixelRect{0, 0, width, kRowHeight}, label_style);

    rows.push_back(row);
  }
  return rows;
}

}  // namespace sort_options
