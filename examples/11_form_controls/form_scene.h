// The scene examples/11_form_controls puts on screen, and the handles a
// check needs.
//
// FOUR CONTROLS, built from what already exists rather than anything new:
//
//   checkbox        WidgetKind::kCheckbox, unmodified from step 3-3 -
//                   included so a regression in this slice's changes to
//                   toggle() would show up here too, not only on the new
//                   radio path that shares the same function.
//
//   radio group A   three WidgetKind::kCheckbox nodes sharing group id 1 -
//                   "checkbox plus a group id", not a new WidgetKind.
//
//   radio group B   two more, sharing group id 2 - proves group A and group
//                   B do not interfere, which a single-group scene cannot.
//
//   sliders         WidgetKind::kSlider, a track (grow=1, so it reacts to a
//                   window resize) and a stepped one (a fixed width, so a
//                   resize does NOT move its thumb - the control case).
//
// NO TEXT, for the identical reason examples/10_scrolling gives: a font
// catalog is machinery this scene does not need to exercise radio/slider
// mechanics. State is legible from indicator/thumb colour and position
// alone, which is what every hand-derived check in form_check.cpp reads.

#pragma once

#include <string>
#include <vector>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/widget/interaction.h"
#include "drawgui/widget/widget_set.h"
#include "drawgui/window/window_manager.h"

namespace form_scene {

inline constexpr int kSliderTrackHeight = 20;
inline constexpr int kThumbSize = 20;
inline constexpr int kFixedTrackWidth = 240;

inline constexpr float kVolumeMin = 0.0F;
inline constexpr float kVolumeMax = 100.0F;
inline constexpr float kVolumeInitial = 30.0F;

inline constexpr float kBrightnessMin = 0.0F;
inline constexpr float kBrightnessMax = 10.0F;
inline constexpr float kBrightnessStep = 1.0F;
inline constexpr float kBrightnessInitial = 3.0F;

struct Handles {
  dg::NodeId body;

  dg::NodeId checkbox;

  std::vector<dg::NodeId> radio_group_a;
  std::vector<dg::NodeId> radio_group_b;

  dg::NodeId slider_volume;
  dg::NodeId slider_brightness;

  // Every kCheckbox node (the plain checkbox and both radio groups) - what a
  // click loop and a resize/hover check both want without caring which is
  // which.
  std::vector<dg::NodeId> toggles;
};

struct Scene {
  dg::LayoutTree tree;
  dg::WidgetSet widgets;
  Handles handles;
  int clicks = 0;
};

Scene build(const dg::TreeSpec& spec);

[[nodiscard]] std::string describe(const Scene& scene, dg::NodeId id);

// Routes one pointer event (move/down/up/leave) through dg::Interaction for
// the checkbox/radio half of the scene - the same shape
// examples/05_widgets::dispatch() has. Sliders are NOT routed through here:
// kSlider is not accepts_pointer(), so a click on one resolves to "no
// widget" and this is a no-op for it by construction, exactly as a click on
// a scroll viewport's plain content is in examples/10_scrolling. The window
// driver checks slidable_owner_of() BEFORE calling this, and skips this call
// entirely while a drag is in flight.
dg::InteractionChange dispatch(Scene& scene, dg::Interaction& interaction,
                               const dg::PointerEvent& event);

// Re-resolves hover at a fixed point without moving anything - the resync a
// resize needs (doc/widgets.md section 4), reused verbatim.
dg::InteractionChange resync(Scene& scene, dg::Interaction& interaction, dg::PixelPoint at);

}  // namespace form_scene
