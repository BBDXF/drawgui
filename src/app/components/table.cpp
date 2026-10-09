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

dg::Status table_of(WinState& window, dg::NodeId id, NodeRecord*& out) {
  out = record_of(window, id);
  if (out == nullptr) {
    return dg::Status::kInvalidNode;
  }
  if (out->kind != NodeKind::kTable) {
    return dg::Status::kUnsupported;
  }
  return dg::Status::kOk;
}

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
  const FocusKeeper keeper(window, c.table.root());
  c.table.destroy(ctx);
  dg::TableSpec spec;
  spec.columns = c.columns;
  spec.rows = c.rows;
  spec.viewport_height =
      std::max(ctx.style.row_height, wrapper_height(window, record, kTableHeight) -
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

void sync_state(WinState& window, NodeRecord& /*record*/, TableState& state) {
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

using app_detail::NodeRecord;
using app_detail::TableState;

Status Window::table_set_columns(NodeId table, std::vector<TableColumn> columns) {
  app_detail::WinState& window = *state_;
  NodeRecord* record = nullptr;
  if (const Status status = app_detail::table_of(window, table, record);
      status != Status::kOk) {
    return status;
  }
  TableState& c = std::get<TableState>(record->state);
  c.columns = std::move(columns);
  c.rows.clear();
  c.sort_column.reset();
  app_detail::components::build_table(window, *record);
  return Status::kOk;
}

Expected<int, Status> Window::table_column_count(NodeId table) const {
  const NodeRecord* record = app_detail::record_of(*state_, table);
  if (record == nullptr) {
    return Unexpected(Status::kInvalidNode);
  }
  if (record->kind != NodeKind::kTable) {
    return Unexpected(Status::kUnsupported);
  }
  return static_cast<int>(std::get<TableState>(record->state).columns.size());
}

Status Window::table_set_rows(NodeId table, std::vector<std::vector<std::string>> rows) {
  app_detail::WinState& window = *state_;
  NodeRecord* record = nullptr;
  if (const Status status = app_detail::table_of(window, table, record);
      status != Status::kOk) {
    return status;
  }
  TableState& c = std::get<TableState>(record->state);
  c.rows = rows;
  dg::ControlContext ctx = app_detail::components::controls(window);
  c.table.set_rows(ctx, std::move(rows));
  return Status::kOk;
}

Expected<int, Status> Window::table_selected(NodeId table) const {
  const NodeRecord* record = app_detail::record_of(*state_, table);
  if (record == nullptr) {
    return Unexpected(Status::kInvalidNode);
  }
  if (record->kind != NodeKind::kTable) {
    return Unexpected(Status::kUnsupported);
  }
  return std::get<TableState>(record->state).table.selected().value_or(-1);
}

Status Window::table_select(NodeId table, int row) {
  app_detail::WinState& window = *state_;
  NodeRecord* record = nullptr;
  if (const Status status = app_detail::table_of(window, table, record);
      status != Status::kOk) {
    return status;
  }
  TableState& c = std::get<TableState>(record->state);
  if (row < -1 || row >= c.table.row_count()) {
    return Status::kValueOutOfRange;
  }
  dg::ControlContext ctx = app_detail::components::controls(window);
  if (row == -1) {
    c.table.set_rows(ctx, c.rows);  // clears the selection
  } else {
    (void)c.table.select(ctx, row);
  }
  return Status::kOk;
}

Status Window::table_set_sort_indicator(NodeId table, std::optional<int> column,
                                        bool ascending) {
  app_detail::WinState& window = *state_;
  NodeRecord* record = nullptr;
  if (const Status status = app_detail::table_of(window, table, record);
      status != Status::kOk) {
    return status;
  }
  TableState& c = std::get<TableState>(record->state);
  if (column.has_value() && (*column < 0 || *column >= static_cast<int>(c.columns.size()))) {
    return Status::kValueOutOfRange;
  }
  c.sort_column = column;
  c.sort_ascending = ascending;
  dg::ControlContext ctx = app_detail::components::controls(window);
  c.table.set_sort_indicator(ctx, c.sort_column, c.sort_ascending);
  return Status::kOk;
}

}  // namespace dg
