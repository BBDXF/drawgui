#include "tooltip_content.h"

namespace tooltip_content {
namespace {

using dg::Color;
using dg::NodeStyle;
using dg::PixelRect;
using dg::TextAlign;

constexpr std::uint32_t kBackground = 0xFF1A1D24;
constexpr std::uint32_t kBorder = 0xFF3A4152;
constexpr std::uint32_t kText = 0xFFE6E9F0;
constexpr int kPadX = 10;
constexpr int kHeight = 26;

// A tooltip's size is declared BEFORE its content exists (PopupHost::show()
// takes content_size up front, matching every other PopupHost client) -
// this project's own standing rule that a node knows its size before its
// content is resolved (doc/image.md's decode-after-size rule, doc/text-
// layout.md's paragraph-build-before-node rule). A fixed-width monospace-ish
// approximation (font_size_px * 0.6 per character) is precise enough for a
// short tooltip string and does not need dg::Paragraph::build() - the exact
// text this demo shows is known in advance, unlike a wrapping paragraph.
int approx_text_width(const std::string& text, int font_size_px) {
  return static_cast<int>(static_cast<float>(text.size()) * static_cast<float>(font_size_px) *
                          0.6F);
}

}  // namespace

dg::PixelSize size_for(const std::string& text, int font_size_px) {
  return dg::PixelSize{approx_text_width(text, font_size_px) + (kPadX * 2), kHeight};
}

void build(dg::RenderTree& tree, dg::NodeId parent, const std::string& text, dg::FontId font,
           float font_size, dg::PixelSize size) {
  NodeStyle background;
  background.fill = Color::from_argb(kBackground);
  background.border_color = Color::from_argb(kBorder);
  background.border_width = dg::BorderWidths::all(1.0F);
  const dg::NodeId box =
      tree.add_child(parent, PixelRect{0, 0, size.width, size.height}, background);

  NodeStyle label_style;
  label_style.text.text = text;
  label_style.text.font = font;
  label_style.text.size = font_size;
  label_style.text.color = Color::from_argb(kText);
  label_style.text.align = TextAlign::kCenter;
  tree.add_child(box, PixelRect{0, 0, size.width, size.height}, label_style);
}

}  // namespace tooltip_content
