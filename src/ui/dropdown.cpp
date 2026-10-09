#include "drawgui/ui/dropdown.h"

#include <utility>

#include "drawgui/widget/interaction.h"
#include "drawgui/widget/widget_set.h"

namespace dg {

DropdownResult Dropdown::open(UiContext& ctx) {
  const WidgetSet& widgets = ctx.tables.widgets;
  const std::optional<int> selected = widgets.dropdown_selected_index(anchor_);
  MenuSpec spec;
  for (const std::string& option : widgets.dropdown_options(anchor_)) {
    MenuItem item;
    item.text = option;
    spec.items.push_back(item);
  }
  if (selected.has_value() && *selected >= 0 &&
      static_cast<std::size_t>(*selected) < spec.items.size()) {
    spec.items[static_cast<std::size_t>(*selected)].checked = true;
    for (MenuItem& item : spec.items) {
      item.checked = item.checked.value_or(false);
    }
  }
  const PixelRect anchor = ctx.tree.absolute_bounds(anchor_);
  spec.min_width = anchor.width;
  spec.initial_highlight = selected;
  if (spec.items.empty() || !menu_.open(ctx, anchor, std::move(spec), anchor_)) {
    return DropdownResult{DropdownEvent::kHandled, selected, std::nullopt};
  }
  return DropdownResult{DropdownEvent::kOpened, selected, std::nullopt};
}

void Dropdown::close(UiContext& ctx) {
  menu_.close(ctx);
}

DropdownResult Dropdown::from_menu(UiContext& ctx, const MenuResult& result) {
  const std::optional<int> before = ctx.tables.widgets.dropdown_selected_index(anchor_);
  switch (result.event) {
    case MenuEvent::kIgnored:
      return DropdownResult{DropdownEvent::kIgnored, before, std::nullopt};
    case MenuEvent::kHandled:
      return DropdownResult{DropdownEvent::kHandled, before, std::nullopt};
    case MenuEvent::kDismissed:
      return DropdownResult{DropdownEvent::kClosed, before, std::nullopt};
    case MenuEvent::kActivated:
      break;
  }
  const bool changed = ctx.fonts != nullptr && ctx.tables.widgets.dropdown_select(
                                                   ctx.tree, *ctx.fonts, anchor_, result.index);
  const std::optional<int> after = ctx.tables.widgets.dropdown_selected_index(anchor_);
  if (!changed) {
    return DropdownResult{DropdownEvent::kClosed, after, std::nullopt};
  }
  return DropdownResult{DropdownEvent::kChanged, after, before};
}

DropdownResult Dropdown::handle_pointer(UiContext& ctx, const PointerEvent& event) {
  if (menu_.is_open()) {
    return from_menu(ctx, menu_.handle_pointer(ctx, event));
  }
  if (event.window == ctx.window && event.action == PointerAction::kDown &&
      event.button == PointerButton::kPrimary &&
      ctx.tables.widgets.widget_at(ctx.tree, PixelPoint{event.x, event.y}) == anchor_ &&
      !is_disabled(ctx.tree, anchor_)) {
    return open(ctx);
  }
  return {};
}

DropdownResult Dropdown::handle_key(UiContext& ctx, const KeyEvent& event) {
  if (menu_.is_open()) {
    return from_menu(ctx, menu_.handle_key(ctx, event));
  }
  if (event.window == ctx.window && event.action == KeyAction::kDown &&
      (event.key == Key::kDown || event.key == Key::kEnter) &&
      ctx.tables.focus.is_focused(anchor_)) {
    return open(ctx);
  }
  return {};
}

DropdownResult Dropdown::handle_text(UiContext& ctx, const TextInputEvent& event) {
  if (!menu_.is_open()) {
    return {};
  }
  return from_menu(ctx, menu_.handle_text(ctx, event));
}

}  // namespace dg
