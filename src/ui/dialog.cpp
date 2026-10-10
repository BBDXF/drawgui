#include "drawgui/ui/dialog.h"

#include <utility>

#include "drawgui/widget/widget_set.h"
#include "ui/ui_visuals.h"

namespace dg {

bool Dialog::open(UiContext& ctx, DialogSpec spec, std::optional<NodeId> restore_focus) {
  close(ctx);
  spec_ = std::move(spec);
  const PixelSize window = ctx.tree.viewport();
  // A full-window overlay: a zero-size anchor at the origin places it at
  // (0, 0). Escape is handled here (it reports the cancel button), and
  // nothing is "outside" a full-window popup, so neither PopupHost
  // dismissal applies.
  const Expected<PopupHandle, WindowError> shown =
      ctx.popups.show(ctx.window, ctx.tree, PixelRect{0, 0, 0, 0}, window,
                      PopupPlacement::kBelow, PopupFlags{false, false});
  if (!shown) {
    return false;
  }
  handle_ = shown.value();
  restore_focus_ = restore_focus;
  const ui_visuals::DialogNodes nodes =
      ui_visuals::build_dialog(ctx, handle_->content_root, window, spec_);
  panel_ = nodes.panel;
  body_ = nodes.body;
  body_content_top_ = nodes.body_content_top;
  buttons_ = nodes.buttons;
  disabled_.assign(buttons_.size(), false);
  adopted_.reset();
  ring_ = FocusRing{};

  ctx.tables.focus.enter_scope(panel_, /*modal=*/true);
  std::optional<NodeId> first;
  const int def = spec_.default_button.value_or(-1);
  if (def >= 0 && static_cast<std::size_t>(def) < buttons_.size()) {
    first = buttons_[static_cast<std::size_t>(def)];
  } else if (!buttons_.empty()) {
    first = buttons_.front();
  }
  ctx.tables.focus.set_guarded(ctx.tree, first);
  refresh_ring(ctx);
  return true;
}

void Dialog::close(UiContext& ctx) {
  if (!handle_.has_value()) {
    return;
  }
  ctx.tables.focus.exit_scope(ctx.tree);
  if (adopted_.has_value() && ctx.tree.is_valid(*adopted_) &&
      ctx.tree.is_valid(adopted_parent_)) {
    (void)ctx.tree.move_child(*adopted_, adopted_parent_, std::nullopt);
  }
  adopted_.reset();
  ctx.popups.destroy(*handle_, ctx.tables);
  handle_.reset();
  buttons_.clear();
  disabled_.clear();
  panel_ = NodeId{};
  body_ = NodeId{};
  ring_ = FocusRing{};
  if (restore_focus_.has_value() && ctx.tree.is_valid(*restore_focus_)) {
    ctx.tables.focus.set(restore_focus_);
  }
  restore_focus_.reset();
}

void Dialog::set_button_disabled(UiContext& ctx, int index, bool disabled) {
  if (!is_open() || index < 0 || static_cast<std::size_t>(index) >= buttons_.size()) {
    return;
  }
  const auto at = static_cast<std::size_t>(index);
  disabled_[at] = disabled;
  ui_visuals::paint_dialog_button(ctx, buttons_[at], spec_.default_button == index, disabled);
  if (disabled && ctx.tables.focus.current() == buttons_[at]) {
    ctx.tables.focus.focus_next(ctx.tree, ctx.tables.widgets, panel_);
    if (ctx.tables.focus.current() == buttons_[at]) {
      ctx.tables.focus.set_guarded(ctx.tree, std::nullopt);
    }
  }
  refresh_ring(ctx);
}

bool Dialog::button_disabled(int index) const {
  return index >= 0 && static_cast<std::size_t>(index) < disabled_.size() &&
         disabled_[static_cast<std::size_t>(index)];
}

void Dialog::adopt(UiContext& ctx, NodeId node) {
  if (!is_open() || !ctx.tree.is_valid(node)) {
    return;
  }
  const NodeId parent = ctx.tree.parent(node);
  if (!ctx.tree.move_child(node, body_, std::nullopt)) {
    return;
  }
  adopted_ = node;
  adopted_parent_ = parent;
}

void Dialog::refresh_ring(UiContext& ctx) {
  if (!is_open()) {
    return;
  }
  const std::optional<NodeId> current = ctx.tables.focus.current();
  update_focus_ring(
      ctx.tree, panel_, ring_,
      current.has_value() && is_within(ctx.tree, panel_, *current) ? current : std::nullopt,
      ctx.style.focus_ring);
}

std::optional<int> Dialog::button_index(NodeId id) const {
  for (std::size_t i = 0; i < buttons_.size(); ++i) {
    if (buttons_[i] == id) {
      return static_cast<int>(i);
    }
  }
  return std::nullopt;
}

DialogResult Dialog::finish(UiContext& ctx, std::optional<int> button) {
  close(ctx);
  return DialogResult{DialogEvent::kClosed, button, std::nullopt};
}

DialogResult Dialog::handle_pointer(UiContext& ctx, const PointerEvent& event) {
  if (!handle_.has_value() || !handle_->open || event.window != handle_->window) {
    return {};
  }
  if (event.action != PointerAction::kDown) {
    return DialogResult{DialogEvent::kHandled, std::nullopt, std::nullopt};
  }
  const std::optional<NodeId> hit =
      ctx.tables.widgets.widget_at(ctx.tree, PixelPoint{event.x, event.y});
  if (hit.has_value() && event.button == PointerButton::kPrimary) {
    if (const std::optional<int> index = button_index(*hit)) {
      if (button_disabled(*index)) {
        return DialogResult{DialogEvent::kHandled, std::nullopt, std::nullopt};
      }
      return finish(ctx, index);
    }
  }
  const bool inside = hit.has_value() && is_within(ctx.tree, panel_, *hit);
  ctx.tables.focus.set_guarded(ctx.tree, inside ? hit : std::nullopt);
  refresh_ring(ctx);
  return DialogResult{DialogEvent::kHandled, std::nullopt, inside ? hit : std::nullopt};
}

DialogResult Dialog::handle_key(UiContext& ctx, const KeyEvent& event) {
  if (!handle_.has_value() || !handle_->open || event.window != handle_->window ||
      event.action != KeyAction::kDown) {
    return {};
  }
  switch (event.key) {
    case Key::kTab:
      if (has(event.mods, Modifier::kShift)) {
        ctx.tables.focus.focus_previous(ctx.tree, ctx.tables.widgets, panel_);
      } else {
        ctx.tables.focus.focus_next(ctx.tree, ctx.tables.widgets, panel_);
      }
      refresh_ring(ctx);
      return DialogResult{DialogEvent::kHandled, std::nullopt, std::nullopt};
    case Key::kEscape:
      return finish(ctx, spec_.cancel_button);
    case Key::kEnter: {
      const std::optional<NodeId> current = ctx.tables.focus.current();
      if (current.has_value()) {
        if (const std::optional<int> index = button_index(*current)) {
          if (button_disabled(*index)) {
            return DialogResult{DialogEvent::kHandled, std::nullopt, std::nullopt};
          }
          return finish(ctx, index);
        }
      }
      const int def = spec_.default_button.value_or(-1);
      if (def >= 0 && static_cast<std::size_t>(def) < buttons_.size()) {
        if (button_disabled(def)) {
          return DialogResult{DialogEvent::kHandled, std::nullopt, std::nullopt};
        }
        return finish(ctx, def);
      }
      return {};
    }
    default:
      return {};
  }
}

}  // namespace dg
