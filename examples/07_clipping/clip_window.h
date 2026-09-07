// The window half of examples/07_clipping.
//
// Event-driven, like the widget demo: nothing animates unless `--toggle-ms`
// asks it to, so the repaint counter sitting still while the pointer sits
// still is part of what is being shown.
//
// `probe()` is what makes the hit-test half demonstrable rather than
// assertable. It lays the scene out at a given size and answers one point,
// which lets a session on a real display ask "is this spot clickable?" at a
// pixel a screenshot has just shown to be clipped away, and get the answer
// from the same code the window uses.

#pragma once

#include <iosfwd>
#include <string>

#include "drawgui/base/pixel_geometry.h"

namespace clip_window {

struct Settings {
  dg::PixelSize size{1180, 520};

  // Flips every panel between `clip` and `visible` on a timer, so the
  // difference is visible in a screenshot pair taken without any input.
  int toggle_ms = 0;

  int run_ms = 0;
};

int run(const Settings& settings, std::ostream& out);
int dump_png(const Settings& settings, const std::string& path, std::ostream& out);
int probe(const Settings& settings, dg::PixelPoint point, std::ostream& out);

}  // namespace clip_window
