// The headless half of examples/17_animation: what has to be true of the
// three panels driving dg::AnimationEngine over the REAL demo scene, checked
// without a display.
//
// Four claims:
//
//   1. slide: explicit animate() on `left`, hand-derived ease-in-out values
//      at exact ticks, AND the pixel moves - not just the BoxStyle field,
//      which is what a real caller (and a real screen) actually sees.
//   2. hover: the implicit transition's first trigger AND the retarget-mid-
//      flight case, both against the SAME node this scene draws.
//   3. caret: the four-phase blink cycles through completion events in the
//      right order, and the reduced-motion CLIENT pattern (freeze solid,
//      never loop) holds under many ticks.
//   4. has_active(): true while anything is running, false once it is not -
//      the signal design.md section 5.15.1's on-demand frame loop reads.

#pragma once

#include <iosfwd>

namespace anim_check {

int run(std::ostream& out);

}  // namespace anim_check
