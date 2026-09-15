// The animation clock, curves and both APIs (explicit/implicit) - slice 6-1.
//
// EVERY ASSERTION HERE IS DRIVEN BY AN EXPLICIT AnimTime, NEVER A SLEEP. That
// is the entire point of clock.h's own design: this project's acceptance bar
// is byte-exact goldens and hand-derived numbers, and a test that slept for
// real milliseconds and then asserted on "roughly halfway" would be the
// first nondeterministic test in this codebase. Every tick() call below
// passes an AnimTime this file chose, so a value like "exactly 25" is not an
// approximation - it is what the arithmetic must produce.
//
// Four things are pinned, matching the task's own subtlety list:
//
//   curve math (evaluate_curve(), hand-derived at t=0.5 for all four curves)
//   the retarget case (set_value() mid-transition uses the CURRENT
//     interpolated value as the new `from`, not the original)
//   handle lifetime (a cancelled/completed handle's generation is stale, and
//     every handle-taking call reports kStaleHandle rather than acting on a
//     reused slot)
//   the reduced-motion collapse (duration forced to zero still runs the full
//     state machine - alive, ticked, completed - just in one frame)

#include "drawgui/anim/animation_engine.h"

#include <cstdint>
#include <optional>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/anim/clock.h"
#include "drawgui/anim/curve.h"
#include "drawgui/graphics/types.h"
#include "drawgui/layout/box.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/props/node_props.h"
#include "drawgui/render/render_tree.h"

namespace {

using dg::AnimationEngine;
using dg::AnimControlStatus;
using dg::AnimEventKind;
using dg::AnimHandle;
using dg::AnimTime;
using dg::BoxStyle;
using dg::Color;
using dg::LayoutKind;
using dg::LayoutTree;
using dg::NodeId;
using dg::NodeStyle;
using dg::PropStatus;
using dg::PropValue;
using dg::PropWrite;

dg::TreeSpec spec_of() {
  dg::TreeSpec spec;
  spec.viewport = dg::PixelSize{200, 200};
  return spec;
}

// One absolute-position container (so `left`/`top` have a consumer, exactly
// like tests/unit/test_props_boundary.cpp's Fixture) plus one plain leaf
// (for `background_color`/`opacity`, which apply to any box).
struct Fixture {
  LayoutTree tree{spec_of()};
  NodeId absolute;
  NodeId panel;

  Fixture() {
    BoxStyle absolute_box;
    absolute_box.kind = LayoutKind::kAbsolute;
    absolute = tree.add_child(LayoutTree::root(), absolute_box, NodeStyle{});

    NodeStyle panel_style;
    panel_style.fill = Color::from_argb(0xFF000000);
    BoxStyle panel_box;
    panel_box.width = 100;
    panel_box.height = 100;
    panel = tree.add_child(LayoutTree::root(), panel_box, panel_style);
    tree.layout();
  }
};

}  // namespace

TEST_CASE("evaluate_curve: hand-derived at the curves' one shared point, t=0.5") {
  CHECK(static_cast<double>(dg::evaluate_curve(dg::kCurveLinear, 0.5F)) ==
        doctest::Approx(0.5));
  CHECK(static_cast<double>(dg::evaluate_curve(dg::kCurveEaseIn, 0.5F)) ==
        doctest::Approx(0.25));
  CHECK(static_cast<double>(dg::evaluate_curve(dg::kCurveEaseOut, 0.5F)) ==
        doctest::Approx(0.75));
  CHECK(static_cast<double>(dg::evaluate_curve(dg::kCurveEaseInOut, 0.5F)) ==
        doctest::Approx(0.5));
}

// Every curve here passes through both ends unchanged - an easing curve that
// did not would make duration_ms=0's "jump straight to `to`" argument (see
// the reduced-motion test below) depend on WHICH curve was declared, which is
// not how design.md section 5.16.1 describes the collapse. A separate
// TEST_CASE per curve (rather than one test looping over all four) is what
// keeps this readable as straight-line code instead of a loop wrapping
// CHECK's own try/catch expansion.
TEST_CASE("evaluate_curve: every curve passes through both ends unchanged (linear)") {
  CHECK(static_cast<double>(dg::evaluate_curve(dg::kCurveLinear, 0.0F)) ==
        doctest::Approx(0.0));
  CHECK(static_cast<double>(dg::evaluate_curve(dg::kCurveLinear, 1.0F)) ==
        doctest::Approx(1.0));
}
TEST_CASE("evaluate_curve: every curve passes through both ends unchanged (ease-in)") {
  CHECK(static_cast<double>(dg::evaluate_curve(dg::kCurveEaseIn, 0.0F)) ==
        doctest::Approx(0.0));
  CHECK(static_cast<double>(dg::evaluate_curve(dg::kCurveEaseIn, 1.0F)) ==
        doctest::Approx(1.0));
}
TEST_CASE("evaluate_curve: every curve passes through both ends unchanged (ease-out)") {
  CHECK(static_cast<double>(dg::evaluate_curve(dg::kCurveEaseOut, 0.0F)) ==
        doctest::Approx(0.0));
  CHECK(static_cast<double>(dg::evaluate_curve(dg::kCurveEaseOut, 1.0F)) ==
        doctest::Approx(1.0));
}
TEST_CASE("evaluate_curve: every curve passes through both ends unchanged (ease-in-out)") {
  CHECK(static_cast<double>(dg::evaluate_curve(dg::kCurveEaseInOut, 0.0F)) ==
        doctest::Approx(0.0));
  CHECK(static_cast<double>(dg::evaluate_curve(dg::kCurveEaseInOut, 1.0F)) ==
        doctest::Approx(1.0));
}

TEST_CASE("explicit animate(): linear float interpolation is exact at hand-derived ticks") {
  Fixture fixture;
  AnimationEngine engine;

  const AnimHandle handle =
      engine.animate(fixture.tree, fixture.panel, DG_PROP_WIDTH, PropValue::length(0.0F),
                     PropValue::length(100.0F), 1000, dg::kCurveLinear);
  REQUIRE(handle == handle);  // a real handle: generation is never 0
  CHECK_FALSE(handle == AnimHandle{});
  CHECK(engine.has_active());

  // First tick establishes the baseline; no time has passed yet, so nothing
  // has moved - creating an animation and ticking it once at the SAME
  // instant must not silently advance it.
  engine.tick(fixture.tree, AnimTime{1000});
  CHECK(fixture.tree.box(fixture.panel).width == std::optional<int>{0});

  engine.tick(fixture.tree, AnimTime{1250});
  CHECK(fixture.tree.box(fixture.panel).width == std::optional<int>{25});

  engine.tick(fixture.tree, AnimTime{1500});
  CHECK(fixture.tree.box(fixture.panel).width == std::optional<int>{50});
  CHECK(engine.has_active());
  CHECK(engine.poll_events().empty());

  engine.tick(fixture.tree, AnimTime{2000});
  CHECK(fixture.tree.box(fixture.panel).width == std::optional<int>{100});
  CHECK_FALSE(engine.has_active());

  const std::vector<dg::AnimEvent> events = engine.poll_events();
  REQUIRE(events.size() == 1);
  CHECK(events[0].kind == AnimEventKind::kCompleted);
  CHECK(events[0].handle == handle);
  CHECK(events[0].node == fixture.panel);
  CHECK(events[0].prop_id == DG_PROP_WIDTH);

  // Drained: a second poll before anything new happens is empty, matching
  // PumpResult's own "drains what is queued" shape.
  CHECK(engine.poll_events().empty());
}

TEST_CASE("explicit animate(): color interpolation, hand-derived at the midpoint") {
  Fixture fixture;
  AnimationEngine engine;
  const AnimHandle handle =
      engine.animate(fixture.tree, fixture.panel, DG_PROP_BACKGROUND_COLOR,
                     PropValue::color(Color::from_argb(0xFF000000)),
                     PropValue::color(Color::from_argb(0xFFFFFFFF)), 1000, dg::kCurveLinear);
  engine.tick(fixture.tree, AnimTime{0});
  engine.tick(fixture.tree, AnimTime{500});
  // std::lround(0 + (255 - 0) * 0.5) == std::lround(127.5) == 128 (round
  // half away from zero), so the midpoint is 0x80, not 0x7F.
  CHECK(fixture.tree.render().style(fixture.panel).fill.argb() == 0xFF808080U);
  CHECK(engine.is_active(handle));
}

// Every OTHER colour test in this file interpolates between two fully-opaque
// colours (alpha 0xFF at both ends), which cannot tell a correct alpha mix
// apart from a bug that hardcodes the result opaque - Color::rgba()'s own
// alpha parameter defaults to 0xFF, so a lerp_color() that forgot to mix the
// alpha channel at all would still pass every other test in this file. A
// defect-injection pass against this file found exactly that gap (dropping
// the alpha mix survived unnoticed); this test is what closes it.
TEST_CASE("explicit animate(): the alpha channel interpolates too, not just RGB") {
  Fixture fixture;
  AnimationEngine engine;
  (void)engine.animate(fixture.tree, fixture.panel, DG_PROP_BACKGROUND_COLOR,
                       PropValue::color(Color::from_argb(0x00000000)),
                       PropValue::color(Color::from_argb(0xFF000000)), 1000, dg::kCurveLinear);
  engine.tick(fixture.tree, AnimTime{0});
  engine.tick(fixture.tree, AnimTime{500});
  // std::lround(0 + (255 - 0) * 0.5) == 128 - the same midpoint formula the
  // RGB test above uses, applied to the alpha byte instead.
  CHECK(fixture.tree.render().style(fixture.panel).fill.argb() == 0x80000000U);
}

TEST_CASE("explicit animate(): pause freezes progress, resume continues from exactly there") {
  Fixture fixture;
  AnimationEngine engine;
  const AnimHandle handle =
      engine.animate(fixture.tree, fixture.panel, DG_PROP_WIDTH, PropValue::length(0.0F),
                     PropValue::length(100.0F), 1000, dg::kCurveLinear);
  engine.tick(fixture.tree, AnimTime{0});
  engine.tick(fixture.tree, AnimTime{300});
  CHECK(fixture.tree.box(fixture.panel).width == std::optional<int>{30});

  CHECK(engine.pause(handle) == AnimControlStatus::kOk);
  // Paused: has_active() must go false so the on-demand frame loop
  // (design.md section 5.15.1) stops asking for frames on this animation's
  // account - a wholly paused engine is exactly the "no dirty region, no
  // active animation" idle state.
  CHECK_FALSE(engine.has_active());

  engine.tick(fixture.tree, AnimTime{900});  // 600ms while paused: must not move
  CHECK(fixture.tree.box(fixture.panel).width == std::optional<int>{30});

  CHECK(engine.resume(handle) == AnimControlStatus::kOk);
  CHECK(engine.has_active());
  engine.tick(fixture.tree, AnimTime{1000});  // 100ms resumed, from 300 -> 400
  CHECK(fixture.tree.box(fixture.panel).width == std::optional<int>{40});
}

TEST_CASE("explicit animate(): reverse plays back from the CURRENT point, not from `to`") {
  Fixture fixture;
  AnimationEngine engine;
  const AnimHandle handle =
      engine.animate(fixture.tree, fixture.panel, DG_PROP_WIDTH, PropValue::length(0.0F),
                     PropValue::length(100.0F), 100, dg::kCurveLinear);
  engine.tick(fixture.tree, AnimTime{0});
  engine.tick(fixture.tree, AnimTime{40});
  CHECK(fixture.tree.box(fixture.panel).width == std::optional<int>{40});

  CHECK(engine.reverse(handle) == AnimControlStatus::kOk);
  engine.tick(fixture.tree, AnimTime{55});  // 15ms backward from 40 -> 25
  CHECK(fixture.tree.box(fixture.panel).width == std::optional<int>{25});
  CHECK(engine.is_active(handle));

  engine.tick(fixture.tree, AnimTime{95});  // 40ms more backward: clamps at 0
  CHECK(fixture.tree.box(fixture.panel).width == std::optional<int>{0});
  CHECK_FALSE(engine.is_active(handle));

  const std::vector<dg::AnimEvent> events = engine.poll_events();
  REQUIRE(events.size() == 1);
  CHECK(events[0].kind == AnimEventKind::kCompleted);
}

TEST_CASE("explicit animate(): cancel freezes the current value and never jumps to `to`") {
  Fixture fixture;
  AnimationEngine engine;
  const AnimHandle handle =
      engine.animate(fixture.tree, fixture.panel, DG_PROP_WIDTH, PropValue::length(0.0F),
                     PropValue::length(100.0F), 1000, dg::kCurveLinear);
  engine.tick(fixture.tree, AnimTime{0});
  engine.tick(fixture.tree, AnimTime{200});
  CHECK(fixture.tree.box(fixture.panel).width == std::optional<int>{20});

  CHECK(engine.cancel(handle) == AnimControlStatus::kOk);
  CHECK_FALSE(engine.has_active());
  CHECK_FALSE(engine.is_active(handle));

  // Ticking further does nothing: the slot is gone, not merely finished.
  engine.tick(fixture.tree, AnimTime{5000});
  CHECK(fixture.tree.box(fixture.panel).width == std::optional<int>{20});

  const std::vector<dg::AnimEvent> events = engine.poll_events();
  REQUIRE(events.size() == 1);
  CHECK(events[0].kind == AnimEventKind::kCancelled);
  CHECK(events[0].handle == handle);
}

TEST_CASE("AnimHandle: a stale handle (reused generation) is reported, never acted on") {
  Fixture fixture;
  AnimationEngine engine;
  const AnimHandle first =
      engine.animate(fixture.tree, fixture.panel, DG_PROP_WIDTH, PropValue::length(0.0F),
                     PropValue::length(100.0F), 100, dg::kCurveLinear);
  CHECK(engine.cancel(first) == AnimControlStatus::kOk);

  // A second call on the SAME handle after it was freed: stale, not a
  // silent no-op success.
  CHECK(engine.cancel(first) == AnimControlStatus::kStaleHandle);
  CHECK(engine.pause(first) == AnimControlStatus::kStaleHandle);
  CHECK(engine.reverse(first) == AnimControlStatus::kStaleHandle);
  CHECK(engine.resume(first) == AnimControlStatus::kStaleHandle);

  // A NEW animation reuses the freed slot's index (the free list is LIFO
  // with one entry) but not its generation - the old handle must not
  // control the new animation.
  const AnimHandle second =
      engine.animate(fixture.tree, fixture.panel, DG_PROP_HEIGHT, PropValue::length(0.0F),
                     PropValue::length(50.0F), 100, dg::kCurveLinear);
  CHECK(second.index == first.index);
  CHECK_FALSE(second.generation == first.generation);
  CHECK(engine.pause(first) == AnimControlStatus::kStaleHandle);
  CHECK(engine.pause(second) == AnimControlStatus::kOk);
}

TEST_CASE("explicit animate(): a non-interpolatable type jumps immediately, no handle") {
  Fixture fixture;
  AnimationEngine engine;
  const AnimHandle handle = engine.animate(
      fixture.tree, fixture.panel, DG_PROP_OVERFLOW, PropValue::option(DG_OVERFLOW_VISIBLE),
      PropValue::option(DG_OVERFLOW_CLIP), 500, dg::kCurveLinear);
  CHECK(handle == AnimHandle{});
  CHECK_FALSE(engine.has_active());
  // Applied immediately through dg::set_prop(), with no tick() at all.
  CHECK(fixture.tree.render().style(fixture.panel).overflow == dg::Overflow::kClip);
}

TEST_CASE("implicit transition: the first trigger reads the node's own current value") {
  Fixture fixture;
  AnimationEngine engine;
  // Fixture builds the panel with fill == 0xFF000000 already.
  engine.set_transition(fixture.panel, DG_PROP_BACKGROUND_COLOR, 200, dg::kCurveLinear);

  const PropWrite write =
      engine.set_value(fixture.tree, fixture.panel, DG_PROP_BACKGROUND_COLOR,
                       PropValue::color(Color::from_argb(0xFFFFFFFF)));
  CHECK(write.status == PropStatus::kApplied);
  // Not applied yet - it lands on the next tick(), matching
  // set_scroll_offset()'s own "the caller sees it on the next frame" shape.
  CHECK(fixture.tree.render().style(fixture.panel).fill.argb() == 0xFF000000U);
  CHECK(engine.has_active());

  engine.tick(fixture.tree, AnimTime{0});
  engine.tick(fixture.tree, AnimTime{100});
  CHECK(fixture.tree.render().style(fixture.panel).fill.argb() == 0xFF808080U);

  engine.tick(fixture.tree, AnimTime{200});
  CHECK(fixture.tree.render().style(fixture.panel).fill.argb() == 0xFFFFFFFFU);
  CHECK_FALSE(engine.has_active());

  const std::vector<dg::AnimEvent> events = engine.poll_events();
  REQUIRE(events.size() == 1);
  CHECK(events[0].kind == AnimEventKind::kCompleted);
  // No host-visible handle exists for a transition - AnimHandle{} names it.
  CHECK(events[0].handle == AnimHandle{});
  CHECK(events[0].node == fixture.panel);
}

TEST_CASE("implicit transition: retargeting mid-flight starts from the CURRENT value") {
  Fixture fixture;
  AnimationEngine engine;
  engine.set_transition(fixture.panel, DG_PROP_BACKGROUND_COLOR, 200, dg::kCurveLinear);

  // 0x000000 -> 0xFFFFFF over 200ms; capture it exactly halfway.
  engine.set_value(fixture.tree, fixture.panel, DG_PROP_BACKGROUND_COLOR,
                   PropValue::color(Color::from_argb(0xFFFFFFFF)));
  engine.tick(fixture.tree, AnimTime{0});
  engine.tick(fixture.tree, AnimTime{100});
  const std::uint32_t halfway = fixture.tree.render().style(fixture.panel).fill.argb();
  CHECK(halfway == 0xFF808080U);

  // Retarget to a THIRD colour before the first transition finished. The
  // defect this guards against: restarting from the ORIGINAL 0x000000
  // instead of from `halfway` - which would make the panel visibly jump
  // backward toward black for one frame before heading to red.
  engine.set_value(fixture.tree, fixture.panel, DG_PROP_BACKGROUND_COLOR,
                   PropValue::color(Color::from_argb(0xFFFF0000)));

  // Same instant as the retarget: zero elapsed time on the NEW slot, so the
  // colour must be exactly where it was the instant before - `halfway` -
  // not `halfway` blended toward black, and not the original black either.
  engine.tick(fixture.tree, AnimTime{100});
  CHECK(fixture.tree.render().style(fixture.panel).fill.argb() == halfway);

  // Let the retargeted transition finish, from `halfway` toward pure red.
  engine.tick(fixture.tree, AnimTime{300});
  CHECK(fixture.tree.render().style(fixture.panel).fill.argb() == 0xFFFF0000U);
  CHECK_FALSE(engine.has_active());
}

TEST_CASE("implicit transition: an undeclared (node, prop) writes straight through") {
  Fixture fixture;
  AnimationEngine engine;
  const PropWrite write =
      engine.set_value(fixture.tree, fixture.panel, DG_PROP_BACKGROUND_COLOR,
                       PropValue::color(Color::from_argb(0xFFFF0000)));
  CHECK(write.status == PropStatus::kApplied);
  // No transition declared: applies immediately, exactly like dg::set_prop().
  CHECK(fixture.tree.render().style(fixture.panel).fill.argb() == 0xFFFF0000U);
  CHECK_FALSE(engine.has_active());
}

TEST_CASE("reduced motion: duration collapses to zero, the same state machine still runs") {
  Fixture fixture;
  AnimationEngine engine;
  engine.set_reduced_motion(true);
  CHECK(engine.reduced_motion());

  const AnimHandle handle =
      engine.animate(fixture.tree, fixture.panel, DG_PROP_WIDTH, PropValue::length(0.0F),
                     PropValue::length(100.0F), 500, dg::kCurveLinear);
  // Alive immediately - has_active() is true the instant the slot exists,
  // exactly as it is for an ordinary animation, which is what lets the
  // on-demand frame loop notice it needs a frame at all. Nothing has been
  // written to the tree yet either way - only tick() ever writes a value,
  // reduced motion or not - so the box still reads whatever Fixture built it
  // with (100), not this animation's own `from` (0).
  CHECK(engine.has_active());
  CHECK(fixture.tree.box(fixture.panel).width == std::optional<int>{100});

  // ONE tick, at any AnimTime, finishes it - the duration is zero, so there
  // is no partial frame to observe, but the value still only lands through
  // tick(), never inside animate() itself.
  engine.tick(fixture.tree, AnimTime{0});
  CHECK(fixture.tree.box(fixture.panel).width == std::optional<int>{100});
  CHECK_FALSE(engine.has_active());

  const std::vector<dg::AnimEvent> events = engine.poll_events();
  REQUIRE(events.size() == 1);
  CHECK(events[0].kind == AnimEventKind::kCompleted);
  CHECK(events[0].handle == handle);
}

TEST_CASE("reduced motion also collapses an implicit transition's duration") {
  Fixture fixture;
  AnimationEngine engine;
  engine.set_transition(fixture.panel, DG_PROP_OPACITY, 400, dg::kCurveEaseInOut);
  engine.set_reduced_motion(true);
  engine.set_value(fixture.tree, fixture.panel, DG_PROP_OPACITY, PropValue::number(0.2F));
  engine.tick(fixture.tree, AnimTime{0});
  CHECK(static_cast<double>(fixture.tree.render().style(fixture.panel).opacity) ==
        doctest::Approx(0.2));
  CHECK_FALSE(engine.has_active());
}

TEST_CASE("left/top interpolate as ordinary lengths on an absolute-positioned child") {
  Fixture fixture;
  AnimationEngine engine;
  BoxStyle child_box;
  const NodeId child = fixture.tree.add_child(fixture.absolute, child_box, NodeStyle{});
  fixture.tree.layout();
  const PropWrite initial_left =
      dg::set_prop(fixture.tree, child, DG_PROP_LEFT, PropValue::number(0.0F));
  CHECK(initial_left.ok());

  engine.set_transition(child, DG_PROP_LEFT, 100, dg::kCurveLinear);
  engine.set_value(fixture.tree, child, DG_PROP_LEFT, PropValue::number(40.0F));
  engine.tick(fixture.tree, AnimTime{0});
  engine.tick(fixture.tree, AnimTime{50});
  CHECK(fixture.tree.box(child).left == std::optional<int>{20});
}
