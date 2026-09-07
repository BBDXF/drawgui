#include "drawgui/widget/interaction.h"

#include <optional>

namespace dg {

void Interaction::set_hover(std::optional<NodeId> target, InteractionChange& change) {
  if (hovered_ == target) {
    return;
  }
  // Both sides of the move are recorded before either is reported, so a caller
  // repainting from this change never sees a half-applied transition.
  change.left = hovered_;
  change.entered = target;
  hovered_ = target;
}

InteractionChange Interaction::moved_over(std::optional<NodeId> target) {
  InteractionChange change;
  if (holding_.has_value()) {
    holding_inside_ = target == holding_;
    set_hover(holding_inside_ ? holding_ : std::nullopt, change);
    return change;
  }
  set_hover(target, change);
  return change;
}

InteractionChange Interaction::pressed_on(std::optional<NodeId> target) {
  InteractionChange change;

  // Hover is settled first because a press can arrive without a preceding
  // move over the same widget - a scripted sequence does it, and so does a
  // real pointer whose motion event was coalesced away. Without this the
  // widget would be drawn pressed while never having been entered, and the
  // caller's enter/leave bookkeeping would be permanently one behind.
  set_hover(target, change);

  if (!target.has_value()) {
    return change;
  }
  holding_ = target;
  holding_inside_ = true;
  change.pressed = target;
  return change;
}

InteractionChange Interaction::released_on(std::optional<NodeId> target) {
  InteractionChange change;

  if (holding_.has_value()) {
    change.released = holding_;
    if (target == holding_) {
      change.clicked = holding_;
    }
    holding_.reset();
    holding_inside_ = false;
  }

  // After the grab ends the pointer hovers whatever it is actually over. When
  // the release landed on the held widget this is a no-op, because that widget
  // was already the hovered one - so a click does not report a spurious
  // re-entry into the thing just clicked.
  set_hover(target, change);
  return change;
}

InteractionChange Interaction::left_window() {
  InteractionChange change;
  holding_inside_ = false;
  set_hover(std::nullopt, change);
  return change;
}

PointerState Interaction::state_of(NodeId id) const {
  PointerState state;
  state.hovered = hovered_ == id;
  state.pressed = holding_ == id && holding_inside_;
  return state;
}

}  // namespace dg
