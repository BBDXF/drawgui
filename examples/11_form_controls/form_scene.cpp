#include "form_scene.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "drawgui/graphics/types.h"
#include "drawgui/layout/box.h"

namespace form_scene {
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
using dg::Widget;
using dg::WidgetKind;

constexpr std::uint32_t kOff = 0xFF10151C;
constexpr std::uint32_t kAccent = 0xFF2E86DE;
constexpr std::uint32_t kToggleNormal = 0xFF2C3644;
constexpr std::uint32_t kToggleHover = 0xFF3B4A5E;
constexpr std::uint32_t kTogglePressed = 0xFF1F2731;
constexpr std::uint32_t kBorder = 0xFF141A22;
constexpr std::uint32_t kTrack = 0xFF20262F;
constexpr std::uint32_t kThumb = 0xFF97A3B4;
constexpr std::uint32_t kSectionFill = 0xFF14171C;
constexpr std::uint32_t kSectionBorder = 0xFF232A34;

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

// One clickable toggle: a 32x32 box (hover/press feedback on its own fill)
// holding a 20x20 indicator (checked/unchecked feedback on ITS fill) - the
// exact shape doc/widgets.md's checkbox already has. `group` unset makes a
// plain checkbox; set, it makes a radio option sharing that group id -
// "checkbox plus one field", not a second construction.
NodeId add_toggle(Scene& scene, NodeId parent, std::optional<int> group,
                  bool initially_checked) {
  const bool round = group.has_value();  // a radio dot is round; a checkbox is not

  BoxStyle box;
  box.kind = LayoutKind::kLeaf;
  box.width = 32;
  box.height = 32;
  box.padding = EdgeInsets::all(6);
  const NodeId node =
      scene.tree.add_child(parent, box, bordered(kToggleNormal, kBorder, round ? 16.0F : 6.0F));

  BoxStyle indicator_box;
  indicator_box.width = 20;
  indicator_box.height = 20;
  NodeStyle indicator_style = flat(initially_checked ? kAccent : kOff);
  if (round) {
    indicator_style.radii = Radii::all(10.0F);
  }
  const NodeId indicator = scene.tree.add_child(node, indicator_box, indicator_style);

  Widget widget;
  widget.kind = WidgetKind::kCheckbox;
  widget.fill_normal = Color::from_argb(kToggleNormal);
  widget.fill_hover = Color::from_argb(kToggleHover);
  widget.fill_pressed = Color::from_argb(kTogglePressed);
  widget.indicator = indicator;
  widget.indicator_on = Color::from_argb(kAccent);
  widget.indicator_off = Color::from_argb(kOff);
  widget.checked = initially_checked;
  widget.group = group;
  scene.widgets.attach(node, widget);
  scene.widgets.attach(indicator, Widget{});

  scene.handles.toggles.push_back(node);
  return node;
}

// A track (kLeaf) and its thumb (the one child). `width` > 0 gives the
// track a fixed width; 0 makes it `grow = 1` inside its row instead, so it
// widens with the window - the two cases form_check.cpp's resize claim
// tells apart.
NodeId add_slider(Scene& scene, NodeId parent, int width, float min_value, float max_value,
                  float step, float initial_value) {
  BoxStyle track_box;
  track_box.kind = LayoutKind::kLeaf;
  if (width > 0) {
    track_box.width = width;
  } else {
    track_box.grow = 1;
  }
  track_box.height = kSliderTrackHeight;
  const NodeId track = scene.tree.add_child(
      parent, track_box, bordered(kTrack, kSectionBorder, kSliderTrackHeight / 2.0F));

  BoxStyle thumb_box;
  thumb_box.width = kThumbSize;
  thumb_box.height = kThumbSize;
  const NodeId thumb =
      scene.tree.add_child(track, thumb_box, bordered(kThumb, kBorder, kThumbSize / 2.0F));

  Widget widget;
  widget.kind = WidgetKind::kSlider;
  widget.thumb = thumb;
  widget.min_value = min_value;
  widget.max_value = max_value;
  widget.step = step;
  widget.value = initial_value;
  scene.widgets.attach(track, widget);
  scene.widgets.attach(thumb, Widget{});
  return track;
}

NodeId add_section(Scene& scene, NodeId parent, int gap, CrossAlign cross) {
  BoxStyle box;
  box.kind = LayoutKind::kRow;
  box.gap = gap;
  box.cross_align = cross;
  box.padding = EdgeInsets::all(14);
  box.height = 60;
  return scene.tree.add_child(parent, box, bordered(kSectionFill, kSectionBorder, 8.0F));
}

}  // namespace

Scene build(const dg::TreeSpec& spec) {
  LayoutTree tree{spec};
  dg::WidgetSet widgets;
  Handles handles;

  BoxStyle body_box;
  body_box.kind = LayoutKind::kColumn;
  body_box.gap = 20;
  body_box.padding = EdgeInsets::all(20);
  body_box.cross_align = CrossAlign::kStretch;
  handles.body = LayoutTree::root();
  tree.set_box(handles.body, body_box);

  Scene scene{std::move(tree), std::move(widgets), std::move(handles)};

  // --- The checkbox: unmodified from step 3-3, present to prove toggle()'s
  //     plain (ungrouped) path still works after this slice's changes. ---
  const NodeId checkbox_section =
      add_section(scene, scene.handles.body, 12, CrossAlign::kCenter);
  scene.handles.checkbox = add_toggle(scene, checkbox_section, std::nullopt, false);

  // --- Radio group A: three mutually exclusive options, none selected. ---
  const NodeId group_a_section =
      add_section(scene, scene.handles.body, 12, CrossAlign::kCenter);
  for (int i = 0; i < 3; ++i) {
    scene.handles.radio_group_a.push_back(add_toggle(scene, group_a_section, 1, false));
  }

  // --- Radio group B: two options, independent of group A. ---
  const NodeId group_b_section =
      add_section(scene, scene.handles.body, 12, CrossAlign::kCenter);
  for (int i = 0; i < 2; ++i) {
    scene.handles.radio_group_b.push_back(add_toggle(scene, group_b_section, 2, false));
  }

  // --- Sliders: one grows with the window, one is fixed and stepped. ---
  const NodeId slider_section = add_section(scene, scene.handles.body, 24, CrossAlign::kCenter);
  scene.handles.slider_volume =
      add_slider(scene, slider_section, 0, kVolumeMin, kVolumeMax, 0.0F, kVolumeInitial);
  scene.handles.slider_brightness =
      add_slider(scene, slider_section, kFixedTrackWidth, kBrightnessMin, kBrightnessMax,
                 kBrightnessStep, kBrightnessInitial);

  scene.tree.layout_full();

  // Nothing has ever positioned a thumb yet - see resync_sliders()'s own
  // doc comment for why this is the same obligation a resize re-triggers.
  scene.widgets.resync_sliders(scene.tree.render());

  return scene;
}

std::string describe(const Scene& scene, NodeId id) {
  const auto index_of = [&](const std::vector<NodeId>& items, const char* label) {
    for (std::size_t i = 0; i < items.size(); ++i) {
      if (items[i] == id) {
        return std::string{label} + " " + std::to_string(i);
      }
    }
    return std::string{};
  };
  std::string found = index_of(scene.handles.radio_group_a, "radio a");
  if (!found.empty()) {
    return found;
  }
  found = index_of(scene.handles.radio_group_b, "radio b");
  if (!found.empty()) {
    return found;
  }
  if (id == scene.handles.checkbox) {
    return "checkbox";
  }
  if (id == scene.handles.slider_volume) {
    return "slider volume";
  }
  if (id == scene.handles.slider_brightness) {
    return "slider brightness";
  }
  return "node " + std::to_string(id.value);
}

dg::InteractionChange dispatch(Scene& scene, dg::Interaction& interaction,
                               const dg::PointerEvent& event) {
  const dg::PixelPoint at{event.x, event.y};
  dg::InteractionChange change;
  switch (event.action) {
    case dg::PointerAction::kMove:
      change = interaction.moved_over(scene.widgets.widget_at(scene.tree.render(), at));
      break;
    case dg::PointerAction::kDown:
      change = interaction.pressed_on(scene.widgets.widget_at(scene.tree.render(), at));
      break;
    case dg::PointerAction::kUp:
      change = interaction.released_on(scene.widgets.widget_at(scene.tree.render(), at));
      break;
    case dg::PointerAction::kLeave:
      change = interaction.left_window();
      break;
    case dg::PointerAction::kWheel:
      // No scrollable widget in this scene - a no-op rather than an
      // omission, matching examples/05_widgets's identical case.
      break;
  }

  if (change.clicked.has_value()) {
    ++scene.clicks;
    scene.widgets.toggle(*change.clicked);
    // The clicked widget's own appearance, plus every OTHER member of its
    // radio group - toggle() cannot repaint what it just deselected, so
    // group_members() is what tells this loop which other widgets to
    // refresh. Empty for a plain checkbox or an unattached node.
    for (const NodeId sibling : scene.widgets.group_members(*change.clicked)) {
      scene.widgets.refresh(scene.tree.render(), sibling, dg::PointerState{});
    }
  }

  const std::optional<NodeId> touched[] = {change.left, change.entered, change.pressed,
                                           change.released, change.clicked};
  for (const std::optional<NodeId>& id : touched) {
    if (id.has_value()) {
      scene.widgets.refresh(scene.tree.render(), *id, interaction.state_of(*id));
    }
  }
  return change;
}

dg::InteractionChange resync(Scene& scene, dg::Interaction& interaction, dg::PixelPoint at) {
  const dg::InteractionChange change =
      interaction.moved_over(scene.widgets.widget_at(scene.tree.render(), at));
  for (const std::optional<NodeId> id : {change.left, change.entered}) {
    if (id.has_value()) {
      scene.widgets.refresh(scene.tree.render(), *id, interaction.state_of(*id));
    }
  }
  return change;
}

}  // namespace form_scene
