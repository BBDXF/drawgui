#include "multiline_scene.h"

#include <algorithm>
#include <utility>

#include "drawgui/layout/box.h"
#include "drawgui/render/paragraph.h"

namespace multiline_scene {
namespace {

using dg::BoxStyle;
using dg::Color;
using dg::EdgeInsets;
using dg::FontCatalog;
using dg::FontId;
using dg::LayoutKind;
using dg::LayoutTree;
using dg::NodeStyle;
using dg::Paragraph;
using dg::TextAlign;
using dg::TextStyle;

constexpr int kInnerWidth = kPanelWidth - (2 * kPanelPadding);

TextStyle panel_text(FontId font, std::string text, std::string language, int max_lines) {
  TextStyle style;
  style.text = std::move(text);
  style.font = font;
  style.language = std::move(language);
  style.size = kFontSize;
  style.color = Color::from_argb(0xFFEAEAEA);
  style.align = TextAlign::kLeft;
  style.wrap = true;
  style.max_lines = max_lines;
  return style;
}

// The height a caller must declare BEFORE the panel's node exists - the one
// place this file calls dg::Paragraph::build(), reused by both the scene
// builder here and multiline_check.cpp's independent re-derivation.
int wrapped_height(const FontCatalog& fonts, const TextStyle& text) {
  dg::Expected<Paragraph, dg::FontError> built =
      Paragraph::build(fonts, text, static_cast<float>(kInnerWidth));
  if (!built.has_value()) {
    return 0;
  }
  return built.value().metrics().height;
}

dg::NodeId add_panel(LayoutTree& tree, dg::NodeId parent, const TextStyle& text, int height) {
  BoxStyle outer;
  outer.kind = LayoutKind::kLeaf;
  outer.width = kPanelWidth;
  outer.height = height + (2 * kPanelPadding);
  outer.padding = EdgeInsets::all(kPanelPadding);

  NodeStyle outer_style;
  outer_style.fill = Color::from_argb(0xFF20262F);
  outer_style.border_color = Color::from_argb(0xFF3E4653);
  outer_style.border_width = dg::BorderWidths::all(1.0F);
  const dg::NodeId panel = tree.add_child(parent, outer, outer_style);

  BoxStyle inner;
  inner.kind = LayoutKind::kLeaf;
  inner.width = kInnerWidth;
  inner.height = height;

  NodeStyle inner_style;
  inner_style.text = text;
  tree.add_child(panel, inner, inner_style);
  return panel;
}

}  // namespace

Scene build(dg::TreeSpec spec, const std::string& font_dir, const std::string& primary_family) {
  dg::Expected<FontCatalog, dg::FontError> scanned = FontCatalog::scan(font_dir);
  FontCatalog fonts =
      scanned.has_value() ? std::move(scanned).value() : FontCatalog::scan(".").value();
  dg::Expected<FontId, dg::FontError> primary = fonts.add(primary_family, false);
  const FontId primary_id = primary.has_value() ? primary.value() : FontId{};

  spec.fonts = fonts;
  LayoutTree tree{spec};
  Handles handles;

  const dg::NodeId root = LayoutTree::root();
  BoxStyle column;
  column.kind = LayoutKind::kColumn;
  column.gap = kPanelGap;
  column.padding = EdgeInsets::all(kPanelGap);
  handles.column = tree.add_child(root, column, NodeStyle{});

  const TextStyle latin = panel_text(primary_id, kLatinText, "", 0);
  handles.latin_panel = add_panel(tree, handles.column, latin, wrapped_height(fonts, latin));

  const TextStyle cjk = panel_text(primary_id, kCjkText, "zh-Hans", 0);
  handles.cjk_panel = add_panel(tree, handles.column, cjk, wrapped_height(fonts, cjk));

  const TextStyle mixed = panel_text(primary_id, kMixedText, "zh-Hans", 0);
  handles.mixed_panel = add_panel(tree, handles.column, mixed, wrapped_height(fonts, mixed));

  const TextStyle bidi = panel_text(primary_id, kBidiText, "", 0);
  handles.bidi_panel = add_panel(tree, handles.column, bidi, wrapped_height(fonts, bidi));

  const TextStyle ellipsized = panel_text(primary_id, kLatinText, "", 2);
  handles.ellipsized_panel =
      add_panel(tree, handles.column, ellipsized, wrapped_height(fonts, ellipsized));

  const dg::LayoutStats stats = tree.layout_full();
  return Scene{std::move(tree), std::move(fonts), primary_id, handles, stats};
}

}  // namespace multiline_scene
