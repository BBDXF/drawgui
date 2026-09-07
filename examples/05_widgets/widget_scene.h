// The scene examples/05_widgets puts on screen, built without a window.
//
// Nothing here touches SDL, Skia or a display, for the reason sub-steps 1 and
// 2 both established: the verification that matters compares the scene the
// user is actually looking at, not a second scene written to be easy to check.
// So this file is compiled into the demo AND into the headless test binaries,
// and the byte-identity check runs against these exact widgets.
//
// It also owns the POINTER ROUTING - dispatch() below - for the same reason.
// A test that re-implemented "hit test, then drive the state machine, then
// refresh what changed" would be checking its own copy of the pipeline. The
// demo and the test call one function, so a defect in it has nowhere to hide.
//
// The scene is built to contain the shapes hit testing can get wrong, because
// sub-step 2 learned the hard way that a demo scene is evidence only about the
// shapes it contains:
//
//   nested       - a button's label is inside the button, which is inside a
//                  panel; the pointer lands on the label and must activate the
//                  button
//   overlapping  - two buttons in an absolutely-positioned lab, deliberately
//                  on top of one another
//   overflowing  - a button whose box runs past its parent panel on two sides
//   adjacent     - buttons sharing an edge, so the boundary column has exactly
//                  one owner
//   inert        - panels and labels that must never take a click

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/font_catalog.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/widget/interaction.h"
#include "drawgui/widget/widget_set.h"
#include "drawgui/window/window_manager.h"

namespace widget_scene {

struct Options {
  dg::TreeSpec spec;

  // Two radius knobs, not one, because sub-step 1 measured that they cost
  // wildly different amounts and conflating them hides the whole finding.
  //
  // A rounded node is clip-atomic and repaints whole. Rounding a small CONTROL
  // therefore costs a little; rounding a CONTAINER makes its entire area the
  // smallest unit of damage anything inside it can produce, which is where the
  // 30x came from. The demo reports both so the trade is chosen rather than
  // discovered.
  bool rounded_controls = false;
  bool rounded_containers = false;

  std::string font_dir = "/usr/share/fonts";
};

struct Handles {
  dg::NodeId readout;
  dg::NodeId counter_label;
  dg::NodeId hover_label;
  dg::NodeId checkbox;
  dg::NodeId checkbox_label;

  // The overlap lab, where the interesting hit-testing cases live.
  dg::NodeId lab;
  dg::NodeId lab_under;
  dg::NodeId lab_over;
  dg::NodeId lab_host;
  dg::NodeId lab_overflow;

  // Anchored to the lab's far corner rather than its near one, so it MOVES
  // when the window is resized. Everything else in the scene is a fixed size
  // at a fixed offset and sits still through a reflow - which would make a
  // resize check pass without testing anything. Sub-step 2 recorded that trap;
  // this is the shape that avoids it.
  dg::NodeId lab_pinned;

  // Every interactive widget, in the order it was created. What a scripted
  // pass walks.
  std::vector<dg::NodeId> interactive;

  // A label inside a button, and the button that owns it: the pair a nesting
  // check needs by name rather than by index.
  dg::NodeId nested_label;
  dg::NodeId nested_owner;

  dg::FontId ui_font;
  dg::FontId bold_font;

  // Resolved only when this machine has the family. There is no fallback
  // chain, so a missing CJK family draws nothing rather than boxes, and the
  // demo says so instead of looking broken.
  dg::FontId cjk_font;
};

struct Scene {
  dg::LayoutTree tree;
  dg::WidgetSet widgets;
  Handles handles;

  // Counted here rather than in the demo so the headless check can assert on
  // it: a click that changes nothing visible is still a click that has to have
  // happened.
  int clicks = 0;
};

[[nodiscard]] Scene build(const Options& options);

// Hit test, drive the state machine, and repaint exactly what changed.
//
// The one path a pointer event takes, shared by the window loop and by every
// headless verification. A scripted mode that skipped it would be proving
// something about a pipeline nobody runs.
dg::InteractionChange dispatch(Scene& scene, dg::Interaction& interaction,
                               const dg::PointerEvent& event);

// Re-resolves the widget under a pointer that has not moved.
//
// A relayout moves widgets under a stationary pointer, and no platform sends a
// motion event for that - so without this the hover would describe the layout
// the window no longer has. This is the "stale hit rectangle after reflow" bug
// in its only surviving form: there are no stored hit rectangles to go stale,
// because hit testing reads the live tree, but the RESOLVED hover is a cached
// answer and it does go stale.
dg::InteractionChange resync(Scene& scene, dg::Interaction& interaction, dg::PixelPoint at);

// Applies whatever `change` implies: toggles a checkbox that was clicked,
// repaints the widgets that entered, left, took or lost a press, and updates
// the labels that report what happened.
void react(Scene& scene, const dg::Interaction& interaction,
           const dg::InteractionChange& change);

[[nodiscard]] std::string describe(const Scene& scene, dg::NodeId id);

}  // namespace widget_scene
