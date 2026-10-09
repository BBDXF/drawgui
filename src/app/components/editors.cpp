// NodeKind::kTextField and kTextArea: default boxes, the composed editing
// children, re-colouring on a theme change, and committed-text insertion.
// Their keys are handled by keys.cpp, their pointer presses by pointer.cpp.

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

#include "app/components/components.h"
#include "app/components/editors.h"
#include "app/internal.h"
#include "drawgui/anim/clock.h"

namespace dg::app_detail {
namespace {

constexpr int kTextFieldWidth = 260;
constexpr int kTextFieldHeight = 36;
constexpr float kTextFieldFontSize = 16.0F;

// NodeKind::kTextArea: the text field's palette and font size above,
// reused verbatim so the two editors look like one family; only the box is
// larger. `kTextAreaHighlightPool` bounds how many selected lines can show
// at once (WidgetSet's pool contract) - comfortably more than the default
// box holds, with room for a host that enlarges it.
constexpr int kTextAreaWidth = 320;
constexpr int kTextAreaHeight = 160;
constexpr int kTextAreaInset = 10;
// The text field's text keeps the text area's distance from its side edges.
constexpr int kTextFieldInset = kTextAreaInset;
constexpr int kTextAreaHighlightPool = 64;
constexpr int kTextAreaUnderlinePool = 4;

}  // namespace

dg::NodeStyle text_field_box_style(const EditorPalette& palette, bool focused) {
  dg::NodeStyle style;
  style.fill = palette.fill;
  style.border_color = focused ? palette.border_focused : palette.border;
  style.border_width = dg::BorderWidths::all(1.0F);
  style.radii = dg::Radii::all(6.0F);
  style.overflow = dg::Overflow::kClip;
  return style;
}

void set_text_field_box_style(const EditorPalette& palette, dg::RenderTree& tree, dg::NodeId id,
                              bool focused) {
  dg::NodeStyle style = text_field_box_style(palette, focused);
  style.disabled = tree.style(id).disabled;
  tree.set_style(id, style);
}

EditorPalette editor_palette_for(const AppState& app) {
  const auto [theme, variant] = theme_for(app);
  EditorPalette palette;
  if (theme == nullptr) {
    return palette;
  }
  const auto pick = [&](dg_token_id id, dg::Color& out) {
    if (const std::optional<dg::Color> c = theme->color_value(id, variant)) {
      out = *c;
    }
  };
  pick(DG_TOKEN_COLOR_SURFACE, palette.fill);
  pick(DG_TOKEN_COLOR_BORDER, palette.border);
  pick(DG_TOKEN_COLOR_PRIMARY, palette.border_focused);
  pick(DG_TOKEN_COLOR_PRIMARY, palette.caret);
  pick(DG_TOKEN_COLOR_ON_SURFACE, palette.text);
  pick(DG_TOKEN_COLOR_ON_SURFACE, palette.underline);
  palette.selection = dg::Color::from_argb((kTextFieldSelectionAlpha << 24U) |
                                           (palette.caret.argb() & 0xFFFFFFU));
  palette.placeholder = dg::Color::from_argb((kTextFieldPlaceholderAlpha << 24U) |
                                             (palette.text.argb() & 0xFFFFFFU));
  return palette;
}

// Re-colours every live text field / text area in `window` with the
// active palette. Writes enabled colours only; refresh_subtree() afterwards
// re-dims the disabled ones (it notices the overwrite and remembers the new
// colour).
void restyle_editors(WinState& window) {
  const EditorPalette palette = editor_palette_for(window.app);
  dg::RenderTree& tree = window.tree.render();
  for (const NodeRecord* record : records_in_order(window)) {
    if (!is_editor_kind(record->kind) || !window.widgets.has(record->id)) {
      continue;
    }
    const dg::NodeId id = record->id;
    set_text_field_box_style(palette, tree, id, window.focus.current() == id);
    // attach() replaces the stored Widget by value; a copy with only the
    // three box fills changed keeps the editing state intact.
    dg::Widget updated = window.widgets.at(id);
    updated.fill_normal = palette.fill;
    updated.fill_hover = palette.fill;
    updated.fill_pressed = palette.fill;
    window.widgets.attach(id, updated);
    const dg::Widget* widget = &window.widgets.at(id);
    dg::TextStyle text = tree.style(widget->content).text;
    text.color = palette.text;
    tree.set_text(widget->content, text);
    tree.set_fill(widget->caret, palette.caret);
    if (widget->kind == dg::WidgetKind::kTextArea) {
      for (const dg::NodeId n : widget->highlight_pool) {
        tree.set_fill(n, palette.selection);
      }
      for (const dg::NodeId n : widget->underline_pool) {
        tree.set_fill(n, palette.underline);
      }
    } else {
      tree.set_fill(widget->selection_highlight, palette.selection);
      tree.set_fill(widget->composition_underline, palette.underline);
    }
    sync_placeholder(window, *record);
  }
}

void sync_placeholder(WinState& window, const NodeRecord& record) {
  if (!window.widgets.has(record.id) || record.placeholder == dg::RenderTree::root() ||
      !window.tree.render().is_valid(record.placeholder)) {
    return;
  }
  dg::RenderTree& tree = window.tree.render();
  const dg::Widget& widget = window.widgets.at(record.id);
  const dg::TextEditState* edit = window.widgets.editor_state(record.id);
  const bool show = !record.placeholder_text.empty() && edit != nullptr && edit->text.empty() &&
                    !edit->composing;
  const dg::TextStyle& content = tree.style(widget.content).text;
  dg::TextStyle text = tree.style(record.placeholder).text;
  text.text = show ? record.placeholder_text : std::string();
  text.font = content.font;
  text.size = content.size;
  text.align = dg::TextAlign::kLeft;
  text.color = editor_palette_for(window.app).placeholder;
  text.wrap = record.kind == NodeKind::kTextArea;
  tree.set_text(record.placeholder, text);
  const dg::PixelRect field = tree.local_bounds(record.id);
  const int inset = std::max(0, std::min(widget.text_inset, field.width / 2));
  const int top = record.kind == NodeKind::kTextArea ? inset : 0;
  const dg::PixelRect placed{inset, top, std::max(1, field.width - (2 * inset)),
                             std::max(1, field.height - (2 * top))};
  if (tree.local_bounds(record.placeholder) != placed) {
    tree.set_local_bounds(record.placeholder, placed);
  }
}

dg::Status set_editor_padding(WinState& window, const NodeRecord& record,
                              const dg::PropValue& value) {
  if (value.type() != dg::PropType::k_float) {
    return dg::Status::kTypeMismatch;
  }
  const float number = value.scalar();
  constexpr float kMaxInset = 1 << 16;
  if (!std::isfinite(number) || number < 0.0F || number > kMaxInset) {
    return dg::Status::kValueOutOfRange;
  }
  dg::Widget updated = window.widgets.at(record.id);
  updated.text_inset = static_cast<int>(std::lround(number));
  window.widgets.attach(record.id, updated);
  refresh_editor_text(window, record);
  sync_placeholder(window, record);
  window.relaid = true;
  return dg::Status::kOk;
}

bool insert_into_editor(WinState& window, dg::NodeId editor, const std::string& text) {
  AppState& app = window.app;
  if (!app.fonts.has_value()) {
    return false;
  }
  window.widgets.set_edit_time(dg::steady_anim_time());
  const bool changed =
      window.widgets.editor_insert(window.tree.render(), *app.fonts, editor, text);
  if (is_text_area(window, editor)) {
    follow_caret(window, editor);
  }
  return changed;
}

// After a text property reached an editor's content node: re-derive the
// editor's display (caret position, truncation, wrapping).
void refresh_editor_text(WinState& window, const NodeRecord& record) {
  AppState& app = window.app;
  if (!app.fonts.has_value()) {
    return;
  }
  dg::RenderTree& tree = window.tree.render();
  const bool focused = window.focus.current() == record.id;
  if (record.kind == NodeKind::kTextArea) {
    window.widgets.text_area_set_focus(tree, *app.fonts, record.id, focused);
  } else {
    window.widgets.text_field_set_focus(tree, *app.fonts, record.id, focused);
  }
}

namespace components {

void editor_defaults(const AppState& app, NodeKind kind, dg::BoxStyle& box,
                     dg::NodeStyle& style) {
  if (kind == NodeKind::kTextField) {
    // examples/12_text_input/text_field_scene.cpp's add_field() box, copied
    // unchanged - starts unfocused (field_style(false)).
    box.width = kTextFieldWidth;
    box.height = kTextFieldHeight;
  } else {
    box.width = kTextAreaWidth;
    box.height = kTextAreaHeight;
  }
  style = text_field_box_style(editor_palette_for(app), false);
}

void compose_editor(WinState& window, NodeRecord& record) {
  AppState& app = window.app;
  const dg::NodeId new_id = record.id;
  if (record.kind == NodeKind::kTextField) {
    // examples/12_text_input/text_field_scene.cpp's add_field() recipe,
    // copied unchanged: four caller-invisible children, in the SAME paint
    // order (selection highlight, then composition underline, then text
    // content, then caret - "background before foreground"), none returned
    // by create(), exactly the checkbox indicator's own trade (see the
    // constant comment on DG_NODE_TYPE_TEXT_FIELD, abi/drawgui.def.toml).
    const EditorPalette editor = editor_palette_for(app);
    dg::BoxStyle placeholder;
    placeholder.kind = dg::LayoutKind::kLeaf;
    placeholder.width = 1;
    placeholder.height = 1;

    dg::NodeStyle highlight_style;
    highlight_style.fill = editor.selection;
    const dg::NodeId highlight = window.tree.add_child(new_id, placeholder, highlight_style);

    // composition_underline: built so Widget::composition_underline names a
    // real NodeId (WidgetSet's own text_field_refresh_display() positions it
    // unconditionally), but never driven - the runtime exports no IME/
    // composition surface (abi/drawgui.def.toml's own "明确不做" entry), so
    // `composing` never becomes true and this child stays zero-size forever.
    dg::NodeStyle underline_style;
    underline_style.fill = editor.underline;
    const dg::NodeId underline = window.tree.add_child(new_id, placeholder, underline_style);

    dg::NodeStyle content_style;
    content_style.text.font = app.default_font;
    content_style.text.size = kTextFieldFontSize;
    content_style.text.color = editor.text;
    content_style.text.align = dg::TextAlign::kLeft;
    const dg::NodeId content = window.tree.add_child(new_id, placeholder, content_style);
    // DG_PROP_PLACEHOLDER's text, under the caret; empty until set.
    record.placeholder = window.tree.add_child(new_id, placeholder, dg::NodeStyle{});

    dg::NodeStyle caret_style;
    caret_style.fill = editor.caret;
    const dg::NodeId caret = window.tree.add_child(new_id, placeholder, caret_style);

    dg::Widget widget;
    widget.kind = dg::WidgetKind::kTextField;
    // The box keeps its fill on hover/press, and gets it back after being
    // disabled (refresh() paints fill_normal for an enabled widget).
    widget.fill_normal = editor.fill;
    widget.fill_hover = widget.fill_normal;
    widget.fill_pressed = widget.fill_normal;
    widget.content = content;
    widget.caret = caret;
    widget.selection_highlight = highlight;
    widget.composition_underline = underline;
    widget.text_inset = kTextFieldInset;
    window.widgets.attach(new_id, widget);
    if (app.fonts.has_value()) {
      window.widgets.text_field_set_focus(window.tree.render(), *app.fonts, new_id, false);
    }
    return;
  }
  // The text field's recipe, multi-line: a selection LAYER holding a pool
  // of highlight nodes (one per visible selected line), an underline pool
  // (one per visible line of an IME composition), the wrapping content,
  // the caret, and a scrollbar track + thumb added last so it paints and
  // hit-tests on top. All caller-invisible, like every composed child.
  const EditorPalette editor = editor_palette_for(app);
  dg::BoxStyle placeholder;
  placeholder.kind = dg::LayoutKind::kLeaf;
  placeholder.width = 1;
  placeholder.height = 1;

  dg::Widget widget;
  widget.kind = dg::WidgetKind::kTextArea;
  widget.fill_normal = editor.fill;
  widget.fill_hover = widget.fill_normal;
  widget.fill_pressed = widget.fill_normal;
  widget.text_inset = kTextAreaInset;

  widget.selection_layer = window.tree.add_child(new_id, placeholder, dg::NodeStyle{});
  dg::NodeStyle highlight_style;
  highlight_style.fill = editor.selection;
  for (int i = 0; i < kTextAreaHighlightPool; ++i) {
    widget.highlight_pool.push_back(
        window.tree.add_child(widget.selection_layer, placeholder, highlight_style));
  }
  dg::NodeStyle underline_style;
  underline_style.fill = editor.underline;
  for (int i = 0; i < kTextAreaUnderlinePool; ++i) {
    widget.underline_pool.push_back(
        window.tree.add_child(new_id, placeholder, underline_style));
  }
  dg::NodeStyle content_style;
  content_style.text.font = app.default_font;
  content_style.text.size = kTextFieldFontSize;
  content_style.text.color = editor.text;
  content_style.text.align = dg::TextAlign::kLeft;
  content_style.text.wrap = true;
  widget.content = window.tree.add_child(new_id, placeholder, content_style);
  record.placeholder = window.tree.add_child(new_id, placeholder, dg::NodeStyle{});
  dg::NodeStyle caret_style;
  caret_style.fill = editor.caret;
  widget.caret = window.tree.add_child(new_id, placeholder, caret_style);
  const dg::NodeId track = window.tree.add_child(new_id, placeholder, dg::NodeStyle{});
  const dg::NodeId thumb = window.tree.add_child(track, placeholder, dg::NodeStyle{});

  window.widgets.attach(new_id, widget);
  window.widgets.attach_scrollbar(window.tree.render(), new_id, track, thumb);
  // Before the first layout the area's bounds are still zero; a refresh
  // after every layout (resync_widgets()) puts the children right.
  if (app.fonts.has_value()) {
    window.widgets.text_area_set_focus(window.tree.render(), *app.fonts, new_id, false);
  }
}

}  // namespace components
}  // namespace dg::app_detail
