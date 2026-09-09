// The window driver for examples/11_form_controls: pump(), click routing for
// the checkbox/radio half and drag routing for the slider half, plus the
// offscreen modes shared with --dump-png and --probe.

#pragma once

#include <optional>
#include <ostream>
#include <string>

#include "drawgui/base/pixel_geometry.h"

namespace form_window {

struct Settings {
  dg::PixelSize size{760, 420};
  int run_ms = 0;

  // Applied once, before the first frame - lets --dump-png/--probe
  // demonstrate a particular state without a live window to click or drag.
  int preset_checked_radio_a = -1;  // index into radio_group_a, or -1 for none
  float preset_volume = -1.0F;      // negative means "leave the built-in default"
  bool preset_checkbox = false;
};

int run(const Settings& settings, std::ostream& out);
int dump_png(const Settings& settings, const std::string& path, std::ostream& out);
int probe(const Settings& settings, dg::PixelPoint point, std::ostream& out);

// Opens a REAL window and drives it through warp_pointer()/post_pointer_button()
// - the same route examples/05_widgets and examples/10_scrolling use to prove
// a scripted run exercises the actual event queue rather than calling the
// widget machinery directly: clicks the checkbox, selects a radio option,
// and drags the volume slider. Prints what happened; not a CTest entry,
// matching every other real-window mode in this project.
int script(const Settings& settings, std::ostream& out);

}  // namespace form_window
