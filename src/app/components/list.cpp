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
#include "app/internal.h"
#include "ui/control_visuals.h"

namespace dg::app_detail {
namespace {

// The default box. A list recycles kListPool rows, so at most kListPool - 2
// rows can be visible at once (list_sync() needs one spare row at each edge).
constexpr int kListWidth = 240;
constexpr int kListHeight = 200;
constexpr int kListPool = 32;
// The frame TreeView / Table draw (control_visuals' build_frame()): a 1px
// color.border outline around a color.surface fill, square corners.
constexpr int kListFrame = 1;

// Everything comes from the same dg::ControlStyle TreeView / Table are
// built from (control_style_from_theme()), so the three share one border,
// fill, text colour, soft selection tint, row height, inset and font size.
struct ListPalette {
  dg::Color text;
  dg::Color selection;
  dg::Color fill;
  dg::Color border;
};

ListPalette list_palette_for(const AppState& app) {
  const dg::ControlStyle style = components::control_style(app);
  return ListPalette{style.on_surface, dg::control_visuals::selection_fill(style), style.surface,
                     style.border};
}

}  // namespace

// The list node's own frame, re-themed; a colour the app overrode is kept.
void restyle_list_frame(WinState& window, const NodeRecord& record) {
  const ListState& list = std::get<ListState>(record.state);
  dg::RenderTree& tree = window.tree.render();
  const ListPalette palette = list_palette_for(window.app);
  dg::NodeStyle style = tree.style(record.id);
  bool changed = false;
  if (style.fill == list.frame_fill && style.fill != palette.fill) {
    style.fill = palette.fill;
    changed = true;
  }
  if (style.border_color == list.frame_border && style.border_color != palette.border) {
    style.border_color = palette.border;
    changed = true;
  }
  list.frame_fill = palette.fill;
  list.frame_border = palette.border;
  if (changed) {
    tree.set_style(record.id, style);
  }
}

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
  // trailing edge (examples/15_list's recipe), framed like TreeView /
  // Table: 1px color.border outline over a color.surface fill.
  const ListPalette palette = list_palette_for(window.app);
  box.kind = dg::LayoutKind::kLeaf;
  box.width = kListWidth;
  box.height = kListHeight;
  box.scroll_axis = dg::ScrollAxis::kNone;
  box.border = dg::EdgeInsets::all(kListFrame);
  box.padding.right = window.widgets.scrollbar_style().size;
  style.overflow = dg::Overflow::kClip;
  style.fill = palette.fill;
  style.border_color = palette.border;
  style.border_width = dg::BorderWidths::all(static_cast<float>(kListFrame));
}

void compose_list(WinState& window, NodeRecord& record) {
  ListState& state = record.state.emplace<ListState>();
  {
    const dg::NodeStyle& own = window.tree.render().style(record.id);
    state.frame_fill = own.fill;
    state.frame_border = own.border_color;
  }
  const dg::NodeId new_id = record.id;
  // A fixed pool of rows, each 100% of the content width (the gutter is
  // padding), recycled by list_sync()/list_scroll_by().
  dg::Widget widget;
  widget.kind = dg::WidgetKind::kList;
  widget.list_axis = dg::ScrollAxis::kVertical;
  widget.list_item_count = 0;  // list_set_items() fills it
  const dg::ControlStyle look = components::control_style(window.app);
  widget.list_item_extent = look.row_height;
  dg::BoxStyle row_box;
  row_box.height = look.row_height;
  row_box.percent.width = 100.0F;
  dg::NodeStyle row_style;
  row_style.text.font = window.app.default_font;
  row_style.text.size = look.font_size;
  row_style.text.align = dg::TextAlign::kLeft;
  row_style.text.inset = look.padding;
  row_style.radii = dg::Radii::all(static_cast<float>(look.radius));
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
using app_detail::components::component_of;

Status ListRef::set_items(std::vector<std::string> items, int item_height) {
  app_detail::WinState& win = window();
  const auto found = component_of<ListState>(win, node());
  if (found.status != Status::kOk) {
    return found.status;
  }
  if (item_height < 0 || items.size() > limits::kListMaxItems) {
    return Status::kInvalidArgument;
  }
  ListState& state = *found.state;
  const app_detail::NodeRecord& record = *found.record;
  state.items = std::move(items);
  state.selected = -1;
  dg::RenderTree& tree = win.tree.render();
  dg::Widget updated = win.widgets.at(record.id);
  if (item_height > 0 && item_height != updated.list_item_extent) {
    updated.list_item_extent = item_height;
    for (const dg::NodeId row : updated.list_pool) {
      dg::BoxStyle box = win.tree.box(row);
      box.height = item_height;
      win.tree.set_box(row, box);
    }
  }
  updated.list_item_count = static_cast<int>(state.items.size());
  win.widgets.attach(record.id, updated);
  const int extent = app_detail::viewport_extent(win, record.id);
  app_detail::paint_list_rows(
      win, record,
      win.widgets.list_reset(tree, record.id, static_cast<int>(state.items.size()), extent));
  win.widgets.sync_scrollbar(tree, record.id, extent);
  win.relaid = true;
  return Status::kOk;
}

Expected<int, Status> ListRef::selected() const {
  const auto found = component_of<ListState>(std::as_const(window()), node());
  if (found.status != Status::kOk) {
    return Unexpected(found.status);
  }
  return found.state->selected;
}

Status ListRef::select(int index) {
  app_detail::WinState& win = window();
  const auto found = component_of<ListState>(win, node());
  if (found.status != Status::kOk) {
    return found.status;
  }
  if (index < -1 || index >= static_cast<int>(found.state->items.size())) {
    return Status::kValueOutOfRange;
  }
  found.state->selected = index;
  app_detail::repaint_list(win, *found.record);
  return Status::kOk;
}

}  // namespace dg
