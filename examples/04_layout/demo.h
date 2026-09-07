// Driving the demo window: mutate one leaf, lay out what that can affect,
// repaint what moved, and keep score.
//
// The comparison is the point. A demo that only ever showed the incremental
// path would be asking to be believed; this one alternates between
// incremental and forced full layout on a timer, keeps a separate rolling
// window of timings for each, and prints both on screen at once. It also
// alternates the container corner radius on a slower timer, so the 30x damage
// difference sub-step 1 measured is something a viewer watches happen rather
// than something they read about.

#pragma once

#include <cstddef>
#include <iosfwd>
#include <string>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/render/render_tree.h"

namespace demo {

struct Settings {
  dg::PixelSize size;
  std::size_t max_damage_rects = dg::DamageRegion::kDefaultMaxRects;

  // When true the demo flips between the two layout strategies on a timer, so
  // both rows of the readout fill in without anyone touching anything.
  bool alternate = true;

  // Which strategy to use when `alternate` is false.
  bool incremental = true;

  // When true the container corner radius flips on a slower timer, which
  // rebuilds the scene. That rebuild is the only full repaint in the session
  // and it is deliberate: it is what makes the two damage figures on screen
  // comparable rather than one remembered and one current.
  bool rounded_alternate = true;
  bool rounded = false;

  int alternate_ms = 1500;
  int rounded_ms = 5000;
  int frame_budget_ms = 16;
  int run_ms = 0;
  std::string font_dir = "/usr/share/fonts";
};

int run(const Settings& settings, std::ostream& out);

}  // namespace demo
