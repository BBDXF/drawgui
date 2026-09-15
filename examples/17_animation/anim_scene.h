// The scene examples/17_animation puts on screen, and the handles the window
// and the headless check both drive.
//
// THREE PANELS, each a different real client of dg::AnimationEngine rather
// than a synthetic interpolation with nothing behind it:
//
//   slide   an explicitly-animated chip - dg::AnimationEngine::animate() on
//           `left` (a plain float property, design.md section 5.9.6's
//           "offset" category), ping-ponging between two positions and
//           pausable/reversible/cancellable through the handle it returns.
//           This is the panel that proves the explicit half of section
//           5.16.1 and the on-demand frame loop of section 5.15.1: while it
//           runs, this window's damage is never empty, so the frame loop
//           keeps going; the moment nothing is animating, it goes back to
//           blocking in WindowManager::pump().
//
//   hover   a button-shaped panel with dg_node_set_transition()'s C++ shape
//           declared on `background_color` - the CSS transition model, and
//           the panel the task itself calls out as "the most direct
//           demonstration of the transition API". Hovering it with the real
//           pointer retargets the colour toward the hover fill; leaving
//           retargets it back - see anim_window.cpp for the retarget-mid-
//           flight proof this panel exists to make visible, not merely
//           assert in a unit test.
//
//   caret   a thin vertical bar blinking like a text cursor - the client
//           doc/text-input.md's 4-9 declined by name for lack of a clock.
//           Built from FOUR chained explicit animate() calls (fade out, hold
//           off, fade in, hold on), each launched from the PREVIOUS one's
//           completion event - proving events arrive through poll_events(),
//           never a callback, and demonstrating the honest reduced-motion
//           caveat doc/animation.md records: a client that loops by
//           re-triggering on completion has to decide for ITSELF what
//           "reduced motion" means for a loop (this one freezes solid),
//           because the engine's own "duration -> 0" policy alone would
//           otherwise flicker at the frame pacer's rate.
//
// Built from the LAYOUT tree, matching every prior interactive demo, so the
// panels are real device-pixel rectangles a real AnimationEngine writes
// through dg::set_prop() - not a hand-rolled position this scene invented
// for its own convenience.

#pragma once

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/types.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/render_tree.h"

namespace anim_scene {

inline constexpr dg::PixelSize kDemoViewport{760, 220};

inline constexpr dg::Color kPanelFill = dg::Color::from_argb(0xFF20262F);
inline constexpr dg::Color kFrameEdge = dg::Color::from_argb(0xFF5A6472);

inline constexpr dg::Color kChipFill = dg::Color::from_argb(0xFF3FA9F5);
inline constexpr int kSlideTravelPx = 200;
inline constexpr std::int64_t kSlideDurationMs = 900;

inline constexpr dg::Color kButtonNormal = dg::Color::from_argb(0xFF2E3846);
inline constexpr dg::Color kButtonHover = dg::Color::from_argb(0xFF3FA9F5);
inline constexpr std::int64_t kHoverTransitionMs = 180;

inline constexpr dg::Color kCaretColor = dg::Color::from_argb(0xFFE7A23C);
inline constexpr std::int64_t kBlinkFadeMs = 120;
inline constexpr std::int64_t kBlinkHoldMs = 380;

struct Handles {
  dg::NodeId row;

  dg::NodeId slide_frame;  // kAbsolute container - `left` has a consumer.
  dg::NodeId slide_chip;   // the child dg::AnimationEngine::animate() moves.

  dg::NodeId hover_panel;  // background_color is the transitioned property.

  dg::NodeId caret_frame;
  dg::NodeId caret;  // opacity is the animated property.
};

struct Scene {
  dg::LayoutTree tree;
  Handles handles;
};

Scene build(const dg::TreeSpec& spec);

}  // namespace anim_scene
