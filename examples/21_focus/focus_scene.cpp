#include "focus_scene.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "drawgui/graphics/types.h"
#include "drawgui/layout/box.h"
#include "drawgui/theme/theme_loader.h"
#include "drawgui/theme/token_ids.generated.h"

namespace focus_scene {
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
using dg::Widget;
using dg::WidgetKind;

constexpr std::uint32_t kSectionFill = 0xFF14171C;
constexpr std::uint32_t kSectionBorder = 0xFF232A34;
constexpr std::uint32_t kButtonNormal = 0xFF2C3644;
constexpr std::uint32_t kButtonHover = 0xFF3B4A5E;
constexpr std::uint32_t kButtonPressed = 0xFF1F2731;
constexpr std::uint32_t kLabelFill = 0xFF10151C;
constexpr std::uint32_t kOff = 0xFF10151C;
constexpr std::uint32_t kAccent = 0xFF2E86DE;
constexpr std::uint32_t kBorder = 0xFF141A22;
constexpr std::uint32_t kTrack = 0xFF20262F;
constexpr std::uint32_t kThumb = 0xFF97A3B4;
constexpr std::uint32_t kFieldFill = 0xFF161C24;
constexpr std::uint32_t kFieldBorder = 0xFF2C3644;
constexpr std::uint32_t kTextColor = 0xFFE8EDF4;
constexpr std::uint32_t kCaretColor = 0xFF2E86DE;
constexpr std::uint32_t kSelectionFill = 0x8A2E86DE;
constexpr std::uint32_t kCompositionUnderlineColor = 0xFFE8B930;

NodeStyle flat(std::uint32_t argb) {
  NodeStyle style;
  style.fill = Color::from_argb(argb);
  return style;
}

NodeStyle bordered(std::uint32_t fill, std::uint32_t border, float radius) {
  NodeStyle style = flat(fill);
  style.border_color = Color::from_argb(border);
  style.border_width = BorderWidths::all(1.0F);
  if (radius > 0.0F) {
    style.radii = Radii::all(radius);
  }
  return style;
}

NodeId add_section(LayoutTree& tree, NodeId parent, int gap) {
  BoxStyle box;
  box.kind = LayoutKind::kRow;
  box.gap = gap;
  box.cross_align = CrossAlign::kCenter;
  box.padding = EdgeInsets::all(14);
  box.height = 60;
  return tree.add_child(parent, box, bordered(kSectionFill, kSectionBorder, 8.0F));
}

NodeId add_button(Scene& scene, NodeId parent, int width, std::optional<int> tab_index) {
  BoxStyle box;
  box.kind = LayoutKind::kLeaf;
  box.width = width;
  box.height = 32;
  const NodeId node = scene.tree.add_child(parent, box, bordered(kButtonNormal, kBorder, 6.0F));

  Widget widget;
  widget.kind = WidgetKind::kButton;
  widget.fill_normal = Color::from_argb(kButtonNormal);
  widget.fill_hover = Color::from_argb(kButtonHover);
  widget.fill_pressed = Color::from_argb(kButtonPressed);
  widget.tab_index = tab_index;
  scene.widgets.attach(node, widget);
  return node;
}

NodeId add_checkbox(Scene& scene, NodeId parent) {
  BoxStyle box;
  box.kind = LayoutKind::kLeaf;
  box.width = 32;
  box.height = 32;
  box.padding = EdgeInsets::all(6);
  const NodeId node = scene.tree.add_child(parent, box, bordered(kButtonNormal, kBorder, 6.0F));

  BoxStyle indicator_box;
  indicator_box.width = 20;
  indicator_box.height = 20;
  const NodeId indicator = scene.tree.add_child(node, indicator_box, flat(kOff));

  Widget widget;
  widget.kind = WidgetKind::kCheckbox;
  widget.fill_normal = Color::from_argb(kButtonNormal);
  widget.fill_hover = Color::from_argb(kButtonHover);
  widget.fill_pressed = Color::from_argb(kButtonPressed);
  widget.indicator = indicator;
  widget.indicator_on = Color::from_argb(kAccent);
  widget.indicator_off = Color::from_argb(kOff);
  scene.widgets.attach(node, widget);
  scene.widgets.attach(indicator, Widget{});
  return node;
}

NodeId add_slider(Scene& scene, NodeId parent) {
  BoxStyle track_box;
  track_box.kind = LayoutKind::kLeaf;
  track_box.width = 140;
  track_box.height = 10;
  const NodeId track = scene.tree.add_child(parent, track_box, bordered(kTrack, kBorder, 5.0F));

  BoxStyle thumb_box;
  thumb_box.width = 18;
  thumb_box.height = 18;
  const NodeId thumb = scene.tree.add_child(track, thumb_box, bordered(kThumb, kBorder, 9.0F));

  Widget widget;
  widget.kind = WidgetKind::kSlider;
  widget.thumb = thumb;
  widget.min_value = 0.0F;
  widget.max_value = 1.0F;
  widget.value = 0.4F;
  scene.widgets.attach(track, widget);
  scene.widgets.attach(thumb, Widget{});
  return track;
}

NodeStyle field_style(bool focused) {
  NodeStyle style;
  style.fill = Color::from_argb(kFieldFill);
  style.border_color = Color::from_argb(focused ? kAccent : kFieldBorder);
  style.border_width = BorderWidths::all(1.0F);
  style.radii = Radii::all(6.0F);
  style.overflow = Overflow::kClip;
  return style;
}

NodeId add_textfield(Scene& scene, NodeId parent, dg::FontId font) {
  BoxStyle field_box;
  field_box.kind = LayoutKind::kLeaf;
  field_box.width = 180;
  field_box.height = 36;
  const NodeId field = scene.tree.add_child(parent, field_box, field_style(false));

  BoxStyle placeholder;
  placeholder.kind = LayoutKind::kLeaf;
  placeholder.width = 1;
  placeholder.height = 1;

  const NodeId highlight = scene.tree.add_child(field, placeholder, flat(kSelectionFill));
  const NodeId underline =
      scene.tree.add_child(field, placeholder, flat(kCompositionUnderlineColor));

  NodeStyle content_style;
  content_style.text.font = font;
  content_style.text.size = 16.0F;
  content_style.text.color = Color::from_argb(kTextColor);
  content_style.text.align = TextAlign::kLeft;
  const NodeId content = scene.tree.add_child(field, placeholder, content_style);

  const NodeId caret = scene.tree.add_child(field, placeholder, flat(kCaretColor));

  Widget widget;
  widget.kind = WidgetKind::kTextField;
  widget.content = content;
  widget.caret = caret;
  widget.selection_highlight = highlight;
  widget.composition_underline = underline;
  scene.widgets.attach(field, widget);
  return field;
}

void apply_side_effects(Scene& scene, dg::WindowManager& manager, dg::WindowId window,
                        const dg::FocusChange& change) {
  if (!change.any()) {
    return;
  }

  if (scene.fonts.has_value()) {
    const dg::FontCatalog& fonts = *scene.fonts;
    if (change.blurred == scene.handles.textfield) {
      scene.widgets.text_field_set_focus(scene.tree.render(), fonts, scene.handles.textfield,
                                         false);
      manager.stop_text_input(window);
      manager.clear_composition(window);
      scene.tree.render().set_style(scene.handles.textfield, field_style(false));
    }
    if (change.focused == scene.handles.textfield) {
      scene.widgets.text_field_set_focus(scene.tree.render(), fonts, scene.handles.textfield,
                                         true);
      // The field's own absolute bounds stand in for the precise caret
      // rectangle 7-3's internal `focused_display_projection()` computes -
      // that helper is not public API, and an IME only needs to land
      // somewhere near the field, not on the exact glyph.
      manager.start_text_input(window,
                               scene.tree.render().absolute_bounds(scene.handles.textfield));
      scene.tree.render().set_style(scene.handles.textfield, field_style(true));
    }
  }

  const dg::Color ring_color =
      scene.theme.color_value(DG_TOKEN_COLOR_FOCUS_RING, dg::ThemeVariant::kDark)
          .value_or(dg::Color::from_argb(0xFFFF8800));
  dg::update_focus_ring(scene.tree.render(), LayoutTree::root(), scene.ring,
                        scene.focus.current(), ring_color);
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

  Scene scene{LayoutTree{spec}, dg::WidgetSet{}, dg::Interaction{}, dg::Focus{},
              dg::FocusRing{},  Handles{},       std::move(fonts),  loaded.value()};

  BoxStyle body_box;
  body_box.kind = LayoutKind::kColumn;
  body_box.gap = 20;
  body_box.padding = EdgeInsets::all(20);
  body_box.cross_align = CrossAlign::kStretch;
  scene.handles.body = LayoutTree::root();
  scene.tree.set_box(scene.handles.body, body_box);

  const NodeId row1 = add_section(scene.tree, scene.handles.body, 16);
  scene.handles.btn_open = add_button(scene, row1, 120, std::nullopt);
  BoxStyle label_box;
  label_box.kind = LayoutKind::kLeaf;
  label_box.width = 90;
  label_box.height = 24;
  scene.handles.label_between = scene.tree.add_child(row1, label_box, flat(kLabelFill));
  scene.handles.checkbox = add_checkbox(scene, row1);
  scene.handles.slider = add_slider(scene, row1);

  const NodeId row2 = add_section(scene.tree, scene.handles.body, 16);
  scene.handles.textfield = add_textfield(scene, row2, ui);
  // `inert` is added BEFORE `reversed` here, on purpose: both sit after
  // every row1/row2 focusable widget in TREE order, so `reversed`'s
  // positive tab_index is what has to override tree order for it to lead
  // the sequence rather than merely happening to be first among equals.
  scene.handles.inert = add_button(scene, row2, 90, -1);
  scene.handles.reversed = add_button(scene, row2, 90, 1);

  scene.tree.layout_full();
  scene.widgets.resync_sliders(scene.tree.render());
  if (scene.fonts.has_value()) {
    scene.widgets.text_field_set_focus(scene.tree.render(), *scene.fonts,
                                       scene.handles.textfield, false);
  }

  return scene;
}

std::string describe(const Scene& scene, dg::NodeId id) {
  if (id == scene.handles.btn_open) {
    return "btn_open";
  }
  if (id == scene.handles.checkbox) {
    return "checkbox";
  }
  if (id == scene.handles.slider) {
    return "slider";
  }
  if (id == scene.handles.textfield) {
    return "textfield";
  }
  if (id == scene.handles.reversed) {
    return "reversed (tab_index=1)";
  }
  if (id == scene.handles.inert) {
    return "inert (tab_index=-1)";
  }
  if (id == scene.handles.label_between) {
    return "label_between (non-focusable)";
  }
  return "node " + std::to_string(id.index);
}

void apply_focus_change(Scene& scene, dg::WindowManager& manager, dg::WindowId window,
                        std::optional<dg::NodeId> target) {
  apply_side_effects(scene, manager, window, scene.focus.set(target));
}

void tab(Scene& scene, dg::WindowManager& manager, dg::WindowId window, bool backwards) {
  // Unlike apply_focus_change(), focus_next()/focus_previous() already
  // performed the transition (they must, to know what "next" means) - so
  // their OWN returned FocusChange is what carries the blur, not a second
  // dg::Focus::set() call re-deriving one against a target that already
  // matches the current state (which would report a no-op and skip every
  // side effect below).
  const dg::FocusChange change =
      backwards
          ? scene.focus.focus_previous(scene.tree.render(), scene.widgets, scene.handles.body)
          : scene.focus.focus_next(scene.tree.render(), scene.widgets, scene.handles.body);
  apply_side_effects(scene, manager, window, change);
}

bool dispatch_pointer(Scene& scene, dg::WindowManager& manager, dg::WindowId window,
                      const dg::PointerEvent& event) {
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
      break;
  }

  if (change.clicked.has_value()) {
    scene.widgets.toggle(*change.clicked);
  }
  const std::optional<NodeId> touched[] = {change.left, change.entered, change.pressed,
                                           change.released, change.clicked};
  for (const std::optional<NodeId>& id : touched) {
    if (id.has_value()) {
      scene.widgets.refresh(scene.tree.render(), *id, scene.interaction.state_of(*id));
    }
  }

  return change.clicked == scene.handles.btn_open;
}

void dispatch_key(Scene& scene, dg::WindowManager& manager, dg::WindowId window,
                  const dg::KeyEvent& event) {
  if (event.action != dg::KeyAction::kDown) {
    return;
  }
  if (event.key == dg::Key::kTab) {
    tab(scene, manager, window, dg::has(event.mods, dg::Modifier::kShift));
    return;
  }
  if (!scene.focus.is_focused(scene.handles.textfield) || !scene.fonts.has_value()) {
    return;
  }
  const dg::FontCatalog& fonts = *scene.fonts;
  dg::RenderTree& tree = scene.tree.render();
  const NodeId field = scene.handles.textfield;
  switch (event.key) {
    case dg::Key::kLeft:
      scene.widgets.text_field_move(tree, fonts, field, dg::TextFieldMove::kCharLeft,
                                    dg::has(event.mods, dg::Modifier::kShift));
      break;
    case dg::Key::kRight:
      scene.widgets.text_field_move(tree, fonts, field, dg::TextFieldMove::kCharRight,
                                    dg::has(event.mods, dg::Modifier::kShift));
      break;
    case dg::Key::kHome:
      scene.widgets.text_field_move(tree, fonts, field, dg::TextFieldMove::kLineStart,
                                    dg::has(event.mods, dg::Modifier::kShift));
      break;
    case dg::Key::kEnd:
      scene.widgets.text_field_move(tree, fonts, field, dg::TextFieldMove::kLineEnd,
                                    dg::has(event.mods, dg::Modifier::kShift));
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

}  // namespace focus_scene
