// The headless half of examples/08_opacity: what has to be true of the scene
// that is on screen, checked without a display so CTest can run it.
//
// Four claims, in increasing order of what they cost to get wrong:
//
//   1. GROUP, NOT PER-OBJECT. The two comparable panels must agree at every
//      pixel one chip covers and DISAGREE at every pixel two chips cover, and
//      the grouped panel must show exactly one colour per topmost chip while
//      the per-object one shows more. That last sentence is the machine
//      readable form of "the group fades as one image", and it needs no
//      reference render to state - which matters, because a reference render
//      would be a second copy of the same code.
//
//   2. NESTED FADES MULTIPLY. The effective alpha is recovered from the
//      pixels - given the card colour C, the chip colour K and the observed
//      colour O, `a = (O - C) / (K - C)` - and the nested panel's must be the
//      square of the single panel's. An implementation that applied the outer
//      alpha once, or composited the inner layer twice, lands somewhere else.
//
//   3. A FADE ANIMATES WITHOUT ARTEFACTS. Sixty frames of a real opacity
//      animation through 1 and 0, each one repainted incrementally, compared
//      byte for byte against the same scene repainted whole. This is the
//      on-screen form of tests/unit/test_opacity_damage.cpp, run against the
//      scene a human actually watches.
//
//   4. INVISIBLE IS STILL CLICKABLE. At every pixel of a stage faded to zero,
//      the surface must show the card underneath and hit testing must still
//      name a chip of that stage. That is the decision render_tree.h records,
//      asserted where a person can see both halves at once.
//
// Claims 1 and 2 run at every width in a ladder, and the ladder is required to
// actually vary the geometry - six widths that produced the same layout would
// be six runs of one case, which is the failure doc/wrapping.md records.

#pragma once

#include <iosfwd>

namespace opacity_check {

int run(std::ostream& out);

}  // namespace opacity_check
