// The window half of examples/14_popup: a real host window with one
// clickable "menu" panel that opens a popup through PopupHost, on either
// branch - `--branch native|overlay` forces which one, so a human can drive
// both through the identical click on this one machine.

#pragma once

#include <iosfwd>
#include <string>

#include "drawgui/base/pixel_geometry.h"

namespace popup_window {

struct Settings {
  dg::PixelSize size{400, 300};
  int run_ms = 0;
  // false: use whatever WindowManager::platform_caps() genuinely reports
  // (native_popup == true on this SDL3/Linux backend). true: override it to
  // false, forcing the overlay branch even though the real caps say native
  // popups are available - this is what proves the overlay branch on a
  // desktop that never needs it in production.
  bool force_overlay = false;
};

int run(const Settings& settings, std::ostream& out);
int dump_png(const Settings& settings, const std::string& path, std::ostream& out);

}  // namespace popup_window
