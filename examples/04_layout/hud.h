// The on-screen readout.
//
// The point of this demo is a number a human can watch, so the numbers are
// drawn into the window rather than printed to a terminal nobody is looking
// at while the layout is moving. Both layout strategies are shown at once,
// side by side, because a demo that only shows the fast path proves nothing -
// the comparison is the evidence.
//
// It draws with Skia directly, for the same reason examples/02 and 03 do:
// text is not a render-tree node yet (the font manager this project has still
// has no fallback chain, which is its own sub-step), so this is an overlay
// painted on top of a square-cornered strip the tree owns and lays out. That
// strip takes part in layout and damage like any other node, and the overlay
// is redrawn for any damage rectangle that touches it.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/render_tree.h"

#include "include/core/SkRefCnt.h"
#include "include/core/SkTypeface.h"

class SkCanvas;

namespace hud {

// Median, p95 and worst of one stage, over a rolling window of frames.
struct Timing {
  double median_ms = 0.0;
  double p95_ms = 0.0;
  double worst_ms = 0.0;
};

// One layout strategy's cost, split into the three stages a frame has. Layout
// is separated from raster because they answer different questions: raster
// cost is a function of damaged pixels, layout cost is a function of
// recomputed nodes, and a single combined number would let one hide the
// other.
struct Lane {
  Timing layout;
  Timing raster;
  Timing present;
  std::size_t frames = 0;
};

struct Readout {
  dg::PixelSize viewport;
  bool incremental = true;
  bool rounded_containers = false;
  std::size_t nodes = 0;
  std::size_t damage_cap = 0;

  dg::LayoutStats last_layout;
  dg::RepaintStats last_paint;

  Lane incremental_lane;
  Lane full_lane;

  // Pixels actually REPAINTED per frame with square containers and with
  // rounded ones. Not the damage layout asked for: the two differ precisely
  // because a damage rectangle grows until every rounded node it cuts is
  // inside it, so the cost of a corner radius is invisible in layout's own
  // number and shows up only after the repaint has expanded it. Sub-step 1
  // measured a 30x gap from one radius; layout decides node bounds, so this
  // is layout's number to surface rather than the theme's to discover later.
  std::int64_t square_repaint = 0;
  std::int64_t rounded_repaint = 0;

  std::string diagnostic;
};

class Hud {
 public:
  [[nodiscard]] static Hud load(const std::string& font_dir);

  [[nodiscard]] bool ready() const { return static_cast<bool>(mono_); }

  // Assumes the caller has already clipped to whatever part of `bounds` is
  // being repainted.
  void draw(SkCanvas& canvas, const dg::PixelRect& bounds, const Readout& readout) const;

 private:
  sk_sp<SkTypeface> mono_;
  sk_sp<SkTypeface> sans_;
};

}  // namespace hud
