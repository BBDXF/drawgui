#include "drawgui/ui/menu.h"

#include <cctype>
#include <utility>

#include "drawgui/widget/focus.h"
#include "ui/ui_visuals.h"

namespace dg {

bool Menu::open(UiContext& ctx, const PixelRect& anchor, MenuSpec spec,
                std::optional<NodeId> restore_focus) {
  close(ctx);
  spec_ = std::move(spec);
  const PixelSize size = ui_visuals::menu_size(ctx, spec_);
  const Expected<PopupHandle, WindowError> shown =
      ctx.popups.show(ctx.window, ctx.tree, anchor, size, spec_.placement, PopupFlags{});
  if (!shown) {
    return false;
  }
  handle_ = shown.value();
  restore_focus_ = restore_focus;
  painted_highlight_.reset();
  rows_ = ui_visuals::build_menu(ctx, handle_->content_root, spec_, size.width);
  ctx.tables.focus.enter_scope(handle_->content_root);

  const int initial = spec_.initial_highlight.value_or(-1);
  if (selectable(initial)) {
    highlight_node(ctx, rows_[static_cast<std::size_t>(initial)]);
  } else {
    const std::vector<NodeId> order =
        focus_order(ctx.tree, ctx.tables.widgets, handle_->content_root);
    highlight_node(ctx, order.empty() ? std::nullopt : std::optional<NodeId>(order.front()));
  }
  return true;
}

void Menu::close(UiContext& ctx) {
  if (!handle_.has_value()) {
    return;
  }
  ctx.tables.focus.exit_scope(ctx.tree);
  ctx.popups.destroy(*handle_, ctx.tables);
  handle_.reset();
  rows_.clear();
  painted_highlight_.reset();
  if (restore_focus_.has_value() && ctx.tree.is_valid(*restore_focus_)) {
    ctx.tables.focus.set(restore_focus_);
  }
  restore_focus_.reset();
}

std::optional<int> Menu::index_of(NodeId id) const {
  for (std::size_t i = 0; i < rows_.size(); ++i) {
    if (rows_[i] == id) {
      return static_cast<int>(i);
    }
  }
  return std::nullopt;
}

bool Menu::selectable(int index) const {
  if (index < 0 || static_cast<std::size_t>(index) >= spec_.items.size() ||
      static_cast<std::size_t>(index) >= rows_.size()) {
    return false;
  }
  const MenuItem& item = spec_.items[static_cast<std::size_t>(index)];
  return !item.separator && !item.disabled;
}

std::optional<int> Menu::highlighted(const UiContext& ctx) const {
  if (!is_open()) {
    return std::nullopt;
  }
  const std::optional<NodeId> current = ctx.tables.focus.current();
  return current.has_value() ? index_of(*current) : std::nullopt;
}

void Menu::set_highlight(UiContext& ctx, int index) {
  if (is_open() && selectable(index)) {
    highlight_node(ctx, rows_[static_cast<std::size_t>(index)]);
  }
}

void Menu::highlight_node(UiContext& ctx, std::optional<NodeId> target) {
  ctx.tables.focus.set(target);
  repaint_highlight(ctx);
}

// Paints Focus::current() as the highlighted row - whatever moved it (keys,
// pointer, or a caller's own Tab handling inside the scope).
void Menu::repaint_highlight(UiContext& ctx) {
  const std::optional<NodeId> current = ctx.tables.focus.current();
  const std::optional<NodeId> now =
      current.has_value() && index_of(*current).has_value() ? current : std::nullopt;
  if (now == painted_highlight_) {
    return;
  }
  if (painted_highlight_.has_value()) {
    if (const std::optional<int> old = index_of(*painted_highlight_)) {
      ui_visuals::paint_menu_item(ctx, *painted_highlight_,
                                  spec_.items[static_cast<std::size_t>(*old)], false);
    }
  }
  if (now.has_value()) {
    if (const std::optional<int> index = index_of(*now)) {
      ui_visuals::paint_menu_item(ctx, *now, spec_.items[static_cast<std::size_t>(*index)],
                                  true);
    }
  }
  painted_highlight_ = now;
}

MenuResult Menu::finish(UiContext& ctx, MenuEvent event, int index) {
  close(ctx);
  return MenuResult{event, index};
}

MenuResult Menu::handle_pointer(UiContext& ctx, const PointerEvent& event) {
  if (!handle_.has_value() || !handle_->open) {
    return {};
  }
  if (ctx.popups.handle_pointer(*handle_, event.window, event)) {
    return finish(ctx, MenuEvent::kDismissed, -1);
  }
  if (event.window != handle_->window) {
    return {};
  }
  const PixelPoint at{event.x, event.y};
  if (event.action == PointerAction::kMove || event.action == PointerAction::kDown) {
    const std::optional<NodeId> hit = ctx.tables.widgets.widget_at(ctx.tree, at);
    const std::optional<int> index = hit.has_value() ? index_of(*hit) : std::nullopt;
    if (index.has_value() && selectable(*index)) {
      if (event.action == PointerAction::kDown) {
        return finish(ctx, MenuEvent::kActivated, *index);
      }
      highlight_node(ctx, hit);
    }
  }
  return MenuResult{MenuEvent::kHandled, -1};
}

MenuResult Menu::handle_key(UiContext& ctx, const KeyEvent& event) {
  if (!handle_.has_value() || !handle_->open) {
    return {};
  }
  if (ctx.popups.handle_key(*handle_, event.window, event)) {
    return finish(ctx, MenuEvent::kDismissed, -1);
  }
  if (event.window != handle_->window || event.action != KeyAction::kDown) {
    return {};
  }
  const NodeId root = handle_->content_root;
  switch (event.key) {
    case Key::kUp:
      ctx.tables.focus.focus_previous(ctx.tree, ctx.tables.widgets, root);
      repaint_highlight(ctx);
      return MenuResult{MenuEvent::kHandled, -1};
    case Key::kDown:
      ctx.tables.focus.focus_next(ctx.tree, ctx.tables.widgets, root);
      repaint_highlight(ctx);
      return MenuResult{MenuEvent::kHandled, -1};
    case Key::kHome:
    case Key::kEnd: {
      const std::vector<NodeId> order = focus_order(ctx.tree, ctx.tables.widgets, root);
      if (!order.empty()) {
        highlight_node(ctx, event.key == Key::kHome ? order.front() : order.back());
      }
      return MenuResult{MenuEvent::kHandled, -1};
    }
    case Key::kEnter: {
      const std::optional<int> index = highlighted(ctx);
      if (index.has_value() && selectable(*index)) {
        return finish(ctx, MenuEvent::kActivated, *index);
      }
      return MenuResult{MenuEvent::kHandled, -1};
    }
    case Key::kTab:
      return MenuResult{MenuEvent::kHandled, -1};
    default:
      return {};
  }
}

MenuResult Menu::handle_text(UiContext& ctx, const TextInputEvent& event) {
  if (!handle_.has_value() || !handle_->open || event.window != handle_->window ||
      event.text.empty()) {
    return {};
  }
  const auto lower = [](char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  };
  const char wanted = lower(event.text.front());
  const int count = static_cast<int>(spec_.items.size());
  const int start = highlighted(ctx).value_or(-1);
  for (int step = 1; step <= count; ++step) {
    const int index = (start + step + count) % count;
    const MenuItem& item = spec_.items[static_cast<std::size_t>(index)];
    if (selectable(index) && !item.text.empty() && lower(item.text.front()) == wanted) {
      highlight_node(ctx, rows_[static_cast<std::size_t>(index)]);
      return MenuResult{MenuEvent::kHandled, -1};
    }
  }
  return {};
}

}  // namespace dg
