// The interactive half of examples/21_focus: a host window with the mixed-
// kind scene focus_scene.h builds, plus a "open popup" button that opens a
// popup through PopupHost - `--branch native|overlay` forces which one,
// matching examples/14_popup's own precedent exactly.

#pragma once

#include <iosfwd>
#include <string>

#include "drawgui/base/pixel_geometry.h"

namespace focus_window {

struct Settings {
  dg::PixelSize size{620, 260};
  int run_ms = 0;
  bool force_overlay = false;
  std::string font_dir = "/usr/share/fonts";
};

int run(const Settings& settings, std::ostream& out);
int dump_png(const Settings& settings, const std::string& path, std::ostream& out);

}  // namespace focus_window
