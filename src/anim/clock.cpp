#include "drawgui/anim/clock.h"

#include <chrono>

namespace dg {

AnimTime steady_anim_time() {
  using Clock = std::chrono::steady_clock;
  const auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(Clock::now());
  return AnimTime{now.time_since_epoch().count()};
}

}  // namespace dg
