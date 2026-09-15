// The headless half of examples/13_image: what has to be true of the scene
// that is on screen, checked without a display so CTest can run it.
//
// Two claims:
//
//   1. every fit mode paints the hand-computed pixels image_scene.h's own
//      geometry predicts - the same oracle technique
//      tests/unit/test_image.cpp already uses, run here against the exact
//      scene the demo window draws rather than a scene built only for the
//      test.
//
//   2. swapping the decoded source for one of a different pixel size, on
//      THIS scene, relays out nothing - LayoutStats numbers, not an
//      assertion that geometry happens to match.

#pragma once

#include <iosfwd>

namespace image_check {

int run(std::ostream& out);

}  // namespace image_check
