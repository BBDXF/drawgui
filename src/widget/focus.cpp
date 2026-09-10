#include "drawgui/widget/focus.h"

namespace dg {

FocusChange Focus::set(std::optional<NodeId> target) {
  FocusChange change;
  if (focused_ == target) {
    return change;
  }
  change.blurred = focused_;
  change.focused = target;
  focused_ = target;
  return change;
}

}  // namespace dg
