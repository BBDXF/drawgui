// The window half of examples/20_multiline_text.
//
// Static, like examples/13_image: nothing here animates (there is no
// editing in this slice - grapheme-cluster cursor movement is the named
// 7-2b follow-up), so the window draws the five-panel scene once and
// repaints only on resize.

#pragma once

#include <iosfwd>
#include <string>

#include "drawgui/base/pixel_geometry.h"

#include "multiline_scene.h"

namespace multiline_window {

struct Settings {
  dg::PixelSize size = multiline_scene::kDemoViewport;
  int run_ms = 0;
  std::string font_dir = "/usr/share/fonts";
};

int run(const Settings& settings, std::ostream& out);
int dump_png(const Settings& settings, const std::string& path, std::ostream& out);

}  // namespace multiline_window
