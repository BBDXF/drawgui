// The host window's own scene for examples/23_menu_tooltip_dialog: five
// kButtons in one row - `before`, `menu_target` (right-click opens a context
// menu), `hover_target` (a sustained hover opens a tooltip), `open_dialog`
// (opens a modal Dialog), `after` - the same "real neighbours on both sides"
// shape examples/21_focus/examples/22_dropdown_menu already established, so
// a Tab-order or button-identity regression is visible against real
// siblings rather than against nothing.

#pragma once

#include <optional>
#include <string>

#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/font_catalog.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/theme/theme.h"
#include "drawgui/widget/focus.h"
#include "drawgui/widget/interaction.h"
#include "drawgui/widget/tooltip.h"
#include "drawgui/widget/widget_set.h"
#include "drawgui/window/window_manager.h"

namespace menu_scene {

struct Handles {
  dg::NodeId body;
  dg::NodeId before;
  dg::NodeId menu_target;
  dg::NodeId hover_target;
  dg::NodeId open_dialog;
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
  dg::HoverTimer hover_timer;
  Handles handles;
  std::optional<dg::FontCatalog> fonts;
  dg::FontId ui_font;
  dg::Theme theme;
};

Scene build(const Options& options);

[[nodiscard]] std::string describe(const Scene& scene, dg::NodeId id);

void apply_focus_change(Scene& scene, std::optional<dg::NodeId> target);
void tab(Scene& scene, bool backwards);

// A PRIMARY-button down/up: ordinary dg::Interaction hover/press bookkeeping
// plus a focus change on kDown - exactly examples/22_dropdown_menu's own
// dispatch_pointer(). SECONDARY-button events are NOT this function's job
// at all: doc/menus.md section 6.1's own routing decision is that a
// right-click is a PARALLEL, non-activating channel, so the window driver
// (menu_window.cpp) intercepts PointerButton::kSecondary BEFORE this
// function ever sees it, and this function's own switch over PointerAction
// is completely unaware button identity exists - a left-click-only view of
// the world, byte-for-byte what examples/22_dropdown_menu's own
// dispatch_pointer() already is.
void dispatch_pointer(Scene& scene, const dg::PointerEvent& event);

void dispatch_key(Scene& scene, const dg::KeyEvent& event);

}  // namespace menu_scene
