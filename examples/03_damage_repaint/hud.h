// The on-screen readout.
//
// The point of this demo is a number a human can watch, so the numbers are
// drawn into the window rather than printed to a terminal nobody is looking
// at while the animation runs. Both modes are shown at once, side by side,
// because a demo that only shows the fast path proves nothing - the
// comparison is the evidence.
//
// It draws with Skia directly, for the same reason examples/02 does: text is
// not a render-tree node yet (the font manager this project has still has no
// fallback chain, which is its own sub-step), so this is an overlay painted
// on top of a square-cornered strip the tree owns. That strip takes part in
// damage like any other node, and the overlay is redrawn for any damage
// rectangle that touches it.

#pragma once

#include <cstddef>
#include <string>

#include "drawgui/base/pixel_geometry.h"
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

// One repaint strategy's cost, split into the two halves step 2 measured
// separately - at 1080p presentation was the more expensive of the two, so
// reporting a single combined number would hide half the story.
struct Lane {
  Timing raster;
  Timing present;
  std::size_t frames = 0;
};

struct Readout {
  dg::PixelSize viewport;
  bool damage_mode = true;
  dg::PaintMode paint_mode = dg::PaintMode::kDirect;
  std::size_t nodes = 0;
  std::size_t damage_cap = 0;
  Lane damage;
  Lane full;
  dg::RepaintStats last;
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
