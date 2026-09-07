// Driving the demo window: animate the scene, repaint it two different ways,
// and keep score.
//
// The comparison is the point. A demo that only ever showed the damage path
// would be asking to be believed; this one alternates between damage-driven
// and forced full repaint on a timer, keeps a separate rolling window of
// timings for each, and prints both on screen at once.

#pragma once

#include <cstddef>
#include <iosfwd>
#include <string>

#include "drawgui/render/render_tree.h"

namespace demo {

struct Settings {
  dg::PixelSize size;
  std::size_t max_damage_rects = dg::DamageRegion::kDefaultMaxRects;
  dg::PaintMode paint_mode = dg::PaintMode::kDirect;

  // When true the demo flips between the two repaint strategies on a timer,
  // so both columns of the readout fill in without anyone touching anything.
  bool alternate = true;

  // Which strategy to use when `alternate` is false. Pinning it to damage is
  // what the stale-pixel evidence run needs: a full repaint every frame would
  // scrub away exactly the corruption that run is looking for.
  bool damage_mode = true;

  int alternate_ms = 1500;
  int frame_budget_ms = 16;
  int run_ms = 0;
  std::string font_dir = "/usr/share/fonts";
};

int run(const Settings& settings, std::ostream& out);

}  // namespace demo
