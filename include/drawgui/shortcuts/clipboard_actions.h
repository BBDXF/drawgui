// Clipboard copy/cut/paste, plus TextField's own select_all (8-4):
// applying one of the four already-generated DG_ACTION_COPY/CUT/PASTE/
// SELECT_ALL actions (input/shortcuts.toml ids 1-4) once route_key_event()
// (router.h) has resolved a chord to one of them - the router's THIRD
// consumer, after 8-3c's keyboard scrolling (keyboard_scroll.h), whose
// shape this file deliberately mirrors: a small free function taking the
// resolved action_id plus what it needs, called by the host after
// route_key_event() answers, never a second place that decides which
// action fired.
//
// ALL FOUR SHARE ActionScope::kTextField (input/shortcuts.toml's own
// grouping comment: "they only mean something with a focused text field"),
// which is why select_all lives here rather than in a file of its own -
// input/shortcuts.toml's own consumer field for select_all names both
// "8-4 clipboard" and "TextField" as this action's consumers, and this is
// where that pairing is realised: one function, one call site, over the
// one ActionScope every one of the four registers itself under.
//
// WHY A GROUPED CONTEXT, NOT FOUR PARAMETERS: `RenderTree&`/`const
// FontCatalog&`/`WidgetSet&`/`WindowManager&` are the four independent
// things every one of copy/cut/paste/select_all needs, and this project's
// own >3-parameter rule (the same one router.h's own RoutingContext
// answers for route_key_event()) is what TextFieldEditContext exists to
// satisfy - grouped once here rather than threaded as four more arguments.
//
// THE CLIPBOARD SEAM IS WindowManager::set_clipboard_text()/
// get_clipboard_text(), NOT A FREE FUNCTION (window_manager.h's own
// comment on those two methods): this file calls them exactly as it calls
// every other WidgetSet method, through a live WindowManager, never around
// one.
//
// COPY WITH NO ACTIVE SELECTION IS A NO-OP, DECIDED DELIBERATELY: the
// clipboard is left untouched and this function returns false, the
// identical "was this worth doing" signal WidgetSet's own scroll_by()/
// set_slider_value() already give a caller. The alternative this project
// declines BY NAME - copying the field's whole text when nothing is
// selected - is a real behaviour some toolkits have, but design.md names
// no such rule, and inventing one here would surprise a user who already
// has an explicit "select everything" action (select_all, this same
// table) to reach for. Cut shares this decision for the identical reason:
// no selection, nothing happens, the clipboard is not touched.
//
// PASTE IS text_field_insert() UNCHANGED (doc/text-input.md's own cross-
// reference, restated in drawgui-p0.md): it already deletes an active
// selection before inserting, so a selected paste-target is replaced with
// no separate primitive. It also already drops ASCII control characters
// (0x00-0x1F, 0x7F) - PRE-EXISTING behaviour this slice does not change,
// named here rather than left to be rediscovered: pasting clipboard text
// containing a newline into this single-line field silently loses it,
// correct for a single-line field, and only fixable by a multi-line text
// field this project does not have.

#pragma once

#include "drawgui/render/font_catalog.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/shortcuts/action_ids.generated.h"
#include "drawgui/widget/widget_set.h"
#include "drawgui/window/window_manager.h"

namespace dg {

// The four read-only-shaped inputs every apply_clipboard_action() call
// needs together - router.h's own RoutingContext precedent, applied here:
// related parameters become a typed value object rather than four more
// arguments alongside action_id/field.
struct TextFieldEditContext {
  RenderTree& tree;
  const FontCatalog& fonts;
  WidgetSet& widgets;
  WindowManager& manager;
};

// `action_id` is expected to be one of DG_ACTION_COPY/CUT/PASTE/
// SELECT_ALL; any other value returns false untouched - this function's
// own job is applying one of these four actions, not validating that
// route_key_event() resolved one, matching apply_keyboard_scroll()'s
// identical stance on an unrelated action_id.
//
// `field` is expected to be the kTextField the action was scoped onto -
// ResolvedAction::target for every one of these four, since all four bind
// at ActionScope::kTextField (never kApp, unlike keyboard scrolling's own
// four actions), so there is no ambiguity about which node to act on the
// way apply_keyboard_scroll() has to re-derive one from `focused`.
[[nodiscard]] bool apply_clipboard_action(dg_action_id action_id, NodeId field,
                                          const TextFieldEditContext& ctx);

}  // namespace dg
