// dg::Event - everything dg::App::pump() reports, one struct per kind.
//
// Each alternative maps to exactly one C ABI DG_EVENT_* kind (or, for
// TreeEvent/TableEvent/WindowStateEvent, one per `kind` value); src/abi's
// own visitor is that mapping. Events name windows by WindowId and nodes by
// NodeId; a node that has no Window::create() record (a composed child) is
// reported as its nearest created ancestor where the C ABI did so, else as
// no node.

#pragma once

#include <cstdint>
#include <optional>
#include <type_traits>
#include <variant>

#include "drawgui/render/node_id.h"
#include "drawgui/window/window_id.h"

namespace dg {
namespace events {

// A completed primary click. `node` = the clicked node when it was created
// through Window::create(). `clicks` = the multi-click count of the press.
// A checkbox or radio button has already changed when this is queued (ask
// Window::checked()). A radio button selected with the arrow keys reports
// one too, with clicks = 0 and x/y = 0.
struct ClickEvent {
  WindowId window;
  std::optional<NodeId> node;
  int x = 0;
  int y = 0;
  int clicks = 1;
  // A press on a row of a List/Table (the row) or a Tree (the pre-order
  // item); none for every other node or a press outside the rows.
  std::optional<int> item = std::nullopt;
  // The column of a Table press; none elsewhere.
  std::optional<int> column = std::nullopt;
};

// A secondary-button press on the nearest created node under the pointer.
struct ContextClickEvent {
  WindowId window;
  std::optional<NodeId> node;
  int x = 0;
  int y = 0;
  int clicks = 1;
  // As ClickEvent::item/column.
  std::optional<int> item = std::nullopt;
  std::optional<int> column = std::nullopt;
};

// A routed shortcut action; `node` = its target (none = app-wide). An
// action_id >= kUserActionBase (app.h) is one the app bound itself
// (App::add_shortcut()/Window::add_shortcut()).
struct ActionEvent {
  WindowId window;
  std::optional<NodeId> node;
  std::uint16_t action_id = 0;
};

// A user edit changed an editor's committed text.
struct TextChangedEvent {
  WindowId window;
  std::optional<NodeId> node;
};

// Keyboard focus moved; `node` = the newly focused node (none = blurred).
struct FocusChangedEvent {
  WindowId window;
  std::optional<NodeId> node;
};

struct ListSelectEvent {
  WindowId window;
  NodeId node;
  int index = -1;
  int clicks = 1;
};

// A dropdown's selection changed; -1 = none.
struct ValueChangedEvent {
  WindowId window;
  NodeId node;
  int selected = -1;
  int previous = -1;
};

// A Window::open_menu() menu was activated (`item`) or dismissed (no item).
struct MenuEvent {
  WindowId window;
  std::optional<NodeId> anchor;
  std::optional<int> item = std::nullopt;
  std::int32_t menu_id = 0;
  // A menu-bar menu (NodeKind::kMenuBar): which of its menus; `anchor` is
  // the bar. None for an open_menu() menu.
  std::optional<int> bar_menu = std::nullopt;
  // The activated entry's MenuEntry::command (0 = none / dismissed).
  std::int32_t command = 0;
};

// `button` = -1 when Escape closed a dialog without a cancel button.
struct DialogResultEvent {
  WindowId window;
  int button = -1;
  std::int32_t dialog_id = 0;
};

struct TabSelectEvent {
  WindowId window;
  NodeId node;
  int index = 0;
};

struct TreeEvent {
  enum class Kind : std::uint8_t { kSelect, kExpand, kCollapse };
  Kind kind = Kind::kSelect;
  WindowId window;
  NodeId node;
  int item = 0;
};

struct TableEvent {
  enum class Kind : std::uint8_t { kSelect, kSort };
  Kind kind = Kind::kSelect;
  WindowId window;
  NodeId node;
  // The row (kSelect) or column (kSort).
  int index = 0;
  // kSelect from the pointer: the press's click count; 0 otherwise.
  int clicks = 0;
};

// The OS changed a window. kResized: x/y = the new drawable width/height;
// kMoved: x/y = the new position.
struct WindowStateEvent {
  enum class Kind : std::uint8_t {
    kResized,
    kMoved,
    kMinimized,
    kMaximized,
    kRestored,
    kFocusGained,
    kFocusLost,
    kEnteredFullscreen,
    kLeftFullscreen,
  };
  WindowId window;
  Kind kind = Kind::kResized;
  int x = 0;
  int y = 0;
};

struct WindowClosedEvent {
  WindowId window;
};

struct TimerEvent {
  TimerId timer;
};

struct UserEvent {
  std::uint64_t payload = 0;
};

// The user moved a slider (a drag, a track press or a key) to a different
// value; Window::slider() writes report none.
struct SliderEvent {
  WindowId window;
  NodeId node;
  float value = 0.0F;
  float previous = 0.0F;
};

// A press on a row part other than the row itself: a List/Tree/Table
// check box (kCheck; the new state is already applied - ask
// ListRef/TreeRef/TableRef::*_checked()) or a List row's trailing action
// button (kAction; `action` = its index into ListRef::set_row_actions()).
// `column` = the Table column, -1 elsewhere; `action` = -1 for kCheck.
struct RowPartEvent {
  enum class Part : std::uint8_t { kCheck, kAction };
  WindowId window;
  NodeId node;
  int item = 0;
  int column = -1;
  Part part = Part::kCheck;
  int action = -1;
};

}  // namespace events

using Event = std::variant<events::ClickEvent, events::ContextClickEvent, events::ActionEvent,
                           events::TextChangedEvent, events::FocusChangedEvent,
                           events::ListSelectEvent, events::ValueChangedEvent,
                           events::MenuEvent, events::DialogResultEvent, events::TabSelectEvent,
                           events::TreeEvent, events::TableEvent, events::WindowStateEvent,
                           events::WindowClosedEvent, events::TimerEvent, events::UserEvent,
                           events::SliderEvent, events::RowPartEvent>;

// std::visit(Overloaded{[](const events::ClickEvent& e) {...}, [](const auto&) {}}, event)
template <class... F>
struct Overloaded : F... {
  using F::operator()...;
};
template <class... F>
Overloaded(F...) -> Overloaded<F...>;

// The window an event names; none for TimerEvent/UserEvent.
[[nodiscard]] inline std::optional<WindowId> window_of(const Event& event) {
  return std::visit(
      [](const auto& e) -> std::optional<WindowId> {
        if constexpr (std::is_same_v<std::decay_t<decltype(e)>, events::TimerEvent> ||
                      std::is_same_v<std::decay_t<decltype(e)>, events::UserEvent>) {
          return std::nullopt;
        } else {
          return e.window;
        }
      },
      event);
}

// The event as `E`, or null: `if (auto* click = event_as<events::ClickEvent>(e))`.
template <class E>
[[nodiscard]] const E* event_as(const Event& event) {
  return std::get_if<E>(&event);
}

}  // namespace dg
