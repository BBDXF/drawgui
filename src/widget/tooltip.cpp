#include "drawgui/widget/tooltip.h"

namespace dg {

void update_hover_timer(HoverTimer& timer, std::optional<NodeId> hovered, AnimTime now) {
  if (timer.target == hovered) {
    return;
  }
  timer.target = hovered;
  timer.since = now;
}

bool hover_ready(const HoverTimer& timer, AnimTime now, int delay_ms) {
  if (!timer.target.has_value()) {
    return false;
  }
  return (now.ms - timer.since.ms) >= delay_ms;
}

}  // namespace dg
