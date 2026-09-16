// The interactive/headless-render half of examples/24_theme_package -
// mirrors examples/18_theme/theme_window.h's shape exactly, parameterized
// by an external package directory instead of the compiled-in builtin
// theme.
#pragma once

#include <ostream>
#include <string>

#include "drawgui/base/pixel_geometry.h"

namespace theme_package_window {

struct Settings {
  dg::PixelSize size = dg::PixelSize{560, 220};
  int run_ms = 0;
  std::string package_dir;
};

int run(const Settings& settings, std::ostream& out);
int dump_png(const Settings& settings, const std::string& path, std::ostream& out);

// Blocks in WindowManager::pump() for `idle_probe_ms` with the package
// loaded and nothing reloading - the measured answer to "does a mechanism
// that CAN hot-reload cost anything while it is not being asked to":
// design.md section 5.15.1's "wait_events() blocks, CPU 0%" claim,
// re-verified for this slice's own new machinery the way 7-5b's own
// idle_probe() already did for HoverTimer.
int idle_probe(const Settings& settings, int idle_probe_ms, std::ostream& out);

}  // namespace theme_package_window
