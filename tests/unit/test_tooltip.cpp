// dg::HoverTimer's own job: "how long has the pointer sat continuously over
// one widget" - 7-5b's prerequisite for Tooltip (doc/menus.md section 6.2).
// No window, no surface: a HoverTimer is a plain value tested exactly the
// way dg::Focus/dg::Interaction already are.

#include <optional>

#include <doctest/doctest.h>

#include "drawgui/anim/clock.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/widget/tooltip.h"

namespace {

using dg::AnimTime;
using dg::HoverTimer;
using dg::NodeId;

TEST_CASE("a fresh HoverTimer is never ready - nothing is being timed") {
  HoverTimer timer;
  CHECK_FALSE(dg::hover_ready(timer, AnimTime{0}, 400));
  CHECK_FALSE(timer.target.has_value());
}

TEST_CASE("update_hover_timer() records the target and start time on first hover") {
  HoverTimer timer;
  const NodeId target{3};
  dg::update_hover_timer(timer, target, AnimTime{1000});
  CHECK(timer.target == target);
  CHECK(timer.since == AnimTime{1000});
}

TEST_CASE("hover_ready() is false before the delay and true at/after it") {
  HoverTimer timer;
  const NodeId target{3};
  dg::update_hover_timer(timer, target, AnimTime{1000});
  CHECK_FALSE(dg::hover_ready(timer, AnimTime{1399}, 400));
  CHECK(dg::hover_ready(timer, AnimTime{1400}, 400));
  CHECK(dg::hover_ready(timer, AnimTime{9999}, 400));
}

TEST_CASE(
    "a repeated update_hover_timer() call with the SAME target is a no-op - it does not "
    "reset an already-running timer") {
  HoverTimer timer;
  const NodeId target{3};
  dg::update_hover_timer(timer, target, AnimTime{1000});
  dg::update_hover_timer(timer, target, AnimTime{1200});
  CHECK(timer.since == AnimTime{1000});
}

TEST_CASE(
    "leaving (hovered becomes nullopt) clears the timer, and hover_ready() reports false "
    "afterward") {
  HoverTimer timer;
  const NodeId target{3};
  dg::update_hover_timer(timer, target, AnimTime{1000});
  dg::update_hover_timer(timer, std::nullopt, AnimTime{2000});
  CHECK_FALSE(timer.target.has_value());
  CHECK_FALSE(dg::hover_ready(timer, AnimTime{5000}, 400));
}

TEST_CASE(
    "re-entering the SAME widget after leaving restarts the delay from zero - it does "
    "not inherit time accumulated before the gap") {
  HoverTimer timer;
  const NodeId target{3};
  dg::update_hover_timer(timer, target, AnimTime{1000});
  CHECK(dg::hover_ready(timer, AnimTime{5000}, 400));
  dg::update_hover_timer(timer, std::nullopt, AnimTime{5000});
  dg::update_hover_timer(timer, target, AnimTime{5001});
  CHECK_FALSE(dg::hover_ready(timer, AnimTime{5001 + 399}, 400));
  CHECK(dg::hover_ready(timer, AnimTime{5001 + 400}, 400));
}

TEST_CASE(
    "hovering a DIFFERENT widget restarts the timer onto it, never accumulating against "
    "the first") {
  HoverTimer timer;
  const NodeId first{3};
  const NodeId second{4};
  dg::update_hover_timer(timer, first, AnimTime{1000});
  dg::update_hover_timer(timer, second, AnimTime{1300});
  CHECK(timer.target == second);
  CHECK(timer.since == AnimTime{1300});
  CHECK_FALSE(dg::hover_ready(timer, AnimTime{1300 + 399}, 400));
}

}  // namespace
