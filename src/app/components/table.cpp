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

std::uint64_t key_at(const TableState& state, int row) {
  return row >= 0 && static_cast<std::size_t>(row) < state.keys.size()
             ? state.keys[static_cast<std::size_t>(row)]
             : 0;
}

// Flips a cell's check box (the Table's copy and the rebuild copy) and
// reports RowPartEvent{kCheck}.
bool toggle_cell(WinState& window, NodeRecord& record, TableState& state, int row, int column) {
  dg::ControlContext ctx = components::controls(window);
  if (!state.table.toggle_check(ctx, row, column)) {
    return false;
  }
  const auto r = static_cast<std::size_t>(row);
  const auto c = static_cast<std::size_t>(column);
  if (r < state.parts.size() && c < state.parts[r].size()) {
    state.parts[r][c].check = state.table.cell_check(row, column);
  }
  emit(window.app, dg::events::RowPartEvent{window.sdl_window, record.id, row, column,
                                            dg::events::RowPartEvent::Part::kCheck, -1,
                                            key_at(state, row)});
  return true;
}

}  // namespace

RowHit table_context_row(WinState& window, NodeRecord& record, dg::PixelPoint at) {
  TableState* state = std::get_if<TableState>(&record.state);
  if (state == nullptr) {
    return {};
  }
  dg::ControlContext ctx = components::controls(window);
  const std::optional<dg::Table::Hit> hit = state->table.hit(ctx, at);
  if (!hit.has_value()) {
    return {};
  }
  if (state->table.select(ctx, hit->row)) {
    table_result(window, record, dg::TableResult{dg::TableEvent::kSelected, hit->row, -1}, 1);
  }
  return RowHit{hit->row, hit->column >= 0 ? std::optional<int>(hit->column) : std::nullopt,
                key_at(*state, hit->row)};
}

bool toggle_table_row_check(WinState& window, NodeRecord& record) {
  TableState* state = std::get_if<TableState>(&record.state);
  if (state == nullptr || window.focus.current() != state->table.root()) {
    return false;
  }
  const std::optional<int> row = state->table.selected();
  if (!row.has_value()) {
    return false;
  }
  const int columns = static_cast<int>(state->columns.size());
  for (int column = 0; column < columns; ++column) {
    if (state->table.cell_check(*row, column).has_value()) {
      return toggle_cell(window, record, *state, *row, column);
    }
  }
  return false;
}

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
  spec.parts = c.parts;
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
  const bool press =
      event.action == dg::PointerAction::kDown && event.button == dg::PointerButton::kPrimary;
  const dg::PixelPoint at{event.x, event.y};
  std::optional<dg::Table::Hit> hit;
  if (press && !dg::is_disabled(ctx.tree(), record.id) &&
      ctx.tables.widgets.widget_at(ctx.tree(), at) == state.table.root()) {
    hit = state.table.hit(ctx, at);
    // A check box beats the row: it toggles without selecting.
    if (hit.has_value() && hit->check && toggle_cell(window, record, state, hit->row, hit->column)) {
      ctx.tables.focus.set_guarded(ctx.tree(), state.table.root());
      return true;
    }
  }
  const dg::TableResult r = state.table.handle_pointer(ctx, event);
  table_result(window, record, r, std::max(event.clicks, 1));
  if (hit.has_value() && r.row >= 0) {
    emit(window.app,
         dg::events::ClickEvent{window.sdl_window, record.id, event.x, event.y,
                                std::max(event.clicks, 1), r.row,
                                hit->column >= 0 ? std::optional<int>(hit->column) : std::nullopt,
                                key_at(state, r.row)});
  }
  return r.event != dg::TableEvent::kIgnored;
}

bool control_key(WinState& window, NodeRecord& record, TableState& state,
                 const dg::KeyEvent& event) {
  // Space toggles the selected row's check box.
  if (event.logical_key == dg::LogicalKey::kSpace && event.mods == dg::Modifier::kNone &&
      event.action == dg::KeyAction::kDown && !dg::is_disabled(window.tree.render(), record.id) &&
      toggle_table_row_check(window, record)) {
    return true;
  }
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
  c.parts.clear();
  c.keys.clear();
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
  c.parts.clear();
  c.keys.clear();
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
  if (row == -1 && !c.parts.empty()) {
    app_detail::components::build_table(win, *found.record);  // keeps the parts
  } else if (row == -1) {
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

namespace dg {

Status TableRef::set_cells(std::vector<std::vector<TableCell>> rows) {
  app_detail::WinState& win = window();
  const auto found = component_of<TableState>(win, node());
  if (found.status != Status::kOk) {
    return found.status;
  }
  TableState& c = *found.state;
  if (rows.size() * std::max<std::size_t>(c.columns.size(), 1) > limits::kMaxDataItems) {
    return Status::kInvalidArgument;
  }
  c.rows.clear();
  c.parts.clear();
  c.keys.clear();
  c.rows.reserve(rows.size());
  c.parts.reserve(rows.size());
  for (std::vector<TableCell>& row : rows) {
    std::vector<std::string> texts;
    std::vector<dg::TableCellPart> parts;
    texts.reserve(row.size());
    parts.reserve(row.size());
    for (TableCell& cell : row) {
      texts.push_back(std::move(cell.text));
      parts.push_back(dg::TableCellPart{cell.icon, cell.check});
    }
    c.rows.push_back(std::move(texts));
    c.parts.push_back(std::move(parts));
  }
  // A rebuild: the pool gains (or drops) its per-cell part nodes.
  app_detail::components::build_table(win, *found.record);
  return Status::kOk;
}

Expected<bool, Status> TableRef::cell_checked(int row, int column) const {
  const auto found = component_of<TableState>(std::as_const(window()), node());
  if (found.status != Status::kOk) {
    return Unexpected(found.status);
  }
  const TableState& c = *found.state;
  if (row < 0 || row >= c.table.row_count() || column < 0 ||
      column >= static_cast<int>(c.columns.size())) {
    return Unexpected(Status::kValueOutOfRange);
  }
  const std::optional<bool> check = c.table.cell_check(row, column);
  if (!check.has_value()) {
    return Unexpected(Status::kNotApplicable);
  }
  return *check;
}

}  // namespace dg

namespace dg {

Status TableRef::set_row_keys(std::vector<std::uint64_t> keys) {
  const auto found = component_of<TableState>(window(), node());
  if (found.status != Status::kOk) {
    return found.status;
  }
  if (keys.size() != found.state->rows.size()) {
    return Status::kInvalidArgument;
  }
  found.state->keys = std::move(keys);
  return Status::kOk;
}

Expected<std::uint64_t, Status> TableRef::key_of(int index) const {
  const auto found = component_of<TableState>(std::as_const(window()), node());
  if (found.status != Status::kOk) {
    return Unexpected(found.status);
  }
  if (index < 0 || static_cast<std::size_t>(index) >= found.state->rows.size()) {
    return Unexpected(Status::kValueOutOfRange);
  }
  const std::vector<std::uint64_t>& keys = found.state->keys;
  return static_cast<std::size_t>(index) < keys.size() ? keys[static_cast<std::size_t>(index)]
                                                       : std::uint64_t{0};
}

Expected<int, Status> TableRef::index_of(std::uint64_t key) const {
  const auto found = component_of<TableState>(std::as_const(window()), node());
  if (found.status != Status::kOk) {
    return Unexpected(found.status);
  }
  if (key == 0) {
    return Unexpected(Status::kInvalidArgument);
  }
  const auto& keys = found.state->keys;
  const auto at = std::find(keys.begin(), keys.end(), key);
  if (at == keys.end()) {
    return Unexpected(Status::kUnknownId);
  }
  return static_cast<int>(at - keys.begin());
}

Status TableRef::select_key(std::uint64_t key) {
  const Expected<int, Status> index = index_of(key);
  return index ? select(index.value()) : index.error();
}

}  // namespace dg
