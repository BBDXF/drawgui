// The headless half of examples/07_clipping: what has to be true of the scene
// that is on screen, checked without a display so CTest can run it.
//
// Three claims, in increasing order of what they cost to get wrong:
//
//   1. hit testing agrees with a paint-order oracle at EVERY pixel, at every
//      window width in the ladder. The oracle re-derives the ancestor clip
//      chain from the tree's parent links and its own containment arithmetic,
//      so it is a second implementation of the rule rather than a reading of
//      the first.
//
//   2. hit testing agrees with the RASTERIZER at every pixel whose colour is
//      unambiguous. This is the literal statement of "what you see is what you
//      click", and it is the one that catches painting and hit testing being
//      wrong together in the same way - which claim 1 cannot see. The rounded
//      panel produces a band of blended pixels along its curve; those are
//      excluded and COUNTED, and the count is required to be small, so the
//      exclusion cannot quietly grow to cover a real defect.
//
//   3. the ladder actually varies the thing under test. Six widths that all
//      produced the same overflow would be six runs of one case, which is the
//      failure doc/wrapping.md records for the wrapping ladder.

#pragma once

#include <iosfwd>

namespace clip_check {

int run(std::ostream& out);

}  // namespace clip_check
