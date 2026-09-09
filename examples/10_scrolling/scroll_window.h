// The window driver for examples/10_scrolling: pump(), wheel and drag
// routing, and the offscreen modes shared with --dump-png and --probe.

#pragma once

#include <optional>
#include <ostream>
#include <string>

#include "drawgui/base/pixel_geometry.h"

namespace scroll_window {

struct Settings {
  dg::PixelSize size{900, 760};
  int run_ms = 0;

  // Applied once, before the first frame - what lets --dump-png and --probe
  // demonstrate a scrolled or overscrolled state without needing a live
  // window to drag.
  int preset_vertical = 0;
  int preset_horizontal = 0;
};

int run(const Settings& settings, std::ostream& out);
int dump_png(const Settings& settings, const std::string& path, std::ostream& out);
int probe(const Settings& settings, dg::PixelPoint point, std::ostream& out);

// Opens a REAL window and drives it through warp_pointer()/post_wheel() -
// the same route examples/05_widgets uses to prove a scripted run exercises
// the actual event queue rather than calling the scroll machinery directly.
// Prints what happened; not a CTest entry, matching every other real-window
// mode in this project ("It needs a display, so there is no CTest entry").
int script(const Settings& settings, std::ostream& out);

}  // namespace scroll_window
