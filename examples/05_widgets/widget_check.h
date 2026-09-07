// What makes the widget layer trustworthy: three checks that need no display.
//
// Sub-steps 1 and 2 both established the technique and both were vindicated by
// it, so it is reused rather than reinvented. The additions here are what
// interaction brings that neither of them had:
//
//   IDENTITY - a scripted pointer path is driven through the SAME dispatch()
//   the window loop uses, on two copies of the scene. One repaints only the
//   damage the interaction produced; the other repaints everything. The two
//   framebuffers must be byte-identical after EVERY event. A hover that
//   forgets to invalidate a border, or a click whose label grew wider than the
//   rectangle it damaged, fails here and nowhere else.
//
//   HIT - every pixel of the real demo scene, against a paint-order oracle
//   rebuilt from the tree's parent links by this file, and against the pixels
//   the rasterizer actually produced. Run at several viewport sizes, because
//   a reflow moves every widget and stale geometry is the classic interaction
//   bug.
//
// The measurement that justified making a text node clip-atomic lives in
// clip_probe.cpp rather than here, and the reason is worth recording: it CANNOT
// be run through the render tree. A text node is already clip-atomic, so the
// tree grows every damage rectangle to swallow it whole before any clip can cut
// it - a probe written at this level reports zero differing pixels and has
// measured nothing but the rule it was supposed to justify.

#pragma once

#include <cstdint>
#include <iosfwd>

#include "drawgui/base/pixel_geometry.h"

namespace widget_check {

struct Config {
  dg::PixelSize viewport{960, 720};
  bool rounded_controls = false;
  bool rounded_containers = false;
};

// Byte identity between interaction-driven repaint and full repaint, over a
// scripted pointer path, at both container styles.
[[nodiscard]] bool verify_interaction(const Config& config, std::ostream& out);

// Every pixel of the demo scene, at several viewport sizes, against two
// independent oracles.
[[nodiscard]] bool verify_hit_testing(const Config& config, std::ostream& out);

// What one interaction costs in damaged pixels, square containers versus
// rounded. Reported, never gated.
void report_damage_cost(const Config& config, std::ostream& out);

// Every interactive widget and where it is, so that a human - or a screenshot
// script driving the X server from outside this process - can aim at one by
// name instead of by guessing at a coordinate.
void report_widgets(const Config& config, std::ostream& out);

}  // namespace widget_check
