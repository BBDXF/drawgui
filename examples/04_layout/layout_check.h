// The three things this demo can do without a display: prove itself correct,
// say how much of the tree it re-laid-out, and time itself.
//
// Kept out of main.cpp because none of them needs a window, and because the
// first is the important one - a layout engine that is fast and wrong is
// worse than one that is slow.

#pragma once

#include <cstddef>
#include <iosfwd>

#include "drawgui/base/pixel_geometry.h"

namespace layout_check {

struct Config {
  dg::PixelSize viewport;
  int frames = 240;
  bool rounded_containers = false;
};

// Replays the mutation script twice - once laying out only what was marked
// dirty, once laying out the whole tree - and requires every node's computed
// bounds to be identical after every frame. Returns false on the first frame
// that disagrees, having reported which node and by how much.
[[nodiscard]] bool verify_layout(const Config& config, std::ostream& out);

// Resizes the scene through a ladder of widths and, at every one, requires hit
// testing to agree with a brute-force paint-order oracle over the wrapping
// band - the region that has just re-broken into a different number of runs.
//
// It is a separate check from verify_layout because it fails for a different
// reason. Bounds identity says the tree computed the right rectangles;
// this says the thing that answers "what is under the pointer" is reading
// those rectangles rather than ones it cached before the reflow. Wrapping
// moves many nodes at once, which is exactly when a stale hit rectangle stops
// being a theoretical bug.
[[nodiscard]] bool verify_hit_after_rewrap(const Config& config, std::ostream& out);

// One row per class of change: how many nodes were entered, how many were
// actually recomputed, how many moved, how many pixels the movement damaged,
// and how many pixels a repaint of that damage actually touched. The last two
// differ because a damage rectangle grows to swallow whole rounded nodes, so
// the table is printed once with square containers and once with rounded ones
// and the gap between them is what a corner radius costs.
void report_scope(const Config& config, std::ostream& out);

// Layout only, at four viewport sizes and then at four tree sizes. Painting
// is sub-step 1's subject and is measured there; this is the cost of deciding
// where things go.
//
// The tree-size ladder is the one that matters. At the demo scene's 92 nodes
// a full layout is already cheap, so the ratio understates what incremental
// layout is for; the question is how the two curves diverge, and that needs
// trees the demo does not have.
void bench(int frames, std::ostream& out);

}  // namespace layout_check
