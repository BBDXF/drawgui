// NodeKind::kList: a fixed pool of recycled rows painted from the items
// Window::list_set_items() copied, with a scrollbar and a selected row.

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "app/components/components.h"
#include "app/components/editors.h"
#include "app/internal.h"

namespace dg::app_detail {
namespace {

// The default box. A list recycles kListPool rows, so at most kListPool - 2
// rows can be visible at once (list_sync() needs one spare row at each edge).
constexpr int kListWidth = 240;
constexpr int kListHeight = 200;
constexpr int kListPool = 32;
constexpr int kListItemHeight = 28;
constexpr int kListRowInset = 8;
constexpr std::uint32_t kListSelectionAlpha = 0x66;

// The rows: text in color.on-surface, the selected row filled with
// color.primary made translucent (the editors' selection rule).
struct ListPalette {
  dg::Color text = dg::Color::from_argb(kTextFieldTextColorArgb);
  dg::Color selection = dg::Color::from_argb((kListSelectionAlpha << 24U) |
                                             (kTextFieldBorderFocusedArgb & 0xFFFFFFU));
};

ListPalette list_palette_for(const AppState& app) {
  const auto [theme, variant] = theme_for(app);
  ListPalette palette;
  if (theme == nullptr) {
    return palette;
  }
  if (const std::optional<dg::Color> c =
          theme->color_value(DG_TOKEN_COLOR_ON_SURFACE, variant)) {
    palette.text = *c;
  }
  if (const std::optional<dg::Color> c = theme->color_value(DG_TOKEN_COLOR_PRIMARY, variant)) {
    palette.selection =
        dg::Color::from_argb((kListSelectionAlpha << 24U) | (c->argb() & 0xFFFFFFU));
  }
  return palette;
}

dg::Status list_of(WinState& window, dg::NodeId id, NodeRecord*& out) {
  out = record_of(window, id);
  if (out == nullptr) {
    return dg::Status::kInvalidNode;
  }
  if (out->kind != NodeKind::kList) {
    return dg::Status::kUnsupported;
  }
  return dg::Status::kOk;
}

}  // namespace

// Paints recycled list rows with the items they now show.
void paint_list_rows(WinState& window, const NodeRecord& record,
                     const std::vector<dg::ListSlot>& rows) {
  const ListState& list = std::get<ListState>(record.state);
  dg::RenderTree& tree = window.tree.render();
  const ListPalette palette = list_palette_for(window.app);
  for (const dg::ListSlot& row : rows) {
    const bool in_range = row.logical_index >= 0 &&
                          static_cast<std::size_t>(row.logical_index) < list.items.size();
    dg::TextStyle text = tree.style(row.node).text;
    text.text =
        in_range ? list.items[static_cast<std::size_t>(row.logical_index)] : std::string();
    text.color = palette.text;
    tree.set_text(row.node, text);
    tree.set_fill(row.node, in_range && row.logical_index == list.selected ? palette.selection
                                                                           : dg::Color{});
  }
}

// Every pool row of a list that currently shows an item, repainted (after a
// selection or theme change).
void repaint_list(WinState& window, const NodeRecord& record) {
  const dg::Widget& widget = window.widgets.at(record.id);
  std::vector<dg::ListSlot> rows;
  for (std::size_t i = 0; i < widget.list_pool.size() && i < widget.list_assigned.size(); ++i) {
    if (widget.list_assigned[i] >= 0) {
      rows.push_back(dg::ListSlot{widget.list_pool[i], widget.list_assigned[i]});
    }
  }
  paint_list_rows(window, record, rows);
}

void apply_scroll_result(WinState& window, dg::NodeId owner, const dg::ScrollResult& result) {
  if (result.slots.empty()) {
    return;
  }
  const NodeRecord* record = record_of(window, owner);
  if (record != nullptr && record->kind == NodeKind::kList) {
    paint_list_rows(window, *record, result.slots);
  }
}

// A primary press on a list row selects it and queues a ListSelectEvent.
// True when `at` hit a row of an enabled list.
bool press_list_row(WinState& window, dg::PixelPoint at, int clicks) {
  dg::RenderTree& tree = window.tree.render();
  const std::optional<dg::NodeId> hit = tree.hit_test(at);
  const std::optional<dg::NodeId> list =
      hit.has_value() ? window.widgets.list_owner_of(tree, *hit) : std::nullopt;
  if (!list.has_value() || dg::is_disabled(tree, *list)) {
    return false;
  }
  const dg::Widget& widget = window.widgets.at(*list);
  NodeRecord* record = record_of(window, *list);
  if (record == nullptr) {
    return false;
  }
  ListState& state = std::get<ListState>(record->state);
  for (std::size_t i = 0; i < widget.list_pool.size() && i < widget.list_assigned.size(); ++i) {
    const int index = widget.list_assigned[i];
    if (widget.list_pool[i] != *hit || index < 0 ||
        static_cast<std::size_t>(index) >= state.items.size()) {
      continue;
    }
    state.selected = index;
    repaint_list(window, *record);
    emit(window.app, dg::events::ListSelectEvent{window.sdl_window, record->id, index, clicks});
    return true;
  }
  return false;
}

// Vertical only, and DG_PROP_FONT_SIZE goes to every recycled row so the
// size survives recycling.
dg::Status set_list_prop(WinState& window, const NodeRecord& record, std::uint16_t prop_id,
                         const dg::PropValue& value) {
  if (prop_id == DG_PROP_SCROLL_AXIS) {
    return dg::Status::kUnsupported;
  }
  for (const dg::NodeId row : window.widgets.at(record.id).list_pool) {
    const dg::PropWrite result =
        dg::set_prop(window.tree, row, static_cast<dg_prop_id>(prop_id), value);
    if (!result.ok()) {
      return to_status(result.status);
    }
  }
  return dg::Status::kOk;
}

// After a layout pass that re-placed nodes: the rows and the scrollbar.
void resync_list(WinState& window, const NodeRecord& record) {
  dg::RenderTree& tree = window.tree.render();
  const int extent = viewport_extent(window, record.id);
  paint_list_rows(
      window, record,
      window.widgets.list_reset(
          tree, record.id, static_cast<int>(std::get<ListState>(record.state).items.size()),
          extent));
  window.widgets.sync_scrollbar(tree, record.id, extent);
}

namespace components {

void list_defaults(const WinState& window, dg::BoxStyle& box, dg::NodeStyle& style) {
  // A clipping kLeaf viewport with the scrollbar's gutter reserved on its
  // trailing edge (examples/15_list's recipe). No fill.
  box.kind = dg::LayoutKind::kLeaf;
  box.width = kListWidth;
  box.height = kListHeight;
  box.scroll_axis = dg::ScrollAxis::kNone;
  box.padding.right = window.widgets.scrollbar_style().size;
  style.overflow = dg::Overflow::kClip;
}

void compose_list(WinState& window, NodeRecord& record) {
  record.state.emplace<ListState>();
  const dg::NodeId new_id = record.id;
  // A fixed pool of rows, each 100% of the content width (the gutter is
  // padding), recycled by list_sync()/list_scroll_by().
  dg::Widget widget;
  widget.kind = dg::WidgetKind::kList;
  widget.list_axis = dg::ScrollAxis::kVertical;
  widget.list_item_count = 0;  // list_set_items() fills it
  widget.list_item_extent = kListItemHeight;
  dg::BoxStyle row_box;
  row_box.height = kListItemHeight;
  row_box.percent.width = 100.0F;
  dg::NodeStyle row_style;
  row_style.text.font = window.app.default_font;
  row_style.text.size = default_text_size();
  row_style.text.align = dg::TextAlign::kLeft;
  row_style.text.inset = kListRowInset;
  for (int i = 0; i < kListPool; ++i) {
    widget.list_pool.push_back(window.tree.add_child(new_id, row_box, row_style));
  }
  window.widgets.attach(new_id, widget);
  const dg::NodeId track = window.tree.add_child(new_id, dg::BoxStyle{}, dg::NodeStyle{});
  const dg::NodeId thumb = window.tree.add_child(track, dg::BoxStyle{}, dg::NodeStyle{});
  window.widgets.attach_scrollbar(window.tree.render(), new_id, track, thumb);
  window.relaid = true;
}

}  // namespace components
}  // namespace dg::app_detail

namespace dg {

using app_detail::ListState;
using app_detail::NodeRecord;

Status Window::list_set_items(NodeId list, std::vector<std::string> items, int item_height) {
  app_detail::WinState& window = *state_;
  NodeRecord* record = nullptr;
  if (const Status status = app_detail::list_of(window, list, record); status != Status::kOk) {
    return status;
  }
  if (item_height < 0) {
    return Status::kInvalidArgument;
  }
  ListState& state = std::get<ListState>(record->state);
  state.items = std::move(items);
  state.selected = -1;
  dg::RenderTree& tree = window.tree.render();
  dg::Widget updated = window.widgets.at(record->id);
  if (item_height > 0 && item_height != updated.list_item_extent) {
    updated.list_item_extent = item_height;
    for (const dg::NodeId row : updated.list_pool) {
      dg::BoxStyle box = window.tree.box(row);
      box.height = item_height;
      window.tree.set_box(row, box);
    }
  }
  updated.list_item_count = static_cast<int>(state.items.size());
  window.widgets.attach(record->id, updated);
  const int extent = app_detail::viewport_extent(window, record->id);
  app_detail::paint_list_rows(
      window, *record,
      window.widgets.list_reset(tree, record->id, static_cast<int>(state.items.size()),
                                extent));
  window.widgets.sync_scrollbar(tree, record->id, extent);
  window.relaid = true;
  return Status::kOk;
}

Expected<int, Status> Window::list_selected(NodeId list) const {
  const NodeRecord* record = app_detail::record_of(*state_, list);
  if (record == nullptr) {
    return Unexpected(Status::kInvalidNode);
  }
  if (record->kind != NodeKind::kList) {
    return Unexpected(Status::kUnsupported);
  }
  return std::get<ListState>(record->state).selected;
}

Status Window::list_select(NodeId list, int index) {
  app_detail::WinState& window = *state_;
  NodeRecord* record = nullptr;
  if (const Status status = app_detail::list_of(window, list, record); status != Status::kOk) {
    return status;
  }
  ListState& state = std::get<ListState>(record->state);
  if (index < -1 || index >= static_cast<int>(state.items.size())) {
    return Status::kValueOutOfRange;
  }
  state.selected = index;
  app_detail::repaint_list(window, *record);
  return Status::kOk;
}

}  // namespace dg
