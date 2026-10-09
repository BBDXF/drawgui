// dg::Menu - a library context menu / option list over PopupHost.
//
// Builds one overlay (PopupHost::show()) holding a background, one row per
// MenuItem (an ordinary kButton widget, or a plain non-widget node for a
// separator) and destroys it again (PopupHost::destroy()) on every close, so
// open/close cycles never accumulate nodes.
//
// HIGHLIGHT IS FOCUS: the highlighted item is dg::Focus::current() inside
// the menu's own focus scope - Up/Down are focus_previous()/focus_next()
// over content_root (the traversal Tab already performs, wrapping), Home/End
// the ends of focus_order(). Disabled items carry NodeStyle::disabled and
// separators carry no widget, so focus_order() already skips both; nothing
// here re-implements that rule. The highlighted row is painted with
// PopupStyle::item_hover.
//
// Nested menus are not supported (PopupHost and Focus both hold a single
// scope - doc/guide/popups.md).

#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "drawgui/ui/ui_context.h"

namespace dg {

struct MenuItem {
  std::string text;
  bool separator = false;  // a divider line; `text`/`disabled`/`checked` are ignored
  bool disabled = false;   // shown dimmed, never highlighted or activated
  // nullopt: no check column for this item. A menu where ANY item has a
  // value reserves the check column on every row so labels line up.
  std::optional<bool> checked;
};

// Builders, so callers need not spell every field (aggregate init with
// omitted members trips -Wmissing-field-initializers).
[[nodiscard]] inline MenuItem menu_item(std::string text, bool disabled = false) {
  MenuItem item;
  item.text = std::move(text);
  item.disabled = disabled;
  return item;
}
[[nodiscard]] inline MenuItem menu_check_item(std::string text, bool checked,
                                              bool disabled = false) {
  MenuItem item = menu_item(std::move(text), disabled);
  item.checked = checked;
  return item;
}
[[nodiscard]] inline MenuItem menu_separator() {
  MenuItem item;
  item.separator = true;
  return item;
}

struct MenuSpec {
  std::vector<MenuItem> items;
  // The menu is at least this wide (a dropdown passes its anchor's width);
  // it grows to fit the widest label.
  int min_width = 0;
  PopupPlacement placement = PopupPlacement::kBelow;
  // The item highlighted on open. nullopt, or an index that is a separator/
  // disabled, highlights the first enabled item instead.
  std::optional<int> initial_highlight;
};

enum class MenuEvent : std::uint8_t {
  kIgnored,    // not the menu's (closed, or an event this menu does not use)
  kHandled,    // consumed; the menu is still open (highlight may have moved)
  kActivated,  // an item was chosen; the menu has closed itself
  kDismissed,  // Escape / click outside; the menu has closed itself
};

struct MenuResult {
  MenuEvent event = MenuEvent::kIgnored;
  int index = -1;  // the activated item's index into MenuSpec::items (kActivated only)

  // True when the menu closed during this call (kActivated/kDismissed):
  // focus was moved back to the restore target, so the caller repaints its
  // own focus ring.
  [[nodiscard]] bool closed() const {
    return event == MenuEvent::kActivated || event == MenuEvent::kDismissed;
  }
};

class Menu {
 public:
  // Opens at `anchor` (window pixels; a zero-size rect at the pointer for a
  // context menu). Enters a focus scope on the menu and highlights the
  // initial item. On close, focus returns to `restore_focus` (nullopt:
  // nothing focused). Opening an already-open menu closes it first.
  // Returns false (and stays closed) when PopupHost::show() fails.
  bool open(UiContext& ctx, const PixelRect& anchor, MenuSpec spec,
            std::optional<NodeId> restore_focus);

  // Closes without activating: exit_scope(), PopupHost::destroy(), focus
  // back to the restore target. No-op when closed.
  void close(UiContext& ctx);

  // While open: a kDown outside (or on another window) dismisses; a kDown on
  // an enabled item activates it; kMove over an enabled item highlights it;
  // everything else on the menu's window is consumed (kHandled).
  MenuResult handle_pointer(UiContext& ctx, const PointerEvent& event);

  // While open: Escape dismisses; Up/Down/Home/End move the highlight; Enter
  // activates the highlighted item; Tab is swallowed (kHandled). Other keys
  // are kIgnored.
  MenuResult handle_key(UiContext& ctx, const KeyEvent& event);

  // Optional type-ahead: highlights the next enabled item (after the current
  // one, wrapping) whose label starts with `event.text`'s first character,
  // ASCII case-insensitively. kHandled when it moved, kIgnored otherwise.
  MenuResult handle_text(UiContext& ctx, const TextInputEvent& event);

  [[nodiscard]] bool is_open() const { return handle_.has_value() && handle_->open; }
  [[nodiscard]] std::optional<int> highlighted(const UiContext& ctx) const;

  // Programmatic highlight (ignored for a separator/disabled index).
  void set_highlight(UiContext& ctx, int index);

  // Valid only while open. One node per MenuSpec item (separators included).
  [[nodiscard]] const std::vector<NodeId>& item_nodes() const { return rows_; }
  [[nodiscard]] std::optional<PopupHandle> handle() const { return handle_; }
  [[nodiscard]] const MenuSpec& spec() const { return spec_; }

 private:
  [[nodiscard]] std::optional<int> index_of(NodeId id) const;
  [[nodiscard]] bool selectable(int index) const;
  void highlight_node(UiContext& ctx, std::optional<NodeId> target);
  void repaint_highlight(UiContext& ctx);
  MenuResult finish(UiContext& ctx, MenuEvent event, int index);

  MenuSpec spec_;
  std::optional<PopupHandle> handle_;
  std::vector<NodeId> rows_;
  std::optional<NodeId> restore_focus_;
  std::optional<NodeId> painted_highlight_;
};

}  // namespace dg
