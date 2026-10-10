#include "ui/ui_visuals.h"

#include <algorithm>
#include <cstdint>

#include "drawgui/render/paragraph.h"
#include "drawgui/widget/widget_set.h"

namespace dg::ui_visuals {
namespace {

constexpr int kFrame = 1;        // menu border width
constexpr int kMenuInset = 4;    // gap between the menu frame and its rows, all sides
constexpr int kTooltipHeight = 26;
constexpr int kTooltipPadX = 10;
constexpr int kTitleHeight = 24;
constexpr int kTitleGap = 6;     // title baseline area -> body
constexpr int kButtonHeight = 32;
constexpr int kButtonMinWidth = 84;
constexpr float kTitleGrow = 3.0F;  // title is this much larger than body text
constexpr const char* kCheckGlyph = "\u2713";
constexpr int kShortcutGap = 24;  // label -> shortcut column

// `base` with `over` laid on top at `amount` (0..1).
Color mix(Color base, Color over, float amount) {
  const auto channel = [amount](std::uint8_t a, std::uint8_t b) {
    return static_cast<std::uint8_t>(static_cast<float>(a) +
                                     ((static_cast<float>(b) - static_cast<float>(a)) * amount) +
                                     0.5F);
  };
  return Color::rgba(channel(base.red(), over.red()), channel(base.green(), over.green()),
                     channel(base.blue(), over.blue()), base.alpha());
}

int row_radius(const PopupStyle& style) { return std::max(2, style.radius / 2); }

bool has_check_column(const MenuSpec& spec) {
  return std::any_of(spec.items.begin(), spec.items.end(), [](const MenuItem& item) {
    return !item.separator && item.checked.has_value();
  });
}

int row_height(const PopupStyle& style, const MenuItem& item) {
  return item.separator ? style.separator_height : style.item_height;
}

TextStyle text_style(const UiContext& ctx, const std::string& text, float size, Color color,
                     TextAlign align) {
  TextStyle out;
  out.text = text;
  out.font = ctx.font;
  out.size = size;
  out.color = color;
  out.align = align;
  return out;
}

Color item_text_color(const PopupStyle& style, const MenuItem& item, bool highlighted) {
  if (item.disabled) {
    return style.on_disabled;
  }
  return highlighted ? style.on_item_hover : style.on_surface;
}

int wrapped_height(const UiContext& ctx, const TextStyle& text, int width) {
  if (text.text.empty() || width <= 0) {
    return 0;
  }
  if (ctx.fonts != nullptr) {
    const Expected<Paragraph, FontError> built =
        Paragraph::build(*ctx.fonts, text, static_cast<float>(width));
    if (built) {
      return built.value().metrics().height;
    }
  }
  const int line = static_cast<int>(text.size * 1.4F);
  const int total = measure_text_width(nullptr, text.font, text.size, text.text);
  return line * std::max(1, (total + width - 1) / width);
}

int shortcut_column(const UiContext& ctx, const MenuSpec& spec) {
  int widest = 0;
  for (const MenuItem& item : spec.items) {
    if (!item.separator && !item.shortcut.empty()) {
      widest = std::max(
          widest, measure_text_width(ctx.fonts, ctx.font, ctx.style.font_size, item.shortcut));
    }
  }
  return widest > 0 ? widest + kShortcutGap : 0;
}

}  // namespace

PixelSize menu_size(const UiContext& ctx, const MenuSpec& spec) {
  const PopupStyle& style = ctx.style;
  const int check_column = has_check_column(spec) ? style.item_height : 0;
  int widest = 0;
  int height = 2 * (kFrame + kMenuInset);
  for (const MenuItem& item : spec.items) {
    height += row_height(style, item);
    if (!item.separator) {
      widest =
          std::max(widest, measure_text_width(ctx.fonts, ctx.font, style.font_size, item.text));
    }
  }
  const int width =
      std::max(spec.min_width, widest + check_column + shortcut_column(ctx, spec) +
                                   ((check_column > 0 ? 1 : 2) * style.padding) +
                                   (2 * style.padding) + (2 * (kFrame + kMenuInset)));
  return PixelSize{width, height};
}

std::vector<NodeId> build_menu(UiContext& ctx, NodeId parent, const MenuSpec& spec, int width) {
  const PopupStyle& style = ctx.style;
  const PixelSize size = menu_size(ctx, spec);
  const int check_column = has_check_column(spec) ? style.item_height : 0;

  NodeStyle background;
  background.fill = style.surface;
  background.border_color = style.border;
  background.border_width = BorderWidths::all(static_cast<float>(kFrame));
  background.radii = Radii::all(static_cast<float>(style.radius));
  ctx.tree.add_child(parent, PixelRect{0, 0, width, size.height}, background);

  std::vector<NodeId> rows;
  rows.reserve(spec.items.size());
  const int row_x = kFrame + kMenuInset;
  const int row_width = width - (2 * row_x);
  int y = row_x;
  for (const MenuItem& item : spec.items) {
    const int h = row_height(style, item);
    NodeStyle row_style;
    if (item.separator) {
      const NodeId row =
          ctx.tree.add_child(parent, PixelRect{row_x, y, row_width, h}, row_style);
      NodeStyle line;
      line.fill = style.border;
      ctx.tree.add_child(row, PixelRect{style.padding / 2, h / 2, row_width - style.padding, 1},
                         line);
      rows.push_back(row);
      y += h;
      continue;
    }
    row_style.fill = style.surface;
    row_style.disabled = item.disabled;
    row_style.radii = Radii::all(static_cast<float>(row_radius(style)));
    const NodeId row =
        ctx.tree.add_child(parent, PixelRect{row_x, y, row_width, h}, row_style);

    Widget widget;
    widget.kind = WidgetKind::kButton;
    widget.fill_normal = style.surface;
    widget.fill_hover = style.item_hover;
    widget.fill_pressed = style.item_pressed;
    widget.fill_disabled = style.surface;
    ctx.tables.widgets.attach(row, widget);

    if (check_column > 0 && item.checked.value_or(false)) {
      NodeStyle mark;
      mark.text = text_style(ctx, kCheckGlyph, style.font_size,
                             item.disabled ? style.on_disabled : style.accent, TextAlign::kCenter);
      ctx.tree.add_child(row, PixelRect{0, 0, check_column, h}, mark);
    }

    if (!item.shortcut.empty()) {
      NodeStyle accel;
      accel.text = text_style(ctx, item.shortcut, style.font_size,
                              item.disabled ? style.on_disabled
                                            : mix(style.on_surface, style.surface, 0.4F),
                              TextAlign::kRight);
      accel.text.inset = style.padding;
      ctx.tree.add_child(row, PixelRect{0, 0, row_width, h}, accel);
    }

    NodeStyle label;
    label.text = text_style(ctx, item.text, style.font_size,
                            item_text_color(style, item, false), TextAlign::kLeft);
    label.text.inset = check_column > 0 ? 0 : style.padding;
    ctx.tree.add_child(row, PixelRect{check_column, 0, row_width - check_column, h}, label);
    rows.push_back(row);
    y += h;
  }
  return rows;
}

void paint_menu_item(UiContext& ctx, NodeId row, const MenuItem& item, bool highlighted) {
  if (item.separator || !ctx.tree.is_valid(row)) {
    return;
  }
  const PopupStyle& style = ctx.style;
  ctx.tree.set_fill(row, highlighted && !item.disabled ? style.item_hover : style.surface);
  const std::vector<NodeId> kids = ctx.tree.children(row);
  if (kids.empty()) {
    return;
  }
  TextStyle text = ctx.tree.style(kids.back()).text;
  const Color color = item_text_color(style, item, highlighted);
  if (text.color != color) {
    text.color = color;
    ctx.tree.set_text(kids.back(), text);
  }
}

PixelSize tooltip_size(const UiContext& ctx, const std::string& text) {
  const int width = measure_text_width(ctx.fonts, ctx.font, ctx.style.small_font_size, text);
  return PixelSize{width + (2 * kTooltipPadX), kTooltipHeight};
}

void build_tooltip(UiContext& ctx, NodeId parent, const std::string& text, PixelSize size) {
  const PopupStyle& style = ctx.style;
  // Inverted (on_surface fill, surface text): a tooltip must read as
  // transient chrome, distinct from the menus and panels around it, in
  // either variant - so it needs no border.
  NodeStyle box;
  box.fill = style.on_surface;
  box.radii = Radii::all(static_cast<float>(row_radius(style)) + 1.0F);
  const NodeId node = ctx.tree.add_child(parent, PixelRect{0, 0, size.width, size.height}, box);

  NodeStyle label;
  label.text =
      text_style(ctx, text, style.small_font_size, style.surface, TextAlign::kCenter);
  ctx.tree.add_child(node, PixelRect{0, 0, size.width, size.height}, label);
}

DialogNodes build_dialog(UiContext& ctx, NodeId parent, PixelSize window,
                         const DialogSpec& spec) {
  const PopupStyle& style = ctx.style;
  const int pad = 3 * style.padding;
  DialogNodes nodes;

  NodeStyle scrim;
  scrim.fill = style.scrim;
  ctx.tree.add_child(parent, PixelRect{0, 0, window.width, window.height}, scrim);

  const int width = std::min(spec.panel_size.width, std::max(0, window.width - pad));
  const int inner = dialog_inner_width(ctx, window, spec);

  // Body text sits a step below the title: the same hue, pulled toward the
  // surface.
  TextStyle body_text = text_style(ctx, spec.body, style.font_size,
                                   mix(style.on_surface, style.surface, 0.22F), TextAlign::kLeft);
  body_text.wrap = true;
  const int body_text_height = wrapped_height(ctx, body_text, inner);
  const int body_area = body_text_height + spec.body_height;

  const int needed = pad + kTitleHeight + kTitleGap + body_area + pad + kButtonHeight + pad;
  const int height = std::max(spec.panel_size.height, needed);
  const int x = (window.width - width) / 2;
  const int y = std::max(0, (window.height - height) / 2);

  NodeStyle panel;
  panel.fill = style.surface;
  panel.border_color = style.border;
  panel.border_width = BorderWidths::all(1.0F);
  panel.radii = Radii::all(static_cast<float>(style.radius + 4));
  panel.shadow = ShadowStyle{};
  panel.shadow->color = Color::from_argb(0x59000000);
  panel.shadow->blur_radius = 14.0F;
  panel.shadow->offset_y = 8.0F;
  nodes.panel = ctx.tree.add_child(parent, PixelRect{x, y, width, height}, panel);

  NodeStyle title;
  title.text =
      text_style(ctx, spec.title, style.font_size + kTitleGrow, style.on_surface, TextAlign::kLeft);
  ctx.tree.add_child(nodes.panel, PixelRect{pad, pad, inner, kTitleHeight}, title);

  NodeStyle body;
  nodes.body = ctx.tree.add_child(
      nodes.panel, PixelRect{pad, pad + kTitleHeight + kTitleGap, inner, body_area}, body);
  nodes.body_content_top = body_text_height;
  if (body_text_height > 0) {
    NodeStyle body_label;
    body_label.text = body_text;
    ctx.tree.add_child(nodes.body, PixelRect{0, 0, inner, body_text_height}, body_label);
  }

  std::vector<int> widths;
  int total = 0;
  for (const std::string& label : spec.buttons) {
    const int w = std::max(
        kButtonMinWidth,
        measure_text_width(ctx.fonts, ctx.font, style.font_size, label) + (4 * style.padding));
    widths.push_back(w);
    total += w;
  }
  total += style.padding * static_cast<int>(spec.buttons.empty() ? 0 : spec.buttons.size() - 1);
  int bx = width - pad - total;
  const int by = height - pad - kButtonHeight;
  for (std::size_t i = 0; i < spec.buttons.size(); ++i) {
    const bool is_default = spec.default_button == static_cast<int>(i);
    const Color normal = is_default ? style.accent : style.surface;
    NodeStyle button;
    button.fill = normal;
    button.border_color = is_default ? style.accent : style.border;
    button.border_width = BorderWidths::all(1.0F);
    button.radii = Radii::all(static_cast<float>(style.radius));
    const NodeId node =
        ctx.tree.add_child(nodes.panel, PixelRect{bx, by, widths[i], kButtonHeight}, button);
    Widget widget;
    widget.kind = WidgetKind::kButton;
    widget.fill_normal = normal;
    widget.fill_hover = is_default ? style.accent_hover : style.item_hover;
    widget.fill_pressed = is_default ? style.accent_pressed : style.item_pressed;
    widget.fill_disabled = style.surface;
    ctx.tables.widgets.attach(node, widget);

    NodeStyle label;
    label.text =
        text_style(ctx, spec.buttons[i], style.font_size,
                   is_default ? style.on_accent : style.on_surface, TextAlign::kCenter);
    ctx.tree.add_child(node, PixelRect{0, 0, widths[i], kButtonHeight}, label);
    nodes.buttons.push_back(node);
    bx += widths[i] + style.padding;
  }
  return nodes;
}

int dialog_inner_width(const UiContext& ctx, PixelSize window, const DialogSpec& spec) {
  const int pad = 3 * ctx.style.padding;
  const int width = std::min(spec.panel_size.width, std::max(0, window.width - pad));
  return std::max(0, width - (2 * pad));
}

void paint_dialog_button(UiContext& ctx, NodeId button, bool is_default, bool disabled) {
  if (!ctx.tree.is_valid(button)) {
    return;
  }
  const PopupStyle& style = ctx.style;
  NodeStyle node = ctx.tree.style(button);
  node.disabled = disabled;
  const bool accent = is_default && !disabled;
  node.fill = accent ? style.accent : style.surface;
  node.border_color = accent ? style.accent : style.border;
  ctx.tree.set_style(button, node);
  const std::vector<NodeId> kids = ctx.tree.children(button);
  if (!kids.empty()) {
    TextStyle text = ctx.tree.style(kids.back()).text;
    text.color = is_default ? style.on_accent : style.on_surface;
    if (disabled) {
      text.color = style.on_disabled;
    }
    ctx.tree.set_text(kids.back(), text);
  }
  ctx.tables.widgets.refresh_subtree(ctx.tree, button, ctx.tables.interaction);
}

}  // namespace dg::ui_visuals
