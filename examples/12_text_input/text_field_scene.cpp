#include "text_field_scene.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "drawgui/graphics/types.h"
#include "drawgui/layout/box.h"
#include "drawgui/shortcuts/action_ids.generated.h"

namespace text_field_scene {
namespace {

using dg::BorderWidths;
using dg::BoxStyle;
using dg::Color;
using dg::CrossAlign;
using dg::EdgeInsets;
using dg::LayoutKind;
using dg::LayoutTree;
using dg::NodeId;
using dg::NodeStyle;
using dg::Overflow;
using dg::Radii;
using dg::TextAlign;
using dg::TextStyle;
using dg::Widget;
using dg::WidgetKind;

constexpr std::uint32_t kFieldFill = 0xFF161C24;
constexpr std::uint32_t kBorderUnfocused = 0xFF2C3644;
constexpr std::uint32_t kBorderFocused = 0xFF2E86DE;
constexpr std::uint32_t kTextColor = 0xFFE8EDF4;
constexpr std::uint32_t kCaretColor = 0xFF2E86DE;
// ~55% alpha over kFieldFill - visible without hiding the selected text drawn
// over it in the same colour every other run uses.
constexpr std::uint32_t kSelectionFill = 0x8A2E86DE;
// 7-3 (doc/ime.md): the composition underline's colour - deliberately
// distinct from the caret and selection so a hand-derived pixel test can
// tell the three apart on screen.
constexpr std::uint32_t kCompositionUnderlineColor = 0xFFE8B930;

NodeStyle field_style(bool focused) {
  NodeStyle style;
  style.fill = Color::from_argb(kFieldFill);
  style.border_color = Color::from_argb(focused ? kBorderFocused : kBorderUnfocused);
  style.border_width = BorderWidths::all(1.0F);
  style.radii = Radii::all(6.0F);
  style.overflow = Overflow::kClip;
  return style;
}

NodeId add_field(dg::LayoutTree& tree, dg::WidgetSet& widgets, dg::ActionScopes& action_scopes,
                 NodeId parent, dg::FontId font, const std::string& initial) {
  BoxStyle field_box;
  field_box.kind = LayoutKind::kLeaf;
  field_box.width = kFieldWidth;
  field_box.height = kFieldHeight;
  const NodeId field = tree.add_child(parent, field_box, field_style(false));

  // Selection highlight FIRST, so it paints below the text content added
  // after it - the same paint-order-is-add-order rule every panel/label/
  // indicator pairing in this project already relies on.
  BoxStyle placeholder;
  placeholder.kind = LayoutKind::kLeaf;
  placeholder.width = 1;
  placeholder.height = 1;

  NodeStyle highlight_style;
  highlight_style.fill = Color::from_argb(kSelectionFill);
  const NodeId highlight = tree.add_child(field, placeholder, highlight_style);

  // 7-3's composition underline (doc/ime.md) - added before `content` so it
  // paints behind the glyphs, the same "background before foreground" order
  // `highlight` already establishes.
  NodeStyle underline_style;
  underline_style.fill = Color::from_argb(kCompositionUnderlineColor);
  const NodeId underline = tree.add_child(field, placeholder, underline_style);

  NodeStyle content_style;
  content_style.text.font = font;
  content_style.text.size = static_cast<float>(kFontSize);
  content_style.text.color = Color::from_argb(kTextColor);
  content_style.text.align = TextAlign::kLeft;
  content_style.text.text = initial;
  const NodeId content = tree.add_child(field, placeholder, content_style);

  NodeStyle caret_style;
  caret_style.fill = Color::from_argb(kCaretColor);
  const NodeId caret = tree.add_child(field, placeholder, caret_style);

  Widget widget;
  widget.kind = WidgetKind::kTextField;
  widget.content = content;
  widget.caret = caret;
  widget.selection_highlight = highlight;
  widget.composition_underline = underline;
  widget.text = initial;
  widget.cursor = static_cast<int>(initial.size());
  widgets.attach(field, widget);

  // 8-4: this field scopes its own clipboard actions onto itself, the
  // exact registration action_scopes.h's own header comment illustrates
  // ("a TextField scoping both select_all and paste") - without this, a
  // real Mod+C over a focused field would resolve to nothing at all: all
  // four bind at ActionScope::kTextField, which has no app-level fallback
  // (router.cpp's own level 4 loop only matches ActionScope::kApp).
  action_scopes.scope(field, DG_ACTION_COPY);
  action_scopes.scope(field, DG_ACTION_CUT);
  action_scopes.scope(field, DG_ACTION_PASTE);
  action_scopes.scope(field, DG_ACTION_SELECT_ALL);
  return field;
}

}  // namespace

Scene build(const Options& options) {
  dg::TreeSpec spec = options.spec;

  dg::FontId ui;
  std::optional<dg::FontCatalog> fonts;
  dg::Expected<dg::FontCatalog, dg::FontError> scanned =
      dg::FontCatalog::scan(options.font_dir);
  if (scanned) {
    dg::FontCatalog catalog = std::move(scanned).value();
    if (const auto id = catalog.add("DejaVu Sans", false)) {
      ui = id.value();
    }
    fonts = catalog;
    spec.fonts = std::move(catalog);
  }

  LayoutTree tree{spec};
  dg::WidgetSet widgets;
  dg::ActionScopes action_scopes;
  Handles handles;

  BoxStyle body_box;
  body_box.kind = LayoutKind::kColumn;
  body_box.gap = 20;
  body_box.padding = EdgeInsets::all(24);
  handles.body = LayoutTree::root();
  tree.set_box(handles.body, body_box);

  handles.field_a = add_field(tree, widgets, action_scopes, handles.body, ui, kFieldAInitial);
  handles.field_b = add_field(tree, widgets, action_scopes, handles.body, ui, "");

  Scene scene{std::move(tree), std::move(widgets),       dg::Focus{},
              handles,         std::move(action_scopes), std::move(fonts)};

  scene.tree.layout_full();

  // Both fields start unfocused - derive their initial display projection
  // (field_a's overflowing default text truncates to an ellipsis; field_b's
  // empty string needs no truncation) the same way resync_sliders() derives
  // a freshly-built slider's thumb position once, right after layout_full().
  if (scene.fonts.has_value()) {
    const dg::FontCatalog& catalog = *scene.fonts;
    scene.widgets.text_field_set_focus(scene.tree.render(), catalog, scene.handles.field_a,
                                       false);
    scene.widgets.text_field_set_focus(scene.tree.render(), catalog, scene.handles.field_b,
                                       false);
  }

  return scene;
}

std::string describe(const Scene& scene, dg::NodeId id) {
  if (id == scene.handles.field_a) {
    return "field a";
  }
  if (id == scene.handles.field_b) {
    return "field b";
  }
  return "node " + std::to_string(id.value);
}

void set_focus(Scene& scene, std::optional<dg::NodeId> target) {
  const dg::FocusChange change = scene.focus.set(target);
  if (!change.any() || !scene.fonts.has_value()) {
    return;
  }
  const dg::FontCatalog& fonts = *scene.fonts;
  if (change.blurred.has_value()) {
    scene.widgets.text_field_set_focus(scene.tree.render(), fonts, *change.blurred, false);
  }
  if (change.focused.has_value()) {
    scene.widgets.text_field_set_focus(scene.tree.render(), fonts, *change.focused, true);
  }
  // The border colour is a plain style change, not WidgetSet state - the
  // same "cosmetic feedback lives beside the widget, not inside it" split
  // doc/form-controls.md section 8 already draws for a slider's un-built
  // hover glow.
  if (change.blurred.has_value()) {
    scene.tree.render().set_style(*change.blurred, field_style(false));
  }
  if (change.focused.has_value()) {
    scene.tree.render().set_style(*change.focused, field_style(true));
  }
}

}  // namespace text_field_scene
