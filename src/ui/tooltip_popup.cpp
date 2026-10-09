#include "drawgui/ui/tooltip_popup.h"

#include <algorithm>
#include <utility>

#include "ui/ui_visuals.h"

namespace dg {

void Tooltip::bind(NodeId target, std::string text) {
  for (Binding& binding : bindings_) {
    if (binding.target == target) {
      binding.text = std::move(text);
      return;
    }
  }
  bindings_.push_back(Binding{target, std::move(text)});
}

void Tooltip::unbind(NodeId target) {
  std::erase_if(bindings_, [&](const Binding& binding) { return binding.target == target; });
}

const std::string* Tooltip::text_for(NodeId id) const {
  for (const Binding& binding : bindings_) {
    if (binding.target == id) {
      return &binding.text;
    }
  }
  return nullptr;
}

TooltipEvent Tooltip::tick(UiContext& ctx, std::optional<NodeId> hovered, AnimTime now) {
  update_hover_timer(timer_, hovered, now);
  if (suppressed_.has_value() && hovered != suppressed_) {
    suppressed_.reset();
  }
  if (is_open() && hovered != shown_for_) {
    hide(ctx);
    return TooltipEvent::kHidden;
  }
  if (is_open() || !hovered.has_value() || hovered == suppressed_) {
    return TooltipEvent::kNone;
  }
  const std::string* text = text_for(*hovered);
  if (text == nullptr || !ctx.tree.is_valid(*hovered) || !hover_ready(timer_, now, delay_ms_)) {
    return TooltipEvent::kNone;
  }
  show(ctx, *hovered, *text);
  return is_open() ? TooltipEvent::kShown : TooltipEvent::kNone;
}

void Tooltip::show(UiContext& ctx, NodeId target, const std::string& text) {
  const PixelRect anchor = ctx.tree.absolute_bounds(target);
  const PixelSize size = ui_visuals::tooltip_size(ctx, text);
  // Neither dismissal flag: a tooltip never eats an event, tick() and
  // handle_pointer() decide when it goes.
  const Expected<PopupHandle, WindowError> shown = ctx.popups.show(
      ctx.window, ctx.tree, PixelRect{anchor.x, anchor.y, anchor.width, anchor.height + 4},
      size, PopupPlacement::kBelow, PopupFlags{false, false});
  if (!shown) {
    return;
  }
  handle_ = shown.value();
  shown_for_ = target;
  ui_visuals::build_tooltip(ctx, handle_->content_root, text, size);
}

TooltipEvent Tooltip::handle_pointer(UiContext& ctx, const PointerEvent& event) {
  if (event.action == PointerAction::kMove) {
    return TooltipEvent::kNone;
  }
  // A press/scroll/leave over a target cancels its pending or shown bubble
  // until the pointer moves off it.
  if (timer_.target.has_value()) {
    suppressed_ = timer_.target;
  }
  if (!is_open()) {
    return TooltipEvent::kNone;
  }
  hide(ctx);
  return TooltipEvent::kHidden;
}

void Tooltip::hide(UiContext& ctx) {
  if (handle_.has_value()) {
    ctx.popups.destroy(*handle_, ctx.tables);
  }
  handle_.reset();
  shown_for_.reset();
}

std::optional<AnimTime> Tooltip::next_deadline() const {
  if (is_open() || !timer_.target.has_value() || timer_.target == suppressed_ ||
      text_for(*timer_.target) == nullptr) {
    return std::nullopt;
  }
  return AnimTime{timer_.since.ms + delay_ms_};
}

}  // namespace dg
