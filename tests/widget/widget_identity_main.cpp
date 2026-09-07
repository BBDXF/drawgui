// The widget interaction gates, as CTest entries of their own.
//
// Two questions, both answered here because both need the demo scene and
// neither needs a display:
//
//   Does an interaction-driven repaint produce the same pixels as repainting
//   everything? That is the acceptance bar sub-steps 1 and 2 both used, and
//   hover and press are just another source of damage.
//
//   Does hit testing name the widget the user can see, at every pixel, at
//   several viewport sizes? Running it after a resize is the point: a widget
//   layer that cached where things were would pass at the size it was built
//   at and fail at every other.
//
// Built unconditionally. It shares its scene and its pointer routing with
// examples/05_widgets, which needs SDL3 and a display and therefore cannot
// gate CI - the same split sub-steps 1 and 2 arrived at.

#include <iostream>

#include "drawgui/base/pixel_geometry.h"

#include "widget_check.h"

namespace {

// Three shapes. Odd sizes make the surface pad its rows, and a size other than
// the one the scene was authored at is the one that catches geometry nobody
// re-derived after a reflow.
constexpr dg::PixelSize kViewports[] = {
    {960, 720},
    {1153, 641},
    {820, 601},
};

}  // namespace

int main() {
  bool ok = true;
  for (const dg::PixelSize& viewport : kViewports) {
    widget_check::Config config;
    config.viewport = viewport;

    if (!widget_check::verify_interaction(config, std::cout)) {
      ok = false;
    }
    if (!widget_check::verify_hit_testing(config, std::cout)) {
      ok = false;
    }
  }
  std::cout << (ok ? "\ninteraction repaint equals full repaint, and every pixel is "
                     "clickable by the right widget\n"
                   : "\nthe widget layer DISAGREES with its reference\n");
  return ok ? 0 : 1;
}
