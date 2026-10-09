// dg::Tooltip - hover-delayed text bubbles over PopupHost.
//
// Timing is dg::HoverTimer (drawgui/widget/tooltip.h) fed with whatever the
// caller's dg::Interaction reports as hovered and an AnimTime the caller
// supplies - steady_anim_time() in an app, any value in a test (the clock
// seam). A bubble opens once its target has been hovered continuously for
// `delay_ms`, placed below the target (flipping above / clamping via
// resolve_popup_placement()), and is destroyed when the pointer leaves the
// target, presses, scrolls or leaves the window. After a press/scroll the
// same target stays suppressed until the pointer moves off it, so a tooltip
// never reappears under a click.
//
// Bindings are a plain (NodeId, text) list owned by this object, not a
// NodeId side table: a removed node's binding is simply never matched
// again (NodeId carries a generation), so remove_subtree() need not know
// about it. unbind() it if you recycle a lot of nodes.

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "drawgui/anim/clock.h"
#include "drawgui/ui/ui_context.h"
#include "drawgui/widget/tooltip.h"

namespace dg {

enum class TooltipEvent : std::uint8_t {
  kNone,
  kShown,
  kHidden,
};

class Tooltip {
 public:
  explicit Tooltip(int delay_ms = 500) : delay_ms_(delay_ms) {}

  void bind(NodeId target, std::string text);
  void unbind(NodeId target);

  // Once per frame/pump with the hovered widget and the current time.
  TooltipEvent tick(UiContext& ctx, std::optional<NodeId> hovered, AnimTime now);

  // kDown / kUp / kWheel / kLeave hide the bubble (and suppress it until the
  // pointer leaves the target). Never consumes the event: the caller still
  // dispatches it normally.
  TooltipEvent handle_pointer(UiContext& ctx, const PointerEvent& event);

  void hide(UiContext& ctx);

  [[nodiscard]] bool is_open() const { return handle_.has_value() && handle_->open; }
  [[nodiscard]] std::optional<NodeId> shown_for() const { return shown_for_; }
  [[nodiscard]] std::optional<PopupHandle> handle() const { return handle_; }
  // When the pending bubble will become due (for a pump() timeout), or
  // nullopt when nothing bound is being hovered.
  [[nodiscard]] std::optional<AnimTime> next_deadline() const;
  [[nodiscard]] int delay_ms() const { return delay_ms_; }

 private:
  [[nodiscard]] const std::string* text_for(NodeId id) const;
  void show(UiContext& ctx, NodeId target, const std::string& text);

  struct Binding {
    NodeId target;
    std::string text;
  };
  int delay_ms_;
  std::vector<Binding> bindings_;
  HoverTimer timer_;
  std::optional<PopupHandle> handle_;
  std::optional<NodeId> shown_for_;
  std::optional<NodeId> suppressed_;
};

}  // namespace dg
