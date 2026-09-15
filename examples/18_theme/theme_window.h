// The window half of examples/18_theme.
//
// Static except for one interaction: pressing SPACE switches the theme
// variant at runtime (dg::ThemeBindings::apply()) - the demo IS the
// switch, not a resize handler noticing something else changed.

#pragma once

#include <iosfwd>
#include <string>

#include "drawgui/base/pixel_geometry.h"

#include "theme_scene.h"

namespace theme_window {

struct Settings {
  dg::PixelSize size = theme_scene::kDemoViewport;
  int run_ms = 0;
};

int run(const Settings& settings, std::ostream& out);
int dump_png(const Settings& settings, const std::string& path, std::ostream& out);

}  // namespace theme_window
