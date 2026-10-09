// FocusKeeper - shared by the tree and table rebuilds.

#pragma once

#include <optional>

#include "app/state.h"
#include "drawgui/widget/focus.h"

namespace dg::app_detail::components {

// A control rebuild loses the focused composed node; focus moves to the
// rebuilt one without reporting a change (the caller-visible focus did not
// move).
struct FocusKeeper {
  bool had = false;

  FocusKeeper(const WinState& window, dg::NodeId old_root) {
    const std::optional<dg::NodeId> current = window.focus.current();
    had = old_root != dg::NodeId{} && current.has_value() &&
          window.tree.render().is_valid(old_root) &&
          dg::is_within(window.tree.render(), old_root, *current);
  }

  void restore(WinState& window, dg::NodeId new_target) const {
    if (!had) {
      return;
    }
    (void)window.focus.set(new_target);
    window.reported_focus = window.focus.current();
  }
};

}  // namespace dg::app_detail::components
