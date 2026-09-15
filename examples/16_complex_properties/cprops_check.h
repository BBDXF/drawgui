// The headless half of examples/16_complex_properties: what has to be true
// of the scene that is on screen, checked without a display so CTest can run
// it.
//
// Three claims, one per property the channel lands:
//
//   1. the gradient panel's pixels match a HAND-DERIVED linear interpolation
//      formula, sampled at the exact pixel-centre coordinates Skia samples
//      at (x + 0.5) - not read off a previous run, computed independently.
//   2. the shadow panel casts an EXACT-colour sliver past its own declared
//      bounds (the hard-edged construction cprops_scene.h explains), and
//      paints exactly the background everywhere the shifted, unblurred
//      shadow rectangle does not reach - the two facts together are the
//      proof that a shadow paints outside the node's box, in both directions
//      (it does reach where geometry says it should; it does NOT reach
//      where geometry says it should not).
//   3. the image panel shows the decoded source's own two halves, proving
//      dg::set_image() reaches the same paint path RenderTree::set_image()
//      already proved in slice 5-1.
//
// Plus one non-visual claim: dg::set_transform() reports kUnsupported, which
// cprops_scene::build() already called once; this file only reads the
// result rather than calling it a second time.

#pragma once

#include <iosfwd>

namespace cprops_check {

int run(std::ostream& out);

}  // namespace cprops_check
