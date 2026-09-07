// The on-screen readout.
//
// The point of this demo is that a human moves a pointer and watches what
// happens, so what happened is written into the window rather than to a
// terminal nobody is looking at. Three things are on it that are not obvious
// from the picture:
//
//   what the pointer is over, so a wrong answer is visible rather than merely
//   felt - a hover that lights the wrong widget is easy to miss and impossible
//   to miss when the widget is also named;
//
//   how many pixels the last interaction repainted, and how many the whole
//   window would be, which is the number the damage system exists to make
//   small;
//
//   what a corner radius costs. Sub-step 1 measured 30x from one radius, the
//   owner has relaxed performance requirements, and both of those are true at
//   once - so the cost is SURFACED rather than gated. A rounded button is a
//   fine choice; a rounded button chosen without knowing this is not.
//
// It draws with Skia directly, as examples/02, 03 and 04 do. Text IS a node
// now, so this could in principle be built from label widgets - but a
// formatted twelve-line table would be a dozen nodes re-laid-out on a timer,
// and the readout would then be measuring itself. When there is a text widget
// that shrink-wraps its content, this is the file that disappears.

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

struct Readout {
  dg::PixelSize viewport;
  std::size_t nodes = 0;
  std::size_t widgets = 0;
  bool rounded_controls = false;

  bool pointer_inside = false;
  dg::PixelPoint pointer;
  std::string hovered = "-";
  std::string pressed = "-";
  std::string last_click = "-";
  int clicks = 0;

  // Enters and leaves counted over the session. They must stay equal to within
  // one - one leave per enter, plus at most the widget currently under the
  // pointer - and a drifting pair is the flicker bug that a still frame cannot
  // show.
  int enters = 0;
  int leaves = 0;

  dg::RepaintStats last_paint;

  // Median pixels repainted per interaction, under each container style.
  std::int64_t square_repaint = 0;
  std::int64_t rounded_repaint = 0;

  std::string diagnostic;
  std::string note;
};

class Hud {
 public:
  [[nodiscard]] static Hud load(const std::string& font_dir);

  [[nodiscard]] bool ready() const { return static_cast<bool>(mono_); }

  void draw(SkCanvas& canvas, const dg::PixelRect& bounds, const Readout& readout) const;

 private:
  sk_sp<SkTypeface> mono_;
  sk_sp<SkTypeface> sans_;
};

}  // namespace hud
