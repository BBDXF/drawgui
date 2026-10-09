// dg::Dropdown - wires a kDropdown widget (WidgetSet: options + selected
// index, the state that outlives the popup) to a dg::Menu (the popup).
//
// Opening builds a Menu from WidgetSet::dropdown_options(), checks and
// highlights the current selection, and is at least the anchor's width.
// Activating an option commits it through WidgetSet::dropdown_select()
// (which repaints the anchor's label) and reports kChanged with the old and
// new index; Escape / click outside closes with no change. Either way focus
// returns to the anchor.

#pragma once

#include <optional>

#include "drawgui/ui/menu.h"

namespace dg {

enum class DropdownEvent : std::uint8_t {
  kIgnored,
  kHandled,  // consumed; still open (or a trigger that failed to open)
  kOpened,
  kChanged,  // an option was chosen and differs from the previous selection; closed
  kClosed,   // closed with the selection unchanged (Escape, outside, same option)
};

struct DropdownResult {
  DropdownEvent event = DropdownEvent::kIgnored;
  std::optional<int> selected;  // the selection after this call
  std::optional<int> previous;  // kChanged: the selection before it
};

class Dropdown {
 public:
  explicit Dropdown(NodeId anchor) : anchor_(anchor) {}

  [[nodiscard]] NodeId anchor() const { return anchor_; }
  [[nodiscard]] bool is_open() const { return menu_.is_open(); }
  [[nodiscard]] const Menu& menu() const { return menu_; }

  // Opens below the anchor (flipping above when it does not fit).
  DropdownResult open(UiContext& ctx);
  void close(UiContext& ctx);

  // Open: routed to the Menu. Closed: a primary kDown on the anchor opens.
  DropdownResult handle_pointer(UiContext& ctx, const PointerEvent& event);
  // Open: routed to the Menu. Closed: Down/Enter while the anchor is
  // focused opens.
  DropdownResult handle_key(UiContext& ctx, const KeyEvent& event);
  DropdownResult handle_text(UiContext& ctx, const TextInputEvent& event);

 private:
  DropdownResult from_menu(UiContext& ctx, const MenuResult& result);

  NodeId anchor_;
  Menu menu_;
};

}  // namespace dg
