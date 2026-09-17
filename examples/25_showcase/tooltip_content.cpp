#include "tooltip_content.h"

#include "drawgui/theme/token_ids.generated.h"

namespace tooltip_content {
namespace {

using dg::Color;
using dg::NodeStyle;
using dg::PixelRect;
using dg::TextAlign;

constexpr int kPadX = 10;
constexpr int kHeight = 26;

int approx_text_width(const std::string& text, int font_size_px) {
  return static_cast<int>(static_cast<float>(text.size()) * static_cast<float>(font_size_px) *
                          0.6F);
}

}  // namespace

dg::PixelSize size_for(const std::string& text, int font_size_px) {
  return dg::PixelSize{approx_text_width(text, font_size_px) + (kPadX * 2), kHeight};
}

void build(dg::RenderTree& tree, dg::NodeId parent, const std::string& text, dg::FontId font,
           float font_size, dg::PixelSize size, const dg::Theme& theme,
           dg::ThemeVariant variant) {
  const Color background =
      theme.color_value(DG_TOKEN_COLOR_SURFACE, variant).value_or(Color::from_argb(0xFF1A1D24));
  const Color border =
      theme.color_value(DG_TOKEN_COLOR_BORDER, variant).value_or(Color::from_argb(0xFF3A4152));
  const Color text_color = theme.color_value(DG_TOKEN_COLOR_ON_SURFACE, variant)
                               .value_or(Color::from_argb(0xFFE6E9F0));

  NodeStyle style;
  style.fill = background;
  style.border_color = border;
  style.border_width = dg::BorderWidths::all(1.0F);
  const dg::NodeId box =
      tree.add_child(parent, PixelRect{0, 0, size.width, size.height}, style);

  NodeStyle label_style;
  label_style.text.text = text;
  label_style.text.font = font;
  label_style.text.size = font_size;
  label_style.text.color = text_color;
  label_style.text.align = TextAlign::kCenter;
  tree.add_child(box, PixelRect{0, 0, size.width, size.height}, label_style);
}

}  // namespace tooltip_content
