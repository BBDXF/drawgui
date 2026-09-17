#include "showcase_scene.h"

#include <algorithm>
#include <cstdint>
#include <utility>

#include "drawgui/graphics/types.h"
#include "drawgui/layout/box.h"
#include "drawgui/props/node_props.h"
#include "drawgui/render/paragraph.h"
#include "drawgui/render/prop_ids.generated.h"
#include "drawgui/theme/theme_loader.h"
#include "drawgui/theme/token_ids.generated.h"

namespace showcase_scene {
namespace {

using dg::BorderWidths;
using dg::BoxStyle;
using dg::Color;
using dg::CrossAlign;
using dg::EdgeInsets;
using dg::LayoutKind;
using dg::LayoutTree;
using dg::ListSlot;
using dg::NodeId;
using dg::NodeStyle;
using dg::Overflow;
using dg::Paragraph;
using dg::Radii;
using dg::ScrollAxis;
using dg::ShadowStyle;
using dg::TextAlign;
using dg::TextStyle;
using dg::Widget;
using dg::WidgetKind;

Color tok_color(const dg::Theme& theme, dg::ThemeVariant variant, dg_token_id id,
                std::uint32_t fallback) {
  return theme.color_value(id, variant).value_or(Color::from_argb(fallback));
}

// Every panel/button radius and spacing this scene uses comes from the SAME
// two int tokens (radius.md/space.md, plus radius.sm for chips) - no
// literal pixel constant is a corner radius or a gap anywhere in this file.
int tok_int(const dg::Theme& theme, dg_token_id id, int fallback) {
  return theme.int_value(id).value_or(fallback);
}

void bind_panel(Scene& scene, NodeId node, bool with_radius) {
  (void)dg::bind_token(scene.tree, scene.bindings, node, DG_PROP_BACKGROUND_COLOR, scene.theme,
                       scene.variant, DG_TOKEN_COLOR_SURFACE);
  (void)dg::bind_token(scene.tree, scene.bindings, node, DG_PROP_BORDER_COLOR, scene.theme,
                       scene.variant, DG_TOKEN_COLOR_BORDER);
  if (with_radius) {
    for (dg_prop_id corner : {DG_PROP_BORDER_RADIUS_TL, DG_PROP_BORDER_RADIUS_TR,
                              DG_PROP_BORDER_RADIUS_BR, DG_PROP_BORDER_RADIUS_BL}) {
      (void)dg::bind_token(scene.tree, scene.bindings, node, corner, scene.theme, scene.variant,
                           DG_TOKEN_RADIUS_MD);
    }
  }
}

}  // namespace

void paint_interactive_widget(Scene& scene, NodeId id, WidgetKind kind) {
  const Color surface =
      tok_color(scene.theme, scene.variant, DG_TOKEN_COLOR_SURFACE, 0xFF2C3644);
  const Color hover =
      tok_color(scene.theme, scene.variant, DG_TOKEN_COLOR_PRIMARY_HOVER, 0xFF3B4A5E);
  const Color pressed =
      tok_color(scene.theme, scene.variant, DG_TOKEN_COLOR_PRIMARY_PRESSED, 0xFF1F2731);
  const Color primary =
      tok_color(scene.theme, scene.variant, DG_TOKEN_COLOR_PRIMARY, 0xFF2E86DE);
  const Color off = tok_color(scene.theme, scene.variant, DG_TOKEN_COLOR_SURFACE, 0xFF10151C);

  if (!scene.widgets.has(id)) {
    return;
  }
  // WidgetSet has no public mutable accessor other than attach() (re-attach
  // replaces the whole entry) - reading the existing entry first is what
  // keeps every OTHER field (options/value/text/list bookkeeping) intact
  // across a theme switch rather than resetting it.
  Widget widget = scene.widgets.at(id);
  widget.fill_normal = surface;
  widget.fill_hover = hover;
  widget.fill_pressed = pressed;
  if (kind == WidgetKind::kCheckbox) {
    widget.indicator_on = primary;
    widget.indicator_off = off;
  }
  scene.widgets.attach(id, widget);
  scene.widgets.refresh(scene.tree.render(), id, scene.interaction.state_of(id));
}

namespace {

NodeId add_button(Scene& scene, NodeId parent, int width, int height, const std::string& label,
                  TextAlign align, int inset) {
  BoxStyle box;
  box.kind = LayoutKind::kLeaf;
  box.width = width;
  box.height = height;
  const NodeId node = scene.tree.add_child(parent, box, NodeStyle{});
  bind_panel(scene, node, true);
  (void)dg::set_prop(scene.tree, node, DG_PROP_BORDER_WIDTH_L, dg::PropValue::length(1.0F));
  (void)dg::set_prop(scene.tree, node, DG_PROP_BORDER_WIDTH_T, dg::PropValue::length(1.0F));
  (void)dg::set_prop(scene.tree, node, DG_PROP_BORDER_WIDTH_R, dg::PropValue::length(1.0F));
  (void)dg::set_prop(scene.tree, node, DG_PROP_BORDER_WIDTH_B, dg::PropValue::length(1.0F));

  BoxStyle label_box;
  label_box.width = width;
  label_box.height = height;
  NodeStyle label_style;
  label_style.text.text = label;
  label_style.text.font = scene.ui_font;
  label_style.text.size = 14.0F;
  label_style.text.color =
      tok_color(scene.theme, scene.variant, DG_TOKEN_COLOR_ON_SURFACE, 0xFFE8EDF4);
  label_style.text.align = align;
  label_style.text.inset = inset;
  scene.tree.add_child(node, label_box, label_style);

  Widget widget;
  widget.kind = WidgetKind::kButton;
  scene.widgets.attach(node, widget);
  paint_interactive_widget(scene, node, WidgetKind::kButton);
  return node;
}

NodeId add_checkbox(Scene& scene, NodeId parent, std::optional<int> group,
                    const std::string& caption) {
  BoxStyle row;
  row.kind = LayoutKind::kRow;
  row.gap = tok_int(scene.theme, DG_TOKEN_SPACE_SM, 8);
  row.cross_align = CrossAlign::kCenter;
  const NodeId container = scene.tree.add_child(parent, row, NodeStyle{});

  BoxStyle box;
  box.kind = LayoutKind::kLeaf;
  box.width = 24;
  box.height = 24;
  box.padding = EdgeInsets::all(5);
  const NodeId node = scene.tree.add_child(container, box, NodeStyle{});
  bind_panel(scene, node, true);
  (void)dg::set_prop(scene.tree, node, DG_PROP_BORDER_WIDTH_L, dg::PropValue::length(1.0F));
  (void)dg::set_prop(scene.tree, node, DG_PROP_BORDER_WIDTH_T, dg::PropValue::length(1.0F));
  (void)dg::set_prop(scene.tree, node, DG_PROP_BORDER_WIDTH_R, dg::PropValue::length(1.0F));
  (void)dg::set_prop(scene.tree, node, DG_PROP_BORDER_WIDTH_B, dg::PropValue::length(1.0F));

  BoxStyle indicator_box;
  indicator_box.width = 14;
  indicator_box.height = 14;
  const NodeId indicator = scene.tree.add_child(node, indicator_box, NodeStyle{});

  BoxStyle label_box;
  label_box.height = 24;
  NodeStyle label_style;
  label_style.text.text = caption;
  label_style.text.font = scene.ui_font;
  label_style.text.size = 13.0F;
  label_style.text.color =
      tok_color(scene.theme, scene.variant, DG_TOKEN_COLOR_ON_SURFACE, 0xFFE8EDF4);
  label_style.text.align = TextAlign::kLeft;
  scene.tree.add_child(container, label_box, label_style);

  Widget widget;
  widget.kind = WidgetKind::kCheckbox;
  widget.indicator = indicator;
  widget.group = group;
  scene.widgets.attach(node, widget);
  scene.widgets.attach(indicator, Widget{});
  paint_interactive_widget(scene, node, WidgetKind::kCheckbox);
  return node;
}

NodeId add_slider(Scene& scene, NodeId parent, int width) {
  BoxStyle track_box;
  track_box.kind = LayoutKind::kLeaf;
  track_box.width = width;
  track_box.height = 10;
  const NodeId track = scene.tree.add_child(parent, track_box, NodeStyle{});
  bind_panel(scene, track, true);

  BoxStyle thumb_box;
  thumb_box.width = 18;
  thumb_box.height = 18;
  const NodeId thumb = scene.tree.add_child(track, thumb_box, NodeStyle{});
  (void)dg::bind_token(scene.tree, scene.bindings, thumb, DG_PROP_BACKGROUND_COLOR, scene.theme,
                       scene.variant, DG_TOKEN_COLOR_PRIMARY);
  for (dg_prop_id corner : {DG_PROP_BORDER_RADIUS_TL, DG_PROP_BORDER_RADIUS_TR,
                            DG_PROP_BORDER_RADIUS_BR, DG_PROP_BORDER_RADIUS_BL}) {
    (void)dg::bind_token(scene.tree, scene.bindings, thumb, corner, scene.theme, scene.variant,
                         DG_TOKEN_RADIUS_MD);
  }

  Widget widget;
  widget.kind = WidgetKind::kSlider;
  widget.thumb = thumb;
  widget.min_value = 0.0F;
  widget.max_value = 1.0F;
  widget.value = 0.6F;
  scene.widgets.attach(track, widget);
  scene.widgets.attach(thumb, Widget{});
  return track;
}

NodeStyle field_style(const Scene& scene, bool focused) {
  NodeStyle style;
  style.fill = tok_color(scene.theme, scene.variant, DG_TOKEN_COLOR_SURFACE, 0xFF161C24);
  style.border_color =
      focused ? tok_color(scene.theme, scene.variant, DG_TOKEN_COLOR_PRIMARY, 0xFF2E86DE)
              : tok_color(scene.theme, scene.variant, DG_TOKEN_COLOR_BORDER, 0xFF2C3644);
  style.border_width = BorderWidths::all(1.0F);
  style.radii = Radii::all(static_cast<float>(tok_int(scene.theme, DG_TOKEN_RADIUS_MD, 8)));
  style.overflow = Overflow::kClip;
  return style;
}

NodeId add_textfield(Scene& scene, NodeId parent, int width) {
  BoxStyle field_box;
  field_box.kind = LayoutKind::kLeaf;
  field_box.width = width;
  field_box.height = 36;
  const NodeId field = scene.tree.add_child(parent, field_box, field_style(scene, false));

  BoxStyle placeholder;
  placeholder.kind = LayoutKind::kLeaf;
  placeholder.width = 1;
  placeholder.height = 1;

  const NodeId highlight = scene.tree.add_child(field, placeholder, [&] {
    NodeStyle s;
    s.fill = tok_color(scene.theme, scene.variant, DG_TOKEN_COLOR_PRIMARY_HOVER, 0x8A2E86DE);
    return s;
  }());
  const NodeId underline = scene.tree.add_child(field, placeholder, [&] {
    NodeStyle s;
    s.fill = tok_color(scene.theme, scene.variant, DG_TOKEN_COLOR_PRIMARY, 0xFFE8B930);
    return s;
  }());

  NodeStyle content_style;
  content_style.text.font = scene.ui_font;
  content_style.text.size = 15.0F;
  content_style.text.color =
      tok_color(scene.theme, scene.variant, DG_TOKEN_COLOR_ON_SURFACE, 0xFFE8EDF4);
  content_style.text.align = TextAlign::kLeft;
  content_style.text.text = "\u641c\u7d22...";  // "Search..." placeholder, CJK
  const NodeId content = scene.tree.add_child(field, placeholder, content_style);

  const NodeId caret = scene.tree.add_child(field, placeholder, [&] {
    NodeStyle s;
    s.fill = tok_color(scene.theme, scene.variant, DG_TOKEN_COLOR_PRIMARY, 0xFF2E86DE);
    return s;
  }());

  Widget widget;
  widget.kind = WidgetKind::kTextField;
  widget.content = content;
  widget.caret = caret;
  widget.selection_highlight = highlight;
  widget.composition_underline = underline;
  scene.widgets.attach(field, widget);
  return field;
}

NodeId add_dropdown(Scene& scene, NodeId parent, int width) {
  BoxStyle box;
  box.kind = LayoutKind::kLeaf;
  box.width = width;
  box.height = 32;
  const NodeId node = scene.tree.add_child(parent, box, NodeStyle{});
  bind_panel(scene, node, true);
  (void)dg::set_prop(scene.tree, node, DG_PROP_BORDER_WIDTH_L, dg::PropValue::length(1.0F));
  (void)dg::set_prop(scene.tree, node, DG_PROP_BORDER_WIDTH_T, dg::PropValue::length(1.0F));
  (void)dg::set_prop(scene.tree, node, DG_PROP_BORDER_WIDTH_R, dg::PropValue::length(1.0F));
  (void)dg::set_prop(scene.tree, node, DG_PROP_BORDER_WIDTH_B, dg::PropValue::length(1.0F));

  BoxStyle label_box;
  label_box.width = width;
  label_box.height = 32;
  NodeStyle label_style;
  label_style.text.font = scene.ui_font;
  label_style.text.size = 14.0F;
  label_style.text.color =
      tok_color(scene.theme, scene.variant, DG_TOKEN_COLOR_ON_SURFACE, 0xFFE8EDF4);
  label_style.text.align = TextAlign::kLeft;
  label_style.text.inset = 10;
  const NodeId label = scene.tree.add_child(node, label_box, label_style);

  Widget widget;
  widget.kind = WidgetKind::kDropdown;
  widget.label = label;
  scene.widgets.attach(node, widget);
  paint_interactive_widget(scene, node, WidgetKind::kDropdown);
  if (scene.fonts.has_value()) {
    scene.widgets.dropdown_set_options(scene.tree.render(), *scene.fonts, node, kSortOptions);
    scene.widgets.dropdown_select(scene.tree.render(), *scene.fonts, node, 0);
  }
  return node;
}

// The description panel: a wrapping paragraph (7-2's display path) UNDER a
// drop shadow (5-4's `dg::set_shadow()`) - a combination doc/text-layout.md
// and doc/complex-properties.md each document on their own but never
// together (see doc/showcase.md's own cross-feature section 6).
NodeId add_description_panel(Scene& scene, NodeId parent, int width) {
  constexpr int kPadding = 14;
  const int inner_width = width - (2 * kPadding);

  TextStyle text;
  text.text = kDescriptionText;
  text.font = scene.ui_font;
  text.language = "zh-Hans";
  text.size = 16.0F;
  text.color = tok_color(scene.theme, scene.variant, DG_TOKEN_COLOR_ON_SURFACE, 0xFFEAEAEA);
  text.align = TextAlign::kLeft;
  text.wrap = true;

  int height = 48;
  if (scene.fonts.has_value()) {
    const dg::Expected<Paragraph, dg::FontError> built =
        Paragraph::build(*scene.fonts, text, static_cast<float>(inner_width));
    if (built.has_value()) {
      height = built.value().metrics().height;
    }
  }

  BoxStyle outer;
  outer.kind = LayoutKind::kLeaf;
  outer.width = width;
  outer.height = height + (2 * kPadding);
  outer.padding = EdgeInsets::all(kPadding);
  const NodeId panel = scene.tree.add_child(parent, outer, NodeStyle{});
  bind_panel(scene, panel, true);
  (void)dg::set_prop(scene.tree, panel, DG_PROP_BORDER_WIDTH_L, dg::PropValue::length(1.0F));
  (void)dg::set_prop(scene.tree, panel, DG_PROP_BORDER_WIDTH_T, dg::PropValue::length(1.0F));
  (void)dg::set_prop(scene.tree, panel, DG_PROP_BORDER_WIDTH_R, dg::PropValue::length(1.0F));
  (void)dg::set_prop(scene.tree, panel, DG_PROP_BORDER_WIDTH_B, dg::PropValue::length(1.0F));

  ShadowStyle shadow;
  shadow.offset_x = 3.0F;
  shadow.offset_y = 4.0F;
  shadow.blur_radius = 6.0F;
  shadow.color = Color::from_argb(0x80000000);
  (void)dg::set_shadow(scene.tree, panel, DG_PROP_SHADOW, shadow);

  Widget widget;
  widget.kind = WidgetKind::kLabel;
  scene.widgets.attach(panel, widget);

  BoxStyle inner;
  inner.kind = LayoutKind::kLeaf;
  inner.width = inner_width;
  inner.height = height;
  NodeStyle inner_style;
  inner_style.text = text;
  scene.tree.add_child(panel, inner, inner_style);
  return panel;
}

// The virtualized "recent files" kList, its pool nodes each carrying a
// REAL attached kButton Widget - the extension doc/menus.md section 3
// evaluated and declined for dropdown/menu rows, but never actually
// forbade in general. This is what gives 7-4's Focus::blur_if_any_of() a
// genuine caller for the first time (doc/focus.md section 6 records it as
// unit-tested only).
NodeId add_recent_list(Scene& scene, NodeId parent, int width) {
  BoxStyle list_box;
  list_box.kind = LayoutKind::kLeaf;
  list_box.width = width;
  list_box.height = kListViewportHeight;
  NodeStyle list_style;
  list_style.overflow = Overflow::kClip;
  const NodeId list = scene.tree.add_child(parent, list_box, list_style);
  bind_panel(scene, list, true);

  Widget list_widget;
  list_widget.kind = WidgetKind::kList;
  list_widget.list_axis = ScrollAxis::kVertical;
  list_widget.list_item_count = kListItemCount;
  list_widget.list_item_extent = kListItemHeight;

  std::vector<NodeId> pool;
  pool.reserve(static_cast<std::size_t>(kListPoolSize));
  for (int i = 0; i < kListPoolSize; ++i) {
    BoxStyle item_box;
    item_box.width = width;
    item_box.height = kListItemHeight;
    const NodeId row = scene.tree.add_child(list, item_box, NodeStyle{});
    Widget row_widget;
    row_widget.kind = WidgetKind::kButton;
    scene.widgets.attach(row, row_widget);
    paint_interactive_widget(scene, row, WidgetKind::kButton);

    BoxStyle label_box;
    label_box.width = width;
    label_box.height = kListItemHeight;
    NodeStyle label_style;
    label_style.text.font = scene.ui_font;
    label_style.text.size = 14.0F;
    label_style.text.color =
        tok_color(scene.theme, scene.variant, DG_TOKEN_COLOR_ON_SURFACE, 0xFFE8EDF4);
    label_style.text.align = TextAlign::kLeft;
    label_style.text.inset = 12;
    scene.tree.add_child(row, label_box, label_style);

    pool.push_back(row);
  }
  list_widget.list_pool = pool;
  list_widget.list_assigned.assign(pool.size(), -1);
  scene.widgets.attach(list, list_widget);
  return list;
}

std::string list_item_text(int logical_index) {
  std::string digits = std::to_string(logical_index);
  while (digits.size() < 4) {
    digits.insert(digits.begin(), '0');
  }
  return "\u6587\u4ef6 " + digits;  // "File 0000"
}

}  // namespace

Scene build(const Options& options) {
  dg::TreeSpec spec = options.spec;

  dg::FontId ui;
  std::optional<dg::FontCatalog> fonts;
  const dg::Expected<dg::FontCatalog, dg::FontError> scanned =
      dg::FontCatalog::scan(options.font_dir);
  if (scanned) {
    dg::FontCatalog catalog = scanned.value();
    if (const auto id = catalog.add("DejaVu Sans", false)) {
      ui = id.value();
    }
    fonts = catalog;
    spec.fonts = std::move(catalog);
  }

  const dg::Expected<dg::Theme, dg::ThemeLoadError> loaded = dg::load_builtin_theme();

  Scene scene{LayoutTree{spec},
              dg::WidgetSet{},
              dg::Interaction{},
              dg::Focus{},
              dg::FocusRing{},
              dg::HoverTimer{},
              dg::ThemeBindings{},
              Handles{},
              std::move(fonts),
              ui,
              loaded.value(),
              dg::ThemeVariant::kLight,
              0};

  BoxStyle body_box;
  body_box.kind = LayoutKind::kColumn;
  body_box.gap = tok_int(scene.theme, DG_TOKEN_SPACE_MD, 16);
  body_box.padding = EdgeInsets::all(tok_int(scene.theme, DG_TOKEN_SPACE_MD, 16));
  scene.handles.body = LayoutTree::root();
  scene.tree.set_box(scene.handles.body, body_box);
  (void)dg::bind_token(scene.tree, scene.bindings, scene.handles.body, DG_PROP_BACKGROUND_COLOR,
                       scene.theme, scene.variant, DG_TOKEN_COLOR_SURFACE);

  // --- Header row ---
  BoxStyle header_box;
  header_box.kind = LayoutKind::kRow;
  header_box.gap = tok_int(scene.theme, DG_TOKEN_SPACE_SM, 10);
  header_box.cross_align = CrossAlign::kCenter;
  scene.handles.header_row = scene.tree.add_child(scene.handles.body, header_box, NodeStyle{});

  BoxStyle title_box;
  title_box.width = 200;
  title_box.height = 28;
  NodeStyle title_style;
  title_style.text.text = "\u5a92\u4f53\u5e93\u8bbe\u7f6e";  // "Media Library Settings"
  title_style.text.font = ui;
  title_style.text.language = "zh-Hans";
  title_style.text.size = 20.0F;
  title_style.text.color =
      tok_color(scene.theme, scene.variant, DG_TOKEN_COLOR_ON_SURFACE, 0xFFEAEAEA);
  title_style.text.align = TextAlign::kLeft;
  scene.handles.title_label =
      scene.tree.add_child(scene.handles.header_row, title_box, title_style);
  scene.widgets.attach(scene.handles.title_label, [] {
    Widget w;
    w.kind = WidgetKind::kLabel;
    return w;
  }());

  scene.handles.theme_toggle =
      add_button(scene, scene.handles.header_row, 110, 32, "\u5207\u6362\u4e3b\u9898",
                 TextAlign::kCenter, 0);  // "Toggle Theme"
  scene.handles.info_button = add_button(scene, scene.handles.header_row, 110, 32,
                                         "\u6587\u4ef6\u4fe1\u606f",  // "File Info"
                                         TextAlign::kCenter, 0);
  scene.handles.help_button =
      add_button(scene, scene.handles.header_row, 90, 32, "\u5e2e\u52a9",  // "Help"
                 TextAlign::kCenter, 0);

  // --- Content row: sidebar | main ---
  BoxStyle content_box;
  content_box.kind = LayoutKind::kRow;
  content_box.gap = tok_int(scene.theme, DG_TOKEN_SPACE_MD, 16);
  scene.handles.content_row =
      scene.tree.add_child(scene.handles.body, content_box, NodeStyle{});

  BoxStyle sidebar_box;
  sidebar_box.kind = LayoutKind::kColumn;
  sidebar_box.width = kSidebarWidth;
  sidebar_box.gap = tok_int(scene.theme, DG_TOKEN_SPACE_MD, 14);
  sidebar_box.padding = EdgeInsets::all(tok_int(scene.theme, DG_TOKEN_SPACE_MD, 14));
  scene.handles.sidebar =
      scene.tree.add_child(scene.handles.content_row, sidebar_box, NodeStyle{});
  bind_panel(scene, scene.handles.sidebar, true);
  (void)dg::set_prop(scene.tree, scene.handles.sidebar, DG_PROP_BORDER_WIDTH_L,
                     dg::PropValue::length(1.0F));
  (void)dg::set_prop(scene.tree, scene.handles.sidebar, DG_PROP_BORDER_WIDTH_T,
                     dg::PropValue::length(1.0F));
  (void)dg::set_prop(scene.tree, scene.handles.sidebar, DG_PROP_BORDER_WIDTH_R,
                     dg::PropValue::length(1.0F));
  (void)dg::set_prop(scene.tree, scene.handles.sidebar, DG_PROP_BORDER_WIDTH_B,
                     dg::PropValue::length(1.0F));
  scene.widgets.attach(scene.handles.sidebar, [] {
    Widget w;
    w.kind = WidgetKind::kPanel;
    return w;
  }());

  scene.handles.anim_checkbox =
      add_checkbox(scene, scene.handles.sidebar, std::nullopt,
                   "\u542f\u7528\u60ac\u505c\u52a8\u753b");  // "Enable hover animation"
  scene.handles.radio_light =
      add_checkbox(scene, scene.handles.sidebar, 1, "\u6d45\u8272");  // "Light"
  scene.handles.radio_dark =
      add_checkbox(scene, scene.handles.sidebar, 1, "\u6df1\u8272");  // "Dark"
  scene.handles.radio_auto =
      add_checkbox(scene, scene.handles.sidebar, 1, "\u8ddf\u968f\u7cfb\u7edf");  // "Auto"
  scene.widgets.toggle(scene.handles.radio_light);
  scene.widgets.refresh(scene.tree.render(), scene.handles.radio_light,
                        scene.interaction.state_of(scene.handles.radio_light));

  scene.handles.volume_slider =
      add_slider(scene, scene.handles.sidebar, kSidebarWidth - (2 * 14));
  scene.handles.sort_dropdown =
      add_dropdown(scene, scene.handles.sidebar, kSidebarWidth - (2 * 14));
  scene.handles.search_field =
      add_textfield(scene, scene.handles.sidebar, kSidebarWidth - (2 * 14));

  // --- Main column ---
  BoxStyle main_box;
  main_box.kind = LayoutKind::kColumn;
  main_box.width = kMainWidth;
  main_box.gap = tok_int(scene.theme, DG_TOKEN_SPACE_MD, 14);
  scene.handles.main_column =
      scene.tree.add_child(scene.handles.content_row, main_box, NodeStyle{});

  scene.handles.description_panel =
      add_description_panel(scene, scene.handles.main_column, kMainWidth);
  scene.handles.recent_list = add_recent_list(scene, scene.handles.main_column, kMainWidth);
  scene.handles.profile_button =
      add_button(scene, scene.handles.main_column, 160, 34,
                 "\u6253\u5f00\u4e2a\u4eba\u8d44\u6599",  // "Open Profile"
                 TextAlign::kCenter, 0);

  scene.tree.layout_full();
  scene.widgets.resync_sliders(scene.tree.render());
  if (scene.fonts.has_value()) {
    scene.widgets.text_field_set_focus(scene.tree.render(), *scene.fonts,
                                       scene.handles.search_field, false);
  }
  (void)list_scroll_to(scene, 0);

  return scene;
}

std::string describe(const Scene& scene, dg::NodeId id) {
  struct Entry {
    dg::NodeId handle;
    const char* name = "";
  };
  const Entry table[] = {
      {scene.handles.theme_toggle, "theme_toggle"},
      {scene.handles.info_button, "info_button"},
      {scene.handles.help_button, "help_button"},
      {scene.handles.anim_checkbox, "anim_checkbox"},
      {scene.handles.radio_light, "radio_light"},
      {scene.handles.radio_dark, "radio_dark"},
      {scene.handles.radio_auto, "radio_auto"},
      {scene.handles.volume_slider, "volume_slider"},
      {scene.handles.sort_dropdown, "sort_dropdown"},
      {scene.handles.search_field, "search_field"},
      {scene.handles.profile_button, "profile_button"},
  };
  for (const Entry& entry : table) {
    if (entry.handle == id) {
      return entry.name;
    }
  }
  return "node " + std::to_string(id.value);
}

std::size_t switch_theme_variant(Scene& scene) {
  scene.variant = scene.variant == dg::ThemeVariant::kLight ? dg::ThemeVariant::kDark
                                                            : dg::ThemeVariant::kLight;
  const std::size_t missing = scene.bindings.apply(scene.tree, scene.theme, scene.variant);

  struct Entry {
    dg::NodeId handle;
    WidgetKind kind = WidgetKind::kButton;
  };
  const Entry widgets[] = {
      {scene.handles.theme_toggle, WidgetKind::kButton},
      {scene.handles.info_button, WidgetKind::kButton},
      {scene.handles.help_button, WidgetKind::kButton},
      {scene.handles.anim_checkbox, WidgetKind::kCheckbox},
      {scene.handles.radio_light, WidgetKind::kCheckbox},
      {scene.handles.radio_dark, WidgetKind::kCheckbox},
      {scene.handles.radio_auto, WidgetKind::kCheckbox},
      {scene.handles.sort_dropdown, WidgetKind::kDropdown},
      {scene.handles.profile_button, WidgetKind::kButton},
  };
  for (const Entry& entry : widgets) {
    paint_interactive_widget(scene, entry.handle, entry.kind);
  }
  if (scene.widgets.has(scene.handles.recent_list)) {
    for (dg::NodeId row : scene.widgets.at(scene.handles.recent_list).list_pool) {
      paint_interactive_widget(scene, row, WidgetKind::kButton);
    }
  }
  return missing;
}

void apply_focus_change(Scene& scene, dg::WindowManager& manager, dg::WindowId window,
                        std::optional<dg::NodeId> target) {
  const dg::FocusChange change = scene.focus.set(target);
  if (!change.any()) {
    return;
  }
  if (scene.fonts.has_value()) {
    const dg::FontCatalog& fonts = *scene.fonts;
    if (change.blurred == scene.handles.search_field) {
      scene.widgets.text_field_set_focus(scene.tree.render(), fonts, scene.handles.search_field,
                                         false);
      manager.stop_text_input(window);
      manager.clear_composition(window);
      scene.tree.render().set_style(scene.handles.search_field, field_style(scene, false));
    }
    if (change.focused == scene.handles.search_field) {
      scene.widgets.text_field_set_focus(scene.tree.render(), fonts, scene.handles.search_field,
                                         true);
      manager.start_text_input(window,
                               scene.tree.render().absolute_bounds(scene.handles.search_field));
      scene.tree.render().set_style(scene.handles.search_field, field_style(scene, true));
    }
  }
  const dg::Color ring_color =
      tok_color(scene.theme, dg::ThemeVariant::kDark, DG_TOKEN_COLOR_FOCUS_RING, 0xFFFF8800);
  dg::update_focus_ring(scene.tree.render(), LayoutTree::root(), scene.ring,
                        scene.focus.current(), ring_color);
}

void tab(Scene& scene, dg::WindowManager& manager, dg::WindowId window, bool backwards) {
  const dg::FocusChange change =
      backwards
          ? scene.focus.focus_previous(scene.tree.render(), scene.widgets, scene.handles.body)
          : scene.focus.focus_next(scene.tree.render(), scene.widgets, scene.handles.body);
  if (!change.any()) {
    return;
  }
  if (scene.fonts.has_value()) {
    const dg::FontCatalog& fonts = *scene.fonts;
    if (change.blurred == scene.handles.search_field) {
      scene.widgets.text_field_set_focus(scene.tree.render(), fonts, scene.handles.search_field,
                                         false);
      manager.stop_text_input(window);
      manager.clear_composition(window);
      scene.tree.render().set_style(scene.handles.search_field, field_style(scene, false));
    }
    if (change.focused == scene.handles.search_field) {
      scene.widgets.text_field_set_focus(scene.tree.render(), fonts, scene.handles.search_field,
                                         true);
      manager.start_text_input(window,
                               scene.tree.render().absolute_bounds(scene.handles.search_field));
      scene.tree.render().set_style(scene.handles.search_field, field_style(scene, true));
    }
  }
  const dg::Color ring_color =
      tok_color(scene.theme, dg::ThemeVariant::kDark, DG_TOKEN_COLOR_FOCUS_RING, 0xFFFF8800);
  dg::update_focus_ring(scene.tree.render(), LayoutTree::root(), scene.ring,
                        scene.focus.current(), ring_color);
}

dg::FocusChange list_scroll_to(Scene& scene, int top_index) {
  const int max_top = std::max(0, kListItemCount - kListPoolSize);
  scene.list_top_index = std::clamp(top_index, 0, max_top);
  const std::vector<ListSlot> reassigned = scene.widgets.list_sync(
      scene.tree.render(), scene.handles.recent_list, scene.list_top_index);
  const Color text_color =
      tok_color(scene.theme, scene.variant, DG_TOKEN_COLOR_ON_SURFACE, 0xFFE8EDF4);
  for (const ListSlot& slot : reassigned) {
    NodeStyle style;
    style.text.font = scene.ui_font;
    style.text.size = 14.0F;
    style.text.color = text_color;
    style.text.align = TextAlign::kLeft;
    style.text.inset = 12;
    style.text.text = list_item_text(slot.logical_index);
    const NodeId label = scene.tree.render().children(slot.node).front();
    scene.tree.render().set_style(label, style);
    paint_interactive_widget(scene, slot.node, WidgetKind::kButton);
  }
  std::vector<NodeId> recycled;
  recycled.reserve(reassigned.size());
  for (const ListSlot& slot : reassigned) {
    recycled.push_back(slot.node);
  }
  return scene.focus.blur_if_any_of(recycled);
}

namespace {

// Wheel-over-the-list handling, pulled out of dispatch_pointer() purely to
// keep that function's own cognitive complexity under this project's
// clang-tidy threshold - identical logic, just named.
void dispatch_wheel(Scene& scene, const dg::PointerEvent& event) {
  const std::optional<NodeId> under_pointer =
      scene.widgets.widget_at(scene.tree.render(), dg::PixelPoint{event.x, event.y});
  const std::optional<NodeId> list_owner =
      scene.widgets.list_owner_of(scene.tree.render(), under_pointer.value_or(NodeId{}));
  if (list_owner != scene.handles.recent_list) {
    return;
  }
  const int delta = event.wheel_y > 0 ? -1 : 1;
  (void)list_scroll_to(scene, scene.list_top_index + delta);
}

// A secondary-button (right-click) press: a PARALLEL, non-activating
// channel exactly like doc/menus.md section 6.1's own routing decision - it
// never touches dg::Interaction's hover/press state, only signals the
// caller (the window driver) that a context menu should open.
bool secondary_click_on_info_button(Scene& scene, const dg::PointerEvent& event) {
  if (event.button != dg::PointerButton::kSecondary ||
      event.action != dg::PointerAction::kDown) {
    return false;
  }
  const std::optional<NodeId> hit =
      scene.widgets.widget_at(scene.tree.render(), dg::PixelPoint{event.x, event.y});
  return hit == scene.handles.info_button;
}

}  // namespace

bool dispatch_pointer(Scene& scene, dg::WindowManager& manager, dg::WindowId window,
                      const dg::PointerEvent& event, bool* wants_context_menu,
                      bool* wants_dropdown) {
  if (wants_context_menu != nullptr) {
    *wants_context_menu = false;
  }
  if (wants_dropdown != nullptr) {
    *wants_dropdown = false;
  }
  if (event.button == dg::PointerButton::kSecondary) {
    if (wants_context_menu != nullptr) {
      *wants_context_menu = secondary_click_on_info_button(scene, event);
    }
    return false;
  }

  const dg::PixelPoint at{event.x, event.y};
  dg::InteractionChange change;
  switch (event.action) {
    case dg::PointerAction::kMove:
      change = scene.interaction.moved_over(scene.widgets.widget_at(scene.tree.render(), at));
      break;
    case dg::PointerAction::kDown: {
      const std::optional<NodeId> hit = scene.widgets.widget_at(scene.tree.render(), at);
      change = scene.interaction.pressed_on(hit);
      const bool is_focusable = hit.has_value() && scene.widgets.has(*hit) &&
                                dg::is_focusable(scene.widgets.at(*hit).kind);
      apply_focus_change(scene, manager, window, is_focusable ? hit : std::nullopt);
      break;
    }
    case dg::PointerAction::kUp:
      change = scene.interaction.released_on(scene.widgets.widget_at(scene.tree.render(), at));
      break;
    case dg::PointerAction::kLeave:
      change = scene.interaction.left_window();
      break;
    case dg::PointerAction::kWheel:
      dispatch_wheel(scene, event);
      break;
  }

  if (change.clicked.has_value()) {
    scene.widgets.toggle(*change.clicked);
    for (dg::NodeId member : scene.widgets.group_members(*change.clicked)) {
      scene.widgets.refresh(scene.tree.render(), member, scene.interaction.state_of(member));
    }
    if (change.clicked == scene.handles.sort_dropdown && wants_dropdown != nullptr) {
      *wants_dropdown = true;
    }
  }
  const std::optional<NodeId> touched[] = {change.left, change.entered, change.pressed,
                                           change.released, change.clicked};
  for (const std::optional<NodeId>& id : touched) {
    if (id.has_value()) {
      scene.widgets.refresh(scene.tree.render(), *id, scene.interaction.state_of(*id));
    }
  }
  return false;
}

void dispatch_key(Scene& scene, dg::WindowManager& manager, dg::WindowId window,
                  const dg::KeyEvent& event) {
  if (event.action != dg::KeyAction::kDown) {
    return;
  }
  if (event.key == dg::Key::kTab) {
    tab(scene, manager, window, event.shift);
    return;
  }
  if (!scene.focus.is_focused(scene.handles.search_field) || !scene.fonts.has_value()) {
    return;
  }
  const dg::FontCatalog& fonts = *scene.fonts;
  dg::RenderTree& tree = scene.tree.render();
  const NodeId field = scene.handles.search_field;
  switch (event.key) {
    case dg::Key::kLeft:
      scene.widgets.text_field_move(tree, fonts, field, dg::TextFieldMove::kCharLeft,
                                    event.shift);
      break;
    case dg::Key::kRight:
      scene.widgets.text_field_move(tree, fonts, field, dg::TextFieldMove::kCharRight,
                                    event.shift);
      break;
    case dg::Key::kHome:
      scene.widgets.text_field_move(tree, fonts, field, dg::TextFieldMove::kLineStart,
                                    event.shift);
      break;
    case dg::Key::kEnd:
      scene.widgets.text_field_move(tree, fonts, field, dg::TextFieldMove::kLineEnd,
                                    event.shift);
      break;
    case dg::Key::kBackspace:
      scene.widgets.text_field_backspace(tree, fonts, field);
      break;
    case dg::Key::kDelete:
      scene.widgets.text_field_delete_forward(tree, fonts, field);
      break;
    case dg::Key::kEscape:
      if (scene.widgets.text_field_is_composing(field)) {
        scene.widgets.text_field_cancel_composition(tree, fonts, field);
        manager.clear_composition(window);
      }
      break;
    case dg::Key::kTab:
    case dg::Key::kUp:
    case dg::Key::kDown:
    case dg::Key::kEnter:
    case dg::Key::kOther:
      break;
  }
}

}  // namespace showcase_scene
