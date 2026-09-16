// Window driver for examples/22_dropdown_menu.
//
// Opening/closing routes through dg::PopupHost exactly like examples/14_popup
// and examples/21_focus - the native branch owns a genuinely separate
// RenderTree/WidgetSet/Focus, the overlay branch appends into the host's own
// (doc/popup.md section 3, doc/focus.md section 5). Keyboard highlight
// movement (Up/Down) is dg::Focus::focus_next()/focus_previous() over the
// popup's own scope - the SAME traversal Tab already performs (7-4) - so
// this file adds no new highlight-cursor state of its own; Enter reads
// whichever row dg::Focus::current() already names.

#pragma once

#include <iosfwd>
#include <string>

#include "drawgui/base/pixel_geometry.h"

namespace dropdown_window {

struct Settings {
  dg::PixelSize size{520, 200};
  bool force_overlay = false;
  int run_ms = 0;
  std::string font_dir = "/usr/share/fonts";
};

int run(const Settings& settings, std::ostream& out);
int dump_png(const Settings& settings, const std::string& path, std::ostream& out);

}  // namespace dropdown_window
