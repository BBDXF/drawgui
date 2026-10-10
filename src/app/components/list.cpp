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

// A rich pool row's children: check box, icon, secondary text, then one
// image per trailing action (kMaxRowActions of them).
constexpr std::size_t kPartCheck = 0;
constexpr std::size_t kPartIcon = 1;
constexpr std::size_t kPartSecondary = 2;
constexpr std::size_t kPartFirstAction = 3;
constexpr std::size_t kMaxRowActions = 8;
constexpr std::size_t kRowParts = kPartFirstAction + kMaxRowActions;

// Gives every pool row its part children (once); the pool size is kept.
void make_list_rich(WinState& window, NodeRecord& record) {
  ListState& state = std::get<ListState>(record.state);
  if (state.rich) {
    return;
  }
  state.rich = true;
  dg::ControlContext ctx = components::controls(window);
  for (const dg::NodeId row : window.widgets.at(record.id).list_pool) {
    for (std::size_t i = 0; i < kRowParts; ++i) {
      (void)dg::control_visuals::add_part(ctx, row);
    }
  }
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

namespace {

// Places one rich row's parts; returns the main text's {pad_left, pad_right}.
std::pair<int, int> paint_row_parts(WinState& window, dg::ControlContext& ctx,
                                    const ListState& list, dg::NodeId row,
                                    const dg::RowItem* item) {
  namespace cv = dg::control_visuals;
  dg::RenderTree& tree = window.tree.render();
  const std::vector<dg::NodeId> kids = tree.children(row);
  if (kids.size() < kRowParts) {
    return {0, 0};
  }
  const dg::PixelRect bounds = tree.local_bounds(row);
  const int h = bounds.height;
  const int pad = ctx.style.padding;
  int x = pad;
  const std::optional<bool> check = item != nullptr ? item->check : std::nullopt;
  cv::paint_check(ctx, kids[kPartCheck], x, h, check);
  if (check.has_value()) {
    x += cv::kCheckSize + cv::kPartGap;
  }
  const dg::ImageId icon = item != nullptr ? item->icon : dg::ImageId{};
  cv::paint_icon(ctx, kids[kPartIcon],
                 dg::PixelRect{x, std::max(0, (h - cv::kIconSize) / 2), cv::kIconSize,
                               cv::kIconSize},
                 icon);
  if (icon.is_valid()) {
    x += cv::kIconSize + cv::kPartGap;
  }
  int right = bounds.width - (pad / 2);
  const std::size_t shown =
      item == nullptr ? 0
                      : std::min<std::size_t>({item->trailing_actions, list.actions.size(),
                                               kMaxRowActions});
  for (std::size_t k = kMaxRowActions; k-- > 0;) {
    const dg::NodeId node = kids[kPartFirstAction + k];
    if (k >= shown) {
      cv::hide_part(ctx, node);
      continue;
    }
    right -= cv::kActionBox;
    const int inset = (cv::kActionBox - cv::kIconSize) / 2;
    const dg::ImageId image = list.actions[k].icon;
    if (image.is_valid()) {
      cv::paint_icon(ctx, node,
                     dg::PixelRect{right + inset, std::max(0, (h - cv::kIconSize) / 2),
                                   cv::kIconSize, cv::kIconSize},
                     image);
    } else {
      cv::hide_part(ctx, node);
    }
  }
  int pad_right = bounds.width - right;
  const std::string& secondary = item != nullptr ? item->secondary : std::string();
  if (!secondary.empty()) {
    const int width = cv::text_width(ctx, ctx.style.font_size, secondary);
    const int left = std::max(x, right - width - (pad / 2));
    cv::paint_part_text(ctx, kids[kPartSecondary],
                        dg::PixelRect{left, 0, std::max(0, right - left - (pad / 2)), h},
                        secondary, dg::TextAlign::kRight);
    pad_right = bounds.width - left;
  } else {
    cv::hide_part(ctx, kids[kPartSecondary]);
  }
  return {x - pad, std::max(0, pad_right - pad)};
}

}  // namespace

// Paints recycled list rows with the items they now show.
void paint_list_rows(WinState& window, const NodeRecord& record,
                     const std::vector<dg::ListSlot>& rows) {
  const ListState& list = std::get<ListState>(record.state);
  dg::RenderTree& tree = window.tree.render();
  const ListPalette palette = list_palette_for(window.app);
  std::optional<dg::ControlContext> ctx;
  if (list.rich) {
    ctx.emplace(components::controls(window));
  }
  for (const dg::ListSlot& row : rows) {
    const bool in_range = row.logical_index >= 0 &&
                          static_cast<std::size_t>(row.logical_index) < list.items.size();
    const auto index = static_cast<std::size_t>(row.logical_index);
    dg::TextStyle text = tree.style(row.node).text;
    text.text = in_range ? list.items[index] : std::string();
    text.color = palette.text;
    if (ctx.has_value()) {
      const dg::RowItem* item = in_range && index < list.rows.size() ? &list.rows[index] : nullptr;
      const auto [left, right] = paint_row_parts(window, *ctx, list, row.node, item);
      text.pad_left = left;
      text.pad_right = right;
    }
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

namespace {

struct ListRowAt {
  NodeRecord* record = nullptr;
  int index = -1;
  // The rich part pressed, if any (kRowParts = the row itself).
  std::size_t part = kRowParts;
};

// The list row (and part) under `at`, for an enabled list.
ListRowAt list_row_at(WinState& window, dg::PixelPoint at) {
  dg::RenderTree& tree = window.tree.render();
  const std::optional<dg::NodeId> hit = tree.hit_test(at);
  const std::optional<dg::NodeId> list =
      hit.has_value() ? window.widgets.list_owner_of(tree, *hit) : std::nullopt;
  if (!list.has_value() || dg::is_disabled(tree, *list)) {
    return {};
  }
  NodeRecord* record = record_of(window, *list);
  if (record == nullptr || std::get_if<ListState>(&record->state) == nullptr) {
    return {};
  }
  const ListState& state = std::get<ListState>(record->state);
  const dg::Widget& widget = window.widgets.at(*list);
  // Climb from the hit node to its pool row.
  dg::NodeId node = *hit;
  dg::NodeId child = node;
  while (node != *list) {
    const auto slot = std::find(widget.list_pool.begin(), widget.list_pool.end(), node);
    if (slot != widget.list_pool.end()) {
      const auto i = static_cast<std::size_t>(slot - widget.list_pool.begin());
      const int index = i < widget.list_assigned.size() ? widget.list_assigned[i] : -1;
      if (index < 0 || static_cast<std::size_t>(index) >= state.items.size()) {
        return {};
      }
      ListRowAt out{record, index, kRowParts};
      if (child != node && state.rich) {
        const std::vector<dg::NodeId> kids = tree.children(node);
        const auto part = static_cast<std::size_t>(
            std::find(kids.begin(), kids.end(), child) - kids.begin());
        out.part = part;
      }
      return out;
    }
    const dg::NodeId parent = tree.parent(node);
    if (parent == node) {
      break;
    }
    child = node;
    node = parent;
  }
  return {};
}

}  // namespace

// A primary press on a list row selects it and queues a ListSelectEvent,
// then a ClickEvent naming the row. A press on a row's check box or action
// button reports RowPartEvent instead and leaves the selection alone. True
// when `at` hit a row of an enabled list.
bool press_list_row(WinState& window, dg::PixelPoint at, int clicks) {
  const ListRowAt row = list_row_at(window, at);
  if (row.record == nullptr) {
    return false;
  }
  NodeRecord& record = *row.record;
  ListState& state = std::get<ListState>(record.state);
  const auto index = static_cast<std::size_t>(row.index);
  const dg::RowItem* item = index < state.rows.size() ? &state.rows[index] : nullptr;
  using Part = dg::events::RowPartEvent::Part;
  if (item != nullptr && row.part == kPartCheck && item->check.has_value()) {
    state.rows[index].check = !*item->check;
    repaint_list(window, record);
    emit(window.app, dg::events::RowPartEvent{window.sdl_window, record.id, row.index, -1,
                                              Part::kCheck, -1, state.rows[index].key});
    return true;
  }
  if (item != nullptr && row.part >= kPartFirstAction && row.part < kRowParts) {
    const int action = static_cast<int>(row.part - kPartFirstAction);
    emit(window.app, dg::events::RowPartEvent{window.sdl_window, record.id, row.index, -1,
                                              Part::kAction, action, item->key});
    return true;
  }
  state.selected = row.index;
  repaint_list(window, record);
  emit(window.app, dg::events::ListSelectEvent{window.sdl_window, record.id, row.index, clicks});
  dg::events::ClickEvent click{window.sdl_window, record.id, at.x, at.y, clicks, row.index};
  click.item_key = item != nullptr ? item->key : 0;
  emit(window.app, click);
  return true;
}

RowHit list_context_row(WinState& window, NodeRecord& record, dg::PixelPoint at) {
  const ListRowAt row = list_row_at(window, at);
  if (row.record != &record) {
    return {};
  }
  ListState& state = std::get<ListState>(record.state);
  if (state.selected != row.index) {
    state.selected = row.index;
    repaint_list(window, record);
    emit(window.app, dg::events::ListSelectEvent{window.sdl_window, record.id, row.index, 1});
  }
  const auto index = static_cast<std::size_t>(row.index);
  return RowHit{row.index, std::nullopt, index < state.rows.size() ? state.rows[index].key : 0};
}

void hover_list_actions(WinState& window, dg::PixelPoint at) {
  const ListRowAt row = list_row_at(window, at);
  const std::string* text = nullptr;
  if (row.record != nullptr && row.part >= kPartFirstAction && row.part < kRowParts) {
    const ListState& state = std::get<ListState>(row.record->state);
    const std::size_t action = row.part - kPartFirstAction;
    if (action < state.actions.size() && !state.actions[action].tooltip.empty()) {
      text = &state.actions[action].tooltip;
    }
  }
  // Give back every other list's borrowed tooltip.
  for (NodeRecord* record : records_in_order(window)) {
    ListState* state = std::get_if<ListState>(&record->state);
    if (state == nullptr || !state->tooltip_borrowed) {
      continue;
    }
    if (record->tooltip != state->borrowed_tooltip) {
      state->tooltip_borrowed = false;  // the app set its own since
      continue;
    }
    if (record == row.record && text != nullptr && *text == state->borrowed_tooltip) {
      continue;
    }
    record->tooltip = state->own_tooltip;
    state->tooltip_borrowed = false;
    dg::UiContext ctx = components::ui(window);
    if (window.tooltip.shown_for() == record->id) {
      window.tooltip.hide(ctx);
    }
    if (record->tooltip.empty()) {
      window.tooltip.unbind(record->id);
    } else {
      window.tooltip.bind(record->id, record->tooltip);
    }
  }
  if (text == nullptr) {
    return;
  }
  ListState& state = std::get<ListState>(row.record->state);
  if (state.tooltip_borrowed) {
    return;
  }
  state.own_tooltip = row.record->tooltip;
  state.borrowed_tooltip = *text;
  state.tooltip_borrowed = true;
  row.record->tooltip = *text;
  window.tooltip.bind(row.record->id, *text);
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
  state.rows.clear();
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

namespace dg {

Status ListRef::set_rows(std::vector<RowItem> rows, int item_height) {
  app_detail::WinState& win = window();
  const auto found = component_of<ListState>(win, node());
  if (found.status != Status::kOk) {
    return found.status;
  }
  for (const RowItem& row : rows) {
    if (row.trailing_actions > app_detail::kMaxRowActions) {
      return Status::kValueOutOfRange;
    }
  }
  std::vector<std::string> texts;
  texts.reserve(rows.size());
  for (const RowItem& row : rows) {
    texts.push_back(row.text);
  }
  const Status status = set_items(std::move(texts), item_height);
  if (status != Status::kOk) {
    return status;
  }
  ListState& state = *found.state;
  const bool parts = std::any_of(rows.begin(), rows.end(), [](const RowItem& row) {
    return row.icon.is_valid() || row.check.has_value() || !row.secondary.empty() ||
           row.trailing_actions > 0;
  });
  state.rows = std::move(rows);
  if (parts) {
    app_detail::make_list_rich(win, *found.record);
  }
  app_detail::repaint_list(win, *found.record);
  return Status::kOk;
}

Status ListRef::set_row_actions(std::vector<RowAction> actions) {
  app_detail::WinState& win = window();
  const auto found = component_of<ListState>(win, node());
  if (found.status != Status::kOk) {
    return found.status;
  }
  if (actions.size() > app_detail::kMaxRowActions) {
    return Status::kValueOutOfRange;
  }
  found.state->actions = std::move(actions);
  if (!found.state->actions.empty()) {
    app_detail::make_list_rich(win, *found.record);
  }
  app_detail::repaint_list(win, *found.record);
  return Status::kOk;
}

Expected<bool, Status> ListRef::row_checked(int row) const {
  const auto found = component_of<ListState>(std::as_const(window()), node());
  if (found.status != Status::kOk) {
    return Unexpected(found.status);
  }
  const ListState& state = *found.state;
  if (row < 0 || static_cast<std::size_t>(row) >= state.items.size()) {
    return Unexpected(Status::kValueOutOfRange);
  }
  const std::optional<bool> check = static_cast<std::size_t>(row) < state.rows.size()
                                        ? state.rows[static_cast<std::size_t>(row)].check
                                        : std::nullopt;
  if (!check.has_value()) {
    return Unexpected(Status::kNotApplicable);
  }
  return *check;
}

}  // namespace dg

namespace dg {

Expected<std::uint64_t, Status> ListRef::key_of(int index) const {
  const auto found = component_of<ListState>(std::as_const(window()), node());
  if (found.status != Status::kOk) {
    return Unexpected(found.status);
  }
  const ListState& state = *found.state;
  if (index < 0 || static_cast<std::size_t>(index) >= state.items.size()) {
    return Unexpected(Status::kValueOutOfRange);
  }
  return static_cast<std::size_t>(index) < state.rows.size()
             ? state.rows[static_cast<std::size_t>(index)].key
             : std::uint64_t{0};
}

Expected<int, Status> ListRef::index_of(std::uint64_t key) const {
  const auto found = component_of<ListState>(std::as_const(window()), node());
  if (found.status != Status::kOk) {
    return Unexpected(found.status);
  }
  if (key == 0) {
    return Unexpected(Status::kInvalidArgument);
  }
  const std::vector<RowItem>& rows = found.state->rows;
  for (std::size_t i = 0; i < rows.size(); ++i) {
    if (rows[i].key == key) {
      return static_cast<int>(i);
    }
  }
  return Unexpected(Status::kUnknownId);
}

Status ListRef::set_row_keys(std::vector<std::uint64_t> keys) {
  const auto found = component_of<ListState>(window(), node());
  if (found.status != Status::kOk) {
    return found.status;
  }
  ListState& state = *found.state;
  if (keys.size() != state.items.size()) {
    return Status::kInvalidArgument;
  }
  if (state.rows.size() != state.items.size()) {
    state.rows.clear();
    for (const std::string& text : state.items) {
      RowItem row;
      row.text = text;
      state.rows.push_back(std::move(row));
    }
  }
  for (std::size_t i = 0; i < keys.size(); ++i) {
    state.rows[i].key = keys[i];
  }
  return Status::kOk;
}

Status ListRef::select_key(std::uint64_t key) {
  const Expected<int, Status> index = index_of(key);
  return index ? select(index.value()) : index.error();
}

}  // namespace dg
