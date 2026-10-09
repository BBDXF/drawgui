// dg::Tabs - a tab strip over a stack of panels, only one of them shown.
//
// Each tab is an ordinary kButton widget (hover/press through Interaction,
// focus through dg::Focus). ROVING FOCUS: only the selected tab is in the
// Tab-key sequence (the others carry tab_index -1); once a tab has focus,
// Left/Right move to the previous/next ENABLED tab (wrapping), Home/End to
// the first/last enabled one. AUTOMATIC ACTIVATION: moving focus with the
// arrow keys selects the tab it lands on (the WAI-ARIA "selection follows
// focus" pattern - the panels are already built, so switching is cheap);
// Enter on the focused tab is accepted and does nothing more. A click
// selects and focuses. Disabled tabs (NodeStyle::disabled) are never
// selected, focused or landed on.
//
// HIDDEN PANELS: every panel stays in the layout (no relayout on a switch).
// An inactive panel is made INERT - NodeStyle::disabled, which takes its
// whole subtree out of Tab order and pointer ownership, and Focus::
// blur_if_disabled() drops focus that was inside it - and its render
// bounds collapse to an empty clip (overflow: kClip), so nothing in it
// paints or hit-tests. Selecting a panel re-enables it and re-runs
// refresh_subtree() over it, putting back any disabled look a whole-tree
// refresh painted on it while hidden.
//
// Caller obligation: sync() after every layout pass (layout restores the
// hidden panels' full bounds).

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "drawgui/layout/box.h"
#include "drawgui/ui/control_context.h"
#include "drawgui/window/window_manager.h"

namespace dg {

struct TabItem {
  std::string label;
  bool disabled = false;
};

struct TabsSpec {
  std::vector<TabItem> tabs;
  // Initially selected tab; a disabled/out-of-range index selects the first
  // enabled tab instead (none when every tab is disabled).
  int selected = 0;
  // The whole control's box (strip + panels). `kind` is forced to kColumn.
  BoxStyle box;
};

enum class TabsEvent : std::uint8_t {
  kIgnored,   // not this control's event
  kHandled,   // consumed, selection unchanged
  kSelected,  // the selected tab changed to `index`
};

struct TabsResult {
  TabsEvent event = TabsEvent::kIgnored;
  int index = -1;
};

class Tabs {
 public:
  void build(ControlContext& ctx, NodeId parent, const TabsSpec& spec);
  void destroy(ControlContext& ctx);

  // The node to add panel `index`'s content under (a kColumn filling the
  // panel area). Root for an out-of-range index.
  [[nodiscard]] NodeId panel(int index) const;

  // A primary kDown on an enabled tab selects and focuses it.
  TabsResult handle_pointer(ControlContext& ctx, const PointerEvent& event);

  // Acts only while one of this control's tabs has focus.
  TabsResult handle_key(ControlContext& ctx, const KeyEvent& event);

  // Programmatic selection. False for a disabled/out-of-range/current index.
  bool select(ControlContext& ctx, int index);

  void sync(ControlContext& ctx);

  [[nodiscard]] std::optional<int> selected() const { return selected_; }
  [[nodiscard]] NodeId root() const { return root_; }
  [[nodiscard]] const std::vector<NodeId>& tab_nodes() const { return tabs_; }
  [[nodiscard]] const std::vector<NodeId>& panel_nodes() const { return panels_; }

 private:
  [[nodiscard]] std::optional<int> tab_index_of(NodeId id) const;
  [[nodiscard]] bool selectable(int index) const;
  void apply_selection(ControlContext& ctx);
  // The next enabled tab from `from` in `step` direction, wrapping.
  [[nodiscard]] std::optional<int> step_from(int from, int step) const;

  NodeId root_;
  NodeId host_;
  std::vector<TabItem> items_;
  std::vector<NodeId> tabs_;
  std::vector<NodeId> labels_;
  std::vector<NodeId> indicators_;
  std::vector<NodeId> panels_;
  std::optional<int> selected_;
  PixelRect panel_rect_;
  bool built_ = false;
};

}  // namespace dg
