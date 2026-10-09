// NodeKind::kTable: a dg::Table built from the columns and rows the host
// set, rebuilt with its selection and sort indicator kept.

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "app/components/components.h"
#include "app/components/focus_keeper.h"
#include "app/internal.h"

namespace dg::app_detail {
namespace {

constexpr int kTableHeight = 240;
constexpr int kFrame = 1;  // control_visuals' tree/table outer border

// `clicks` = the pointer press's click count for a selection, 0 from keys.
void table_result(WinState& window, const NodeRecord& record, const dg::TableResult& r,
                  int clicks) {
  using Kind = dg::events::TableEvent::Kind;
  if (r.event == dg::TableEvent::kSelected) {
    emit(window.app,
         dg::events::TableEvent{Kind::kSelect, window.sdl_window, record.id, r.row, clicks});
  } else if (r.event == dg::TableEvent::kSortRequested) {
    emit(window.app,
         dg::events::TableEvent{Kind::kSort, window.sdl_window, record.id, r.column, 0});
  }
}

}  // namespace

namespace components {

void build_table(WinState& window, NodeRecord& record, std::optional<int> select) {
  TableState& c = std::get<TableState>(record.state);
  dg::ControlContext ctx = controls(window);
  c.built_height = laid_out_height(window, record, kTableHeight);
  const FocusKeeper keeper(window, c.table.root());
  c.table.destroy(ctx);
  dg::TableSpec spec;
  spec.columns = c.columns;
  spec.rows = c.rows;
  spec.viewport_height =
      std::max(ctx.style.row_height, c.built_height -
                                         (2 * kFrame) - ctx.style.row_height);
  spec.box.percent.width = 100.0F;
  c.table.build(ctx, record.id, std::move(spec));
  if (c.sort_column.has_value()) {
    c.table.set_sort_indicator(ctx, c.sort_column, c.sort_ascending);
  }
  if (select.has_value()) {
    (void)c.table.select(ctx, *select);
  }
  keeper.restore(window, c.table.root());
  window.relaid = true;
}

bool control_pointer(WinState& window, NodeRecord& record, TableState& state,
                     const dg::PointerEvent& event) {
  dg::ControlContext ctx = controls(window);
  const dg::TableResult r = state.table.handle_pointer(ctx, event);
  table_result(window, record, r, std::max(event.clicks, 1));
  return r.event != dg::TableEvent::kIgnored;
}

bool control_key(WinState& window, NodeRecord& record, TableState& state,
                 const dg::KeyEvent& event) {
  dg::ControlContext ctx = controls(window);
  const dg::TableResult r = state.table.handle_key(ctx, event);
  table_result(window, record, r, 0);
  return r.event != dg::TableEvent::kIgnored;
}

void sync_state(WinState& window, NodeRecord& record, TableState& state) {
  if (laid_out_height(window, record, kTableHeight) != state.built_height) {
    build_table(window, record, state.table.selected());
    return;
  }
  dg::ControlContext ctx = controls(window);
  state.table.sync(ctx);
}

void restyle_state(WinState& window, NodeRecord& record, TableState& state) {
  build_table(window, record, state.table.selected());
}

void after_prop_state(WinState& window, NodeRecord& record, TableState& state,
                      std::uint16_t prop_id) {
  if (prop_id == DG_PROP_HEIGHT) {
    build_table(window, record, state.table.selected());
  }
}

dg::NodeId focus_target_state(const NodeRecord& /*record*/, const TableState& state) {
  return state.table.root();
}

}  // namespace components
}  // namespace dg::app_detail

namespace dg {

using app_detail::TableState;
using app_detail::components::component_of;

Status TableRef::set_columns(std::vector<TableColumnSpec> columns) {
  app_detail::WinState& win = window();
  const auto found = component_of<TableState>(win, node());
  if (found.status != Status::kOk) {
    return found.status;
  }
  if (columns.empty() || columns.size() > limits::kMaxTableColumns) {
    return Status::kInvalidArgument;
  }
  TableState& c = *found.state;
  c.columns.clear();
  for (TableColumnSpec& spec : columns) {
    dg::TableColumn column;
    column.title = std::move(spec.title);
    column.width_px = spec.width_px;
    column.width_percent = spec.width_percent;
    column.sortable = spec.sortable;
    c.columns.push_back(std::move(column));
  }
  c.rows.clear();
  c.sort_column.reset();
  app_detail::components::build_table(win, *found.record);
  return Status::kOk;
}

Expected<int, Status> TableRef::column_count() const {
  const auto found = component_of<TableState>(std::as_const(window()), node());
  if (found.status != Status::kOk) {
    return Unexpected(found.status);
  }
  return static_cast<int>(found.state->columns.size());
}

Status TableRef::set_rows(std::vector<std::vector<std::string>> rows) {
  app_detail::WinState& win = window();
  const auto found = component_of<TableState>(win, node());
  if (found.status != Status::kOk) {
    return found.status;
  }
  TableState& c = *found.state;
  if (rows.size() * std::max<std::size_t>(c.columns.size(), 1) > limits::kMaxDataItems) {
    return Status::kInvalidArgument;
  }
  c.rows = rows;
  dg::ControlContext ctx = app_detail::components::controls(win);
  c.table.set_rows(ctx, std::move(rows));
  return Status::kOk;
}

Expected<int, Status> TableRef::selected() const {
  const auto found = component_of<TableState>(std::as_const(window()), node());
  if (found.status != Status::kOk) {
    return Unexpected(found.status);
  }
  return found.state->table.selected().value_or(-1);
}

Status TableRef::select(int row) {
  app_detail::WinState& win = window();
  const auto found = component_of<TableState>(win, node());
  if (found.status != Status::kOk) {
    return found.status;
  }
  TableState& c = *found.state;
  if (row < -1 || row >= c.table.row_count()) {
    return Status::kValueOutOfRange;
  }
  dg::ControlContext ctx = app_detail::components::controls(win);
  if (row == -1) {
    c.table.set_rows(ctx, c.rows);  // clears the selection
  } else {
    (void)c.table.select(ctx, row);
  }
  return Status::kOk;
}

Status TableRef::set_sort_indicator(std::optional<int> column, bool ascending) {
  app_detail::WinState& win = window();
  const auto found = component_of<TableState>(win, node());
  if (found.status != Status::kOk) {
    return found.status;
  }
  TableState& c = *found.state;
  if (column.has_value() && (*column < 0 || *column >= static_cast<int>(c.columns.size()))) {
    return Status::kValueOutOfRange;
  }
  c.sort_column = column;
  c.sort_ascending = ascending;
  dg::ControlContext ctx = app_detail::components::controls(win);
  c.table.set_sort_indicator(ctx, c.sort_column, c.sort_ascending);
  return Status::kOk;
}

}  // namespace dg
