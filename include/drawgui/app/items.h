// Plain values the runtime's popups and data components take: menu entries,
// tree items, table columns and dialog options. No src/ui type crosses
// dg::Window's interface; the runtime converts these itself.

#pragma once

#include <optional>
#include <string>
#include <vector>

namespace dg {

// One row of Window::open_menu().
struct MenuEntry {
  std::string text;
  // A divider line; text/disabled/checked are ignored.
  bool separator = false;
  bool disabled = false;
  // nullopt = no check mark column for this entry.
  std::optional<bool> checked;
};

// One row of TreeRef::set_items(), in display (pre-order) order. `depth`
// is 0 for a root, at most one more than the item before it.
struct TreeEntry {
  std::string label;
  int depth = 0;
  bool expanded = false;
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
};

}  // namespace dg
