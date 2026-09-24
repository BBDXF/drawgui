// Window driver for examples/22_dropdown_menu.
//
// Opening/closing routes through dg::PopupHost exactly like examples/21_focus
// - overlay-only (native OS popup windows were removed),
// appending content into the host's own RenderTree. Keyboard highlight
// movement (Up/Down) is dg::Focus::focus_next()/focus_previous() over the
// popup's own scope - the SAME traversal Tab already performs (7-4) - so
// this file adds no new highlight-cursor state of its own; Enter reads
// whichever row dg::Focus::current() already names.

#pragma once

#include <iosfwd>
#include <string>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/window/window_manager.h"

namespace dropdown_window {

struct Settings {
  dg::PixelSize size{520, 200};
  int run_ms = 0;
  std::string font_dir = "/usr/share/fonts";
};

int run(const Settings& settings, std::ostream& out);
int dump_png(const Settings& settings, const std::string& path, std::ostream& out);

// Test-only seam for --verify-dropdown: the pixel size Runner's constructor
// actually uses for its first scene/tree/surface build - see the .cpp's own
// definition comment for why this is exported rather than file-local.
dg::PixelSize initial_viewport(dg::WindowManager& manager, dg::WindowId window,
                               const Settings& settings);

}  // namespace dropdown_window
