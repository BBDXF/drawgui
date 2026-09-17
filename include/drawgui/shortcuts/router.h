// The four-level shortcut router (design.md section 5.5.2), and the single
// entry point that makes level 1 (IME isolation) and the text-editing-key
// bypass structural rather than a checkable flag somewhere inside it.
//
// design.md section 5.5.2, quoted in full because every clause is load-
// bearing:
//
//   1. IME composing?              -> the IME owns every key; never routed
//   2. focused node, up the ancestor chain (component level: a focused
//      TextField's own "select all")
//   3. window-level table            (a window menu, a dialog's default button)
//   4. app-level table                (global menu accelerators)
//
// LEVEL 3 IS DECLINED FOR THIS SLICE, BY NAME: `ActionScope` (action_scope.
// generated.h) has exactly two values, kApp and kTextField, because
// input/shortcuts.toml has no action that needs a window scope yet -
// design.md's own examples for level 3 (a window menu, a dialog's default
// button) have no real consumer anywhere in this repo, and this project's
// standing rule is that an interface is extracted from a working
// implementation, never written ahead of one (the same rule window_manager.
// h's own top comment states for a second platform backend). The
// PREREQUISITE that would unblock it: a window-scoped action - a window
// menu or a dialog's default-button binding - registering a THIRD
// ActionScope enumerator the generator would then emit. Levels 2 and 4 are
// built for real below; adding level 3 later is a new branch inside
// resolve_action() (a window-scope lookup between the two that exist), not
// a rewrite of either.
//
// LEVEL 2 IS "NEAREST NODE WINS", WITH NO SPECIAL CASE: resolve_action()
// walks `focused` up RenderTree::parent() to the root, and the FIRST node
// on that walk that ActionScopes::consumes() the chord's action wins. This
// is what makes a focused TextField's own "select all" (ActionScope::
// kTextField, scoped onto the field's own NodeId) beat an app menu's
// binding for the identical chord - the walk simply reaches the field
// before it ever reaches whatever scoped the app-level fallback, if
// anything ever does; design.md's own words are "冒泡优先天然给出这个结果，
// 无需特例" ("bubbling priority naturally gives this result, no special
// case needed").
//
// LEVEL 1 (IME) AND THE TEXT-EDITING-KEY BYPASS ARE NOT THE SAME MECHANISM,
// EVEN THOUGH BOTH KEEP A KEY OUT OF resolve_action(): design.md section
// 5.5.2 states them as two separate sentences for a reason.
//
//   - "文本编辑按键不是快捷键" (a text-editing key is not a shortcut): when
//     the FOCUSED WIDGET KIND consumes `KeyEvent::key` directly as an
//     editing intent (Left/Right/Home/End/Backspace/Delete on a
//     kTextField - window_manager.h's own Key enum comment lists exactly
//     these as "the editing intents... belonging to a text field"), the
//     event is handled there and NEVER built into a Chord or passed to
//     resolve_action() at all. This is NOT level 2 outranking level 4 for
//     these keys - the shortcut table is never consulted for them, which is
//     the only way `scroll_to_start` (app scope, bound to bare `Home`) and
//     a TextField's own line-start (also `Home`, no shortcut binding at
//     all) can coexist with no special case: a naive "level 2 finds nothing
//     for this focused node, fall through to level 4" reading would fire
//     `scroll_to_start` while a text field is focused, which is precisely
//     what this sentence forbids.
//   - IME composing is level 1, a HARD isolation: EVERY key, including
//     arrows/Enter/Escape that are ALSO editing intents outside
//     composition, belongs to the candidate window while `WidgetSet::
//     text_field_is_composing(focused)` is true - 8-3b's own defect-
//     injection test (tests/unit/test_shortcut_routing.cpp) proves this by
//     removing the early return below and showing the router IS reached.
//
// route_key_event() is what makes both of these STRUCTURAL: it is written
// so that the composing check is the very first statement, with an early
// return, before resolve_action() is even named in the function body - a
// caller that only ever dispatches a KeyEvent through this one entry point
// cannot reach resolve_action() while composing, because there is no path
// through this function's own text that gets there. This is weaker than a
// language-level guarantee (nothing stops a second caller from calling
// resolve_action() directly), but it is exactly the shape 8-3b's defect
// injection can prove and a future regression can be caught reintroducing:
// delete the early return, and the injection test in test_shortcut_routing.
// cpp fails.

#pragma once

#include <optional>
#include <span>

#include "drawgui/render/render_tree.h"
#include "drawgui/shortcuts/action_scopes.h"
#include "drawgui/shortcuts/chord.h"
#include "drawgui/widget/widget_set.h"
#include "drawgui/window/window_manager.h"

namespace dg {

// What one resolved chord fired: `target` is the node whose own
// ActionScopes entry matched (level 2), or `std::nullopt` for the app-level
// table (level 4) - level 3's own would-be target, a window, is not a
// NodeId at all, which is one more reason it is a new branch rather than a
// field this struct already has to carry.
struct ResolvedAction {
  dg_action_id action_id = DG_ACTION_INVALID;
  std::optional<NodeId> target;
  WindowId window;
};

// Levels 2 and 4 of design.md section 5.5.2, over already-parsed input:
// building the Chord (from a KeyEvent's mods/logical_key) and deciding
// whether this key is even eligible to reach here (level 1's IME isolation,
// the text-editing-key bypass) are route_key_event()'s job, not this one's
// - resolve_action() itself has no notion of "is this key an editing
// intent" and never will, which is what keeps it a plain, total function
// over a Chord rather than a second place the bypass rule could be
// half-implemented.
[[nodiscard]] std::optional<ResolvedAction> resolve_action(
    Chord chord, WindowId window, std::optional<NodeId> focused, const RenderTree& tree,
    const ActionScopes& scopes, std::span<const ShortcutBinding> app_table);

// The four read-only inputs every route_key_event() call needs together -
// grouped rather than passed as four more parameters alongside the event/
// window/focused already there, the same "related parameters become a
// typed value object" rule every other >3-argument call site in this
// project already follows.
struct RoutingContext {
  const RenderTree& tree;
  const WidgetSet& widgets;
  const ActionScopes& scopes;
  std::span<const ShortcutBinding> app_table;
};

// Which of the three structurally different things happened to one
// KeyEvent - never a bare bool, because "was an action resolved" and "did
// this event even reach the router" are different questions a caller (and
// 8-3b's own injection test) needs to tell apart.
enum class KeyRouteOutcome : std::uint8_t {
  // Level 1: `focused` was a composing text field. `action` is always
  // std::nullopt - resolve_action() was never called.
  kIme,

  // The text-editing-key bypass: `focused`'s WidgetKind consumes
  // event.key directly (window_manager.h's Key enum). `action` is always
  // std::nullopt - resolve_action() was never called.
  kEditingIntent,

  // Neither of the above: a Chord was built from event.mods/logical_key
  // and passed to resolve_action(). `action` holds its result, which is
  // itself std::nullopt for an unbound chord - that is a real answer
  // ("routed, nothing matched"), not the same as this event never having
  // reached the router at all.
  kRouted,
};

struct KeyRouteResult {
  KeyRouteOutcome outcome = KeyRouteOutcome::kRouted;
  std::optional<ResolvedAction> action;
};

// The single key-dispatch entry point design.md section 5.5.2's four
// levels are meant to be read through: checks level 1 first, with an early
// return that keeps resolve_action() unreached while composing (this
// file's own top comment argues why that is the whole of what "structural,
// not a checkable if" can mean for a free function), then the text-
// editing-key bypass, also an early return before resolve_action() is
// named, and only then builds a Chord and calls resolve_action() for
// levels 2 and 4.
[[nodiscard]] KeyRouteResult route_key_event(const KeyEvent& event, WindowId window,
                                             std::optional<NodeId> focused,
                                             const RoutingContext& ctx);

}  // namespace dg
