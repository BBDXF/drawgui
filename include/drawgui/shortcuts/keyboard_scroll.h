// Keyboard scrolling (8-3c): applying one of the four already-generated
// scroll_page_up/scroll_page_down/scroll_to_start/scroll_to_end actions
// (input/shortcuts.toml ids 5-8) once route_key_event() (router.h) has
// resolved a chord to one of them. This closes doc/scrolling.md's own named
// decline ("design.md routes PageUp/Down/Home/End through the intent
// mechanism... 5.5.1 does not exist" - it does now, as the router).
//
// THE TARGET IS THE NEAREST SCROLLABLE ANCESTOR OF THE FOCUSED NODE, NOT
// `ResolvedAction::target`: these four actions bind at ActionScope::kApp
// (input/shortcuts.toml: no ScrollView registers a component-level scope),
// so `ResolvedAction::target` is always std::nullopt for them - the field
// that DOES carry which node this feature scrolls is the `focused` NodeId
// route_key_event() itself was called with, exactly the same value
// resolve_action() bubbled from. Re-deriving `WidgetSet::scrollable_owner_of
// ()` from it here rather than threading a target through ActionScopes is
// what keeps the router itself ignorant of what a "scrollable ancestor"
// even is - resolve_action() answers "which action", never "which node
// consumes it downstream".
//
// WHY THE FOCUSED NODE, NOT THE POINTER: `dg::is_focusable()`
// (widget/focus.h) returns false for `kScrollView` - a scroll viewport can
// never itself hold focus - so "scroll whatever is focused" cannot mean
// "scroll the focused viewport" the way wheel scrolling means "scroll the
// viewport under the pointer" (`WidgetSet::scrollable_owner_of()`, called
// from the hit-tested node, already built by 4-7). The only coherent
// meaning left is this one: climb from whatever non-scrollable widget DOES
// hold focus (a button, a text field, a panel) to its nearest scrollable
// ancestor - the identical climb wheel scrolling already uses, started from
// a different node. No new mechanism, only a different starting point.
//
// VERTICAL AXIS ONLY, AND NOT BY A SPECIAL CASE: PageUp/PageDown/Home/End
// are this project's (and every desktop toolkit's) vertical-scroll
// convention, and input/shortcuts.toml defines exactly these four actions,
// none for a horizontal equivalent - a horizontal binding has no
// `consumer` naming a slice that wants it, so none was added. This function
// always passes `dx = 0`; `WidgetSet::scroll_by()`'s own axis gate (its own
// header comment: "Only the axis `scroll_axis` names moves; the other
// delta is ignored") is what makes a horizontal-only scrollable ancestor a
// silent no-op - the identical mechanism a vertical ancestor is scrolled
// by, not a second code path built to decline the horizontal case.
//
// A LEGITIMATE NO-OP, NOT AN ERROR: returns false, changing nothing, when
// nothing is focused, the focused node has no scrollable ancestor, or the
// resolved ancestor is already at scroll_by()'s own clamp (or is
// horizontal-only, per the paragraph above). None of these invents a
// fallback target ("scroll the only/first scroll view in the tree") -
// design.md names no such rule, and doc/scrolling.md's own standing
// argument against inventing a caller that does not exist applies here
// exactly as it did to nested-scroll delegation.

#pragma once

#include <optional>

#include "drawgui/layout/layout_tree.h"
#include "drawgui/shortcuts/action_ids.generated.h"
#include "drawgui/widget/widget_set.h"

namespace dg {

// `action_id` is expected to be one of DG_ACTION_SCROLL_PAGE_UP/_DOWN/
// DG_ACTION_SCROLL_TO_START/_END; any other value returns false without
// touching `layout` - this function's own job is applying a keyboard-
// scrolling action, not validating that route_key_event() resolved one, so
// an unrelated action_id is the caller's mistake to have routed here, not
// this function's to diagnose.
[[nodiscard]] bool apply_keyboard_scroll(dg_action_id action_id, std::optional<NodeId> focused,
                                         LayoutTree& layout, const WidgetSet& widgets);

}  // namespace dg
