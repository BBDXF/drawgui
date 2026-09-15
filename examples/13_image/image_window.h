// The window half of examples/13_image.
//
// Static, on purpose: nothing about an image node animates in this slice
// (design.md section 5.10.3's async decode - the thing that WOULD move a
// pixel after the window first opens - is out of scope), so the window just
// draws the five-panel scene once and repaints only on resize, exactly like
// examples/07_clipping's `open`/`cut` pair being the whole demonstration.

#pragma once

#include <iosfwd>
#include <string>

#include "drawgui/base/pixel_geometry.h"

#include "image_scene.h"

namespace image_window {

struct Settings {
  dg::PixelSize size = image_scene::kDemoViewport;
  int run_ms = 0;
};

int run(const Settings& settings, std::ostream& out);
int dump_png(const Settings& settings, const std::string& path, std::ostream& out);

}  // namespace image_window
