#include "drawgui/ui/progress_bar.h"

#include <algorithm>
#include <cmath>

#include "ui/control_visuals.h"

namespace dg {

void ProgressBar::build(ControlContext& ctx, NodeId parent, const ProgressBarSpec& spec,
                        AnimTime now) {
  destroy(ctx);
  const control_visuals::ProgressNodes nodes =
      control_visuals::build_progress(ctx, parent, spec.box);
  track_ = nodes.track;
  fill_ = nodes.fill;
  built_ = true;
  value_ = std::clamp(spec.value, 0.0F, 1.0F);
  indeterminate_ = spec.indeterminate;
  start_ = now;
  now_ = now;
}

void ProgressBar::destroy(ControlContext& ctx) {
  if (built_ && ctx.tree().is_valid(track_)) {
    remove_subtree(ctx.layout, track_, ctx.tables);
  }
  built_ = false;
}

bool ProgressBar::set_value(ControlContext& ctx, float value) {
  const float clamped = std::clamp(value, 0.0F, 1.0F);
  if (clamped == value_) {
    return false;
  }
  value_ = clamped;
  place(ctx);
  return true;
}

void ProgressBar::set_indeterminate(ControlContext& ctx, bool indeterminate, AnimTime now) {
  if (indeterminate && !indeterminate_) {
    start_ = now;
  }
  indeterminate_ = indeterminate;
  now_ = now;
  place(ctx);
}

bool ProgressBar::tick(ControlContext& ctx, AnimTime now) {
  if (!built_ || !indeterminate_) {
    return false;
  }
  now_ = now;
  const PixelRect before = ctx.tree().local_bounds(fill_);
  place(ctx);
  return ctx.tree().local_bounds(fill_) != before;
}

bool ProgressBar::animating(const ControlContext& ctx) const {
  return built_ && indeterminate_ && !ctx.tables.animation.reduced_motion();
}

void ProgressBar::sync(ControlContext& ctx) {
  place(ctx);
}

void ProgressBar::place(ControlContext& ctx) {
  if (!built_) {
    return;
  }
  RenderTree& tree = ctx.tree();
  const PixelRect track = tree.local_bounds(track_);
  PixelRect rect{0, 0, 0, track.height};
  if (indeterminate_) {
    const int segment =
        std::max(1, track.width * std::clamp(ctx.style.indeterminate_percent, 1, 100) / 100);
    rect.width = segment;
    if (ctx.tables.animation.reduced_motion()) {
      rect.x = (track.width - segment) / 2;
    } else {
      const std::int64_t period = std::max(1, ctx.style.indeterminate_period_ms);
      const std::int64_t phase = (((now_.ms - start_.ms) % period) + period) % period;
      // The segment enters fully off the left edge and leaves fully off the
      // right one: travel = track + segment.
      rect.x = static_cast<int>((phase * (track.width + segment)) / period) - segment;
    }
  } else {
    rect.width = static_cast<int>(
        std::lround(static_cast<double>(value_) * static_cast<double>(track.width)));
  }
  if (tree.local_bounds(fill_) != rect) {
    tree.set_local_bounds(fill_, rect);
  }
}

}  // namespace dg
