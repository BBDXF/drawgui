// Animation in the runtime: Window::animate()/set_transition()/
// cancel_animation() over each window's AnimationEngine, and the per-pump
// step process_pump() drives it with.

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "app/internal.h"

namespace dg::app_detail {
namespace {

constexpr int kFrameMs = 16;

bool known_curve(std::uint16_t curve) {
  return curve >= kCurveLinear && curve <= kCurveEaseInOut;
}

// kOk when `value` may be written to `prop_id`, else why not.
Status check_value(std::uint16_t prop_id, const PropValue& value) {
  const std::optional<PropType> declared = prop_type(prop_id);
  if (!declared.has_value()) {
    return Status::kUnknownId;
  }
  if (value.type() != *declared) {
    return Status::kTypeMismatch;
  }
  return Status::kOk;
}

}  // namespace

AnimTime anim_now(const AppState& app) {
  return app.clock ? app.clock() : steady_anim_time();
}

// Brings the engine's clock up to now before a new animation or transition
// starts, so its first frame does not count the idle time before it.
void sync_animation_clock(WinState& window) {
  window.animation.tick(window.tree, anim_now(window.app));
}

std::optional<PropWrite> transition_write(WinState& window, NodeId target,
                                          std::uint16_t prop_id, const PropValue& value) {
  if (!window.animation.has_transition(target, prop_id)) {
    return std::nullopt;
  }
  sync_animation_clock(window);
  return window.animation.set_value(window.tree, target, prop_id, value);
}

void tick_animations(WinState& window) {
  window.animation.tick(window.tree, anim_now(window.app));
  for (const AnimEvent& event : window.animation.poll_events()) {
    const auto found =
        std::find_if(window.animations.begin(), window.animations.end(),
                     [&event](const auto& entry) { return entry.second == event.handle; });
    if (found == window.animations.end()) {
      continue;
    }
    const AnimationId id = found->first;
    window.animations.erase(found);
    if (event.kind == AnimEventKind::kCompleted) {
      emit(window.app, events::AnimationFinishedEvent{window.sdl_window, event.node, id});
    }
  }
}

int clamp_animation_timeout(const AppState& app, int timeout_ms) {
  const bool active = std::any_of(app.windows.begin(), app.windows.end(),
                                  [](const std::unique_ptr<Window>& window) {
                                    return !window->impl().closed &&
                                           window->impl().animation.has_active();
                                  });
  if (!active) {
    return timeout_ms;
  }
  return timeout_ms < 0 ? kFrameMs : std::min(timeout_ms, kFrameMs);
}

}  // namespace dg::app_detail

namespace dg {

Expected<AnimationId, Status> Window::animate(NodeId node, std::uint16_t prop_id,
                                              std::optional<PropValue> from, const PropValue& to,
                                              int duration_ms, std::uint16_t curve) {
  app_detail::WinState& window = *state_;
  if (app_detail::record_of(window, node) == nullptr) {
    return Unexpected(Status::kInvalidNode);
  }
  if (duration_ms < 0 || !app_detail::known_curve(curve)) {
    return Unexpected(Status::kValueOutOfRange);
  }
  Status checked = app_detail::check_value(prop_id, to);
  if (checked == Status::kOk && from.has_value()) {
    checked = app_detail::check_value(prop_id, *from);
  }
  if (checked != Status::kOk) {
    return Unexpected(checked);
  }
  if (!from.has_value()) {
    from = AnimationEngine::current_value(window.tree, node, prop_id);
    if (!from.has_value()) {
      return Unexpected(Status::kUnsupported);
    }
  }
  app_detail::sync_animation_clock(window);
  const AnimHandle handle =
      window.animation.animate(window.tree, node, prop_id, *from, to, duration_ms, curve);
  if (handle.generation == 0) {
    return AnimationId{0};
  }
  const AnimationId id = window.next_animation;
  window.next_animation = id >= static_cast<AnimationId>(std::numeric_limits<std::int32_t>::max())
                              ? 1
                              : id + 1;
  window.animations[id] = handle;
  return id;
}

Status Window::set_transition(NodeId node, std::uint16_t prop_id, int duration_ms,
                              std::uint16_t curve) {
  app_detail::WinState& window = *state_;
  if (app_detail::record_of(window, node) == nullptr) {
    return Status::kInvalidNode;
  }
  if (!prop_type(prop_id).has_value()) {
    return Status::kUnknownId;
  }
  if (duration_ms < 0 || !app_detail::known_curve(curve)) {
    return Status::kValueOutOfRange;
  }
  if (duration_ms == 0) {
    window.animation.clear_transition(node, prop_id);
  } else {
    window.animation.set_transition(node, prop_id, duration_ms, curve);
  }
  return Status::kOk;
}

Status Window::cancel_animation(AnimationId animation) {
  app_detail::WinState& window = *state_;
  const auto found = window.animations.find(animation);
  if (found == window.animations.end()) {
    return Status::kUnknownId;
  }
  const AnimHandle handle = found->second;
  window.animations.erase(found);
  return window.animation.cancel(handle) == AnimControlStatus::kOk ? Status::kOk
                                                                   : Status::kUnknownId;
}

bool Window::animating() const {
  return state_->animation.has_active();
}

}  // namespace dg
