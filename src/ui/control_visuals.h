// Node building and painting for the data controls (ProgressBar, Tabs,
// TreeView, Table) - every colour, size and node shape they put on screen,
// apart from their behaviour (progress_bar.cpp, tabs.cpp, tree_view.cpp,
// table.cpp), so a visual pass touches only control_visuals.cpp.

#pragma once

#include <string>
#include <vector>

#include "drawgui/ui/control_context.h"
#include "drawgui/ui/table.h"
#include "drawgui/ui/tabs.h"
#include "drawgui/ui/tree_view.h"

namespace dg::control_visuals {

[[nodiscard]] int text_width(const ControlContext& ctx, float size, const std::string& text);

struct ProgressNodes {
  NodeId track;
  NodeId fill;
};
ProgressNodes build_progress(ControlContext& ctx, NodeId parent, const BoxStyle& box);

struct TabsNodes {
  NodeId root;
  NodeId host;
  std::vector<NodeId> tabs;
  std::vector<NodeId> labels;
  std::vector<NodeId> indicators;
  std::vector<NodeId> panels;
};
TabsNodes build_tabs(ControlContext& ctx, NodeId parent, const TabsSpec& spec);
void paint_tab(ControlContext& ctx, NodeId label, NodeId indicator, const TabItem& item,
               bool selected);

// The shared virtualized-row skeleton: `root` (the focusable kButton
// frame), `list` (the kList), its scrollbar and the pool.
struct RowListNodes {
  NodeId root;
  NodeId header;  // Table only (root otherwise)
  std::vector<NodeId> header_cells;
  NodeId list;
  NodeId track;
  NodeId thumb;
  std::vector<NodeId> pool;
};
RowListNodes build_tree(ControlContext& ctx, NodeId parent, const TreeViewSpec& spec);
RowListNodes build_table(ControlContext& ctx, NodeId parent, const TableSpec& spec);

// Paint-time layout + content of one tree row (a kLeaf whose disclosure and
// label children are positioned here, by depth).
// Row-local x of the disclosure column at `depth` (one indent wide); the
// label starts one indent after it. TreeView hit-tests with the same value.
[[nodiscard]] int tree_disclosure_x(const ControlContext& ctx, int depth);
void paint_tree_row(ControlContext& ctx, NodeId row, const TreeItem& item, bool has_children,
                    bool expanded, bool selected);
void paint_table_row(ControlContext& ctx, NodeId row, const std::vector<std::string>* cells,
                     bool selected);
void paint_table_header(ControlContext& ctx, NodeId cell, const std::string& title);

}  // namespace dg::control_visuals
