#include "drawgui/shortcuts/keyboard_scroll.h"

#include <limits>

namespace dg {
namespace {

// Large enough that `offset + kJump`/`offset - kJump` cannot overflow
// `int` from any offset `scroll_by()`'s own clamp could ever have produced
// (offset is always in `[0, content - viewport]`, both non-negative and far
// short of `INT_MAX`), and large enough to exceed any real scene's content
// extent - the same "clamp does the real work" shape
// `tests/unit/test_scroll.cpp`'s overscroll test already uses with a
// literal 1000000, generalised here so it holds regardless of how tall a
// future scene's content grows.
constexpr int kJump = std::numeric_limits<int>::max() / 2;

}  // namespace

bool apply_keyboard_scroll(dg_action_id action_id, std::optional<NodeId> focused,
                           LayoutTree& layout, const WidgetSet& widgets) {
  if (!focused.has_value()) {
    return false;
  }
  const std::optional<NodeId> target = widgets.scrollable_owner_of(layout.render(), *focused);
  if (!target.has_value()) {
    return false;
  }

  const PixelRect viewport_content = layout.content_bounds(*target);
  int dy = 0;
  if (action_id == DG_ACTION_SCROLL_PAGE_UP) {
    dy = -viewport_content.height;
  } else if (action_id == DG_ACTION_SCROLL_PAGE_DOWN) {
    dy = viewport_content.height;
  } else if (action_id == DG_ACTION_SCROLL_TO_START) {
    dy = -kJump;
  } else if (action_id == DG_ACTION_SCROLL_TO_END) {
    dy = kJump;
  } else {
    return false;
  }
  return widgets.scroll_by(layout.render(), *target, viewport_content, 0, dy);
}

}  // namespace dg
