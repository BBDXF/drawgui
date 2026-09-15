// The headless half of examples/18_theme: what has to be true of the scene
// on screen, checked without a display so CTest can run it - matching
// examples/16_complex_properties' own cprops_check.h shape.
//
// Three claims:
//
//   1. every token-bound panel's pixels match the LOADED THEME's own
//      resolved value for the CURRENT variant - not a literal this check
//      invented, but dg::Theme::color_value()/int_value() read back
//      through the exact same accessor theme_scene.cpp bound against.
//   2. switching light->dark moves every one of those pixels to the
//      OTHER variant's value, and switching back returns to the first -
//      P3's own "light/dark 运行时切换" acceptance bar, proven on pixels.
//   3. the LayoutStats measurement this slice's task asks for: a plain
//      variant switch (colour tokens only, since the shipped theme's int
//      tokens are variant-independent by design - design.md section
//      5.7.4) costs ZERO relayout, checked directly against LayoutStats
//      rather than only against pixels.

#pragma once

#include <iosfwd>

namespace theme_check {

int run(std::ostream& out);

}  // namespace theme_check
