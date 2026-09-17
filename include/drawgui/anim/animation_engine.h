// AnimationEngine - the clock and interpolator design.md section 5.16.1
// requires, and the state machine section 5.15.1 drives the frame loop with.
//
// THIS IS THE C++ SHAPE OF §5.16.1's TWO C SIGNATURES, not the ABI itself
// (that is slice 6-3, explicitly out of scope here):
//
//   dg_anim_t* dg_animate(dg_node_t*, uint16_t prop_id, const dg_value* from,
//                         const dg_value* to, uint32_t duration_ms,
//                         uint16_t curve_id);
//   int dg_node_set_transition(dg_node_t*, uint16_t prop_id,
//                              uint32_t duration_ms, uint16_t curve_id);
//
// `animate()` below is the first, over `LayoutTree&`/`NodeId` in place of
// `dg_node_t*` (exactly how `dg::set_prop()` already stands in for
// `dg_node_set_prop`); `set_transition()` plus `set_value()` together are the
// second - `set_transition()` is the one-time declaration, and `set_value()`
// is the write a host makes afterwards, which is where the CSS "any
// subsequent change interpolates automatically" behaviour actually lives.
//
// INTERPOLATION AND PROPERTY WRITING NEVER LEAVE C++, per section 5.16.1's
// own words: tick() is the only place a value is computed, and it writes
// through dg::set_prop() - the exact function every non-animated property
// write already goes through - so an animated frame and a manually-set frame
// are indistinguishable to everything downstream (layout, damage, paint).
//
// COMPLETION AND CANCELLATION ARE EVENTS, drained by poll_events(), never a
// callback - section 5.16.1 forbids the callback explicitly ("不用跨线程回调"),
// and PumpResult (window_manager.h) is this project's own precedent for the
// shape: "everything one call turned up, split by what the caller has to do
// about it", not one delivered as it happens.
//
// WHAT IS INTERPOLATED: PropType::k_float, k_length and k_color - the three
// scalar types PropValue can hold that also have a defined "halfway between".
// k_enum, k_gradient, k_shadow, k_transform and k_image are NOT interpolated;
// a caller asking to animate one of those gets an immediate jump to the
// target value (never an error - the property write itself may still be
// rejected by dg::set_prop() for the ordinary reasons) and, in a debug build
// only, a diagnostic on stderr naming the property - section 5.16.1's "不报错
// 但 Debug 构建告警". `transform`'s decomposed translate/scale/rotate
// components (section 5.9.6) are exactly the kind of thing this would
// interpolate once `dg::set_transform()` stops declining every call
// (doc/complex-properties.md section 4) - nothing here needs to change that
// day, because the gate below is on PropType, not on a hand-picked property
// list.
//
// HANDLE LIFETIME: see AnimHandle's own comment. The short version is a
// generation counter, which is a NEW mechanism for this project - slice 5-3's
// "never free a pool node" precedent for the dangling-NodeId problem does not
// transfer here, and doc/animation.md records why in full.
#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "drawgui/anim/clock.h"
#include "drawgui/anim/curve.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/props/node_props.h"
#include "drawgui/render/render_tree.h"

namespace dg {

// Names one explicit animation (one `animate()` call), across its whole
// lifetime, including after it has finished.
//
// TWO FIELDS RATHER THAN ONE, and the second is the whole point. `index`
// alone is exactly NodeId's shape - "meaningful only while the thing it
// names is still alive" - and slice 5-3 could make that work for a render
// node because render nodes are NEVER destroyed (doc/list.md section 1: "it
// never dangles, because it is never freed"). An animation cannot take the
// same vow: unlike a UI's node count, the number of animations a long-running
// process creates is unbounded - a button hovered ten thousand times has
// created ten thousand short-lived slots - so AnimationEngine MUST reclaim a
// finished slot's storage for a later animate() call to reuse, and reusing
// storage is exactly what makes a bare index ambiguous: a handle from
// animation #1, held past its completion, and a handle from animation #401
// that reused #1's slot must not compare equal or one is silently
// controlling the other's animation. `generation` is bumped every time a
// slot is freed (on completion, on cancellation, or by being reclaimed for
// reuse), and every accessor below checks it before touching the slot - a
// mismatch reports `kStaleHandle` rather than acting on someone else's
// animation. This is the ordinary generational-index technique, and it is
// the mechanism `doc/widgets.md`'s own comment on WidgetSet already named as
// what a future removal path would need ("THE DAY REMOVAL ARRIVES ... the
// generation counter it will need") - that day is this slice, for animation
// slots specifically, because they are the first thing in this project that
// both comes into existence and goes out of it during a session.
struct AnimHandle {
  std::uint32_t index = 0;
  std::uint32_t generation = 0;

  friend bool operator==(AnimHandle, AnimHandle) = default;
};

enum class AnimEventKind : std::uint8_t {
  kCompleted,
  kCancelled,
};

// One entry of poll_events(). `handle` is AnimHandle{} (index 0, generation
// 0 - never a value animate() can return, since generation starts at 1) for
// an event raised by an IMPLICIT transition, which has no handle a host ever
// held; `node`/`prop_id` are what a caller reads instead, and are always
// meaningful in both cases.
struct AnimEvent {
  AnimHandle handle;
  NodeId node;
  dg_prop_id prop_id = DG_PROP_INVALID;
  AnimEventKind kind = AnimEventKind::kCompleted;
};

// Whether a handle-taking call found a live animation to act on.
enum class AnimControlStatus : std::uint8_t {
  kOk,
  kStaleHandle,
};

// Declares that writes to (node, prop_id) interpolate from now on - the state
// dg_node_set_transition() records, kept here rather than beside it because
// AnimationEngine is the one thing that reads it.
struct AnimTransition {
  std::int64_t duration_ms = 0;
  dg_curve_id curve_id = kCurveLinear;
};

// One in-flight interpolation - either an explicit animate() call, or the
// transition currently retargeting (node, prop_id).
struct AnimSlot {
  bool alive = false;
  std::uint32_t generation = 1;

  NodeId node;
  dg_prop_id prop_id = DG_PROP_INVALID;
  PropType type = PropType::k_float;

  PropValue from = PropValue::number(0.0F);
  PropValue to = PropValue::number(0.0F);

  std::int64_t duration_ms = 0;
  dg_curve_id curve_id = kCurveLinear;

  // How far into [0, duration_ms] this slot has played. Not wall-clock time:
  // tick() advances it by an explicit delta, which is what lets pause() stop
  // it cold and reverse() run it the other way without either touching
  // `duration_ms` or `from`/`to`.
  std::int64_t progress_ms = 0;

  bool paused = false;
  bool reversed = false;

  // True for a slot a transition is driving (retargeted by set_value(),
  // never handed a host-visible AnimHandle); false for an explicit animate()
  // call. Only affects bookkeeping on completion - see tick()'s own comment.
  bool implicit = false;
};

// The clock, the interpolator and both APIs section 5.16.1 asks for, over one
// LayoutTree. Owned by whoever owns the scene - one per window, matching the
// per-window dirty flag section 5.15.1 already wants ("多窗口下每窗口独立脏
// 标记"): two windows animating independently must not contend over one
// engine's slot pool or one engine's reduced-motion flag racing a per-window
// query that has not landed yet.
class AnimationEngine {
 public:
  AnimationEngine() = default;

  // design.md section 5.14.2's reduced-motion query is a T2 SERVICE - this
  // project's platform layer has no such query yet, and building one is not
  // this slice's job. What IS this slice's job is honouring the flag once a
  // caller supplies it, which is what set_reduced_motion() is for: a host
  // reads the platform's actual preference (or, in this engine's own demo
  // and tests, simply asks for it) and tells the engine once.
  //
  // WHY DURATIONS GO TO ZERO RATHER THAN THE LOGIC BEING SKIPPED: section
  // 5.16.1 states this outcome without deriving it, so the derivation is
  // recorded here rather than only in doc/animation.md. A duration of zero
  // still allocates a slot, still runs through tick(), still promotes the
  // node to a repaint boundary and still raises a completion event - every
  // step an ordinary animation takes, collapsed to one frame instead of
  // spread over many. Skipping the logic instead (writing `to` directly and
  // never touching the slot machinery) would be a SECOND code path a test can
  // only exercise by asserting on the reduced-motion branch specifically,
  // which is exactly the "two copies that can disagree" shape this project
  // has refused everywhere else (props/drawgui.props.toml's whole existence
  // is the same argument at table scale). One state machine, one duration
  // that happens to be zero, is the version nothing can drift out of step
  // with.
  void set_reduced_motion(bool enabled);
  [[nodiscard]] bool reduced_motion() const { return reduced_motion_; }

  // dg_animate(). `from`/`to` must share PropValue::type() with each other
  // and with dg::prop_type(prop_id); a mismatch is reported by returning
  // AnimHandle{} (generation 0, which no real handle ever has) rather than by
  // an exception - matching PropWrite's own "every outcome is reported, none
  // is silent" shape, one layer up.
  //
  // NON-INTERPOLATABLE prop_id (or one dg::prop_type() does not recognise at
  // all): the write jumps to `to` immediately, through dg::set_prop(), and
  // this still returns AnimHandle{} - there is nothing for pause/reverse/
  // cancel to act on, because nothing is running.
  [[nodiscard]] AnimHandle animate(LayoutTree& tree, NodeId node, dg_prop_id prop_id,
                                   const PropValue& from, const PropValue& to,
                                   std::int64_t duration_ms, dg_curve_id curve_id);

  [[nodiscard]] AnimControlStatus pause(AnimHandle handle);
  [[nodiscard]] AnimControlStatus resume(AnimHandle handle);

  // Flips playback direction from wherever the animation currently is - NOT
  // a swap of `from`/`to` - so reversing a slot mid-flight continues from its
  // current interpolated value exactly as retargeting a transition does (see
  // set_value()'s own comment); reversing a slot that already reached an end
  // plays it back towards the other one.
  [[nodiscard]] AnimControlStatus reverse(AnimHandle handle);

  // Stops the animation where it is - the node keeps whatever value the last
  // tick() wrote, it is never snapped to `to` or back to `from` - and raises
  // AnimEventKind::kCancelled. Frees the slot for reuse, bumping generation,
  // so a caller that calls cancel() twice on the same handle gets kOk the
  // first time and kStaleHandle the second - matching request_close()'s own
  // idempotence shape one layer down, except the SECOND call is the one that
  // reports it, because unlike a window there is a real difference to tell
  // apart: the first call did something.
  [[nodiscard]] AnimControlStatus cancel(AnimHandle handle);

  // The per-node cancel node_lifecycle.h's on_node_removed() needs and
  // this class did not have: every slot (explicit animate() or an
  // in-flight implicit transition) currently animating `node`, cancelled -
  // raising AnimEventKind::kCancelled for each exactly like cancel() does -
  // and every DECLARED transition on `node` forgotten too, since a
  // removed node has nothing left to retarget when a later add_child()
  // reuses its numeric index for an unrelated one. A scan, not a lookup:
  // `node` may be driving several (node, prop_id) pairs at once, the same
  // reason ThemeBindings/ActionScopes keep a small vector per node rather
  // than one slot.
  void cancel_all_for(NodeId node);

  [[nodiscard]] bool is_active(AnimHandle handle) const;

  // dg_node_set_transition(). Persists until overwritten or cleared -
  // independent of anything currently animating (node, prop_id), exactly like
  // CSS's own `transition` declaration is independent of the value it will
  // eventually apply to.
  void set_transition(NodeId node, dg_prop_id prop_id, std::int64_t duration_ms,
                      dg_curve_id curve_id);

  // Removes the declaration (future writes to (node, prop_id) go straight
  // through dg::set_prop() again). Does NOT touch a slot already retargeting
  // this (node, prop_id) - it plays out to completion, matching CSS's own
  // `transition: none` leaving an in-flight transition alone.
  void clear_transition(NodeId node, dg_prop_id prop_id);

  // The write a transition-aware caller makes in place of dg::set_prop() -
  // this project's C++ stand-in for what `dg_node_set_prop` would route
  // through once a transition is declared (the ABI itself is 6-3). Falls
  // straight through to dg::set_prop() when (node, prop_id) has no
  // transition declared, so a caller can use this UNCONDITIONALLY once an
  // engine exists, whether or not the property is animated.
  //
  // THE RETARGET CASE: if (node, prop_id) already has a slot in flight - a
  // second hover-out before the hover-in transition finished, say - this does
  // NOT restart from the ORIGINAL `from`. It computes the slot's CURRENT
  // interpolated value (curve(progress/duration) at this exact instant,
  // recomputed from the slot's own state rather than re-read from the tree,
  // so it is exact even between ticks) and makes THAT the new `from`, resets
  // progress to zero, and sets `to` to the newly requested value. A restart-
  // from-original implementation would make the node visibly jump backward
  // to where the first transition began every time the property changes
  // again before the first one finishes - the defect the task names by name.
  //
  // WHEN NO SLOT IS YET RUNNING for (node, prop_id): the "from" has to be the
  // property's CURRENT value, which this engine cannot get from just any
  // prop_id - see the private read_current() for exactly which properties it
  // covers and why that is a deliberately bounded, not exhaustive, list.
  // Asking to transition a property outside that list on its very first
  // trigger jumps (with the same debug diagnostic a non-interpolatable type
  // gets) rather than guessing a `from`.
  PropWrite set_value(LayoutTree& tree, NodeId node, dg_prop_id prop_id,
                      const PropValue& target);

  // The frame step. `now` is normally steady_anim_time(); a test supplies any
  // AnimTime advancing however it likes - see clock.h's own comment for why
  // that is the whole of what makes this deterministic. Advances every alive,
  // unpaused slot by `now` minus the time of the previous tick() call (zero
  // on the very first call, so creating an animation and ticking it once
  // with the SAME AnimTime does not silently jump it forward), writes the
  // interpolated value through dg::set_prop(), and queues an
  // AnimEventKind::kCompleted for every slot that reached its end.
  void tick(LayoutTree& tree, AnimTime now);

  // True while any slot is alive and not paused - the signal section 5.15.1's
  // frame loop switches on: an on-demand runner passes a short timeout to
  // WindowManager::pump() while this is true, and passes a timeout that
  // blocks (see doc/animation.md's own recorded measurement of what
  // "blocks" should mean) once it goes false again.
  [[nodiscard]] bool has_active() const;

  // Drains and returns every event queued by tick()/cancel() since the last
  // call - the §5.8 polling queue's C++ shape (dg_poll_events is 6-3), and
  // PumpResult's own "everything one call turned up" pattern one layer over.
  [[nodiscard]] std::vector<AnimEvent> poll_events();

 private:
  [[nodiscard]] static float progress_fraction(const AnimSlot& slot);
  [[nodiscard]] static PropValue interpolated_value(const AnimSlot& slot);
  [[nodiscard]] static bool at_an_end(const AnimSlot& slot);

  // The bounded read-back table set_value()'s own comment names. Only
  // properties this slice's own clients exercise are here -
  // background_color/border_color/opacity (NodeStyle) and left/top/width/
  // height (BoxStyle) - both structs are public and already readable through
  // LayoutTree::box()/RenderTree::style(), so extending this to another
  // float/length/color property is one added case, not new machinery; it is
  // not exhaustive over the property table's other ~29 interpolatable
  // entries because nothing in this slice exercises them. doc/animation.md
  // records this as a deliberate, named scope boundary.
  [[nodiscard]] static std::optional<PropValue> read_current(const LayoutTree& tree,
                                                             NodeId node, dg_prop_id prop_id);

  [[nodiscard]] std::size_t allocate_slot();
  void free_slot(std::size_t index);
  [[nodiscard]] bool valid(AnimHandle handle) const;

  std::vector<AnimSlot> slots_;
  std::vector<std::size_t> free_list_;

  // Key: (node.index << 16) | prop_id. This is exact for the FULL 32-bit
  // range of NodeId::index, not merely "comfortable" for a scene nowhere
  // near 2^16 nodes - a claim this comment used to make and which 8-5's
  // NodeId{index, generation} split makes worth re-deriving rather than
  // re-typing: the shift promotes `node.index` to std::uint64_t BEFORE
  // shifting it left 16 bits, so the result occupies bits 16..47 of a
  // 64-bit key with no truncation at any index value a std::uint32_t can
  // hold. The only real width constraint is on `prop_id`, which is
  // uint16_t by definition and therefore always fits the low 16 bits with
  // no possible collision against the shifted index above it.
  std::unordered_map<std::uint64_t, AnimTransition> transitions_;
  std::unordered_map<std::uint64_t, std::size_t> running_transition_slot_;

  std::optional<AnimTime> last_tick_;
  bool reduced_motion_ = false;

  std::vector<AnimEvent> pending_events_;
};

}  // namespace dg
