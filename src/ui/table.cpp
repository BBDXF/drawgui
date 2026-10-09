#include "drawgui/ui/table.h"

#include <utility>

#include "drawgui/widget/focus.h"
#include "drawgui/widget/interaction.h"
#include "drawgui/widget/widget_set.h"
#include "ui/control_visuals.h"
#include "ui/row_list.h"

namespace dg {

void Table::build(ControlContext& ctx, NodeId parent, TableSpec spec) {
  destroy(ctx);
  control_visuals::RowListNodes nodes = control_visuals::build_table(ctx, parent, spec);
  root_ = nodes.root;
  list_ = nodes.list;
  track_ = nodes.track;
  thumb_ = nodes.thumb;
  headers_ = std::move(nodes.header_cells);
  pool_ = std::move(nodes.pool);
  columns_ = std::move(spec.columns);
  rows_ = std::move(spec.rows);
  built_ = true;
  reset_rows(ctx);
}

void Table::destroy(ControlContext& ctx) {
  if (built_ && ctx.tree().is_valid(root_)) {
    if (dragging_) {
      ctx.tables.widgets.scrollbar_release();
    }
    remove_subtree(ctx.layout, root_, ctx.tables);
  }
  built_ = false;
  dragging_ = false;
  columns_.clear();
  rows_.clear();
  headers_.clear();
  pool_.clear();
  selected_.reset();
}

int Table::viewport(const ControlContext& ctx) const {
  return row_list::viewport(ctx, list_);
}

void Table::reset_rows(ControlContext& ctx) {
  ctx.tables.widgets.list_reset(ctx.tree(), list_, row_count(), viewport(ctx));
  repaint(ctx);
}

void Table::repaint(ControlContext& ctx) {
  const Widget& list = ctx.tables.widgets.at(list_);
  for (std::size_t slot = 0; slot < list.list_pool.size() && slot < list.list_assigned.size();
       ++slot) {
    const int row = list.list_assigned[slot];
    if (row < 0 || row >= row_count()) {
      continue;
    }
    control_visuals::paint_table_row(ctx, list.list_pool[slot],
                                     &rows_[static_cast<std::size_t>(row)], selected_ == row);
  }
}

void Table::sync(ControlContext& ctx) {
  if (built_) {
    reset_rows(ctx);
  }
}

void Table::set_rows(ControlContext& ctx, std::vector<std::vector<std::string>> rows) {
  if (!built_) {
    return;
  }
  rows_ = std::move(rows);
  selected_.reset();
  reset_rows(ctx);
}

void Table::reveal(ControlContext& ctx, int row) {
  row_list::reveal(ctx, list_, row);
}

bool Table::select(ControlContext& ctx, int row) {
  if (!built_ || row < 0 || row >= row_count() || selected_ == row) {
    return false;
  }
  selected_ = row;
  reveal(ctx, row);
  repaint(ctx);
  return true;
}

void Table::set_sort_indicator(ControlContext& ctx, std::optional<int> column, bool ascending) {
  for (std::size_t i = 0; i < headers_.size(); ++i) {
    std::string title = columns_[i].title;
    if (column == static_cast<int>(i)) {
      title += ascending ? " \u25B2" : " \u25BC";
    }
    control_visuals::paint_table_header(ctx, headers_[i], title);
  }
}

TableResult Table::handle_pointer(ControlContext& ctx, const PointerEvent& event) {
  if (!built_) {
    return {};
  }
  bool scrolled = false;
  if (row_list::scroll_pointer(ctx, root_, list_, event, dragging_, scrolled)) {
    if (scrolled) {
      repaint(ctx);
    }
    return TableResult{TableEvent::kHandled, -1, -1};
  }
  if (event.action != PointerAction::kDown || event.button != PointerButton::kPrimary) {
    return {};
  }
  const PixelPoint at{event.x, event.y};
  const std::optional<NodeId> owner = ctx.tables.widgets.widget_at(ctx.tree(), at);
  if (!owner.has_value()) {
    return {};
  }
  for (std::size_t i = 0; i < headers_.size(); ++i) {
    if (headers_[i] == *owner && columns_[i].sortable) {
      return TableResult{TableEvent::kSortRequested, -1, static_cast<int>(i)};
    }
  }
  if (*owner != root_) {
    return {};
  }
  ctx.tables.focus.set_guarded(ctx.tree(), root_);
  const std::optional<int> row = row_list::row_at(ctx, list_, at, row_count());
  if (!row.has_value()) {
    return TableResult{TableEvent::kHandled, -1, -1};
  }
  if (select(ctx, *row)) {
    return TableResult{TableEvent::kSelected, *row, -1};
  }
  return TableResult{TableEvent::kHandled, *row, -1};
}

TableResult Table::handle_key(ControlContext& ctx, const KeyEvent& event) {
  if (!built_ || !row_list::owns_key(ctx, root_, event)) {
    return {};
  }
  const std::optional<int> target =
      row_list::nav_target(ctx, list_, event.key, selected_, row_count());
  if (!target.has_value()) {
    return {};
  }
  if (select(ctx, *target)) {
    return TableResult{TableEvent::kSelected, *target, -1};
  }
  reveal(ctx, *target);
  repaint(ctx);
  return TableResult{TableEvent::kHandled, *target, -1};
}

}  // namespace dg
