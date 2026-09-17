#include "profile_dialog.h"

#include "drawgui/theme/token_ids.generated.h"

namespace profile_dialog {
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

constexpr std::uint32_t kBackdrop = 0x80000000;  // translucent scrim - not tokenized, see
                                                 // doc/showcase.md (no schema token models one)
constexpr int kFieldWidth = 220;
constexpr int kFieldHeight = 32;
constexpr int kButtonWidth = 90;
constexpr int kButtonHeight = 30;
constexpr int kMargin = 16;

}  // namespace

Handles build(dg::RenderTree& tree, dg::WidgetSet& widgets, dg::NodeId parent,
              dg::PixelSize window_size, dg::PixelSize panel_size, dg::FontId font,
              const dg::Theme& theme, dg::ThemeVariant variant) {
  Handles handles;

  const Color surface = tok(theme, variant, DG_TOKEN_COLOR_SURFACE, 0xFF1C2129);
  const Color border = tok(theme, variant, DG_TOKEN_COLOR_BORDER, 0xFF3A4152);
  const Color primary = tok(theme, variant, DG_TOKEN_COLOR_PRIMARY, 0xFF2E86DE);
  const Color hover = tok(theme, variant, DG_TOKEN_COLOR_PRIMARY_HOVER, 0xFF3B4A5E);
  const Color pressed = tok(theme, variant, DG_TOKEN_COLOR_PRIMARY_PRESSED, 0xFF1F2731);
  const Color text_color = tok(theme, variant, DG_TOKEN_COLOR_ON_SURFACE, 0xFFE6E9F0);

  NodeStyle backdrop_style;
  backdrop_style.fill = Color::from_argb(kBackdrop);
  handles.backdrop = tree.add_child(
      parent, PixelRect{0, 0, window_size.width, window_size.height}, backdrop_style);

  const int panel_x = (window_size.width - panel_size.width) / 2;
  const int panel_y = (window_size.height - panel_size.height) / 2;
  NodeStyle panel_style;
  panel_style.fill = surface;
  panel_style.border_color = border;
  panel_style.border_width = dg::BorderWidths::all(1.0F);
  handles.panel = tree.add_child(
      parent, PixelRect{panel_x, panel_y, panel_size.width, panel_size.height}, panel_style);

  NodeStyle title_style;
  title_style.text.text = "\u4e2a\u4eba\u8d44\u6599 - Tab/\u70b9\u51fb\u65e0\u6cd5\u9003\u79bb";
  title_style.text.font = font;
  title_style.text.language = "zh-Hans";
  title_style.text.size = 14.0F;
  title_style.text.color = text_color;
  title_style.text.align = TextAlign::kCenter;
  tree.add_child(handles.panel, PixelRect{0, 14, panel_size.width, 20}, title_style);

  // --- The name kTextField: content/caret/selection_highlight/
  // composition_underline, the identical four-child shape every other
  // TextField in this project already has (widget_set.h). ---
  const int field_x = (panel_size.width - kFieldWidth) / 2;
  const int field_y = 44;
  NodeStyle field_style;
  field_style.fill = surface;
  field_style.border_color = border;
  field_style.border_width = dg::BorderWidths::all(1.0F);
  field_style.radii = dg::Radii::all(6.0F);
  field_style.overflow = dg::Overflow::kClip;
  handles.name_field = tree.add_child(
      handles.panel, PixelRect{field_x, field_y, kFieldWidth, kFieldHeight}, field_style);

  NodeStyle placeholder;
  const dg::NodeId highlight = tree.add_child(handles.name_field, PixelRect{0, 0, 1, 1}, [&] {
    NodeStyle s;
    s.fill = hover;
    return s;
  }());
  const dg::NodeId underline = tree.add_child(handles.name_field, PixelRect{0, 0, 1, 1}, [&] {
    NodeStyle s;
    s.fill = primary;
    return s;
  }());
  NodeStyle content_style;
  content_style.text.font = font;
  content_style.text.size = 15.0F;
  content_style.text.color = text_color;
  content_style.text.align = TextAlign::kLeft;
  const dg::NodeId content =
      tree.add_child(handles.name_field, PixelRect{0, 0, 1, 1}, content_style);
  const dg::NodeId caret = tree.add_child(handles.name_field, PixelRect{0, 0, 1, 1}, [&] {
    NodeStyle s;
    s.fill = primary;
    return s;
  }());

  Widget field_widget;
  field_widget.kind = WidgetKind::kTextField;
  field_widget.content = content;
  field_widget.caret = caret;
  field_widget.selection_highlight = highlight;
  field_widget.composition_underline = underline;
  widgets.attach(handles.name_field, field_widget);

  const int close_x = (panel_size.width - kButtonWidth) / 2;
  const int close_y = panel_size.height - kButtonHeight - kMargin;
  NodeStyle close_style;
  close_style.fill = surface;
  close_style.border_color = border;
  close_style.border_width = dg::BorderWidths::all(1.0F);
  close_style.radii = dg::Radii::all(6.0F);
  handles.close_button = tree.add_child(
      handles.panel, PixelRect{close_x, close_y, kButtonWidth, kButtonHeight}, close_style);

  Widget close_widget;
  close_widget.kind = WidgetKind::kButton;
  close_widget.fill_normal = surface;
  close_widget.fill_hover = hover;
  close_widget.fill_pressed = pressed;
  widgets.attach(handles.close_button, close_widget);

  NodeStyle close_label;
  close_label.text.text = "\u5173\u95ed";  // "Close"
  close_label.text.font = font;
  close_label.text.size = 14.0F;
  close_label.text.color = text_color;
  close_label.text.align = TextAlign::kCenter;
  tree.add_child(handles.close_button, PixelRect{0, 0, kButtonWidth, kButtonHeight},
                 close_label);

  return handles;
}

}  // namespace profile_dialog
