#include "drawgui/widget/click_counter.h"

#include <cstdint>
#include <cstdlib>

namespace dg {

int register_click(ClickCounter& counter, PixelPoint at, AnimTime now) {
  const std::int64_t elapsed = now.ms - counter.last_time.ms;
  const bool continues = counter.last_at.has_value() && counter.count > 0 && elapsed >= 0 &&
                         elapsed <= kMultiClickMs &&
                         std::abs(at.x - counter.last_at->x) <= kMultiClickSlopPx &&
                         std::abs(at.y - counter.last_at->y) <= kMultiClickSlopPx;
  counter.count = (continues && counter.count < 3) ? counter.count + 1 : 1;
  counter.last_at = at;
  counter.last_time = now;
  return counter.count;
}

}  // namespace dg
