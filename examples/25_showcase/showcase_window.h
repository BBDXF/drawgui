// The interactive/scripted half of examples/25_showcase.

#pragma once

#include <iosfwd>
#include <string>

#include "drawgui/base/pixel_geometry.h"

namespace showcase_window {

struct Settings {
  dg::PixelSize size{900, 560};
  int run_ms = 0;
  bool force_overlay = false;
  bool script = false;
  int tooltip_delay_ms = 400;
  std::string font_dir = "/usr/share/fonts";
};

int run(const Settings& settings, std::ostream& out);
int dump_png(const Settings& settings, const std::string& path, std::ostream& out);
int idle_probe(const Settings& settings, int idle_probe_ms, std::ostream& out);

}  // namespace showcase_window
