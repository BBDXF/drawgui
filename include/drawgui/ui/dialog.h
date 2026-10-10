// dg::Dialog - a library modal dialog over PopupHost.
//
// One full-window overlay: a scrim (PopupStyle::scrim) that swallows every
// click, a centred panel with a title, wrapped body text, an optional
// caller-owned body area (body()) and a right-aligned row of buttons. Focus
// is trapped with the existing MODAL focus scope (Focus::enter_scope(panel,
// true) + set_guarded()); Tab/Shift-Tab cycle inside the panel; Escape
// reports the cancel button; Enter activates the focused button, or the
// default button when focus is not on one. Closing destroys the overlay
// (PopupHost::destroy()) and returns focus to the restore target.
//
// A disabled button (set_button_disabled()) is dimmed, skipped by Tab and
// never activated - not by a click, nor by Enter as the default button.
// adopt() hangs a caller-owned node into the body for the dialog's life;
// close() hands it back (re-parented under its former parent) before the
// overlay is destroyed.

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "drawgui/ui/ui_context.h"
#include "drawgui/widget/focus.h"

namespace dg {

struct DialogSpec {
  std::string title;
  std::string body;  // wrapped; may be empty
  std::vector<std::string> buttons{"OK"};
  // Enter (when focus is not on a button) activates this; it is also focused
  // on open (unless `initial_focus` says otherwise) and painted in the accent
  // colour. nullopt: no default button.
  std::optional<int> default_button = 0;
  // Escape reports this button. nullopt: Escape closes with no button.
  std::optional<int> cancel_button;
  PixelSize panel_size{360, 170};
  // Extra height reserved between the body text and the buttons for
  // caller-built content under body() (e.g. a text field). Body text, if
  // any, sits at the top of the same area.
  int body_height = 0;
};

enum class DialogEvent : std::uint8_t {
  kIgnored,  // closed; or a key the dialog does not use (route it to the focused body widget)
  kHandled,  // consumed (a click on the scrim/panel, Tab); still open
  kClosed,   // a button / Escape closed it; `button` says which
};

struct DialogResult {
  DialogEvent event = DialogEvent::kIgnored;
  std::optional<int>
      button;  // kClosed: the button index; nullopt = Escape with no cancel button
  // kHandled pointer kDown: the widget under the pointer inside the panel
  // (already focused via set_guarded()), so a caller can forward the click
  // to its own body content (a text field's caret placement, say).
  std::optional<NodeId> target;
};

class Dialog {
 public:
  // Covers the whole window (ctx.tree.viewport()). Focuses the default
  // button, else the first button (to focus body content instead, build it
  // under body() and call Focus::set_guarded() + refresh_ring()). Returns
  // false when PopupHost::show() fails.
  bool open(UiContext& ctx, DialogSpec spec, std::optional<NodeId> restore_focus);
  void close(UiContext& ctx);

  // Modal: every pointer event on the dialog's window is consumed. kDown on
  // a button closes with it; elsewhere focuses the widget under the pointer
  // (or nothing) through set_guarded().
  DialogResult handle_pointer(UiContext& ctx, const PointerEvent& event);
  DialogResult handle_key(UiContext& ctx, const KeyEvent& event);

  // Disables/enables button `index` (ignored when out of range or closed).
  void set_button_disabled(UiContext& ctx, int index, bool disabled);
  [[nodiscard]] bool button_disabled(int index) const;

  // Re-parents (render side only) `node` under body() while open; close()
  // moves it back under its former parent before destroying the overlay.
  // The caller positions it (body-local; body_content_top() is free).
  void adopt(UiContext& ctx, NodeId node);
  [[nodiscard]] std::optional<NodeId> adopted() const { return adopted_; }

  // Repositions the dialog's own focus ring onto Focus::current() - call
  // after moving focus yourself (e.g. into a body widget).
  void refresh_ring(UiContext& ctx);

  [[nodiscard]] bool is_open() const { return handle_.has_value() && handle_->open; }
  // Valid only while open.
  [[nodiscard]] NodeId panel() const { return panel_; }
  [[nodiscard]] NodeId body() const { return body_; }
  // Where caller content under body() starts (body-local y): below the body
  // text, if any.
  [[nodiscard]] int body_content_top() const { return body_content_top_; }
  [[nodiscard]] const std::vector<NodeId>& buttons() const { return buttons_; }
  [[nodiscard]] std::optional<PopupHandle> handle() const { return handle_; }

 private:
  [[nodiscard]] std::optional<int> button_index(NodeId id) const;
  DialogResult finish(UiContext& ctx, std::optional<int> button);

  DialogSpec spec_;
  std::optional<PopupHandle> handle_;
  NodeId panel_;
  NodeId body_;
  int body_content_top_ = 0;
  std::vector<NodeId> buttons_;
  std::vector<bool> disabled_;
  std::optional<NodeId> adopted_;
  NodeId adopted_parent_;
  FocusRing ring_;
  std::optional<NodeId> restore_focus_;
};

}  // namespace dg
