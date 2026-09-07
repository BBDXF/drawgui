// The window half of examples/08_opacity.
//
// Event-driven like its neighbours, with one addition: `--fade-ms` animates
// the fourth panel's opacity from 1 to 0 and back on a timer. That is the one
// thing in this demo a still screenshot cannot show - a fade is exactly the
// change most likely to leave a rim of the previous frame behind at the
// layer's boundary, and watching it run is what makes the absence of that rim
// visible rather than merely asserted.
//
// `probe()` answers one point offscreen, reporting the pixel AND the hit
// together, which is how "invisible but still clickable" is demonstrated at a
// coordinate a screenshot has just shown to be blank.

#pragma once

#include <iosfwd>
#include <string>

#include "drawgui/base/pixel_geometry.h"

namespace opacity_window {

struct Settings {
  dg::PixelSize size{1560, 460};

  // Milliseconds for one full fade of the fourth panel, out and back. Zero
  // leaves it opaque, so nothing moves unless it is asked to.
  int fade_ms = 0;

  int run_ms = 0;

  // Fades the fourth panel to exactly this before rendering, for a screenshot
  // that has to catch a particular moment rather than whatever the timer was
  // doing. Negative leaves it alone.
  float freeze_at = -1.0F;
};

int run(const Settings& settings, std::ostream& out);
int dump_png(const Settings& settings, const std::string& path, std::ostream& out);
int probe(const Settings& settings, dg::PixelPoint point, std::ostream& out);

}  // namespace opacity_window
