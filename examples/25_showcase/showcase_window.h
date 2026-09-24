// The interactive/scripted half of examples/25_showcase.

#pragma once

#include <iosfwd>
#include <string>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/window/window_manager.h"

namespace showcase_window {

struct Settings {
  dg::PixelSize size{900, 560};
  int run_ms = 0;
  bool script = false;
  int tooltip_delay_ms = 400;
  std::string font_dir = "/usr/share/fonts";
};

int run(const Settings& settings, std::ostream& out);
int dump_png(const Settings& settings, const std::string& path, std::ostream& out);
int idle_probe(const Settings& settings, int idle_probe_ms, std::ostream& out);

// Test-only seam for --verify-showcase: the pixel size Runner's constructor
// actually uses for its first scene/tree/surface build - see the .cpp's
// own definition comment for why this is exported rather than file-local.
dg::PixelSize initial_viewport(dg::WindowManager& manager, dg::WindowId window,
                               const Settings& settings);

}  // namespace showcase_window
