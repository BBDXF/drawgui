// The window half of examples/09_sizing.
//
// Event-driven, and deliberately without an animation flag: everything this
// demo has to show is driven by the window's own size, so the interesting
// motion is the drag itself. The readout prints the toolbar's three widths and
// the thumbnails' derived sizes on every resize, so what is on screen and what
// the arithmetic says can be compared without a colour picker.

#pragma once

#include <iosfwd>
#include <string>

#include "drawgui/base/pixel_geometry.h"

namespace sizing_window {

struct Settings {
  dg::PixelSize size{1280, 700};
  int run_ms = 0;
};

int run(const Settings& settings, std::ostream& out);
int dump_png(const Settings& settings, const std::string& path, std::ostream& out);
int probe(const Settings& settings, dg::PixelPoint point, std::ostream& out);

}  // namespace sizing_window
