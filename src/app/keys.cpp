// Key events: components first, then Tab traversal, the editors' history,
// clipboard and editing keys, then design.md section 5.5.2's four-level
// router (with the keyboard-scrolling actions applied) - every routed
// action becomes an ActionEvent.

#include <optional>

#include "app/components/components.h"
#include "app/internal.h"
#include "drawgui/anim/clock.h"
#include "drawgui/shortcuts/chord.h"
#include "drawgui/shortcuts/clipboard_actions.h"
#include "drawgui/shortcuts/keyboard_scroll.h"
#include "drawgui/shortcuts/router.h"

namespace dg::app_detail {
namespace {

void text_changed(WinState& window, dg::NodeId editor) {
  emit(window.app, dg::events::TextChangedEvent{window.sdl_window, exact_node(window, editor)});
}

// P6 sub-step a2 (second half): dispatches Left/Right/Home/End/Backspace/
// Delete on a focused NodeKind::kTextField DIRECTLY to WidgetSet's own
// text_field_move()/backspace()/delete_forward() - the SAME switch
// examples/12_text_input/text_field_window.cpp's own Runner::handle_key()
// already runs, copied rather than routed through design.md section
// 5.5.2's four-level router: ActionScope::kTextField only ever carries the
// four clipboard actions (dg::clipboard_actions.h's own header comment -
// copy/cut/paste/select_all), never a caret move or a delete, so there is
// no ActionId for route_key_event() to resolve one of these keys to.
// Returns whether `field`'s COMMITTED TEXT changed (Backspace/Delete only -
// a plain move never touches Widget::edit.text), which is what tells the caller
// whether to queue a TextChangedEvent.
// kTextArea's counterpart of handle_text_field_key(): its larger key set
// (router.cpp's is_text_editing_intent() names the same one). Returns
// whether the committed text changed; `consumed` reports whether the key
// was one of the area's own (everything else continues to the router).
bool handle_text_area_key(WinState& window, const dg::FontCatalog& fonts, dg::NodeId area,
                          const dg::KeyEvent& event, bool& consumed) {
  dg::RenderTree& tree = window.tree.render();
  const bool shift = dg::has(event.mods, dg::Modifier::kShift);
  const bool mod = dg::has(event.mods, dg::Modifier::kMod);
  const auto move = [&](dg::TextAreaMove where) {
    window.widgets.text_area_move(tree, fonts, area, where, shift);
    return false;
  };
  consumed = true;
  switch (event.key) {
    case dg::Key::kLeft:
      return move(mod ? dg::TextAreaMove::kWordLeft : dg::TextAreaMove::kCharLeft);
    case dg::Key::kRight:
      return move(mod ? dg::TextAreaMove::kWordRight : dg::TextAreaMove::kCharRight);
    case dg::Key::kUp:
      return move(dg::TextAreaMove::kLineUp);
    case dg::Key::kDown:
      return move(dg::TextAreaMove::kLineDown);
    case dg::Key::kPageUp:
      return move(dg::TextAreaMove::kPageUp);
    case dg::Key::kPageDown:
      return move(dg::TextAreaMove::kPageDown);
    case dg::Key::kHome:
      return move(mod ? dg::TextAreaMove::kDocStart : dg::TextAreaMove::kLineStart);
    case dg::Key::kEnd:
      return move(mod ? dg::TextAreaMove::kDocEnd : dg::TextAreaMove::kLineEnd);
    case dg::Key::kBackspace:
      return mod ? window.widgets.editor_delete_word(tree, fonts, area, true)
                 : window.widgets.text_area_backspace(tree, fonts, area);
    case dg::Key::kDelete:
      return mod ? window.widgets.editor_delete_word(tree, fonts, area, false)
                 : window.widgets.text_area_delete_forward(tree, fonts, area);
    case dg::Key::kEnter:
      if (!mod && !dg::has(event.mods, dg::Modifier::kAlt)) {
        return window.widgets.text_area_insert(tree, fonts, area, "\n");
      }
      break;
    case dg::Key::kEscape:
    case dg::Key::kTab:
    case dg::Key::kOther:
      break;
  }
  consumed = false;
  return false;
}

// Undo/redo (input/shortcuts.toml's undo/redo/redo_alt chords) on a focused,
// enabled editor, applied here before the router like the editing keys
// above - the host does not have to scope or handle them. Returns whether
// the key was one of those chords (consumed even when nothing was undone).
bool apply_editor_history_key(WinState& window, dg::NodeId editor, const dg::KeyEvent& event) {
  AppState& app = window.app;
  if (!app.fonts.has_value() || !is_text_editor(window, editor) ||
      dg::is_disabled(window.tree.render(), editor)) {
    return false;
  }
  const dg::Chord chord{event.mods, event.logical_key};
  for (const dg::ShortcutBinding& binding : dg::all_shortcut_bindings()) {
    if (binding.chord != chord) {
      continue;
    }
    bool changed = false;
    if (binding.action_id == DG_ACTION_UNDO) {
      changed = window.widgets.editor_undo(window.tree.render(), *app.fonts, editor);
    } else if (binding.action_id == DG_ACTION_REDO || binding.action_id == DG_ACTION_REDO_ALT) {
      changed = window.widgets.editor_redo(window.tree.render(), *app.fonts, editor);
    } else {
      continue;
    }
    if (changed) {
      text_changed(window, editor);
    }
    if (is_text_area(window, editor)) {
      follow_caret(window, editor);
    }
    return true;
  }
  return false;
}

// Copy/cut/paste/select-all on a focused, enabled editor, applied here like
// the history chords (the host has no API to perform them). Reuses
// dg::apply_clipboard_action. Returns whether the key was one of those chords.
bool apply_editor_clipboard_key(WinState& window, dg::NodeId editor,
                                const dg::KeyEvent& event) {
  AppState& app = window.app;
  if (!app.fonts.has_value() || !is_text_editor(window, editor) ||
      dg::is_disabled(window.tree.render(), editor)) {
    return false;
  }
  const dg::TextFieldEditContext ctx{window.tree.render(), *app.fonts, window.widgets, app.wm};
  bool changed = false;
  if (!dg::apply_clipboard_key(event, editor, ctx, &changed)) {
    return false;
  }
  if (changed) {
    text_changed(window, editor);
  }
  if (is_text_area(window, editor)) {
    follow_caret(window, editor);
  }
  return true;
}

bool handle_text_field_key(WinState& window, const dg::FontCatalog& fonts, dg::NodeId field,
                           const dg::KeyEvent& event) {
  dg::RenderTree& tree = window.tree.render();
  const bool shift = dg::has(event.mods, dg::Modifier::kShift);
  const bool mod = dg::has(event.mods, dg::Modifier::kMod);
  switch (event.key) {
    case dg::Key::kLeft:
      window.widgets.text_field_move(
          tree, fonts, field, mod ? dg::TextFieldMove::kWordLeft : dg::TextFieldMove::kCharLeft,
          shift);
      return false;
    case dg::Key::kRight:
      window.widgets.text_field_move(
          tree, fonts, field,
          mod ? dg::TextFieldMove::kWordRight : dg::TextFieldMove::kCharRight, shift);
      return false;
    case dg::Key::kHome:
      window.widgets.text_field_move(tree, fonts, field, dg::TextFieldMove::kLineStart, shift);
      return false;
    case dg::Key::kEnd:
      window.widgets.text_field_move(tree, fonts, field, dg::TextFieldMove::kLineEnd, shift);
      return false;
    case dg::Key::kBackspace:
      return mod ? window.widgets.editor_delete_word(tree, fonts, field, true)
                 : window.widgets.text_field_backspace(tree, fonts, field);
    case dg::Key::kDelete:
      return mod ? window.widgets.editor_delete_word(tree, fonts, field, false)
                 : window.widgets.text_field_delete_forward(tree, fonts, field);
    case dg::Key::kEscape:
    case dg::Key::kTab:
    case dg::Key::kUp:
    case dg::Key::kDown:
    case dg::Key::kEnter:
    case dg::Key::kPageUp:
    case dg::Key::kPageDown:
    case dg::Key::kOther:
      return false;
  }
  return false;
}

// ABI 0.2: Tab / Shift+Tab move focus through the focusable widgets in
// tree order (DG_PROP_TAB_INDEX overrides it), before any editor sees the
// key - Tab is focus traversal in a text area too.
bool handle_tab(WinState& window, const dg::KeyEvent& event) {
  if (event.key != dg::Key::kTab || dg::has(event.mods, dg::Modifier::kMod) ||
      dg::has(event.mods, dg::Modifier::kAlt)) {
    return false;
  }
  dg::RenderTree& tree = window.tree.render();
  const dg::FocusChange change =
      dg::has(event.mods, dg::Modifier::kShift)
          ? window.focus.focus_previous(tree, window.widgets, dg::LayoutTree::root())
          : window.focus.focus_next(tree, window.widgets, dg::LayoutTree::root());
  apply_focus_change(window, change);
  return true;
}

}  // namespace

// 8-3d: design.md section 5.5.2's four-level router, driven by a REAL
// dg::KeyEvent the runtime's own SDL pump produced (or, for the C ABI's
// dg_debug_post_key() test injection, a real SDL key event posted through
// the real queue exactly like dg_debug_post_pointer_button() does for
// clicks) - the runtime's own second caller of dg::route_key_event(),
// after examples/10_scrolling's C++ one (8-3c). Only KeyAction::kDown
// reaches resolve_action(): route_key_event() itself is blind to
// event.action (a chord fires identically whichever way this file calls
// it), so gating here, once, is what keeps a single key press from firing
// its resolved action twice - on press AND on release.
void process_key_event(WinState& window, const dg::KeyEvent& event) {
  AppState& app = window.app;
  if (components::route_key(window, event)) {
    return;
  }
  if (event.action != dg::KeyAction::kDown) {
    return;
  }
  // P6 sub-step a2 (second half): a focused text field's own editing keys
  // are consumed HERE, before the router runs at all - see
  // handle_text_field_key()'s own header comment for why these six keys
  // have no ActionId to route to in the first place. Consuming them first
  // also sidesteps a real ambiguity the router alone cannot resolve: Home/
  // End are ALSO the app-scope keyboard-scrolling chord (keyboard_scroll.h)
  // - a text field's own caret move must win while it is focused, matching
  // every desktop toolkit's "a focused text field owns Home/End, a
  // scrollable region only gets them when nothing more specific does".
  const std::optional<dg::NodeId> focused = window.focus.current();
  window.widgets.set_edit_time(dg::steady_anim_time());
  // A key press between two clicks ends a multi-click sequence: the second
  // click places a caret instead of selecting a word (effective_clicks()).
  window.key_since_press = true;
  if (handle_tab(window, event)) {
    return;
  }
  if (focused.has_value() && apply_editor_history_key(window, *focused, event)) {
    return;
  }
  if (focused.has_value() && apply_editor_clipboard_key(window, *focused, event)) {
    return;
  }
  if (focused.has_value() && is_text_area(window, *focused) && app.fonts.has_value() &&
      !dg::is_disabled(window.tree.render(), *focused)) {
    bool consumed = false;
    const bool text_changed_now =
        handle_text_area_key(window, *app.fonts, *focused, event, consumed);
    if (text_changed_now) {
      text_changed(window, *focused);
    }
    if (consumed) {
      follow_caret(window, *focused);
      return;
    }
  }
  if (focused.has_value() && window.widgets.has(*focused) &&
      window.widgets.at(*focused).kind == dg::WidgetKind::kTextField && app.fonts.has_value()) {
    const bool text_changed_now = handle_text_field_key(window, *app.fonts, *focused, event);
    if (text_changed_now) {
      text_changed(window, *focused);
    }
    if (event.key != dg::Key::kOther) {
      // One of the six editing keys handle_text_field_key() switches on
      // (kOther is everything else - a plain character key SDL also
      // reports as a KeyEvent beside its own TextInputEvent, which must
      // still reach the router for e.g. Mod+C).
      return;
    }
  }
  const dg::RoutingContext ctx{window.tree.render(), window.widgets, window.action_scopes,
                               dg::all_shortcut_bindings()};
  const dg::KeyRouteResult routed =
      dg::route_key_event(event, window.sdl_window, window.focus.current(), ctx);
  if (routed.outcome != dg::KeyRouteOutcome::kRouted || !routed.action.has_value()) {
    return;
  }
  const dg::ResolvedAction& action = *routed.action;
  // ABI 0.2: the keyboard-scrolling actions scroll the focused node's
  // scroll view (a no-op when there is none); still reported below.
  if (action.action_id == DG_ACTION_SCROLL_PAGE_UP ||
      action.action_id == DG_ACTION_SCROLL_PAGE_DOWN ||
      action.action_id == DG_ACTION_SCROLL_TO_START ||
      action.action_id == DG_ACTION_SCROLL_TO_END) {
    (void)dg::apply_keyboard_scroll(action.action_id, window.focus.current(), window.tree,
                                    window.widgets);
  }
  emit(app, dg::events::ActionEvent{
                window.sdl_window,
                action.target.has_value() ? exact_node(window, *action.target) : std::nullopt,
                static_cast<std::uint16_t>(action.action_id)});
}

}  // namespace dg::app_detail
