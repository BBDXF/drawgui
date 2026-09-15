// The window half of examples/16_complex_properties.
//
// Static, matching examples/13_image's own reasoning: nothing here animates
// (no animation clock exists project-wide, and none of gradient/shadow/image
// as built by this slice has a runtime-mutable state the way scroll offset
// or a slider's value do), so the window draws the three-panel scene once
// and repaints only on resize.

#pragma once

#include <iosfwd>
#include <string>

#include "drawgui/base/pixel_geometry.h"

#include "cprops_scene.h"

namespace cprops_window {

struct Settings {
  dg::PixelSize size = cprops_scene::kDemoViewport;
  int run_ms = 0;
};

int run(const Settings& settings, std::ostream& out);
int dump_png(const Settings& settings, const std::string& path, std::ostream& out);

}  // namespace cprops_window
