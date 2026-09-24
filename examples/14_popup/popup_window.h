// The window half of examples/14_popup: a real host window with one
// clickable "menu" panel that opens an overlay popup through PopupHost.
//
// PopupHost is overlay-only (doc/popup.md) - there is no second branch to
// choose between any more, so this Settings struct carries no
// force_overlay/native switch. This example's remaining job is what its own
// header comment in popup_scene.h and popup_check.cpp now record: the
// smallest real (on-screen, real WindowManager) exercise of PopupHost's
// placement/dismissal plumbing, and the test bed the horizontal flip/clamp
// addition to resolve_popup_placement() extended rather than re-derived
// from scratch.

#pragma once

#include <iosfwd>
#include <string>

#include "drawgui/base/pixel_geometry.h"

namespace popup_window {

struct Settings {
  dg::PixelSize size{400, 300};
  int run_ms = 0;
};

int run(const Settings& settings, std::ostream& out);
int dump_png(const Settings& settings, const std::string& path, std::ostream& out);

}  // namespace popup_window
