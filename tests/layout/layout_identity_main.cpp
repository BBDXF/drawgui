// The layout identity gate, as a CTest entry of its own.
//
// tests/unit already runs this comparison inside doctest, and that is the
// coverage. This binary exists because the gate deserves a name in the CTest
// list: `ctest -R layout` should say whether incremental layout still agrees
// with full layout, without a reader having to know that it is one case
// inside a suite called `unit`.
//
// It needs no display and no SDL3, unlike the demo that shares its scene, so
// it is built unconditionally and CI runs it on every configuration.

#include <iostream>

#include "drawgui/base/pixel_geometry.h"

#include "layout_check.h"

namespace {

// Three shapes rather than one. A layout bug that depends on how the flexible
// space divides is invisible at a width where it happens to divide evenly, and
// odd sizes make the surface pad its rows as well.
constexpr dg::PixelSize kViewports[] = {
    {641, 421},
    {1280, 800},
    {903, 517},
};

constexpr int kFrames = 180;

}  // namespace

int main() {
  bool ok = true;
  for (const dg::PixelSize& viewport : kViewports) {
    for (const bool rounded : {false, true}) {
      layout_check::Config config;
      config.viewport = viewport;
      config.frames = kFrames;
      config.rounded_containers = rounded;
      if (!layout_check::verify_layout(config, std::cout)) {
        ok = false;
      }
    }
  }
  std::cout << (ok ? "\nincremental layout equals full layout everywhere tested\n"
                   : "\nincremental layout DISAGREES with full layout\n");
  return ok ? 0 : 1;
}
