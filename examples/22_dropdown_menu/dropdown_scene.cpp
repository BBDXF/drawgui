#include "dropdown_scene.h"

#include <cstdint>
#include <utility>

#include "drawgui/graphics/types.h"
#include "drawgui/layout/box.h"
#include "drawgui/theme/theme_loader.h"
#include "drawgui/theme/token_ids.generated.h"

namespace dropdown_scene {
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
using dg::Radii;
using dg::TextAlign;
using dg::Widget;
using dg::WidgetKind;

constexpr std::uint32_t kBodyFill = 0xFF0E1218;
constexpr std::uint32_t kButtonNormal = 0xFF2C3644;
constexpr std::uint32_t kButtonHover = 0xFF3B4A5E;
constexpr std::uint32_t kButtonPressed = 0xFF1F2731;
constexpr std::uint32_t kBorder = 0xFF141A22;
constexpr std::uint32_t kTextColor = 0xFFE8EDF4;

NodeStyle flat(std::uint32_t argb) {
  NodeStyle style;
  style.fill = Color::from_argb(argb);
  return style;
}

NodeStyle bordered(std::uint32_t fill, std::uint32_t border, float radius) {
  NodeStyle style = flat(fill);
  style.border_color = Color::from_argb(border);
  style.border_width = BorderWidths::all(1.0F);
  style.radii = Radii::all(radius);
  return style;
}

NodeId add_button(Scene& scene, NodeId parent, int width, const std::string& label,
                  dg::FontId font) {
  BoxStyle box;
  box.kind = LayoutKind::kLeaf;
  box.width = width;
  box.height = 32;
  const NodeId node = scene.tree.add_child(parent, box, bordered(kButtonNormal, kBorder, 6.0F));

  BoxStyle label_box;
  label_box.width = width;
  label_box.height = 32;
  NodeStyle label_style;
  label_style.text.text = label;
  label_style.text.font = font;
  label_style.text.size = 15.0F;
  label_style.text.color = Color::from_argb(kTextColor);
  label_style.text.align = TextAlign::kCenter;
  scene.tree.add_child(node, label_box, label_style);

  Widget widget;
  widget.kind = WidgetKind::kButton;
  widget.fill_normal = Color::from_argb(kButtonNormal);
  widget.fill_hover = Color::from_argb(kButtonHover);
  widget.fill_pressed = Color::from_argb(kButtonPressed);
  scene.widgets.attach(node, widget);
  return node;
}

NodeId add_dropdown(Scene& scene, NodeId parent, int width, dg::FontId font) {
  BoxStyle box;
  box.kind = LayoutKind::kLeaf;
  box.width = width;
  box.height = 32;
  const NodeId node = scene.tree.add_child(parent, box, bordered(kButtonNormal, kBorder, 6.0F));

  BoxStyle label_box;
  label_box.width = width;
  label_box.height = 32;
  NodeStyle label_style;
  label_style.text.text = "Choose a fruit";
  label_style.text.font = font;
  label_style.text.size = 15.0F;
  label_style.text.color = Color::from_argb(kTextColor);
  label_style.text.align = TextAlign::kLeft;
  label_style.text.inset = 10;
  const NodeId label = scene.tree.add_child(node, label_box, label_style);

  Widget widget;
  widget.kind = WidgetKind::kDropdown;
  widget.fill_normal = Color::from_argb(kButtonNormal);
  widget.fill_hover = Color::from_argb(kButtonHover);
  widget.fill_pressed = Color::from_argb(kButtonPressed);
  widget.label = label;
  widget.options = kOptions;
  scene.widgets.attach(node, widget);
  return node;
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

  spec.background.fill = Color::from_argb(kBodyFill);
  Scene scene{LayoutTree{spec}, dg::WidgetSet{}, dg::Interaction{}, dg::Focus{},
              dg::FocusRing{},  Handles{},       std::move(fonts),  ui,
              loaded.value()};

  BoxStyle body_box;
  body_box.kind = LayoutKind::kRow;
  body_box.gap = 16;
  body_box.padding = EdgeInsets::all(20);
  body_box.cross_align = CrossAlign::kCenter;
  scene.handles.body = LayoutTree::root();
  scene.tree.set_box(scene.handles.body, body_box);

  scene.handles.before = add_button(scene, scene.handles.body, 90, "Before", ui);
  scene.handles.dropdown = add_dropdown(scene, scene.handles.body, 180, ui);
  scene.handles.after = add_button(scene, scene.handles.body, 90, "After", ui);

  scene.tree.layout_full();

  return scene;
}

std::string describe(const Scene& scene, dg::NodeId id) {
  if (id == scene.handles.before) {
    return "before";
  }
  if (id == scene.handles.dropdown) {
    return "dropdown";
  }
  if (id == scene.handles.after) {
    return "after";
  }
  return "node " + std::to_string(id.index);
}

void apply_focus_change(Scene& scene, std::optional<dg::NodeId> target) {
  const dg::FocusChange change = scene.focus.set(target);
  if (!change.any()) {
    return;
  }
  const dg::Color ring_color =
      scene.theme.color_value(DG_TOKEN_COLOR_FOCUS_RING, dg::ThemeVariant::kDark)
          .value_or(dg::Color::from_argb(0xFFFF8800));
  dg::update_focus_ring(scene.tree.render(), LayoutTree::root(), scene.ring,
                        scene.focus.current(), ring_color);
}

void tab(Scene& scene, bool backwards) {
  const dg::FocusChange change =
      backwards
          ? scene.focus.focus_previous(scene.tree.render(), scene.widgets, scene.handles.body)
          : scene.focus.focus_next(scene.tree.render(), scene.widgets, scene.handles.body);
  if (!change.any()) {
    return;
  }
  const dg::Color ring_color =
      scene.theme.color_value(DG_TOKEN_COLOR_FOCUS_RING, dg::ThemeVariant::kDark)
          .value_or(dg::Color::from_argb(0xFFFF8800));
  dg::update_focus_ring(scene.tree.render(), LayoutTree::root(), scene.ring,
                        scene.focus.current(), ring_color);
}

bool dispatch_pointer(Scene& scene, const dg::PointerEvent& event) {
  const dg::PixelPoint at{event.x, event.y};
  dg::InteractionChange change;
  switch (event.action) {
    case dg::PointerAction::kMove:
      change = scene.interaction.moved_over(scene.widgets.widget_at(scene.tree.render(), at));
      break;
    case dg::PointerAction::kDown: {
      const std::optional<NodeId> hit = scene.widgets.widget_at(scene.tree.render(), at);
      change = scene.interaction.pressed_on(hit);
      const bool focusable = hit.has_value() && scene.widgets.has(*hit) &&
                             dg::is_focusable(scene.widgets.at(*hit).kind);
      apply_focus_change(scene, focusable ? hit : std::nullopt);
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

  const std::optional<NodeId> touched[] = {change.left, change.entered, change.pressed,
                                           change.released, change.clicked};
  for (const std::optional<NodeId>& id : touched) {
    if (id.has_value()) {
      scene.widgets.refresh(scene.tree.render(), *id, scene.interaction.state_of(*id));
    }
  }

  return change.clicked == scene.handles.dropdown;
}

void dispatch_key(Scene& scene, const dg::KeyEvent& event) {
  if (event.action != dg::KeyAction::kDown || event.key != dg::Key::kTab) {
    return;
  }
  tab(scene, dg::has(event.mods, dg::Modifier::kShift));
}

}  // namespace dropdown_scene
