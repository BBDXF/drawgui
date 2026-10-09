// dg::TreeView - a virtualized, expandable outline.
//
// THE MODEL is a flat pre-order list of TreeItems, each with a `depth`: an
// item's children are the items right after it with depth + 1 (and their
// own subtrees), up to the next item at its depth or shallower. Expanding/
// collapsing re-derives the FLATTENED VISIBLE ROWS (an O(n) walk) and feeds
// their count to the kList underneath (WidgetSet::list_reset()) - the same
// recycling pool kList always had, so the node count is bounded by the
// viewport (ceil(viewport_height / row_height) + 1 rows) whether the tree
// holds 10 items or 10,000.
//
// FOCUS: the whole control is ONE focusable kButton (its outer node); rows
// are plain nodes, so a click anywhere in them resolves to the control and
// handle_pointer() maps the pointer to a row arithmetically. The selected
// row is painted with ControlStyle::accent; a focus ring is the caller's
// usual update_focus_ring() on root().
//
// KEYS (while root() has focus): Up/Down move the selection over visible
// rows, Home/End/PageUp/PageDown jump; Right expands a collapsed parent or
// moves to its first child; Left collapses an expanded parent or moves to
// the parent. A click on the disclosure column toggles; a click elsewhere on
// a row selects it. Wheel and the scrollbar scroll without selecting.
//
// Collapsing a node whose descendant was selected moves the selection to
// that node (reported as kCollapsed; read selected() after).
//
// Caller obligation: sync() after every layout pass.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "drawgui/layout/box.h"
#include "drawgui/ui/control_context.h"
#include "drawgui/window/window_manager.h"

namespace dg {

struct TreeItem {
  std::string label;
  int depth = 0;
  bool expanded = false;
};

struct TreeViewSpec {
  std::vector<TreeItem> items;
  // The row area's height; fixes the pool size. The outer box's height is
  // derived from it (plus the 1px frame).
  int viewport_height = 240;
  // The outer box (width, margin, grow...). kind/height are overridden.
  BoxStyle box;
};

enum class TreeEvent : std::uint8_t {
  kIgnored,
  kHandled,    // consumed (scrolled, or a no-op key) - nothing reportable changed
  kSelected,   // `item` is now selected
  kExpanded,   // `item` was expanded
  kCollapsed,  // `item` was collapsed
};

struct TreeResult {
  TreeEvent event = TreeEvent::kIgnored;
  int item = -1;  // index into TreeViewSpec::items
};

class TreeView {
 public:
  void build(ControlContext& ctx, NodeId parent, TreeViewSpec spec);
  void destroy(ControlContext& ctx);

  TreeResult handle_pointer(ControlContext& ctx, const PointerEvent& event);
  TreeResult handle_key(ControlContext& ctx, const KeyEvent& event);

  bool select(ControlContext& ctx, int item);  // also expands its ancestors
  bool set_expanded(ControlContext& ctx, int item, bool expanded);
  void sync(ControlContext& ctx);

  [[nodiscard]] std::optional<int> selected() const { return selected_; }
  [[nodiscard]] bool has_children(int item) const;
  [[nodiscard]] bool is_expanded(int item) const;
  [[nodiscard]] int parent_of(int item) const;  // -1 for a top-level item
  // Item indices of the visible rows, top to bottom.
  [[nodiscard]] const std::vector<int>& visible_rows() const { return visible_; }

  [[nodiscard]] NodeId root() const { return root_; }
  [[nodiscard]] NodeId list() const { return list_; }
  [[nodiscard]] const std::vector<NodeId>& pool() const { return pool_; }

 private:
  void rebuild_visible();
  void reset_rows(ControlContext& ctx);
  void repaint(ControlContext& ctx);
  [[nodiscard]] int viewport(const ControlContext& ctx) const;
  [[nodiscard]] std::optional<int> row_of(int item) const;
  void reveal(ControlContext& ctx, int row);
  TreeResult select_row(ControlContext& ctx, int row);

  std::vector<TreeItem> items_;
  std::vector<int> visible_;
  std::optional<int> selected_;
  NodeId root_;
  NodeId list_;
  NodeId track_;
  NodeId thumb_;
  std::vector<NodeId> pool_;
  bool dragging_ = false;
  bool built_ = false;
};

}  // namespace dg
