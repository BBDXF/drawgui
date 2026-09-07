// The scene examples/03_damage_repaint animates, and the script that animates
// it.
//
// It lives here rather than inside main.cpp because tests/unit compiles this
// same file: the byte-identical damage-versus-full verification is worth much
// more when it runs the scene that is actually on screen than when it runs a
// second scene written to be easy to verify. It needs no window and no SDL,
// only the render tree, which is what lets that verification be a CTest entry
// on a headless machine.
//
// The script is deterministic - frame number in, tree state out, no clock and
// no randomness - because the verification replays it twice and compares
// bytes. Anything time-dependent here would turn that comparison into a coin
// toss.
//
// Between them the four mutations cover every damage case the design notes
// call out: a node changing colour in place, a node moving so that both its
// old and its new bounds are stale, a node changing underneath an overlapping
// node that must be redrawn on top of it, and two far-apart nodes dirty in
// the same frame - which is the case that separates a rectangle list from a
// single union.

#pragma once

#include "drawgui/render/render_tree.h"

namespace scene {

// The nodes the animation touches. Everything else is drawn once and never
// changes, which is what a real application's chrome does and what makes the
// damage numbers meaningful.
struct Handles {
  // Changes colour in place, and sits underneath `badge`.
  dg::NodeId pulse;

  // Overlaps `pulse` and never changes. If a repaint of `pulse` fails to
  // redraw this node on top, the badge gets painted over - the classic
  // z-order bug in a damage system.
  dg::NodeId badge;

  // Slides along `marker_track`, so each frame damages where it was as well
  // as where it now is.
  dg::NodeId marker;

  // Near-opposite corners of the content area. Dirty on the same frames, so
  // their bounding box is most of the window while their combined area is
  // tiny.
  dg::NodeId corner_a;
  dg::NodeId corner_b;

  // A flat strip across the bottom that the demo prints its timings onto. It
  // is a tree node rather than something painted beside the tree so that it
  // occludes the scene properly and takes part in damage like anything else;
  // the text itself is drawn over it by the demo, which is why the node is
  // square-cornered and therefore safe to clip anywhere.
  dg::NodeId readout;

  dg::PixelRect marker_track;
};

struct Scene {
  dg::RenderTree tree;
  Handles handles;
};

[[nodiscard]] Scene build(const dg::TreeSpec& spec);

void apply_frame(dg::RenderTree& tree, const Handles& handles, int frame);

}  // namespace scene
