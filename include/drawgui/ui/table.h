// dg::Table - a header row over virtualized rows of text cells.
//
// Rows recycle through kList's pool exactly like TreeView (pool size
// ceil(viewport_height / row_height) + 1, independent of the row count).
// Columns have FIXED widths - device pixels or a percentage of the row
// width (BoxPercents) - and a column with neither takes the remaining
// space. The scrollbar is vertical; its gutter is reserved on the header
// too so header and cells line up.
//
// The table OWNS a copy of the cell strings (set_rows()); SORTING IS THE
// APP'S: clicking a sortable column's header reports kSortRequested with
// the column, the app sorts its data, calls set_rows() and, if it likes,
// set_sort_indicator(). Header cells are kButton widgets with tab_index -1
// (clickable, never in the Tab sequence); the control's outer node is the
// one focusable kButton, as in TreeView.
//
// KEYS (while root() has focus): Up/Down/Home/End/PageUp/PageDown move the
// row selection, scrolling it into view.
//
// Caller obligation: sync() after every layout pass.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "drawgui/layout/box.h"
#include "drawgui/render/image_catalog.h"
#include "drawgui/ui/control_context.h"
#include "drawgui/window/window_manager.h"

namespace dg {

struct TableColumn {
  std::string title;
  std::optional<int> width_px;
  std::optional<float> width_percent;  // of the row width, 50.0F is half
  bool sortable = true;
};

// A cell's optional icon and check box (TableSpec::parts), before its text.
struct TableCellPart {
  ImageId icon{};
  std::optional<bool> check = std::nullopt;
};

struct TableSpec {
  std::vector<TableColumn> columns;
  std::vector<std::vector<std::string>> rows;  // a missing cell shows empty
  // Parallel to `rows` (may be shorter / empty); any icon or check builds
  // the part nodes per cell.
  std::vector<std::vector<TableCellPart>> parts;
  int viewport_height = 240;                   // the row area, below the header
  BoxStyle box;                                // kind/height overridden
};

enum class TableEvent : std::uint8_t {
  kIgnored,
  kHandled,
  kSelected,       // `row` is now selected
  kSortRequested,  // the header of `column` was clicked
};

struct TableResult {
  TableEvent event = TableEvent::kIgnored;
  int row = -1;
  int column = -1;
};

class Table {
 public:
  void build(ControlContext& ctx, NodeId parent, TableSpec spec);
  void destroy(ControlContext& ctx);

  TableResult handle_pointer(ControlContext& ctx, const PointerEvent& event);
  TableResult handle_key(ControlContext& ctx, const KeyEvent& event);

  // Replaces the data. The selection is cleared; the scroll offset is
  // clamped to the new row count.
  void set_rows(ControlContext& ctx, std::vector<std::vector<std::string>> rows);
  bool select(ControlContext& ctx, int row);
  // Flips a checkable cell's check box; false when it has none.
  bool toggle_check(ControlContext& ctx, int row, int column);
  [[nodiscard]] std::optional<bool> cell_check(int row, int column) const;
  // The cell under absolute `point` and whether the point is on its check
  // box; nothing outside the rows.
  struct Hit {
    int row = -1;
    int column = -1;
    bool check = false;
  };
  [[nodiscard]] std::optional<Hit> hit(const ControlContext& ctx, PixelPoint point) const;
  // Shows " ^" (ascending) or " v" after `column`'s title; nullopt clears.
  void set_sort_indicator(ControlContext& ctx, std::optional<int> column, bool ascending);
  void sync(ControlContext& ctx);

  [[nodiscard]] std::optional<int> selected() const { return selected_; }
  [[nodiscard]] int row_count() const { return static_cast<int>(rows_.size()); }
  [[nodiscard]] NodeId root() const { return root_; }
  [[nodiscard]] NodeId list() const { return list_; }
  [[nodiscard]] const std::vector<NodeId>& pool() const { return pool_; }
  [[nodiscard]] const std::vector<NodeId>& header_cells() const { return headers_; }

 private:
  void reset_rows(ControlContext& ctx);
  void repaint(ControlContext& ctx);
  [[nodiscard]] int viewport(const ControlContext& ctx) const;
  void reveal(ControlContext& ctx, int row);

  std::vector<TableColumn> columns_;
  std::vector<std::vector<std::string>> rows_;
  std::vector<std::vector<TableCellPart>> parts_;
  bool rich_ = false;
  std::optional<int> selected_;
  NodeId root_;
  NodeId list_;
  NodeId track_;
  NodeId thumb_;
  std::vector<NodeId> headers_;
  std::vector<NodeId> pool_;
  bool dragging_ = false;
  bool built_ = false;
};

}  // namespace dg
