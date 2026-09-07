// Driving the demo window: real pointer events in, hover and press out, and
// only the pixels that changed redrawn.
//
// The loop is deliberately EVENT-DRIVEN rather than paced. Sub-steps 1 and 2
// animated something every frame because they were measuring throughput; a
// widget demo has nothing to animate, and a GUI that repaints on a timer while
// nobody is touching it is the thing damage tracking exists to avoid. So
// pump() blocks until something happens, and a frame is drawn only when
// something changed. Watching the repaint counter sit still while the pointer
// sits still is part of the demonstration.
//
// A SCRIPTED MODE drives the same window through the same queue: warp the real
// pointer, push a real button event, and let pump() deliver both. Nothing
// calls the state machine directly, and nothing skips hit testing - a scripted
// pass that did would be exercising a path no user can reach.

#pragma once

#include <cstddef>
#include <iosfwd>
#include <string>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/render/damage.h"

namespace demo {

struct Settings {
  dg::PixelSize size{1120, 800};
  std::size_t max_damage_rects = dg::DamageRegion::kDefaultMaxRects;

  bool rounded_controls = false;
  bool rounded_containers = false;

  // Walks the pointer over every widget, clicks each one, and exercises the
  // cancel and re-enter paths - through the platform's own event queue.
  bool script = false;

  // How long to dwell on each scripted step. Long enough that a human can
  // watch it happen, and long enough that the display server is not being
  // asked to coalesce.
  int script_step_ms = 90;

  int run_ms = 0;
  std::string font_dir = "/usr/share/fonts";
};

int run(const Settings& settings, std::ostream& out);

}  // namespace demo
