// Keyboard focus: every focus change's editor side effects and its
// FocusChangedEvent, and the DG_PROP_DISABLED follow-ups that can blur.

#include <optional>

#include "app/components/components.h"
#include "app/components/editors.h"
#include "app/internal.h"

namespace dg::app_detail {

// Focus/blur side effects for either text editor: the display mode, the
// border colour, and (text area) the IME's caret rectangle.
void set_editor_focus(WinState& window, dg::NodeId id, bool focused) {
  AppState& app = window.app;
  if (!app.fonts.has_value() || !is_text_editor(window, id)) {
    return;
  }
  dg::RenderTree& tree = window.tree.render();
  if (is_text_area(window, id)) {
    window.widgets.text_area_set_focus(tree, *app.fonts, id, focused);
    if (focused) {
      follow_caret(window, id);
    }
  } else {
    window.widgets.text_field_set_focus(tree, *app.fonts, id, focused);
  }
  set_text_field_box_style(editor_palette_for(app), tree, id, focused);
}

void follow_caret(WinState& window, dg::NodeId area) {
  const std::optional<dg::PixelRect> caret =
      window.widgets.text_area_caret_rect(window.tree.render(), area);
  if (caret.has_value()) {
    window.app.wm.start_text_input(window.sdl_window, *caret);
  }
}

// Every focus change ends here: the editors' focus/blur side effects, then
// one FocusChangedEvent naming the newly focused node (none = blurred).
void apply_focus_change(WinState& window, const dg::FocusChange& change) {
  if (change.blurred.has_value()) {
    set_editor_focus(window, *change.blurred, false);
  }
  if (change.focused.has_value()) {
    set_editor_focus(window, *change.focused, true);
  }
  if (!change.any()) {
    return;
  }
  window.reported_focus = window.focus.current();
  // A component's composed node (a tab, a tree) reports the component.
  emit(window.app,
       dg::events::FocusChangedEvent{
           window.sdl_window,
           change.focused.has_value() ? nearest_node(window, *change.focused) : std::nullopt});
}

// After a DG_PROP_DISABLED write on `node`: cancel a press/hover the write
// just disabled (no click fires), blur a focus it just disabled, and repaint
// every widget in the subtree, since the bit inherits.
void sync_disabled(WinState& window, dg::NodeId node) {
  dg::RenderTree& tree = window.tree.render();
  const dg::InteractionChange change = window.interaction.cancel_disabled(tree);
  apply_focus_change(window, window.focus.blur_if_disabled(tree));
  const std::optional<dg::NodeId> touched[] = {change.left, change.released};
  for (const std::optional<dg::NodeId>& id : touched) {
    if (id.has_value()) {
      window.widgets.refresh(tree, *id, window.interaction.state_of(*id));
    }
  }
  window.widgets.refresh_subtree(tree, node, window.interaction);
}

void report_focus_drift(WinState& window) {
  if (window.dialog.is_open() || window.menu.is_open() ||
      components::any_dropdown_open(window)) {
    return;  // popups move focus inside themselves; it comes back on close
  }
  const std::optional<dg::NodeId> current = window.focus.current();
  if (current == window.reported_focus) {
    return;
  }
  dg::FocusChange change;
  change.blurred = window.reported_focus;
  change.focused = current;
  apply_focus_change(window, change);
}

}  // namespace dg::app_detail

namespace dg {

Status Window::focus(NodeId node) {
  app_detail::WinState& window = *state_;
  const app_detail::NodeRecord* record = app_detail::record_of(window, node);
  if (record == nullptr) {
    return Status::kInvalidNode;
  }
  const dg::NodeId id = app_detail::components::focus_target(*record);
  if (!window.widgets.has(id) || !dg::is_focusable(window.widgets.at(id).kind) ||
      dg::is_disabled(window.tree.render(), id)) {
    return Status::kUnsupported;
  }
  app_detail::apply_focus_change(window, window.focus.set(id));
  return Status::kOk;
}

void Window::clear_focus() {
  app_detail::apply_focus_change(*state_, state_->focus.set(std::nullopt));
}

std::optional<NodeId> Window::focused() const {
  return state_->focus.current();
}

}  // namespace dg
