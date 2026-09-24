// The interactive half of examples/21_focus: a host window with the mixed-
// kind scene focus_scene.h builds, plus a "open popup" button that opens a
// popup through PopupHost - overlay-only (native OS popup windows were
// removed; see doc/popup.md).

#pragma once

#include <iosfwd>
#include <string>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/window/window_manager.h"

namespace focus_window {

struct Settings {
  dg::PixelSize size{620, 260};
  int run_ms = 0;
  std::string font_dir = "/usr/share/fonts";
};

int run(const Settings& settings, std::ostream& out);
int dump_png(const Settings& settings, const std::string& path, std::ostream& out);

// Test-only seam for --verify-focus: the pixel size Runner's constructor
// actually uses for its first scene/tree/surface build - see the .cpp's own
// definition comment for why this is exported rather than file-local.
dg::PixelSize initial_viewport(dg::WindowManager& manager, dg::WindowId window,
                               const Settings& settings);

}  // namespace focus_window
