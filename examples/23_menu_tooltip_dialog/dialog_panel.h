// The overlay-branch Dialog's own content: a full-window, non-interactive
// backdrop (a plain kPanel - WidgetSet::interactive() already excludes it
// from pointer input, the same exclusion kPanel has everywhere else in this
// project) plus a centred panel carrying one "Close" kButton.
//
// The backdrop is what makes a click OUTSIDE the panel hit NOTHING rather
// than falling through to whatever host widget is behind it: it is the
// TOPMOST node in paint order (appended after the host's own content,
// RenderTree's own paint-order-is-add-order rule, unchanged since 4-4), so
// RenderTree::hit_test() resolves a click anywhere in the window to the
// backdrop (or the panel/button on top of it) FIRST - and WidgetSet::
// owner_of()'s ancestor climb from a non-interactive backdrop never reaches
// a host sibling, because owner_of() climbs PARENTS, not siblings. This is
// the identical mechanism examples/22_dropdown_menu's own popup rows already
// rely on for their own background rectangle, applied here at the whole-
// window scale a MODAL dialog needs rather than a dropdown's own anchored
// rectangle.
//
// dg::Focus::set_guarded() (focus.h) is the SEPARATE mechanism that refuses
// a DIRECT, programmatic Focus target outside `panel_root` - this backdrop
// is what blocks an ordinary CLICK from ever resolving to one in the first
// place, and the two are deliberately not the same code path: a caller
// that calls scene.focus.set_guarded(tree, some_host_widget) directly,
// without going through hit testing at all, still gets refused.

#pragma once

#include "drawgui/render/render_tree.h"
#include "drawgui/widget/widget_set.h"

namespace dialog_panel {

struct Handles {
  dg::NodeId backdrop;
  dg::NodeId panel;
  dg::NodeId close_button;
};

// Builds the backdrop (covering `window_size`) and a centred panel
// `panel_size` wide/tall carrying one "Close" button, all appended directly
// to `tree` under `parent` (RenderTree::add_child(), never through a
// LayoutTree - PopupHost's own overlay-branch precedent, doc/popup.md
// section 3). Returns `panel` as the node a caller passes to
// dg::Focus::enter_scope(panel_root, /*modal=*/true) - Tab/click/set()
// confinement is scoped to the PANEL, not the backdrop, so the backdrop
// itself (never focusable - it carries no Widget at all) is correctly
// outside the scope and yet still unclickable, for the reason above.
Handles build(dg::RenderTree& tree, dg::WidgetSet& widgets, dg::NodeId parent,
              dg::PixelSize window_size, dg::PixelSize panel_size, dg::FontId font);

}  // namespace dialog_panel
