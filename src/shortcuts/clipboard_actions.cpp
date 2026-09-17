#include "drawgui/shortcuts/clipboard_actions.h"

#include <string>

namespace dg {

bool apply_clipboard_action(dg_action_id action_id, NodeId field,
                            const TextFieldEditContext& ctx) {
  if (action_id == DG_ACTION_SELECT_ALL) {
    return ctx.widgets.text_field_select_all(ctx.tree, ctx.fonts, field);
  }

  if (action_id == DG_ACTION_COPY || action_id == DG_ACTION_CUT) {
    const std::optional<TextSelection> selection = ctx.widgets.text_field_selection(field);
    // No active selection: a deliberate no-op, this file's own header
    // comment names why - the clipboard is left exactly as it was.
    if (!selection.has_value()) {
      return false;
    }
    const std::string& text = ctx.widgets.text_field_text(field);
    const std::string copied =
        text.substr(static_cast<std::size_t>(selection->start),
                    static_cast<std::size_t>(selection->end - selection->start));
    if (!ctx.manager.set_clipboard_text(copied)) {
      return false;
    }
    if (action_id == DG_ACTION_COPY) {
      return true;
    }
    // Cut: the selection is still active in the model (copying it never
    // touched the model), so text_field_insert() with an empty
    // replacement deletes it through the exact same path a Backspace over
    // a selection already uses - not a second, parallel delete.
    return ctx.widgets.text_field_insert(ctx.tree, ctx.fonts, field, "");
  }

  if (action_id == DG_ACTION_PASTE) {
    const std::string clipboard_text = ctx.manager.get_clipboard_text();
    return ctx.widgets.text_field_insert(ctx.tree, ctx.fonts, field, clipboard_text);
  }

  return false;
}

}  // namespace dg
