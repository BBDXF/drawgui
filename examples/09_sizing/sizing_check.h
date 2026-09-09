// The headless half of examples/09_sizing: what has to be true of the scene
// that is on screen, checked without a display so CTest can run it.
//
// Six claims, and the first is the one the whole slice rests on:
//
//   1. EXACTLY ONCE. Every node of the demo scene is entered once and
//      recomputed once per full pass. doc/sizing.md section 1 claims this
//      slice adds no second measurement of anything, and LayoutStats reports
//      entered and recomputed separately, so the claim is directly checkable
//      against the tree a human is watching rather than only against a tree
//      written to be easy to check.
//
//   2. THE LADDER CROSSES THE TRANSITION. The widths are required to produce
//      at least one surplus frame AND at least one deficit frame, and the
//      heights to produce at least three distinct thumbnail sizes. A ladder
//      that never crossed either would be one arrangement checked six times,
//      which is the failure doc/wrapping.md records against a scene that
//      lacks the shape.
//
//   3. THE DEFICIT IS EXACT, AND ORDERED BY WEIGHT. In deficit the buttons
//      must fill the row's content box to the pixel - no pixel invented, none
//      lost - and must be strictly ordered by their shrink weights, with the
//      gaps between them non-trivial so that "ordered" is not satisfied by
//      three equal widths.
//
//   4. THE RATIO HOLDS ON THE DERIVED AXIS. Each thumbnail's height is the
//      row's content height and its width is exactly what the ratio makes of
//      it, recomputed here rather than read back.
//
//   5. FILLING THE MAIN AXIS REACHES THE EDGE. The footer's last item ends
//      exactly at the row's content right edge, which it can only do because
//      the row filled.
//
//   6. HIT TESTING FOLLOWS. Shrinking moves many nodes at once, so every
//      pixel of the toolbar and the chip row is compared against a
//      paint-order oracle that shares no code with hit_test().
//
// Plus the standing gate: incremental layout and full layout agree on every
// node's bounds, across a resize script that runs both directions through the
// transition, and the frames they paint are byte-identical.

#pragma once

#include <iosfwd>

namespace sizing_check {

int run(std::ostream& out);

}  // namespace sizing_check
