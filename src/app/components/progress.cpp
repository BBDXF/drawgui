// NodeKind::kProgress: a dg::ProgressBar filling the node, determinate or
// indeterminate (animated by tick()).

#include <cmath>
#include <utility>
#include <optional>
#include <variant>

#include "app/components/components.h"
#include "app/internal.h"
#include "drawgui/anim/clock.h"

namespace dg::app_detail::components {
namespace {

constexpr int kProgressHeight = 8;

}  // namespace

void build_progress(WinState& window, NodeRecord& record) {
  ProgressState& state = std::get<ProgressState>(record.state);
  dg::ControlContext ctx = controls(window);
  const float value = state.progress.value();
  const bool indeterminate = state.progress.indeterminate();
  state.progress.destroy(ctx);
  dg::ProgressBarSpec spec;
  spec.box.percent.width = 100.0F;
  spec.box.height = wrapper_height(window, record, kProgressHeight);
  spec.value = value;
  spec.indeterminate = indeterminate;
  state.progress.build(ctx, record.id, spec, dg::steady_anim_time());
  window.relaid = true;
}

void sync_state(WinState& window, NodeRecord& /*record*/, ProgressState& state) {
  dg::ControlContext ctx = controls(window);
  state.progress.sync(ctx);
}

void restyle_state(WinState& window, NodeRecord& record, ProgressState& /*state*/) {
  build_progress(window, record);
}

void tick_state(WinState& window, NodeRecord& /*record*/, ProgressState& state,
                dg::AnimTime now) {
  if (state.progress.indeterminate()) {
    dg::ControlContext controls_ctx = controls(window);
    (void)state.progress.tick(controls_ctx, now);
  }
}

bool animating(const ProgressState& state) {
  return state.progress.indeterminate();
}

std::optional<dg::Status> set_prop_state(WinState& window, NodeRecord& /*record*/,
                                         ProgressState& state, std::uint16_t prop_id,
                                         const dg::PropValue& value) {
  if (prop_id != DG_PROP_VALUE && prop_id != DG_PROP_INDETERMINATE) {
    return std::nullopt;
  }
  dg::ControlContext ctx = controls(window);
  dg::ProgressBar& bar = state.progress;
  if (prop_id == DG_PROP_VALUE) {
    if (value.type() != dg::PropType::k_float) {
      return dg::Status::kTypeMismatch;
    }
    if (!std::isfinite(value.scalar()) || value.scalar() < 0.0F || value.scalar() > 1.0F) {
      return dg::Status::kValueOutOfRange;
    }
    (void)bar.set_value(ctx, value.scalar());
    return dg::Status::kOk;
  }
  if (value.type() != dg::PropType::k_bool) {
    return dg::Status::kTypeMismatch;
  }
  if (value.ordinal() > 1U) {
    return dg::Status::kValueOutOfRange;
  }
  bar.set_indeterminate(ctx, value.ordinal() == 1U, dg::steady_anim_time());
  return dg::Status::kOk;
}

void after_prop_state(WinState& window, NodeRecord& record, ProgressState& /*state*/,
                      std::uint16_t prop_id) {
  if (prop_id == DG_PROP_HEIGHT) {
    build_progress(window, record);
  }
}

}  // namespace dg::app_detail::components

namespace dg {

using app_detail::ProgressState;
using app_detail::components::component_of;

Status ProgressRef::set_value(float value) {
  app_detail::WinState& win = window();
  const auto found = component_of<ProgressState>(win, node());
  if (found.status != Status::kOk) {
    return found.status;
  }
  return app_detail::components::set_prop_state(win, *found.record, *found.state,
                                                 DG_PROP_VALUE, PropValue::number(value))
      .value_or(Status::kUnsupported);
}

Expected<float, Status> ProgressRef::value() const {
  const auto found = component_of<ProgressState>(std::as_const(window()), node());
  if (found.status != Status::kOk) {
    return Unexpected(found.status);
  }
  return found.state->progress.value();
}

Status ProgressRef::set_indeterminate(bool indeterminate) {
  app_detail::WinState& win = window();
  const auto found = component_of<ProgressState>(win, node());
  if (found.status != Status::kOk) {
    return found.status;
  }
  return app_detail::components::set_prop_state(win, *found.record, *found.state,
                                                 DG_PROP_INDETERMINATE,
                                                 PropValue::boolean(indeterminate))
      .value_or(Status::kUnsupported);
}

Expected<bool, Status> ProgressRef::indeterminate() const {
  const auto found = component_of<ProgressState>(std::as_const(window()), node());
  if (found.status != Status::kOk) {
    return Unexpected(found.status);
  }
  return found.state->progress.indeterminate();
}

}  // namespace dg
