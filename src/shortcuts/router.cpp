#include "drawgui/shortcuts/router.h"

namespace dg {
namespace {

// design.md section 5.5.2's "文本编辑按键不是快捷键": whether `key` is one of
// the editing intents window_manager.h's own Key enum comment lists as
// belonging to a focused kTextField (Left/Right/Home/End/Backspace/
// Delete) - the exact set every existing TextField key-handling call site
// (examples/12_text_input's Runner::handle_key) already dispatches on
// directly, restated here as a predicate so route_key_event() can bypass
// the router for it rather than duplicating that switch a second time.
[[nodiscard]] bool is_text_editing_intent(WidgetKind kind, Key key) {
  if (kind != WidgetKind::kTextField) {
    return false;
  }
  switch (key) {
    case Key::kLeft:
    case Key::kRight:
    case Key::kHome:
    case Key::kEnd:
    case Key::kBackspace:
    case Key::kDelete:
      return true;
    case Key::kOther:
    case Key::kEscape:
    case Key::kTab:
    case Key::kUp:
    case Key::kDown:
    case Key::kEnter:
      return false;
  }
  return false;
}

}  // namespace

std::optional<ResolvedAction> resolve_action(Chord chord, WindowId window,
                                             std::optional<NodeId> focused,
                                             const RenderTree& tree, const ActionScopes& scopes,
                                             std::span<const ShortcutBinding> app_table) {
  // Level 2: focused -> ancestors -> root, nearest match wins. RenderTree::
  // parent()'s own comment is what lets this stop on `id == parent(id)`
  // with no separate root check.
  if (focused.has_value()) {
    NodeId node = *focused;
    while (true) {
      for (const ShortcutBinding& binding : app_table) {
        if (binding.chord == chord && scopes.consumes(node, binding.action_id)) {
          return ResolvedAction{binding.action_id, node, window};
        }
      }
      const NodeId parent = tree.parent(node);
      if (parent == node) {
        break;
      }
      node = parent;
    }
  }

  // Level 3 (a window-level table: a window menu, a dialog's default
  // button) is declined for this slice, by name - see this file's own
  // header comment. There is no ActionScope::kWindow to look up yet.

  // Level 4: the app-wide table, no target node.
  for (const ShortcutBinding& binding : app_table) {
    if (binding.chord == chord && binding.scope == ActionScope::kApp) {
      return ResolvedAction{binding.action_id, std::nullopt, window};
    }
  }
  return std::nullopt;
}

KeyRouteResult route_key_event(const KeyEvent& event, WindowId window,
                               std::optional<NodeId> focused, const RoutingContext& ctx) {
  // Level 1, checked FIRST, with an early return before resolve_action() is
  // named anywhere below - see this file's header comment for why that
  // ordering, not a flag resolve_action() itself consults, is what makes
  // the router structurally unreachable while composing. 8-3b's own
  // defect-injection test proves this by deleting exactly this early
  // return and observing the router get reached.
  if (focused.has_value() && ctx.widgets.text_field_is_composing(*focused)) {
    return KeyRouteResult{KeyRouteOutcome::kIme, std::nullopt};
  }

  // The text-editing-key bypass: also an early return before
  // resolve_action() is reached, and also unconditional on `event.action`
  // - a kUp KeyEvent for one of these keys is exactly as much "not a
  // shortcut" as a kDown one.
  if (focused.has_value() && ctx.widgets.has(*focused) &&
      is_text_editing_intent(ctx.widgets.at(*focused).kind, event.key)) {
    return KeyRouteResult{KeyRouteOutcome::kEditingIntent, std::nullopt};
  }

  const Chord chord{event.mods, event.logical_key};
  KeyRouteResult result;
  result.outcome = KeyRouteOutcome::kRouted;
  result.action = resolve_action(chord, window, focused, ctx.tree, ctx.scopes, ctx.app_table);
  return result;
}

}  // namespace dg
