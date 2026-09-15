// See include/drawgui/anim/animation_engine.h for the design; this file is
// the mechanism.

#include "drawgui/anim/animation_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

namespace dg {
namespace {

std::uint64_t transition_key(NodeId node, dg_prop_id prop_id) {
  return (static_cast<std::uint64_t>(node.value) << 16U) | prop_id;
}

bool is_interpolatable(PropType type) {
  return type == PropType::k_float || type == PropType::k_length || type == PropType::k_color;
}

// design.md section 5.16.1: "不可插值属性直接跳变，不报错但 Debug 构建告警" - a
// release build pays nothing for this (the whole function compiles away),
// and a debug build gets a name and a reason on stderr, the same channel
// dg::expected_contract_violation() already uses for a violated precondition
// (base/expected.h) - this is a diagnostic, not that kind of contract
// violation, so it does not abort.
//
// std::fputs with a pre-built std::string, not std::fprintf: the latter is a
// C-style vararg function, which this project avoids the same way
// base/expected.h's own stderr diagnostic does (std::fputs, no format
// string) - Core Guidelines F.55 / cppcoreguidelines-pro-type-vararg.
void warn_non_interpolatable(dg_prop_id prop_id, const char* why) {
#ifndef NDEBUG
  const std::string message = "dg: prop_id " + std::to_string(prop_id) + " " + why +
                              "; the write still applies, but jumps instead of interpolating\n";
  std::fputs(message.c_str(), stderr);
#else
  (void)prop_id;
  (void)why;
#endif
}

Color lerp_color(Color from, Color to, float t) {
  const auto mix = [t](std::uint8_t a, std::uint8_t b) {
    const float blended =
        static_cast<float>(a) + ((static_cast<float>(b) - static_cast<float>(a)) * t);
    return static_cast<std::uint8_t>(std::clamp(std::lround(blended), 0L, 255L));
  };
  return Color::rgba(mix(from.red(), to.red()), mix(from.green(), to.green()),
                     mix(from.blue(), to.blue()), mix(from.alpha(), to.alpha()));
}

}  // namespace

void AnimationEngine::set_reduced_motion(bool enabled) {
  reduced_motion_ = enabled;
}

float AnimationEngine::progress_fraction(const AnimSlot& slot) {
  if (slot.duration_ms <= 0) {
    return slot.reversed ? 0.0F : 1.0F;
  }
  return static_cast<float>(slot.progress_ms) / static_cast<float>(slot.duration_ms);
}

bool AnimationEngine::at_an_end(const AnimSlot& slot) {
  if (slot.duration_ms <= 0) {
    return true;
  }
  return slot.reversed ? slot.progress_ms <= 0 : slot.progress_ms >= slot.duration_ms;
}

PropValue AnimationEngine::interpolated_value(const AnimSlot& slot) {
  const float t = progress_fraction(slot);
  const float eased = evaluate_curve(slot.curve_id, t);
  switch (slot.type) {
    case PropType::k_float:
      return PropValue::number(slot.from.scalar() +
                               ((slot.to.scalar() - slot.from.scalar()) * eased));
    case PropType::k_length:
      return PropValue::length(slot.from.scalar() +
                               ((slot.to.scalar() - slot.from.scalar()) * eased));
    case PropType::k_color:
      return PropValue::color(lerp_color(slot.from.as_color(), slot.to.as_color(), eased));
    case PropType::k_enum:
    case PropType::k_gradient:
    case PropType::k_shadow:
    case PropType::k_transform:
    case PropType::k_image:
      // A slot of one of these types is never created (animate()/set_value()
      // both gate on is_interpolatable() first) - this arm exists only so the
      // switch stays exhaustive over PropType, matching this project's
      // "closed-enum kinds with exhaustive switch" rule, and answers `to`
      // rather than being unreachable, so a future caller that somehow got
      // here sees the jump behaviour rather than a stale interpolation.
      return slot.to;
  }
  return slot.to;
}

std::optional<PropValue> AnimationEngine::read_current(const LayoutTree& tree, NodeId node,
                                                       dg_prop_id prop_id) {
  // The bounded table - see this method's own declaration in the header for
  // why it is bounded rather than exhaustive over every float/length/color
  // property.
  switch (prop_id) {
    case DG_PROP_BACKGROUND_COLOR:
      return PropValue::color(tree.render().style(node).fill);
    case DG_PROP_BORDER_COLOR:
      return PropValue::color(tree.render().style(node).border_color);
    case DG_PROP_OPACITY:
      return PropValue::number(tree.render().style(node).opacity);
    case DG_PROP_LEFT: {
      const std::optional<int> value = tree.box(node).left;
      if (!value.has_value()) {
        return std::nullopt;
      }
      return PropValue::number(static_cast<float>(*value));
    }
    case DG_PROP_TOP: {
      const std::optional<int> value = tree.box(node).top;
      if (!value.has_value()) {
        return std::nullopt;
      }
      return PropValue::number(static_cast<float>(*value));
    }
    case DG_PROP_WIDTH: {
      const std::optional<int> value = tree.box(node).width;
      if (!value.has_value()) {
        return std::nullopt;
      }
      return PropValue::length(static_cast<float>(*value));
    }
    case DG_PROP_HEIGHT: {
      const std::optional<int> value = tree.box(node).height;
      if (!value.has_value()) {
        return std::nullopt;
      }
      return PropValue::length(static_cast<float>(*value));
    }
    default:
      return std::nullopt;
  }
}

std::size_t AnimationEngine::allocate_slot() {
  if (!free_list_.empty()) {
    const std::size_t index = free_list_.back();
    free_list_.pop_back();
    return index;
  }
  slots_.emplace_back();
  return slots_.size() - 1;
}

void AnimationEngine::free_slot(std::size_t index) {
  AnimSlot& slot = slots_[index];
  slot.alive = false;
  slot.generation += 1;
  free_list_.push_back(index);
}

bool AnimationEngine::valid(AnimHandle handle) const {
  return handle.index < slots_.size() && slots_[handle.index].alive &&
         slots_[handle.index].generation == handle.generation;
}

AnimHandle AnimationEngine::animate(LayoutTree& tree, NodeId node, dg_prop_id prop_id,
                                    const PropValue& from, const PropValue& to,
                                    std::int64_t duration_ms, dg_curve_id curve_id) {
  const std::optional<PropType> declared = prop_type(prop_id);
  const bool type_ok =
      declared.has_value() && from.type() == to.type() && from.type() == *declared;

  if (!type_ok || !is_interpolatable(*declared)) {
    warn_non_interpolatable(
        prop_id, !declared.has_value() || from.type() != to.type() || from.type() != *declared
                     ? "was given a from/to value of the wrong type to animate"
                     : "is not an interpolatable type");
    (void)set_prop(tree, node, prop_id, to);
    return AnimHandle{};
  }

  const std::size_t index = allocate_slot();
  AnimSlot& slot = slots_[index];
  slot.alive = true;
  slot.node = node;
  slot.prop_id = prop_id;
  slot.type = *declared;
  slot.from = from;
  slot.to = to;
  slot.duration_ms = reduced_motion_ ? 0 : std::max<std::int64_t>(0, duration_ms);
  slot.curve_id = curve_id;
  slot.progress_ms = 0;
  slot.paused = false;
  slot.reversed = false;
  slot.implicit = false;
  return AnimHandle{static_cast<std::uint32_t>(index), slot.generation};
}

AnimControlStatus AnimationEngine::pause(AnimHandle handle) {
  if (!valid(handle)) {
    return AnimControlStatus::kStaleHandle;
  }
  slots_[handle.index].paused = true;
  return AnimControlStatus::kOk;
}

AnimControlStatus AnimationEngine::resume(AnimHandle handle) {
  if (!valid(handle)) {
    return AnimControlStatus::kStaleHandle;
  }
  slots_[handle.index].paused = false;
  return AnimControlStatus::kOk;
}

AnimControlStatus AnimationEngine::reverse(AnimHandle handle) {
  if (!valid(handle)) {
    return AnimControlStatus::kStaleHandle;
  }
  AnimSlot& slot = slots_[handle.index];
  slot.reversed = !slot.reversed;
  return AnimControlStatus::kOk;
}

AnimControlStatus AnimationEngine::cancel(AnimHandle handle) {
  if (!valid(handle)) {
    return AnimControlStatus::kStaleHandle;
  }
  const std::size_t index = handle.index;
  AnimEvent event;
  event.handle = handle;
  event.node = slots_[index].node;
  event.prop_id = slots_[index].prop_id;
  event.kind = AnimEventKind::kCancelled;
  pending_events_.push_back(event);
  free_slot(index);
  return AnimControlStatus::kOk;
}

bool AnimationEngine::is_active(AnimHandle handle) const {
  return valid(handle);
}

void AnimationEngine::set_transition(NodeId node, dg_prop_id prop_id, std::int64_t duration_ms,
                                     dg_curve_id curve_id) {
  transitions_[transition_key(node, prop_id)] =
      AnimTransition{std::max<std::int64_t>(0, duration_ms), curve_id};
}

void AnimationEngine::clear_transition(NodeId node, dg_prop_id prop_id) {
  transitions_.erase(transition_key(node, prop_id));
}

PropWrite AnimationEngine::set_value(LayoutTree& tree, NodeId node, dg_prop_id prop_id,
                                     const PropValue& target) {
  const std::uint64_t key = transition_key(node, prop_id);
  const auto declared_it = transitions_.find(key);
  if (declared_it == transitions_.end()) {
    return set_prop(tree, node, prop_id, target);
  }

  const std::optional<PropType> declared_type = prop_type(prop_id);
  const bool type_ok = declared_type.has_value() && target.type() == *declared_type;
  if (!type_ok || !is_interpolatable(*declared_type)) {
    warn_non_interpolatable(prop_id, !type_ok
                                         ? "was given a value of the wrong type to transition"
                                         : "is not an interpolatable type");
    return set_prop(tree, node, prop_id, target);
  }

  std::size_t index = 0;
  PropValue from_value = target;
  const auto running_it = running_transition_slot_.find(key);
  if (running_it != running_transition_slot_.end()) {
    // RETARGET: the new `from` is the slot's CURRENT interpolated value, not
    // the original `from` it started at - see this method's own header
    // comment for why a restart-from-original implementation is a visible
    // defect rather than a simplification.
    index = running_it->second;
    from_value = interpolated_value(slots_[index]);
  } else {
    const std::optional<PropValue> current = read_current(tree, node, prop_id);
    if (!current.has_value()) {
      warn_non_interpolatable(
          prop_id, "has no declared transition read-back (see AnimationEngine::read_current)");
      return set_prop(tree, node, prop_id, target);
    }
    from_value = *current;
    index = allocate_slot();
    running_transition_slot_[key] = index;
  }

  AnimSlot& slot = slots_[index];
  slot.alive = true;
  slot.node = node;
  slot.prop_id = prop_id;
  slot.type = *declared_type;
  slot.from = from_value;
  slot.to = target;
  slot.duration_ms = reduced_motion_ ? 0 : declared_it->second.duration_ms;
  slot.curve_id = declared_it->second.curve_id;
  slot.progress_ms = 0;
  slot.paused = false;
  slot.reversed = false;
  slot.implicit = true;

  // kApplied: the write is accepted and scheduled. It lands on the next
  // tick(), the same "state changes on the next frame, not this call" shape
  // set_scroll_offset()/set_local_origin() already have for their own
  // reasons (render_tree.h) - a caller reading the node back before the next
  // tick() would see the PREVIOUS value, which is correct: nothing has been
  // drawn yet.
  return PropWrite{};
}

void AnimationEngine::tick(LayoutTree& tree, AnimTime now) {
  std::int64_t delta = 0;
  if (last_tick_.has_value()) {
    delta = now.ms - last_tick_->ms;
    if (delta < 0) {
      // A clock that runs backwards is a bug in the caller (steady_clock
      // never does; a test feeding a smaller AnimTime than the last one did).
      // Clamping to zero is what design.md section 5.17.5 calls "recoverable
      // degradation" for a release build - the animation simply does not
      // advance this tick rather than running in reverse - while a caller
      // watching stderr in a debug build still finds out.
      warn_non_interpolatable(DG_PROP_INVALID,
                              "AnimationEngine::tick() was given AnimTime that "
                              "moved backwards; this tick advances nothing");
      delta = 0;
    }
  }
  last_tick_ = now;

  for (std::size_t i = 0; i < slots_.size(); ++i) {
    AnimSlot& slot = slots_[i];
    if (!slot.alive || slot.paused) {
      continue;
    }
    if (slot.duration_ms > 0) {
      slot.progress_ms += slot.reversed ? -delta : delta;
      slot.progress_ms = std::clamp<std::int64_t>(slot.progress_ms, 0, slot.duration_ms);
    }

    const PropValue value = interpolated_value(slot);
    (void)set_prop(tree, slot.node, slot.prop_id, value);

    if (at_an_end(slot)) {
      AnimEvent event;
      event.node = slot.node;
      event.prop_id = slot.prop_id;
      event.kind = AnimEventKind::kCompleted;
      if (!slot.implicit) {
        event.handle = AnimHandle{static_cast<std::uint32_t>(i), slot.generation};
      }
      pending_events_.push_back(event);
      if (slot.implicit) {
        running_transition_slot_.erase(transition_key(slot.node, slot.prop_id));
      }
      free_slot(i);
    }
  }
}

bool AnimationEngine::has_active() const {
  return std::ranges::any_of(slots_,
                             [](const AnimSlot& slot) { return slot.alive && !slot.paused; });
}

std::vector<AnimEvent> AnimationEngine::poll_events() {
  std::vector<AnimEvent> drained;
  drained.swap(pending_events_);
  return drained;
}

}  // namespace dg
