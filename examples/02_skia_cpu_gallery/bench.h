// The measurement half of the gallery.
//
// The question this example exists to answer is whether CPU raster is good
// enough for a general GUI toolkit, and that is answered in milliseconds or
// not at all. Everything here reports a median and a worst case over many
// frames rather than a mean, because a mean hides exactly the occasional long
// frame a user perceives as a stutter.

#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

#include "drawgui/window/window_manager.h"

#include "gallery.h"

namespace bench {

struct Stats {
  double min_ms = 0.0;
  double median_ms = 0.0;
  double p95_ms = 0.0;
  double max_ms = 0.0;
  std::size_t samples = 0;

  [[nodiscard]] double median_fps() const;
};

[[nodiscard]] Stats summarize(std::vector<double> samples_ms);

// Rasterizes into an offscreen surface, so it needs no display and measures
// nothing but Skia. Reports a size ladder, the cost of each panel group in
// isolation, and a small dirty rectangle against a full repaint.
void run_offscreen(const gallery::Resources& resources, int frames, std::ostream& out);

// Splits raster time from the cost of getting those pixels onto the screen.
// Under WSLg the second number includes a copy into the window surface and a
// round trip through the X server, and conflating it with raster time would
// make the rasterizer look responsible for the compositor.
void run_windowed(dg::WindowManager& manager, dg::WindowId window,
                  const gallery::Resources& resources, int frames, std::ostream& out);

// Creates and destroys a raster surface at many sizes, drawing a real frame
// into each. This is the shape of a window being dragged by its corner, and
// it is where a surface-lifetime leak shows up under a sanitizer.
[[nodiscard]] bool stress_surfaces(const gallery::Resources& resources, int cycles,
                                   std::ostream& out);

}  // namespace bench
