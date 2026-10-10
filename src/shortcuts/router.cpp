#include "drawgui/shortcuts/router.h"

#include "drawgui/widget/interaction.h"

namespace dg {
namespace {

// design.md section 5.5.2's "文本编辑按键不是快捷键": whether `key` is one of
// the editing intents window_manager.h's own Key enum comment lists as
// belonging to a focused kTextField (Left/Right/Home/End/Backspace/
// Delete) - the exact set every existing TextField key-handling call site
// (examples/12_text_input's Runner::handle_key) already dispatches on
// directly, restated here as a predicate so route_key_event() can bypass
// the router for it rather than duplicating that switch a second time.
[[nodiscard]] bool is_text_editing_intent(WidgetKind kind, const KeyEvent& event) {
  if (kind == WidgetKind::kTextArea) {
    // The multi-line editor's own set: the single-line six plus vertical
    // movement and Enter. Enter is an editing intent only bare or with
    // Shift (both insert a newline); Ctrl/Alt/Mod+Enter still reach the
    // router so an app can bind e.g. Mod+Enter to "submit". Tab is never
    // here - it stays focus traversal.
    switch (event.key) {
      case Key::kLeft:
      case Key::kRight:
      case Key::kHome:
      case Key::kEnd:
      case Key::kBackspace:
      case Key::kDelete:
      case Key::kUp:
      case Key::kDown:
      case Key::kPageUp:
      case Key::kPageDown:
        return true;
      case Key::kEnter:
        return !has(event.mods, Modifier::kMod) && !has(event.mods, Modifier::kAlt);
      case Key::kOther:
      case Key::kEscape:
      case Key::kTab:
        return false;
    }
    return false;
  }
  if (kind != WidgetKind::kTextField) {
    return false;
  }
  // Deliberately blind to `event.mods`, and that is a real limitation worth
  // naming rather than discovering later: with a TextField focused, `Mod+Home`
  // is swallowed here exactly as bare `Home` is, so an app-scoped action bound
  // to `Mod+Home` would silently never fire while a field has focus.
  //
  // Nothing binds a modified form of these six keys today, so this costs
  // nothing yet, and the direction matches design.md section 5.5.1's own
  // framing - it lists `Ctrl+Backspace`/`Option+Delete` (delete-previous-word)
  // as belonging to a TextField's editing intents, NOT to the shortcut table.
  // The day a caller does want a modified chord over one of these keys, the
  // fix is to consult `mods` here, not to special-case it at the call site.
  switch (event.key) {
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
    case Key::kPageUp:
    case Key::kPageDown:
      return false;
  }
  return false;
}

// A key that types a character when pressed bare or with Shift.
[[nodiscard]] bool is_typing_key(LogicalKey key) {
  switch (key) {
    case LogicalKey::kSpace:
    case LogicalKey::kMinus:
    case LogicalKey::kEquals:
    case LogicalKey::kComma:
    case LogicalKey::kPeriod:
    case LogicalKey::kSlash:
      return true;
    default:
      break;
  }
  return logical_key_name(key).size() == 1;  // a letter or a digit
}

// Level 2 at one node: its user binding of `chord`, else a generated
// action it scopes.
[[nodiscard]] std::optional<dg_action_id> scoped_action(
    Chord chord, NodeId node, const ActionScopes& scopes,
    std::span<const ShortcutBinding> app_table, std::span<const UserShortcut> user) {
  for (const UserShortcut& binding : user) {
    if (binding.chord == chord && binding.scope == node) {
      return binding.action_id;
    }
  }
  for (const ShortcutBinding& binding : app_table) {
    if (binding.chord == chord && scopes.consumes(node, binding.action_id)) {
      return binding.action_id;
    }
  }
  return std::nullopt;
}

}  // namespace

std::optional<ResolvedAction> resolve_action(Chord chord, WindowId window,
                                             std::optional<NodeId> focused,
                                             const RenderTree& tree, const ActionScopes& scopes,
                                             std::span<const ShortcutBinding> app_table) {
  return resolve_action(chord, window, focused, tree, scopes, app_table, {});
}

std::optional<ResolvedAction> resolve_action(Chord chord, WindowId window,
                                             std::optional<NodeId> focused,
                                             const RenderTree& tree, const ActionScopes& scopes,
                                             std::span<const ShortcutBinding> app_table,
                                             std::span<const UserShortcut> user) {
  // Level 2: focused -> ancestors -> root, nearest match wins. RenderTree::
  // parent()'s own comment is what lets this stop on `id == parent(id)`
  // with no separate root check. A DISABLED focus (a caller that wrote
  // DG_PROP_DISABLED without Focus::blur_if_disabled()) resolves nothing
  // node-scoped: actions scoped to a disabled widget never fire.
  if (focused.has_value() && !is_disabled(tree, *focused)) {
    NodeId node = *focused;
    while (true) {
      if (const std::optional<dg_action_id> id =
              scoped_action(chord, node, scopes, app_table, user)) {
        return ResolvedAction{*id, node, window};
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
  for (const UserShortcut& binding : user) {
    if (binding.chord == chord && !binding.scope.has_value()) {
      return ResolvedAction{binding.action_id, std::nullopt, window};
    }
  }
  for (const ShortcutBinding& binding : app_table) {
    if (binding.chord == chord && binding.scope == ActionScope::kApp) {
      return ResolvedAction{binding.action_id, std::nullopt, window};
    }
  }
  return std::nullopt;
}

KeyRouteResult route_key_event(const KeyEvent& event, WindowId window,
                               std::optional<NodeId> focused, const RoutingContext& ctx) {
  return route_key_event(event, window, focused, ctx, {});
}

KeyRouteResult route_key_event(const KeyEvent& event, WindowId window,
                               std::optional<NodeId> focused, const RoutingContext& ctx,
                               std::span<const UserShortcut> user) {
  if (focused.has_value() && is_disabled(ctx.tree, *focused)) {
    focused.reset();
  }
  // Level 1, checked FIRST, with an early return before resolve_action() is
  // named anywhere below - see this file's header comment for why that
  // ordering, not a flag resolve_action() itself consults, is what makes
  // the router structurally unreachable while composing. 8-3b's own
  // defect-injection test proves this by deleting exactly this early
  // return and observing the router get reached.
  if (focused.has_value() && ctx.widgets.editor_is_composing(*focused)) {
    return KeyRouteResult{KeyRouteOutcome::kIme, std::nullopt};
  }

  // The text-editing-key bypass: also an early return before
  // resolve_action() is reached, and also unconditional on `event.action`
  // - a kUp KeyEvent for one of these keys is exactly as much "not a
  // shortcut" as a kDown one.
  if (focused.has_value() && ctx.widgets.has(*focused) &&
      is_text_editing_intent(ctx.widgets.at(*focused).kind, event)) {
    return KeyRouteResult{KeyRouteOutcome::kEditingIntent, std::nullopt};
  }

  const Chord chord{event.mods, event.logical_key};
  const bool typing = focused.has_value() && ctx.widgets.has(*focused) &&
                      (ctx.widgets.at(*focused).kind == WidgetKind::kTextField ||
                       ctx.widgets.at(*focused).kind == WidgetKind::kTextArea) &&
                      !has(event.mods, Modifier::kMod) && !has(event.mods, Modifier::kAlt) &&
                      is_typing_key(event.logical_key);
  KeyRouteResult result;
  result.outcome = KeyRouteOutcome::kRouted;
  result.action = resolve_action(chord, window, focused, ctx.tree, ctx.scopes, ctx.app_table,
                                 typing ? std::span<const UserShortcut>{} : user);
  return result;
}

}  // namespace dg
