// AnimTime - the single seam that makes the animation system testable.
//
// design.md section 5.16.1 requires the clock to be owned by C++ and driven
// by vsync; this project's own acceptance bar (byte-exact goldens, hand-
// derived assertions) requires every test to be deterministic. Wall-clock
// time is neither: two runs of the same test sleep for different real
// durations depending on scheduler noise, which is exactly the kind of
// nondeterminism this project has refused everywhere else (the whole point
// of `--script` driving real SDL events instead of asserting on timing, and
// of every `verify_demo_scene` building one exact scene rather than sampling
// a running one).
//
// THE SEAM IS A VALUE, NOT A VIRTUAL INTERFACE. This project has zero
// `virtual` in src/ and include/ across twelve slices, and every previous
// seam was built by making the varying thing an explicit PARAMETER rather
// than an injected strategy object - WindowManager::warp_pointer() takes an
// x/y instead of abstracting "an input device", RenderTree::set_scroll_offset
// takes an offset instead of abstracting "a scroll source". AnimationEngine
// follows the identical pattern: AnimTime is a plain value, and
// AnimationEngine::tick() takes one as an ordinary argument. Production code
// calls steady_anim_time(); a test calls tick() with any AnimTime it likes,
// advancing by exact, hand-chosen deltas with no sleep anywhere. There is no
// abstract "Clock" type to inherit from and nothing to mock - the whole
// "virtual for testability" problem does not arise because nothing about
// AnimationEngine reaches out to find the time itself.
#pragma once

#include <cstdint>

namespace dg {

// Milliseconds since an arbitrary, monotonic origin. Never wall-clock time of
// day - only differences between two AnimTime values are meaningful, which is
// what a monotonic source guarantees and a wall clock does not (a system
// clock adjustment must never make an animation run backwards).
struct AnimTime {
  std::int64_t ms = 0;

  friend bool operator==(AnimTime, AnimTime) = default;
};

[[nodiscard]] constexpr bool operator<(AnimTime lhs, AnimTime rhs) {
  return lhs.ms < rhs.ms;
}

// The real clock. This is the ONLY function in the animation system that
// reads an actual timer - src/anim/clock.cpp is three lines around
// std::chrono::steady_clock, chosen over SDL's own timer because the engine
// takes an AnimTime by value and never touches SDL (include/ has zero SDL
// leakage), and over a frame counter because a frame counter cannot express
// "this animation takes 200ms" without also fixing a frame rate, which the
// on-demand frame loop (design.md section 5.15.1) deliberately does not have.
[[nodiscard]] AnimTime steady_anim_time();

}  // namespace dg
