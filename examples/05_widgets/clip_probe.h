// The measurement that decides which nodes may be cut by a damage rectangle.
//
// Separate from widget_check.h because it is the only part of this example
// that talks to Skia outside the readout, and because it must NOT be compiled
// into the headless test binaries - they do not link Skia, and they should
// not: this reports a property of the rasterizer, not of drawgui, so it is
// printed for a human rather than asserted in CI. A number that moved because
// Skia changed is something to read and think about, not a red build.

#pragma once

#include <iosfwd>
#include <string>

namespace clip_probe {

void report(std::ostream& out, const std::string& font_dir);

}  // namespace clip_probe
