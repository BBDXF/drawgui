// The host window's own scene for examples/22_dropdown_menu: a `before`
// kButton, the kDropdown anchor, and an `after` kButton - shaped exactly
// like examples/21_focus's own mixed-widget row (a Tab sequence with real
// neighbours on both sides), so a Tab-order regression that skips or
// misplaces the dropdown is visible against real siblings rather than
// against nothing. Five distinguishable options - `doc/completeness.md`'s
// own "missing scene shape" failure mode named this by name: a dropdown
// test over identical options proves nothing, so every option here is its
// own word, none a substring or rotation of another, and the demo's own
// headless check (dropdown_check.cpp) selects the first, a middle, and the
// last position, plus wraparound in both keyboard directions.

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/font_catalog.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/theme/theme.h"
#include "drawgui/widget/focus.h"
#include "drawgui/widget/interaction.h"
#include "drawgui/widget/widget_set.h"
#include "drawgui/window/window_manager.h"

namespace dropdown_scene {

inline const std::vector<std::string> kOptions = {"Apple", "Banana", "Cherry", "Date",
                                                  "Elderberry"};

struct Handles {
  dg::NodeId body;
  dg::NodeId before;
  dg::NodeId dropdown;
  dg::NodeId after;
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
  dg::FontId ui_font;
  dg::Theme theme;
};

Scene build(const Options& options);

[[nodiscard]] std::string describe(const Scene& scene, dg::NodeId id);

// Same shape as examples/21_focus's own apply_focus_change()/tab(): moves
// dg::Focus and repositions the shared ring. Dropdown has no per-widget
// side effect (unlike kTextField's IME hooks), so this is thinner than
// focus_scene's own version.
void apply_focus_change(Scene& scene, std::optional<dg::NodeId> target);
void tab(Scene& scene, bool backwards);

// A left-button down/up: ordinary dg::Interaction hover/press bookkeeping
// plus a focus change on kDown, exactly like examples/21_focus's own
// dispatch_pointer(). Returns true when the click landed on
// `handles.dropdown` specifically - the caller's cue to open the popup,
// kept out of this function because PopupHost belongs to the window layer.
bool dispatch_pointer(Scene& scene, const dg::PointerEvent& event);

// Tab/Shift-Tab only (routed to tab() above) while no popup is open; the
// window driver intercepts Up/Down/Enter/Escape itself while one is.
void dispatch_key(Scene& scene, const dg::KeyEvent& event);

}  // namespace dropdown_scene
