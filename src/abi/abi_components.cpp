// ABI 0.2 phase B: the C entry points of the src/ui component layer -
// menus, dropdowns, tooltips, dialogs, tabs, trees and tables. Each one
// validates its C arguments, builds the runtime's own C++ values from them,
// and calls dg::Window (src/app), which owns the components; their results
// reach the host as dg_events through the runtime's event queue.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "abi_impl.h"
#include "abi_internal.h"
#include "drawgui/app/limits.h"
#include "drawgui/render/node_lifecycle.h"

namespace dg::abi {
namespace {

constexpr std::uint32_t kMaxMenuItems = dg::limits::kMaxMenuItems;
constexpr std::uint32_t kMaxDialogButtons = dg::limits::kMaxDialogButtons;
constexpr std::uint32_t kMaxColumns = dg::limits::kMaxTableColumns;
constexpr std::uint32_t kMaxItems = dg::limits::kMaxDataItems;

// A live slot of `type_id`, or the DG_ERR_* saying why not.
struct Resolved {
  NodeSlot* slot = nullptr;
  AppImpl* impl = nullptr;
  dg::Window* window = nullptr;
  std::int32_t status = DG_ERR_OK;
};

Resolved resolve(dg_node_t* node_h, std::uint16_t type_id) {
  Resolved out;
  out.slot = slot_of(node_h);
  if (out.slot == nullptr) {
    out.status = DG_ERR_INVALID_HANDLE;
    return out;
  }
  if (out.slot->state != NodeSlot::State::kLive) {
    out.status = DG_ERR_NO_WINDOW;
    return out;
  }
  if (out.slot->type_id != type_id) {
    out.status = DG_ERR_UNSUPPORTED;
    return out;
  }
  out.impl = app_of(node_h->app);
  out.window = out.impl->windows[out.slot->window_index]->window;
  return out;
}

std::string text_or_empty(const char* text) {
  return text == nullptr ? std::string() : std::string(text);
}

// An Expected<int, Status> getter's answer: the value, else its DG_ERR_*.
std::int32_t value_or_error(const dg::Expected<int, dg::Status>& result) {
  return result ? result.value() : to_error_code(result.error());
}

}  // namespace

std::int32_t menu_open(dg_window_t* window_h, dg_node_t* anchor, std::int32_t x, std::int32_t y,
                       const char* const* labels, const std::uint32_t* flags,
                       std::uint32_t count) {
  WindowImpl* window = window_of(window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  if (labels == nullptr || count == 0 || count > kMaxMenuItems) {
    set_last_error("dg_menu_open: need 1..4096 labels");
    return DG_ERR_INVALID_ARGUMENT;
  }
  if (window->window->has_dialog()) {
    set_last_error("dg_menu_open: a dialog is open on this window");
    return DG_ERR_UNSUPPORTED;
  }
  std::optional<dg::NodeId> anchor_id;
  if (anchor != nullptr) {
    const NodeSlot* slot = slot_of(anchor);
    if (slot == nullptr) {
      return DG_ERR_INVALID_HANDLE;
    }
    if (anchor->app != window_h->app || slot->state != NodeSlot::State::kLive ||
        slot->window_index != window->self_index) {
      set_last_error("dg_menu_open: the anchor is not live in this window");
      return DG_ERR_INVALID_ARGUMENT;
    }
    anchor_id = slot->node_id;
  }
  std::vector<dg::MenuEntry> items;
  bool any_selectable = false;
  for (std::uint32_t i = 0; i < count; ++i) {
    const std::uint32_t bits = flags == nullptr ? 0U : flags[i];
    dg::MenuEntry item;
    item.text = text_or_empty(labels[i]);
    item.separator = (bits & DG_MENU_ITEM_SEPARATOR) != 0U;
    item.disabled = (bits & DG_MENU_ITEM_DISABLED) != 0U;
    if ((bits & (DG_MENU_ITEM_CHECKED | DG_MENU_ITEM_CHECKABLE)) != 0U) {
      item.checked = (bits & DG_MENU_ITEM_CHECKED) != 0U;
    }
    any_selectable = any_selectable || (!item.separator && !item.disabled);
    items.push_back(std::move(item));
  }
  if (!any_selectable) {
    set_last_error("dg_menu_open: no item can be chosen");
    return DG_ERR_INVALID_ARGUMENT;
  }
  const dg::Expected<std::int32_t, dg::Status> opened =
      window->window->open_menu(anchor_id, dg::PixelPoint{x, y}, items);
  if (!opened) {
    set_last_error("dg_menu_open: the popup could not be shown");
    return to_error_code(opened.error());
  }
  return opened.value();
}

std::int32_t menu_close(dg_window_t* window_h) {
  WindowImpl* window = window_of(window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  window->window->close_menu();
  return DG_ERR_OK;
}

std::int32_t dropdown_set_items(dg_node_t* node_h, const char* const* items,
                                std::uint32_t count) {
  const Resolved r = resolve(node_h, DG_NODE_TYPE_DROPDOWN);
  if (r.status != DG_ERR_OK) {
    return r.status;
  }
  if ((items == nullptr && count > 0) || count > kMaxMenuItems) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  std::vector<std::string> options;
  options.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    options.push_back(text_or_empty(items[i]));
  }
  return to_error_code(r.window->dropdown(r.slot->node_id).value().set_items(std::move(options)));
}

std::int32_t dropdown_get_selected(dg_node_t* node_h) {
  const Resolved r = resolve(node_h, DG_NODE_TYPE_DROPDOWN);
  if (r.status != DG_ERR_OK) {
    return r.status;
  }
  return value_or_error(r.window->dropdown(r.slot->node_id).value().selected());
}

std::int32_t dropdown_set_selected(dg_node_t* node_h, std::int32_t index) {
  const Resolved r = resolve(node_h, DG_NODE_TYPE_DROPDOWN);
  if (r.status != DG_ERR_OK) {
    return r.status;
  }
  return to_error_code(r.window->dropdown(r.slot->node_id).value().select(index));
}

std::int32_t node_set_tooltip(dg_node_t* node_h, const char* utf8) {
  NodeSlot* slot = slot_of(node_h);
  if (slot == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  if (slot->state != NodeSlot::State::kLive) {
    return DG_ERR_NO_WINDOW;
  }
  AppImpl& impl = *app_of(node_h->app);
  return to_error_code(impl.windows[slot->window_index]->window->set_tooltip(
      slot->node_id, text_or_empty(utf8)));
}

std::int32_t app_set_tooltip_delay(dg_app_t* app, std::int32_t delay_ms) {
  AppImpl* impl = app_of(app);
  if (impl == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  return to_error_code(impl->app.set_tooltip_delay(delay_ms));
}

std::int32_t dialog_open(dg_window_t* window_h, const char* title, const char* body,
                         const char* const* buttons, std::uint32_t count,
                         std::int32_t default_button, std::int32_t cancel_button) {
  WindowImpl* window = window_of(window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  if (title == nullptr || (buttons == nullptr && count > 0) || count > kMaxDialogButtons) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  if (window->window->has_dialog()) {
    set_last_error("dg_dialog_open: a dialog is already open on this window");
    return DG_ERR_UNSUPPORTED;
  }
  if (default_button < -1 || cancel_button < -1) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  std::vector<std::string> labels;
  labels.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    labels.push_back(text_or_empty(buttons[i]));
  }
  const auto button = [](std::int32_t index) {
    return index >= 0 ? std::optional<int>(index) : std::nullopt;
  };
  dg::DialogOptions options;
  options.title = title;
  options.body = text_or_empty(body);
  options.buttons = std::move(labels);
  options.default_button = button(default_button);
  options.cancel_button = button(cancel_button);
  const dg::Expected<std::int32_t, dg::Status> opened =
      window->window->open_dialog(std::move(options));
  if (!opened) {
    if (opened.error() == dg::Status::kUnsupported) {
      set_last_error("dg_dialog_open: the popup could not be shown");
    }
    return to_error_code(opened.error());
  }
  return opened.value();
}

std::int32_t dialog_close(dg_window_t* window_h) {
  WindowImpl* window = window_of(window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  window->window->close_dialog();
  return DG_ERR_OK;
}

dg_node_t* tabs_add(dg_node_t* node_h, const char* label, std::int32_t disabled) {
  const Resolved r = resolve(node_h, DG_NODE_TYPE_TABS);
  if (r.status != DG_ERR_OK) {
    set_last_error("dg_tabs_add: not a live DG_NODE_TYPE_TABS node");
    return nullptr;
  }
  const std::size_t window_index = r.slot->window_index;
  const dg::Expected<dg::NodeId, dg::Status> panel =
      r.window->tabs(r.slot->node_id).value().add(text_or_empty(label), disabled != 0);
  if (!panel) {
    set_last_error(panel.error() == dg::Status::kValueOutOfRange
                       ? "dg_tabs_add: at most 64 tabs"
                       : "dg_tabs_add: could not attach the panel");
    return nullptr;
  }
  return adopt_node(node_h->app, window_index, panel.value(), DG_NODE_TYPE_BOX);
}

std::int32_t tabs_get_selected(dg_node_t* node_h) {
  const Resolved r = resolve(node_h, DG_NODE_TYPE_TABS);
  if (r.status != DG_ERR_OK) {
    return r.status;
  }
  return value_or_error(r.window->tabs(r.slot->node_id).value().selected());
}

std::int32_t tabs_set_selected(dg_node_t* node_h, std::int32_t index) {
  const Resolved r = resolve(node_h, DG_NODE_TYPE_TABS);
  if (r.status != DG_ERR_OK) {
    return r.status;
  }
  return to_error_code(r.window->tabs(r.slot->node_id).value().select(index));
}

std::int32_t tree_set_items(dg_node_t* node_h, const char* const* labels,
                            const std::int32_t* depths, const std::uint32_t* flags,
                            std::uint32_t count) {
  const Resolved r = resolve(node_h, DG_NODE_TYPE_TREE);
  if (r.status != DG_ERR_OK) {
    return r.status;
  }
  if ((count > 0 && (labels == nullptr || depths == nullptr)) || count > kMaxItems) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  std::vector<dg::TreeEntry> items;
  items.reserve(count);
  int previous = -1;
  for (std::uint32_t i = 0; i < count; ++i) {
    const int depth = depths[i];
    if (depth < 0 || depth > previous + 1) {
      set_last_error("dg_tree_set_items: item " + std::to_string(i) +
                     " is more than one level deeper than the item before it");
      return DG_ERR_INVALID_ARGUMENT;
    }
    previous = depth;
    dg::TreeEntry item;
    item.label = text_or_empty(labels[i]);
    item.depth = depth;
    item.expanded = flags != nullptr && (flags[i] & DG_TREE_ITEM_EXPANDED) != 0U;
    items.push_back(std::move(item));
  }
  return to_error_code(r.window->tree(r.slot->node_id).value().set_items(std::move(items)));
}

std::int32_t tree_get_selected(dg_node_t* node_h) {
  const Resolved r = resolve(node_h, DG_NODE_TYPE_TREE);
  if (r.status != DG_ERR_OK) {
    return r.status;
  }
  return value_or_error(r.window->tree(r.slot->node_id).value().selected());
}

std::int32_t tree_set_selected(dg_node_t* node_h, std::int32_t index) {
  const Resolved r = resolve(node_h, DG_NODE_TYPE_TREE);
  if (r.status != DG_ERR_OK) {
    return r.status;
  }
  return to_error_code(r.window->tree(r.slot->node_id).value().select(index));
}

std::int32_t tree_set_expanded(dg_node_t* node_h, std::int32_t index, std::int32_t expanded) {
  const Resolved r = resolve(node_h, DG_NODE_TYPE_TREE);
  if (r.status != DG_ERR_OK) {
    return r.status;
  }
  return to_error_code(r.window->tree(r.slot->node_id).value().set_expanded(index, expanded != 0));
}

std::int32_t table_set_columns(dg_node_t* node_h, const char* const* titles,
                               const float* widths, const std::uint32_t* units,
                               const std::uint32_t* flags, std::uint32_t count) {
  const Resolved r = resolve(node_h, DG_NODE_TYPE_TABLE);
  if (r.status != DG_ERR_OK) {
    return r.status;
  }
  if (titles == nullptr || count == 0 || count > kMaxColumns) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  std::vector<dg::TableColumnSpec> columns;
  for (std::uint32_t i = 0; i < count; ++i) {
    dg::TableColumnSpec column;
    column.title = text_or_empty(titles[i]);
    column.sortable = flags != nullptr && (flags[i] & DG_TABLE_COLUMN_SORTABLE) != 0U;
    const float width = widths == nullptr ? 0.0F : widths[i];
    const std::uint32_t unit = units == nullptr ? DG_LENGTH_UNIT_PX : units[i];
    if (!std::isfinite(width) || unit > DG_LENGTH_UNIT_PERCENT) {
      return DG_ERR_INVALID_ARGUMENT;
    }
    if (width > 0.0F) {
      if (unit == DG_LENGTH_UNIT_PERCENT) {
        column.width_percent = std::min(width, 100.0F);
      } else {
        column.width_px = static_cast<int>(std::lround(std::min(width, 1.0e6F)));
      }
    }
    columns.push_back(std::move(column));
  }
  return to_error_code(r.window->table(r.slot->node_id).value().set_columns(std::move(columns)));
}

std::int32_t table_set_cells(dg_node_t* node_h, const char* const* cells, std::uint32_t rows) {
  const Resolved r = resolve(node_h, DG_NODE_TYPE_TABLE);
  if (r.status != DG_ERR_OK) {
    return r.status;
  }
  const dg::Expected<int, dg::Status> column_count =
      r.window->table(r.slot->node_id).value().column_count();
  if (!column_count) {
    return to_error_code(column_count.error());
  }
  const auto columns = static_cast<std::size_t>(column_count.value());
  if (rows > 0 && (cells == nullptr || columns == 0 ||
                   static_cast<std::uint64_t>(rows) * columns > kMaxItems)) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  std::vector<std::vector<std::string>> data(rows);
  for (std::size_t row = 0; row < rows; ++row) {
    data[row].reserve(columns);
    for (std::size_t col = 0; col < columns; ++col) {
      data[row].push_back(text_or_empty(cells[(row * columns) + col]));
    }
  }
  return to_error_code(r.window->table(r.slot->node_id).value().set_rows(std::move(data)));
}

std::int32_t table_get_selected(dg_node_t* node_h) {
  const Resolved r = resolve(node_h, DG_NODE_TYPE_TABLE);
  if (r.status != DG_ERR_OK) {
    return r.status;
  }
  return value_or_error(r.window->table(r.slot->node_id).value().selected());
}

std::int32_t table_set_selected(dg_node_t* node_h, std::int32_t row) {
  const Resolved r = resolve(node_h, DG_NODE_TYPE_TABLE);
  if (r.status != DG_ERR_OK) {
    return r.status;
  }
  return to_error_code(r.window->table(r.slot->node_id).value().select(row));
}

std::int32_t table_set_sort_indicator(dg_node_t* node_h, std::int32_t column,
                                      std::int32_t ascending) {
  const Resolved r = resolve(node_h, DG_NODE_TYPE_TABLE);
  if (r.status != DG_ERR_OK) {
    return r.status;
  }
  if (column < -1) {
    return DG_ERR_VALUE_OUT_OF_RANGE;
  }
  return to_error_code(r.window->table(r.slot->node_id).value()
                           .set_sort_indicator(
                               column >= 0 ? std::optional<int>(column) : std::nullopt,
                               ascending != 0));
}

std::int32_t debug_node_count(dg_window_t* window_h) {
  WindowImpl* window = window_of(window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  return static_cast<std::int32_t>(
      dg::subtree_of(window->window->layout().render(), dg::RenderTree::root()).size());
}

}  // namespace dg::abi
