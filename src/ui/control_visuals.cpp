#include "ui/control_visuals.h"

#include <algorithm>
#include <cstdint>
#include <optional>

#include "drawgui/render/paragraph.h"
#include "drawgui/theme/token_ids.generated.h"
#include "drawgui/widget/widget_set.h"

namespace dg {
namespace {

void read_color(const Theme& theme, ThemeVariant variant, dg_token_id id, Color& out) {
  if (const std::optional<Color> value = theme.color_value(id, variant)) {
    out = *value;
  }
}

void read_int(const Theme& theme, dg_token_id id, int& out) {
  if (const std::optional<int> value = theme.int_value(id)) {
    out = *value;
  }
}

}  // namespace

ControlStyle control_style_from_theme(const Theme& theme, ThemeVariant variant) {
  ControlStyle style;
  read_color(theme, variant, DG_TOKEN_COLOR_SURFACE, style.surface);
  read_color(theme, variant, DG_TOKEN_COLOR_ON_SURFACE, style.on_surface);
  read_color(theme, variant, DG_TOKEN_COLOR_BORDER, style.border);
  read_color(theme, variant, DG_TOKEN_COLOR_PRIMARY, style.accent);
  read_color(theme, variant, DG_TOKEN_COLOR_ON_PRIMARY, style.on_accent);
  read_color(theme, variant, DG_TOKEN_COLOR_ON_DISABLED, style.on_disabled);
  read_int(theme, DG_TOKEN_RADIUS_SM, style.radius);
  read_int(theme, DG_TOKEN_SPACE_MD, style.padding);
  return style;
}

}  // namespace dg

namespace dg::control_visuals {
namespace {

constexpr int kFrame = 1;      // tree/table outer border
constexpr int kIndicator = 3;  // active-tab underline thickness
constexpr int kIndicatorInset = 8;  // underline is narrower than the tab

// `base` with `over` laid on top at `amount` (0..1); alpha from `base`.
// Same computation as popup_style_from_theme's soft tint.
Color mix(Color base, Color over, float amount) {
  const auto channel = [amount](std::uint8_t a, std::uint8_t b) {
    return static_cast<std::uint8_t>(static_cast<float>(a) +
                                     ((static_cast<float>(b) - static_cast<float>(a)) * amount) +
                                     0.5F);
  };
  return Color::rgba(channel(base.red(), over.red()), channel(base.green(), over.green()),
                     channel(base.blue(), over.blue()), base.alpha());
}

bool is_dark(const ControlStyle& style) {
  const Color c = style.surface;
  return (299 * c.red()) + (587 * c.green()) + (114 * c.blue()) < 128000;
}

// Neutral hover / header / track shades derived from surface + on-surface.
Color neutral(const ControlStyle& style, float light, float dark) {
  return mix(style.surface, style.on_surface, is_dark(style) ? dark : light);
}
Color muted_text(const ControlStyle& style) { return mix(style.on_surface, style.surface, 0.30F); }

TextStyle text_style(const ControlContext& ctx, const std::string& text, Color color,
                     TextAlign align) {
  TextStyle out;
  out.text = text;
  out.font = ctx.font;
  out.size = ctx.style.font_size;
  out.color = color;
  out.align = align;
  return out;
}

void set_text_if(RenderTree& tree, NodeId node, const std::string& text, Color color) {
  TextStyle style = tree.style(node).text;
  if (style.text != text || style.color != color) {
    style.text = text;
    style.color = color;
    tree.set_text(node, style);
  }
}

void set_fill_if(RenderTree& tree, NodeId node, Color fill) {
  if (tree.style(node).fill != fill) {
    tree.set_fill(node, fill);
  }
}

void set_bounds_if(RenderTree& tree, NodeId node, const PixelRect& rect) {
  if (tree.local_bounds(node) != rect) {
    tree.set_local_bounds(node, rect);
  }
}

// The focusable frame both virtualized controls share.
NodeId build_frame(ControlContext& ctx, NodeId parent, BoxStyle box, int height) {
  const ControlStyle& style = ctx.style;
  box.kind = LayoutKind::kColumn;
  box.cross_align = CrossAlign::kStretch;
  box.border = EdgeInsets::all(kFrame);
  box.height = height + (2 * kFrame);
  NodeStyle frame;
  frame.fill = style.surface;
  frame.border_color = style.border;
  frame.border_width = BorderWidths::all(static_cast<float>(kFrame));
  const NodeId root = ctx.layout.add_child(parent, box, frame);
  Widget widget;
  widget.kind = WidgetKind::kButton;
  widget.fill_normal = style.surface;
  widget.fill_hover = style.surface;
  widget.fill_pressed = style.surface;
  ctx.tables.widgets.attach(root, widget);
  return root;
}

// The kList itself, `viewport_height` tall, with the scrollbar gutter.
NodeId build_list(ControlContext& ctx, NodeId parent, int viewport_height) {
  const int gutter = ctx.tables.widgets.scrollbar_style().size;
  BoxStyle box;
  box.kind = LayoutKind::kLeaf;
  box.height = viewport_height;
  box.padding.right = gutter;
  NodeStyle style;
  style.fill = ctx.style.surface;
  style.overflow = Overflow::kClip;
  const NodeId list = ctx.layout.add_child(parent, box, style);
  return list;
}

int pool_size(const ControlContext& ctx, int viewport_height) {
  const int row = std::max(1, ctx.style.row_height);
  return ((std::max(0, viewport_height) + row - 1) / row) + 1;
}

void finish_list(ControlContext& ctx, RowListNodes& nodes, int count) {
  Widget list;
  list.kind = WidgetKind::kList;
  list.list_axis = ScrollAxis::kVertical;
  list.list_item_count = count;
  list.list_item_extent = std::max(1, ctx.style.row_height);
  list.list_pool = nodes.pool;
  list.list_assigned.assign(nodes.pool.size(), -1);
  ctx.tables.widgets.attach(nodes.list, list);
  nodes.track = ctx.layout.add_child(nodes.list, BoxStyle{}, NodeStyle{});
  nodes.thumb = ctx.layout.add_child(nodes.track, BoxStyle{}, NodeStyle{});
  ctx.tables.widgets.attach_scrollbar(ctx.tree(), nodes.list, nodes.track, nodes.thumb);
}

// A cell's BoxStyle from its column: px, percent, or the remaining space.
BoxStyle column_box(const TableColumn& column, int height) {
  BoxStyle box;
  box.height = height;
  if (column.width_px.has_value()) {
    box.width = column.width_px;
  } else if (column.width_percent.has_value()) {
    box.percent.width = column.width_percent;
  } else {
    box.grow = 1;
    box.shrink = 1;
  }
  return box;
}

}  // namespace

Color secondary_text(const ControlStyle& style) {
  return muted_text(style);
}

NodeId add_part(ControlContext& ctx, NodeId row) {
  BoxStyle box;
  box.kind = LayoutKind::kLeaf;
  box.width = 0;
  box.height = 0;
  return ctx.layout.add_child(row, box, NodeStyle{});
}

void hide_part(ControlContext& ctx, NodeId node) {
  set_bounds_if(ctx.tree(), node, PixelRect{0, 0, 0, 0});
}

void paint_check(ControlContext& ctx, NodeId node, int x, int height, std::optional<bool> check) {
  if (!check.has_value()) {
    hide_part(ctx, node);
    return;
  }
  const ControlStyle& style = ctx.style;
  RenderTree& tree = ctx.tree();
  set_bounds_if(tree, node,
                PixelRect{x, std::max(0, (height - kCheckSize) / 2), kCheckSize, kCheckSize});
  NodeStyle want = tree.style(node);
  want.radii = Radii::all(3.0F);
  want.border_width = BorderWidths::all(1.0F);
  want.fill = *check ? style.accent : style.surface;
  want.border_color = *check ? style.accent : muted_text(style);
  want.text = text_style(ctx, *check ? "\u2713" : "", style.on_accent, TextAlign::kCenter);
  want.text.size = std::max(8.0F, style.font_size - 3.0F);
  const NodeStyle& now = tree.style(node);
  if (now.fill != want.fill || now.border_color != want.border_color ||
      now.text.text != want.text.text || now.text.color != want.text.color ||
      !(now.radii == want.radii) || !(now.border_width == want.border_width) ||
      now.text.size != want.text.size || now.text.font != want.text.font) {
    tree.set_style(node, want);
  }
}

void paint_icon(ControlContext& ctx, NodeId node, const PixelRect& rect, ImageId image) {
  if (!image.is_valid()) {
    hide_part(ctx, node);
    return;
  }
  RenderTree& tree = ctx.tree();
  set_bounds_if(tree, node, rect);
  const NodeStyle& now = tree.style(node);
  if (now.image.source != image || now.image.fit != ImageFit::kContain) {
    NodeStyle want = now;
    want.image.source = image;
    want.image.fit = ImageFit::kContain;
    tree.set_style(node, want);
  }
}

void paint_part_text(ControlContext& ctx, NodeId node, const PixelRect& rect,
                     const std::string& text, TextAlign align) {
  RenderTree& tree = ctx.tree();
  if (text.empty() || rect.width <= 0) {
    hide_part(ctx, node);
    return;
  }
  set_bounds_if(tree, node, rect);
  const TextStyle& now = tree.style(node).text;
  const Color color = muted_text(ctx.style);
  if (now.text != text || now.color != color || now.align != align || now.font != ctx.font ||
      now.size != ctx.style.font_size) {
    tree.set_text(node, text_style(ctx, text, color, align));
  }
}

// A selected row: soft primary tint of the surface, text stays on-surface.
Color selection_fill(const ControlStyle& style) {
  return mix(style.surface, style.accent, is_dark(style) ? 0.30F : 0.16F);
}

int text_width(const ControlContext& ctx, float size, const std::string& text) {
  if (text.empty()) {
    return 0;
  }
  if (ctx.fonts != nullptr) {
    TextStyle style;
    style.text = text;
    style.font = ctx.font;
    style.size = size;
    style.align = TextAlign::kLeft;
    const Expected<Paragraph, FontError> built = Paragraph::build(*ctx.fonts, style, 100000.0F);
    if (built) {
      return static_cast<int>(built.value().caret_x(static_cast<int>(text.size())) + 0.999F);
    }
  }
  return static_cast<int>(static_cast<float>(text.size()) * size * 0.6F);
}

ProgressNodes build_progress(ControlContext& ctx, NodeId parent, const BoxStyle& box) {
  const ControlStyle& style = ctx.style;
  BoxStyle track_box = box;
  track_box.kind = LayoutKind::kLeaf;
  NodeStyle track;
  // Fully rounded capsule when the height is known.
  const float pill =
      box.height.has_value() ? static_cast<float>(*box.height) / 2.0F
                             : static_cast<float>(style.radius);
  track.fill = neutral(style, 0.12F, 0.14F);
  track.radii = Radii::all(pill);
  track.overflow = Overflow::kClip;
  ProgressNodes nodes;
  nodes.track = ctx.layout.add_child(parent, track_box, track);

  BoxStyle fill_box;
  fill_box.percent.height = 100.0F;
  NodeStyle fill;
  fill.fill = style.accent;
  fill.radii = Radii::all(pill);
  nodes.fill = ctx.layout.add_child(nodes.track, fill_box, fill);
  return nodes;
}

TabsNodes build_tabs(ControlContext& ctx, NodeId parent, const TabsSpec& spec) {
  const ControlStyle& style = ctx.style;
  TabsNodes nodes;
  BoxStyle root_box = spec.box;
  root_box.kind = LayoutKind::kColumn;
  root_box.cross_align = CrossAlign::kStretch;
  nodes.root = ctx.layout.add_child(parent, root_box, NodeStyle{});

  BoxStyle strip_box;
  strip_box.kind = LayoutKind::kRow;
  strip_box.height = style.tab_height;
  strip_box.cross_align = CrossAlign::kStretch;
  strip_box.border.bottom = 1;
  // The strip's own surface spans the whole tabs node, not only the tabs.
  NodeStyle strip;
  strip.fill = style.surface;
  strip.border_color = style.border;
  strip.border_width.bottom = 1.0F;
  const NodeId strip_node = ctx.layout.add_child(nodes.root, strip_box, strip);

  for (const TabItem& item : spec.tabs) {
    BoxStyle tab_box;
    tab_box.kind = LayoutKind::kColumn;
    tab_box.cross_align = CrossAlign::kStretch;
    tab_box.width = text_width(ctx, style.font_size, item.label) + (2 * style.padding);
    NodeStyle tab;
    tab.fill = style.surface;
    tab.disabled = item.disabled;
    const NodeId node = ctx.layout.add_child(strip_node, tab_box, tab);
    Widget widget;
    widget.kind = WidgetKind::kButton;
    widget.fill_normal = style.surface;
    widget.fill_hover = neutral(style, 0.05F, 0.07F);
    widget.fill_pressed = neutral(style, 0.09F, 0.12F);
    widget.fill_disabled = style.surface;
    ctx.tables.widgets.attach(node, widget);

    BoxStyle label_box;
    label_box.grow = 1;
    NodeStyle label;
    label.text = text_style(ctx, item.label, style.on_surface, TextAlign::kCenter);
    nodes.labels.push_back(ctx.layout.add_child(node, label_box, label));

    BoxStyle bar_box;
    bar_box.height = kIndicator;
    bar_box.margin.left = kIndicatorInset;
    bar_box.margin.right = kIndicatorInset;
    NodeStyle bar;
    const auto top = static_cast<float>(kIndicator);
    bar.radii = Radii{top, top, 0.0F, 0.0F};
    nodes.indicators.push_back(ctx.layout.add_child(node, bar_box, bar));
    nodes.tabs.push_back(node);
  }

  BoxStyle host_box;
  host_box.kind = LayoutKind::kLeaf;
  host_box.grow = 1;
  nodes.host = ctx.layout.add_child(nodes.root, host_box, NodeStyle{});
  for (std::size_t i = 0; i < spec.tabs.size(); ++i) {
    BoxStyle panel_box;
    panel_box.kind = LayoutKind::kColumn;
    panel_box.percent.width = 100.0F;
    panel_box.percent.height = 100.0F;
    NodeStyle panel;
    panel.overflow = Overflow::kClip;
    nodes.panels.push_back(ctx.layout.add_child(nodes.host, panel_box, panel));
  }
  return nodes;
}

void paint_tab(ControlContext& ctx, NodeId label, NodeId indicator, const TabItem& item,
               bool selected) {
  const ControlStyle& style = ctx.style;
  Color text = selected ? style.accent : muted_text(style);
  if (item.disabled) {
    text = style.on_disabled;
  }
  set_text_if(ctx.tree(), label, item.label, text);
  set_fill_if(ctx.tree(), indicator, selected ? style.accent : Color{});
}

RowListNodes build_tree(ControlContext& ctx, NodeId parent, const TreeViewSpec& spec) {
  const ControlStyle& style = ctx.style;
  RowListNodes nodes;
  nodes.root = build_frame(ctx, parent, spec.box, spec.viewport_height);
  nodes.header = nodes.root;
  nodes.list = build_list(ctx, nodes.root, spec.viewport_height);
  const int count = pool_size(ctx, spec.viewport_height);
  const bool rich = std::any_of(spec.items.begin(), spec.items.end(), [](const TreeItem& item) {
    return item.icon.is_valid() || item.check.has_value() || !item.secondary.empty();
  });
  for (int i = 0; i < count; ++i) {
    BoxStyle row_box;
    row_box.kind = LayoutKind::kLeaf;
    row_box.percent.width = 100.0F;
    row_box.height = style.row_height;
    NodeStyle row_style;
    row_style.radii = Radii::all(static_cast<float>(style.radius));
    const NodeId row = ctx.layout.add_child(nodes.list, row_box, row_style);
    BoxStyle part_box;
    part_box.width = style.indent;
    part_box.height = style.row_height;
    NodeStyle disclosure;
    disclosure.text = text_style(ctx, "", style.on_surface, TextAlign::kCenter);
    ctx.layout.add_child(row, part_box, disclosure);
    NodeStyle label;
    label.text = text_style(ctx, "", style.on_surface, TextAlign::kLeft);
    ctx.layout.add_child(row, part_box, label);
    if (rich) {
      for (int part = 0; part < 3; ++part) {  // check, icon, secondary
        (void)add_part(ctx, row);
      }
    }
    nodes.pool.push_back(row);
  }
  finish_list(ctx, nodes, 0);
  return nodes;
}

void paint_tree_row(ControlContext& ctx, NodeId row, const TreeItem& item, bool has_children,
                    bool expanded, bool selected) {
  const ControlStyle& style = ctx.style;
  RenderTree& tree = ctx.tree();
  const std::vector<NodeId> kids = tree.children(row);
  if (kids.size() < 2) {
    return;
  }
  const PixelRect bounds = tree.local_bounds(row);
  const int x = tree_disclosure_x(ctx, item.depth);
  set_bounds_if(tree, kids[0], PixelRect{x, 0, style.indent, bounds.height});

  int label_x = x + style.indent;
  if (kids.size() >= 5) {
    paint_check(ctx, kids[2], label_x, bounds.height, item.check);
    if (item.check.has_value()) {
      label_x += kCheckSize + kPartGap;
    }
    paint_icon(ctx, kids[3],
               PixelRect{label_x, std::max(0, (bounds.height - kIconSize) / 2), kIconSize,
                         kIconSize},
               item.icon);
    if (item.icon.is_valid()) {
      label_x += kIconSize + kPartGap;
    }
    const int label_w =
        item.secondary.empty() ? 0 : text_width(ctx, style.font_size, item.label);
    const int sec_x = label_x + label_w + style.padding;
    paint_part_text(ctx, kids[4],
                    PixelRect{sec_x, 0, std::max(0, bounds.width - sec_x), bounds.height},
                    item.secondary, TextAlign::kLeft);
  }
  set_bounds_if(tree, kids[1],
                PixelRect{label_x, 0, std::max(0, bounds.width - label_x), bounds.height});
  set_fill_if(tree, row, selected ? selection_fill(style) : Color{});
  const char* disclosure = expanded ? "\u25BE" : "\u25B8";
  set_text_if(tree, kids[0], has_children ? disclosure : "",
              selected ? style.accent : muted_text(style));
  set_text_if(tree, kids[1], item.label, style.on_surface);
}

int tree_disclosure_x(const ControlContext& ctx, int depth) {
  return (ctx.style.padding / 2) + (std::max(0, depth) * ctx.style.indent);
}

RowListNodes build_table(ControlContext& ctx, NodeId parent, const TableSpec& spec) {
  const ControlStyle& style = ctx.style;
  const int gutter = ctx.tables.widgets.scrollbar_style().size;
  RowListNodes nodes;
  const int header_height = spec.header_height > 0 ? spec.header_height : style.row_height;
  nodes.root = build_frame(ctx, parent, spec.box, header_height + spec.viewport_height);

  BoxStyle header_box;
  header_box.kind = LayoutKind::kRow;
  header_box.height = header_height;
  header_box.padding.right = gutter;
  const Color header_fill = neutral(style, 0.05F, 0.07F);
  NodeStyle header;
  header.fill = header_fill;
  header_box.border.bottom = 1;
  header.border_color = style.border;
  header.border_width.bottom = 1.0F;
  nodes.header = ctx.layout.add_child(nodes.root, header_box, header);
  for (const TableColumn& column : spec.columns) {
    NodeStyle cell;
    cell.fill = header_fill;
    cell.text = text_style(ctx, column.title, muted_text(style), TextAlign::kLeft);
    cell.text.inset = style.padding;
    BoxStyle cell_box = column_box(column, header_height - 1);
    if (&column != &spec.columns.back()) {
      // Column separator.
      cell_box.border.right = 1;
      cell.border_color = style.border;
      cell.border_width.right = 1.0F;
    }
    const NodeId node = ctx.layout.add_child(nodes.header, cell_box, cell);
    if (column.sortable) {
      Widget widget;
      widget.kind = WidgetKind::kButton;
      widget.tab_index = -1;
      widget.fill_normal = header_fill;
      widget.fill_hover = neutral(style, 0.10F, 0.13F);
      widget.fill_pressed = neutral(style, 0.14F, 0.18F);
      ctx.tables.widgets.attach(node, widget);
    }
    nodes.header_cells.push_back(node);
  }

  nodes.list = build_list(ctx, nodes.root, spec.viewport_height);
  const int count = pool_size(ctx, spec.viewport_height);
  const bool rich = std::any_of(spec.parts.begin(), spec.parts.end(), [](const auto& row) {
    return std::any_of(row.begin(), row.end(), [](const TableCellPart& part) {
      return part.icon.is_valid() || part.check.has_value();
    });
  });
  for (int i = 0; i < count; ++i) {
    BoxStyle row_box;
    row_box.kind = LayoutKind::kRow;
    row_box.percent.width = 100.0F;
    row_box.height = style.row_height;
    const NodeId row = ctx.layout.add_child(nodes.list, row_box, NodeStyle{});
    for (const TableColumn& column : spec.columns) {
      NodeStyle cell;
      cell.text = text_style(ctx, "", style.on_surface, TextAlign::kLeft);
      cell.text.inset = style.padding;
      BoxStyle cell_box = column_box(column, style.row_height);
      if (rich) {
        cell_box.kind = LayoutKind::kLeaf;
      }
      const NodeId cell_node = ctx.layout.add_child(row, cell_box, cell);
      if (rich) {
        (void)add_part(ctx, cell_node);  // check
        (void)add_part(ctx, cell_node);  // icon
      }
    }
    nodes.pool.push_back(row);
  }
  finish_list(ctx, nodes, 0);
  return nodes;
}

void paint_table_row(ControlContext& ctx, NodeId row, const std::vector<std::string>* cells,
                     const std::vector<TableCellPart>* parts, bool selected) {
  const ControlStyle& style = ctx.style;
  RenderTree& tree = ctx.tree();
  set_fill_if(tree, row, selected ? selection_fill(style) : Color{});
  const Color text = style.on_surface;
  const std::vector<NodeId> kids = tree.children(row);
  static const std::string kEmpty;
  for (std::size_t i = 0; i < kids.size(); ++i) {
    const bool has = cells != nullptr && i < cells->size();
    const std::vector<NodeId> cell_parts = tree.children(kids[i]);
    int pad = 0;
    if (cell_parts.size() >= 2) {
      const TableCellPart part =
          parts != nullptr && i < parts->size() ? (*parts)[i] : TableCellPart{};
      const int height = tree.local_bounds(kids[i]).height;
      int x = style.padding;
      paint_check(ctx, cell_parts[0], x, height, part.check);
      if (part.check.has_value()) {
        x += kCheckSize + kPartGap;
      }
      paint_icon(ctx, cell_parts[1],
                 PixelRect{x, std::max(0, (height - kIconSize) / 2), kIconSize, kIconSize},
                 part.icon);
      if (part.icon.is_valid()) {
        x += kIconSize + kPartGap;
      }
      pad = x - style.padding;
    }
    set_text_if(tree, kids[i], has ? (*cells)[i] : kEmpty, text);
    if (tree.style(kids[i]).text.pad_left != pad) {
      TextStyle shifted = tree.style(kids[i]).text;
      shifted.pad_left = pad;
      tree.set_text(kids[i], shifted);
    }
  }
}

void paint_table_header(ControlContext& ctx, NodeId cell, const std::string& title) {
  // A sorted column's title reads at full strength; others stay muted.
  const bool sorted = title.size() >= 4 && (title.compare(title.size() - 4, 4, " \u25B2") == 0 ||
                                            title.compare(title.size() - 4, 4, " \u25BC") == 0);
  set_text_if(ctx.tree(), cell, title, sorted ? ctx.style.on_surface : muted_text(ctx.style));
}

}  // namespace dg::control_visuals
