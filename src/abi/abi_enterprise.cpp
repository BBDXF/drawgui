// ABI 0.2 enterprise features: app shortcuts, app-wide images, rich
// list/tree/table rows, the menu bar, and the menu/dialog *_ex variants.
// Each entry point validates its C arguments, builds the runtime's C++
// values and calls dg::App/dg::Window (src/app), mapping Status to DG_ERR_*.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "abi_impl.h"
#include "abi_internal.h"
#include "drawgui/app/limits.h"

namespace dg::abi {
namespace {

constexpr std::uint32_t kMaxMenuItems = dg::limits::kMaxMenuItems;
constexpr std::uint32_t kMaxDialogButtons = dg::limits::kMaxDialogButtons;
constexpr std::uint32_t kMaxItems = dg::limits::kMaxDataItems;
constexpr std::uint32_t kMaxRowActions = 8;

struct Resolved {
  NodeSlot* slot = nullptr;
  AppImpl* impl = nullptr;
  dg::Window* window = nullptr;
  std::int32_t status = DG_ERR_OK;
};

// A live node (of `type_id`, unless 0), or the DG_ERR_* saying why not.
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
  if (type_id != 0 && out.slot->type_id != type_id) {
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

std::string text_at(const char* const* texts, std::size_t i) {
  return texts == nullptr ? std::string() : text_or_empty(texts[i]);
}

dg::ImageId image_at(const std::uint32_t* icons, std::size_t i) {
  if (icons == nullptr || icons[i] > 0xFFFFU) {
    return dg::ImageId{};
  }
  return dg::ImageId{static_cast<std::uint16_t>(icons[i])};
}

std::optional<bool> check_of(std::uint32_t bits, std::uint32_t checkable, std::uint32_t checked) {
  if ((bits & (checkable | checked)) == 0U) {
    return std::nullopt;
  }
  return (bits & checked) != 0U;
}

dg::MenuEntry menu_entry(const char* label, const char* shortcut, std::uint32_t bits,
                         std::int32_t command) {
  dg::MenuEntry item;
  item.text = text_or_empty(label);
  item.separator = (bits & DG_MENU_ITEM_SEPARATOR) != 0U;
  item.disabled = (bits & DG_MENU_ITEM_DISABLED) != 0U;
  item.checked = check_of(bits, DG_MENU_ITEM_CHECKABLE, DG_MENU_ITEM_CHECKED);
  item.shortcut_label = text_or_empty(shortcut);
  item.command = command;
  return item;
}

std::int32_t bool_or_error(const dg::Expected<bool, dg::Status>& result) {
  return result ? (result.value() ? 1 : 0) : to_error_code(result.error());
}

std::optional<dg::Color> tint_of(std::uint32_t argb) {
  if ((argb >> 24U) == 0U) {
    return std::nullopt;
  }
  return dg::Color::from_argb(argb);
}

std::int32_t image_result(const dg::Expected<dg::ImageId, dg::AppError>& loaded,
                          const char* function) {
  if (!loaded) {
    set_last_error(std::string(function) + ": " + loaded.error().message);
    return to_error_code(loaded.error().status);
  }
  return loaded.value().value;
}

std::string& chord_label_storage() {
  static thread_local std::string buffer;
  return buffer;
}

}  // namespace

// -- Shortcuts ---------------------------------------------------------------

std::int32_t app_add_shortcut(dg_app_t* app, const char* chord, std::uint16_t action_id) {
  AppImpl* impl = app_of(app);
  if (impl == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  if (chord == nullptr) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  return to_error_code(impl->app.add_shortcut(chord, action_id));
}

std::int32_t window_add_shortcut(dg_node_t* scope, const char* chord, std::uint16_t action_id) {
  const Resolved r = resolve(scope, 0);
  if (r.status != DG_ERR_OK) {
    return r.status;
  }
  if (chord == nullptr) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  return to_error_code(r.window->add_shortcut(r.slot->node_id, chord, action_id));
}

std::int32_t app_remove_shortcut(dg_app_t* app, const char* chord) {
  AppImpl* impl = app_of(app);
  if (impl == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  if (chord == nullptr) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  return to_error_code(impl->app.remove_shortcut(chord));
}

const char* chord_label(dg_app_t* app, const char* chord) {
  AppImpl* impl = app_of(app);
  if (impl == nullptr || chord == nullptr) {
    set_last_error("dg_chord_label: invalid argument");
    return nullptr;
  }
  dg::Expected<std::string, dg::Status> label = impl->app.chord_label(chord);
  if (!label) {
    set_last_error("dg_chord_label: the chord has no label");
    return nullptr;
  }
  std::string& buffer = chord_label_storage();
  buffer = std::move(label.value());
  return buffer.c_str();
}

// -- Images ------------------------------------------------------------------

std::int32_t app_load_image(dg_app_t* app, const void* bytes, std::uint32_t len,
                            std::uint32_t svg_tint_argb) {
  AppImpl* impl = app_of(app);
  if (impl == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  return image_result(impl->app.load_image(static_cast<const std::uint8_t*>(bytes), len,
                                           tint_of(svg_tint_argb)),
                      "dg_app_load_image");
}

std::int32_t app_load_image_file(dg_app_t* app, const char* path, std::uint32_t svg_tint_argb) {
  AppImpl* impl = app_of(app);
  if (impl == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  if (path == nullptr) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  return image_result(impl->app.load_image_file(path, tint_of(svg_tint_argb)),
                      "dg_app_load_image_file");
}

// -- Rich rows ---------------------------------------------------------------

std::int32_t list_set_rows(dg_node_t* node_h, const char* const* texts,
                           const char* const* secondaries, const std::uint32_t* icons,
                           const std::uint32_t* flags, const std::uint32_t* trailing_actions,
                           std::uint32_t count, std::int32_t item_height) {
  const Resolved r = resolve(node_h, DG_NODE_TYPE_LIST);
  if (r.status != DG_ERR_OK) {
    return r.status;
  }
  if ((count > 0 && texts == nullptr) || count > kMaxItems || item_height < 0) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  std::vector<dg::RowItem> rows;
  rows.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    dg::RowItem row;
    row.text = text_at(texts, i);
    row.secondary = text_at(secondaries, i);
    row.icon = image_at(icons, i);
    row.check = check_of(flags == nullptr ? 0U : flags[i], DG_ROW_CHECKABLE, DG_ROW_CHECKED);
    const std::uint32_t actions = trailing_actions == nullptr ? 0U : trailing_actions[i];
    if (actions > kMaxRowActions) {
      return DG_ERR_VALUE_OUT_OF_RANGE;
    }
    row.trailing_actions = static_cast<std::uint8_t>(actions);
    rows.push_back(std::move(row));
  }
  return to_error_code(
      r.window->list(r.slot->node_id).value().set_rows(std::move(rows), item_height));
}

std::int32_t list_set_row_actions(dg_node_t* node_h, const std::uint32_t* icons,
                                  const char* const* tooltips, std::uint32_t count) {
  const Resolved r = resolve(node_h, DG_NODE_TYPE_LIST);
  if (r.status != DG_ERR_OK) {
    return r.status;
  }
  if (count > kMaxRowActions) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  std::vector<dg::RowAction> actions;
  actions.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    actions.push_back(dg::RowAction{image_at(icons, i), text_at(tooltips, i)});
  }
  return to_error_code(
      r.window->list(r.slot->node_id).value().set_row_actions(std::move(actions)));
}

std::int32_t list_row_checked(dg_node_t* node_h, std::int32_t row) {
  const Resolved r = resolve(node_h, DG_NODE_TYPE_LIST);
  if (r.status != DG_ERR_OK) {
    return r.status;
  }
  return bool_or_error(r.window->list(r.slot->node_id).value().row_checked(row));
}

std::int32_t tree_set_items_ex(dg_node_t* node_h, const char* const* labels,
                               const std::int32_t* depths, const std::uint32_t* flags,
                               const std::uint32_t* icons, const char* const* secondaries,
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
      set_last_error("dg_tree_set_items_ex: item " + std::to_string(i) +
                     " is more than one level deeper than the item before it");
      return DG_ERR_INVALID_ARGUMENT;
    }
    previous = depth;
    const std::uint32_t bits = flags == nullptr ? 0U : flags[i];
    dg::TreeEntry item;
    item.label = text_or_empty(labels[i]);
    item.depth = depth;
    item.expanded = (bits & DG_TREE_ITEM_EXPANDED) != 0U;
    item.icon = image_at(icons, i);
    item.secondary = text_at(secondaries, i);
    item.check = check_of(bits, DG_TREE_ITEM_CHECKABLE, DG_TREE_ITEM_CHECKED);
    items.push_back(std::move(item));
  }
  return to_error_code(r.window->tree(r.slot->node_id).value().set_items(std::move(items)));
}

std::int32_t tree_row_checked(dg_node_t* node_h, std::int32_t index) {
  const Resolved r = resolve(node_h, DG_NODE_TYPE_TREE);
  if (r.status != DG_ERR_OK) {
    return r.status;
  }
  return bool_or_error(r.window->tree(r.slot->node_id).value().row_checked(index));
}

std::int32_t table_set_cells_ex(dg_node_t* node_h, const char* const* cells,
                                const std::uint32_t* icons, const std::uint32_t* flags,
                                std::uint32_t rows) {
  const Resolved r = resolve(node_h, DG_NODE_TYPE_TABLE);
  if (r.status != DG_ERR_OK) {
    return r.status;
  }
  dg::TableRef table = r.window->table(r.slot->node_id).value();
  const dg::Expected<int, dg::Status> column_count = table.column_count();
  if (!column_count) {
    return to_error_code(column_count.error());
  }
  const auto columns = static_cast<std::size_t>(column_count.value());
  if (rows > 0 && (cells == nullptr || columns == 0 ||
                   static_cast<std::uint64_t>(rows) * columns > kMaxItems)) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  std::vector<std::vector<dg::TableCell>> data(rows);
  for (std::size_t row = 0; row < rows; ++row) {
    data[row].reserve(columns);
    for (std::size_t col = 0; col < columns; ++col) {
      const std::size_t i = (row * columns) + col;
      dg::TableCell cell;
      cell.text = text_or_empty(cells[i]);
      cell.icon = image_at(icons, i);
      cell.check = check_of(flags == nullptr ? 0U : flags[i], DG_ROW_CHECKABLE, DG_ROW_CHECKED);
      data[row].push_back(std::move(cell));
    }
  }
  return to_error_code(table.set_cells(std::move(data)));
}

std::int32_t table_cell_checked(dg_node_t* node_h, std::int32_t row, std::int32_t column) {
  const Resolved r = resolve(node_h, DG_NODE_TYPE_TABLE);
  if (r.status != DG_ERR_OK) {
    return r.status;
  }
  return bool_or_error(r.window->table(r.slot->node_id).value().cell_checked(row, column));
}

std::int32_t list_set_row_keys(dg_node_t* node_h, const std::uint64_t* keys,
                               std::uint32_t count) {
  const Resolved r = resolve(node_h, DG_NODE_TYPE_LIST);
  if (r.status != DG_ERR_OK) {
    return r.status;
  }
  if (count > 0 && keys == nullptr) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  return to_error_code(r.window->list(r.slot->node_id).value().set_row_keys(
      std::vector<std::uint64_t>(keys, keys + count)));
}

std::int32_t tree_set_item_keys(dg_node_t* node_h, const std::uint64_t* keys,
                                std::uint32_t count) {
  const Resolved r = resolve(node_h, DG_NODE_TYPE_TREE);
  if (r.status != DG_ERR_OK) {
    return r.status;
  }
  if (count > 0 && keys == nullptr) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  return to_error_code(r.window->tree(r.slot->node_id).value().set_row_keys(
      std::vector<std::uint64_t>(keys, keys + count)));
}

std::int32_t table_set_row_keys(dg_node_t* node_h, const std::uint64_t* keys,
                                std::uint32_t count) {
  const Resolved r = resolve(node_h, DG_NODE_TYPE_TABLE);
  if (r.status != DG_ERR_OK) {
    return r.status;
  }
  if (count > 0 && keys == nullptr) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  return to_error_code(r.window->table(r.slot->node_id).value().set_row_keys(
      std::vector<std::uint64_t>(keys, keys + count)));
}

// -- Menu bar ----------------------------------------------------------------

std::int32_t menubar_set_menus(dg_node_t* node_h, const char* const* titles,
                               const std::uint32_t* entry_counts, std::uint32_t menu_count,
                               const char* const* labels, const char* const* shortcut_labels,
                               const std::uint32_t* flags, const std::int32_t* commands,
                               std::uint32_t entry_total) {
  const Resolved r = resolve(node_h, DG_NODE_TYPE_MENUBAR);
  if (r.status != DG_ERR_OK) {
    return r.status;
  }
  if ((menu_count > 0 && (titles == nullptr || entry_counts == nullptr)) ||
      (entry_total > 0 && labels == nullptr) || menu_count > kMaxMenuItems ||
      entry_total > kMaxItems) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  std::vector<dg::MenuBarMenu> menus;
  menus.reserve(menu_count);
  std::uint64_t next = 0;
  for (std::uint32_t m = 0; m < menu_count; ++m) {
    if (entry_counts[m] > kMaxMenuItems || next + entry_counts[m] > entry_total) {
      set_last_error("dg_menubar_set_menus: entry_counts exceed entry_total");
      return DG_ERR_INVALID_ARGUMENT;
    }
    dg::MenuBarMenu menu;
    menu.title = text_or_empty(titles[m]);
    for (std::uint32_t k = 0; k < entry_counts[m]; ++k, ++next) {
      const auto i = static_cast<std::size_t>(next);
      menu.entries.push_back(menu_entry(labels[i],
                                        shortcut_labels == nullptr ? nullptr : shortcut_labels[i],
                                        flags == nullptr ? 0U : flags[i],
                                        commands == nullptr ? 0 : commands[i]));
    }
    menus.push_back(std::move(menu));
  }
  if (next != entry_total) {
    set_last_error("dg_menubar_set_menus: entry_counts do not sum to entry_total");
    return DG_ERR_INVALID_ARGUMENT;
  }
  return to_error_code(
      r.window->menu_bar(r.slot->node_id).value().set_menus(std::move(menus)));
}

std::int32_t menubar_set_entry(dg_node_t* node_h, std::int32_t menu, std::int32_t item,
                               const char* label, const char* shortcut_label,
                               std::uint32_t flags, std::int32_t command) {
  const Resolved r = resolve(node_h, DG_NODE_TYPE_MENUBAR);
  if (r.status != DG_ERR_OK) {
    return r.status;
  }
  return to_error_code(r.window->menu_bar(r.slot->node_id)
                           .value()
                           .set_entry(menu, item, menu_entry(label, shortcut_label, flags,
                                                             command)));
}

// -- Menu / dialog *_ex ------------------------------------------------------

std::int32_t menu_open_ex(dg_window_t* window_h, dg_node_t* anchor, std::int32_t x,
                          std::int32_t y, const char* const* labels, const std::uint32_t* flags,
                          const char* const* shortcut_labels, const std::int32_t* commands,
                          std::uint32_t count) {
  WindowImpl* window = window_of(window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  if (labels == nullptr || count == 0 || count > kMaxMenuItems) {
    set_last_error("dg_menu_open_ex: need 1..4096 labels");
    return DG_ERR_INVALID_ARGUMENT;
  }
  std::optional<dg::NodeId> anchor_id;
  if (anchor != nullptr) {
    const NodeSlot* slot = slot_of(anchor);
    if (slot == nullptr) {
      return DG_ERR_INVALID_HANDLE;
    }
    if (anchor->app != window_h->app || slot->state != NodeSlot::State::kLive ||
        slot->window_index != window->self_index) {
      set_last_error("dg_menu_open_ex: the anchor is not live in this window");
      return DG_ERR_INVALID_ARGUMENT;
    }
    anchor_id = slot->node_id;
  }
  std::vector<dg::MenuEntry> items;
  items.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    items.push_back(menu_entry(labels[i], shortcut_labels == nullptr ? nullptr : shortcut_labels[i],
                               flags == nullptr ? 0U : flags[i],
                               commands == nullptr ? 0 : commands[i]));
  }
  const dg::Expected<std::int32_t, dg::Status> opened =
      window->window->open_menu(anchor_id, dg::PixelPoint{x, y}, items);
  return opened ? opened.value() : to_error_code(opened.error());
}

std::int32_t dialog_open_ex(dg_window_t* window_h, const char* title, const char* body,
                            const char* const* buttons, std::uint32_t count,
                            std::int32_t default_button, std::int32_t cancel_button,
                            dg_node_t* content) {
  WindowImpl* window = window_of(window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  if (title == nullptr || (buttons == nullptr && count > 0) || count > kMaxDialogButtons ||
      default_button < -1 || cancel_button < -1) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  dg::DialogOptions options;
  if (content != nullptr) {
    const NodeSlot* slot = slot_of(content);
    if (slot == nullptr) {
      return DG_ERR_INVALID_HANDLE;
    }
    if (content->app != window_h->app || slot->state != NodeSlot::State::kLive ||
        slot->window_index != window->self_index) {
      set_last_error("dg_dialog_open_ex: the content is not live in this window");
      return DG_ERR_INVALID_ARGUMENT;
    }
    options.content = slot->node_id;
  }
  const auto button = [](std::int32_t index) {
    return index >= 0 ? std::optional<int>(index) : std::nullopt;
  };
  options.title = title;
  options.body = text_or_empty(body);
  for (std::uint32_t i = 0; i < count; ++i) {
    options.buttons.push_back(text_or_empty(buttons[i]));
  }
  options.default_button = button(default_button);
  options.cancel_button = button(cancel_button);
  const dg::Expected<std::int32_t, dg::Status> opened =
      window->window->open_dialog(std::move(options));
  return opened ? opened.value() : to_error_code(opened.error());
}

std::int32_t dialog_set_button_disabled(dg_window_t* window_h, std::int32_t dialog_id,
                                        std::int32_t button, std::int32_t disabled) {
  WindowImpl* window = window_of(window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  return to_error_code(
      window->window->set_dialog_button_disabled(dialog_id, button, disabled != 0));
}

}  // namespace dg::abi
