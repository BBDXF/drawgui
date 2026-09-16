// The host window's own scene for examples/21_focus: five focusable widgets
// of FOUR different WidgetKinds, a non-focusable label sandwiched between
// two of them, and one sibling whose Tab position (tab_index=1) is the
// REVERSE of its tree position - deliberately shaped so a wrong Tab order
// permutes visibly distinct widgets rather than a row of identical chips
// (the "missing scene shape" failure mode named in .omo/plans/drawgui-
// kernel.md's own catalogue).
//
//   row1: btn_open (kButton) | label_between (no Widget at all - non-
//         focusable) | checkbox (kCheckbox) | slider (kSlider)
//   row2: textfield (kTextField) | reversed (kButton, tab_index=1) |
//         inert (kButton, tab_index=-1 - focusable by click, never a Tab
//         stop)
//
// Default Tab order: [reversed, btn_open, checkbox, slider, textfield] -
// `reversed`'s positive tab_index sorts it in front of the whole
// tab_index-unset group despite being the LAST child added; `inert` never
// appears in it at all.

#pragma once

#include <optional>
#include <string>

#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/font_catalog.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/theme/theme.h"
#include "drawgui/widget/focus.h"
#include "drawgui/widget/interaction.h"
#include "drawgui/widget/widget_set.h"
#include "drawgui/window/window_manager.h"

namespace focus_scene {

struct Handles {
  dg::NodeId body;
  dg::NodeId btn_open;
  dg::NodeId label_between;
  dg::NodeId checkbox;
  dg::NodeId slider;
  dg::NodeId textfield;
  dg::NodeId reversed;
  dg::NodeId inert;
};

struct Options {
  dg::TreeSpec spec;
  std::string font_dir = "/usr/share/fonts";
};

struct Scene {
  dg::LayoutTree tree;
  dg::WidgetSet widgets;
  dg::Interaction interaction;
  dg::Focus focus;
  dg::FocusRing ring;
  Handles handles;
  std::optional<dg::FontCatalog> fonts;
  dg::Theme theme;
};

Scene build(const Options& options);

[[nodiscard]] std::string describe(const Scene& scene, dg::NodeId id);

// Applies a NEW focus target: updates dg::Focus, the ring (always moved to
// scene.focus.current(), whatever this call left it as), and the
// kTextField side effects (design.md section 5.2's own event-routing
// context; doc/focus.md section 6) - start/stop the platform's text-input
// mechanism on gain/loss, and clear the platform's own IME state on loss
// alongside the model-level cancellation dg::WidgetSet::text_field_set_
// focus(false) already performs internally. A no-op (aside from the ring
// staying where it was) when `target` is already the current focus.
void apply_focus_change(Scene& scene, dg::WindowManager& manager, dg::WindowId window,
                        std::optional<dg::NodeId> target);

// Tab/Shift-Tab, routed the same way: focus_next()/focus_previous() over
// the CURRENT scope (none, while no popup is open), then the identical
// side-effect application apply_focus_change() performs for a click.
void tab(Scene& scene, dg::WindowManager& manager, dg::WindowId window, bool backwards);

// A left-button down: resolves the click through widgets.widget_at(), then
// applies it as a focus change if the resolved widget is_focusable() (or
// blurs everything when it resolved to nothing, or to a non-focusable
// widget like label_between) - design.md's own "clicking a focusable widget
// focuses it; clicking anything else blurs it" rule, restated by 4-9's
// original focus.h and unchanged here. Also drives dg::Interaction's own
// hover/press bookkeeping and the checkbox/slider's ordinary click/drag
// behaviour, exactly like every prior widget demo's own dispatch().
//
// Returns true when the click landed on `handles.btn_open` specifically -
// the caller's cue to open the popup, kept out of this function because
// PopupHost belongs to the window layer, not the scene.
bool dispatch_pointer(Scene& scene, dg::WindowManager& manager, dg::WindowId window,
                      const dg::PointerEvent& event);

// Left/Right/Home/End/Backspace/Delete route to the focused kTextField
// exactly like examples/12_text_input's own handle_key(); Tab/Shift-Tab
// (KeyEvent::shift) route to tab() above instead of to any widget.
void dispatch_key(Scene& scene, dg::WindowManager& manager, dg::WindowId window,
                  const dg::KeyEvent& event);

}  // namespace focus_scene
