// Multi-click detection (double/triple click) over the AnimTime clock seam.
//
// The platform's own click count (SDL_MouseButtonEvent::clicks) is not
// carried by dg::PointerEvent, and an injected test press has none, so the
// count is derived here from the presses themselves: a press within
// `kMultiClickMs` of the previous one and within `kMultiClickSlopPx` of it
// (on each axis) continues the sequence, anything else restarts it at 1.
// The count cycles 1, 2, 3, 1, ... so a fourth rapid press starts over.

#pragma once

#include <optional>

#include "drawgui/anim/clock.h"
#include "drawgui/base/pixel_geometry.h"

namespace dg {

inline constexpr int kMultiClickMs = 500;
inline constexpr int kMultiClickSlopPx = 4;

struct ClickCounter {
  std::optional<PixelPoint> last_at;
  AnimTime last_time;
  int count = 0;
};

// Registers one primary-button press and returns its position in the
// sequence (1, 2 or 3).
int register_click(ClickCounter& counter, PixelPoint at, AnimTime now);

}  // namespace dg
