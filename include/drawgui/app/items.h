// Plain values the runtime's popups and data components take: menu entries,
// tree items, table columns and dialog options. No src/ui type crosses
// dg::Window's interface; the runtime converts these itself.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "drawgui/render/image_catalog.h"
#include "drawgui/render/node_id.h"

namespace dg {

// One row of Window::open_menu().
struct MenuEntry {
  std::string text;
  // A divider line; text/disabled/checked are ignored.
  bool separator = false;
  bool disabled = false;
  // nullopt = no check mark column for this entry.
  std::optional<bool> checked;
  // Right-aligned accelerator text (e.g. App::chord_label()); display only.
  std::string shortcut_label{};
  // Echoed in MenuEvent::command when chosen; 0 = none.
  std::int32_t command = 0;
};

// One menu of MenuBarRef::set_menus().
struct MenuBarMenu {
  std::string title;
  std::vector<MenuEntry> entries;
};

// One row of ListRef::set_rows(). `icon` = an App::load_image() id (invalid
// = no icon); `check` = a leading check box (nullopt = none);
// `trailing_actions` = how many of ListRef::set_row_actions()' buttons the
// row shows (the first N; up to 8).
struct RowItem {
  std::string text;
  std::string secondary{};
  ImageId icon{};
  std::optional<bool> check = std::nullopt;
  std::uint8_t trailing_actions = 0;
};

// One trailing action button shared by every row of a list.
struct RowAction {
  ImageId icon{};
  std::string tooltip{};
};

// One cell of TableRef::set_cells().
struct TableCell {
  std::string text;
  ImageId icon{};
  std::optional<bool> check = std::nullopt;
};

// One row of TreeRef::set_items(), in display (pre-order) order. `depth`
// is 0 for a root, at most one more than the item before it.
struct TreeEntry {
  std::string label;
  int depth = 0;
  bool expanded = false;
  ImageId icon{};
  // Dimmed text after the label.
  std::string secondary{};
  // A check box before the label (nullopt = none).
  std::optional<bool> check = std::nullopt;
};

// One column of TableRef::set_columns(). Neither width = an equal share of
// what is left; width_percent is of the row width (50 = half).
struct TableColumnSpec {
  std::string title;
  std::optional<int> width_px;
  std::optional<float> width_percent;
  bool sortable = true;
};

// Window::open_dialog(). An empty `buttons` gets one "OK" button.
// default_button: Enter activates it and it starts focused; cancel_button:
// Escape reports it (nullopt = Escape reports button -1).
struct DialogOptions {
  std::string title;
  std::string body;
  std::vector<std::string> buttons;
  std::optional<int> default_button = 0;
  std::optional<int> cancel_button;
  // An app-built subtree (a live create() node of the dialog's window,
  // detached or not) shown in the dialog body below `body`; it is reparented
  // into the dialog while open and detached again (alive, parentless) when
  // the dialog closes, for the app to read, re-use or remove.
  std::optional<NodeId> content = std::nullopt;
};

}  // namespace dg
