// dg::ProgressBar - a track with a fill. Determinate: the fill covers
// `value` (clamped to [0, 1]) of the track. Indeterminate: a segment
// (ControlStyle::indeterminate_percent of the track) sweeps left to right
// once per indeterminate_period_ms, a pure function of the AnimTime passed
// to tick() - the same clock seam WidgetSet::set_edit_time() uses, so a test
// gets identical frames for identical times. Under
// AnimationEngine::reduced_motion() the segment stays parked in the middle.
//
// Not focusable and not interactive: no widget is attached to any of its
// nodes. The track clips (overflow: kClip), so the sweeping segment never
// paints outside it.
//
// Caller obligation: sync() after every layout pass (layout re-places the
// fill at the track's origin, exactly like a slider's thumb).

#pragma once

#include "drawgui/anim/clock.h"
#include "drawgui/layout/box.h"
#include "drawgui/ui/control_context.h"

namespace dg {

struct ProgressBarSpec {
  // The track's box (size, margin, grow...). `kind` is forced to kLeaf.
  BoxStyle box;
  float value = 0.0F;
  bool indeterminate = false;
};

class ProgressBar {
 public:
  // Builds the track (child of `parent`) and its fill. `now` starts the
  // indeterminate sweep's phase.
  void build(ControlContext& ctx, NodeId parent, const ProgressBarSpec& spec,
             AnimTime now = AnimTime{});

  // Removes both nodes (remove_subtree()); the object is empty afterwards.
  void destroy(ControlContext& ctx);

  // Determinate value, clamped to [0, 1]. False when unchanged. Setting a
  // value does not leave indeterminate mode.
  bool set_value(ControlContext& ctx, float value);

  // Switching ON restarts the sweep's phase at `now`.
  void set_indeterminate(ControlContext& ctx, bool indeterminate, AnimTime now);

  // Repositions an indeterminate segment for `now`. True when the fill
  // moved (a repaint is due). A no-op in determinate mode.
  bool tick(ControlContext& ctx, AnimTime now);

  // Whether the host should keep scheduling frames (indeterminate and not
  // reduced-motion).
  [[nodiscard]] bool animating(const ControlContext& ctx) const;

  // Re-derives the fill from the track's current size (after layout).
  void sync(ControlContext& ctx);

  [[nodiscard]] float value() const { return value_; }
  [[nodiscard]] bool indeterminate() const { return indeterminate_; }
  [[nodiscard]] NodeId track() const { return track_; }
  [[nodiscard]] NodeId fill() const { return fill_; }

 private:
  void place(ControlContext& ctx);

  NodeId track_;
  NodeId fill_;
  bool built_ = false;
  float value_ = 0.0F;
  bool indeterminate_ = false;
  AnimTime start_;
  AnimTime now_;
};

}  // namespace dg
