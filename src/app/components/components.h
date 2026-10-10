// The per-kind hooks of the runtime. Each kind's builder lives in its own
// translation unit under src/app/components/; the window-wide hooks below
// dispatch to them, by NodeKind before a node exists (defaults/compose) and
// by std::visit on NodeRecord::state afterwards.
//
// The src/ui components (dropdown, progress bar, tabs, tree, table) and the
// popups (menu, dialog, tooltip) are poll-based: the pump hands every
// pointer/key/text event to route_*() first (an open dialog, then an open
// menu, then dropdowns, then the controls), ticks tooltips and
// indeterminate progress bars, and calls sync() after a layout pass. Every
// typed result a component returns becomes one dg::Event.

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "app/internal.h"
#include "app/state.h"
#include "drawgui/app/limits.h"
#include "drawgui/layout/box.h"
#include "drawgui/props/node_props.h"
#include "drawgui/render/node_lifecycle.h"
#include "drawgui/ui/control_context.h"
#include "drawgui/ui/ui_context.h"

namespace dg::app_detail::components {

// -- Shared context (components/context.cpp) ------------------------------------

[[nodiscard]] dg::NodeSideTables tables_of(WinState& window);
[[nodiscard]] const dg::FontCatalog* fonts_of(const AppState& app);
[[nodiscard]] dg::UiContext ui(WinState& window);
[[nodiscard]] dg::ControlStyle control_style(const AppState& app);
[[nodiscard]] dg::ControlContext controls(WinState& window);
// NodeKind::kLabel's engine-chosen default text size.
[[nodiscard]] float default_text_size();

// -- Window-wide hooks (components/dispatch.cpp) ----------------------------------

// The default box/style of a node of `kind`, before it is added.
void defaults(const WinState& window, NodeKind kind, dg::BoxStyle& box, dg::NodeStyle& style);
// Builds the composed children (and state) inside the freshly added node.
void compose(WinState& window, NodeRecord& record);
void init_window(WinState& window);

// Event routing, before the core's own handling. True = consumed.
bool route_pointer(WinState& window, const dg::PointerEvent& event);
bool route_key(WinState& window, const dg::KeyEvent& event);
bool route_text(WinState& window, const dg::TextInputEvent& event);

// `timeout_ms` cut to the nearest tooltip deadline / animation frame.
[[nodiscard]] int clamp_timeout(const AppState& app, int timeout_ms);
void tick(WinState& window);
// After a layout pass that re-placed nodes.
void sync(WinState& window);
void restyle(WinState& window);
// Before Window::remove() detaches `subtree`.
void before_remove(WinState& window, const std::vector<dg::NodeId>& subtree);

// Component-specific property/text writes; nullopt = not handled here.
std::optional<dg::Status> set_prop(WinState& window, NodeRecord& record, std::uint16_t prop_id,
                                   const dg::PropValue& value);
void after_prop(WinState& window, NodeRecord& record, std::uint16_t prop_id);
std::optional<dg::Status> set_text(WinState& window, NodeRecord& record, std::string_view text);
// The node keyboard focus goes to for Window::focus(record).
[[nodiscard]] dg::NodeId focus_target(const NodeRecord& record);

[[nodiscard]] bool any_dropdown_open(WinState& window);
// The nearest ancestor-or-self of the node under the pointer that has a
// tooltip.
[[nodiscard]] std::optional<dg::NodeId> tooltip_target(WinState& window);

// The record and component state of a live create() node whose state is
// `S`: status kInvalidNode when there is no such node, kUnsupported when it
// is another kind (each component kind owns exactly one state type).
template <class R, class S>
struct Found {
  R* record = nullptr;
  S* state = nullptr;
  dg::Status status = dg::Status::kOk;
};

template <class S>
Found<NodeRecord, S> component_of(WinState& window, dg::NodeId id) {
  Found<NodeRecord, S> out;
  out.record = record_of(window, id);
  if (out.record == nullptr) {
    out.status = dg::Status::kInvalidNode;
    return out;
  }
  out.state = std::get_if<S>(&out.record->state);
  if (out.state == nullptr) {
    out.status = dg::Status::kUnsupported;
  }
  return out;
}

template <class S>
Found<const NodeRecord, const S> component_of(const WinState& window, dg::NodeId id) {
  Found<const NodeRecord, const S> out;
  out.record = record_of(window, id);
  if (out.record == nullptr) {
    out.status = dg::Status::kInvalidNode;
    return out;
  }
  out.state = std::get_if<S>(&out.record->state);
  if (out.state == nullptr) {
    out.status = dg::Status::kUnsupported;
  }
  return out;
}

inline int wrapper_height(const WinState& window, const NodeRecord& record, int fallback) {
  return window.tree.box(record.id).height.value_or(fallback);
}

// The height a composed view should fill: the laid-out one once a layout
// has settled it (so grow/stretch/percentages count), else the box's own.
inline int laid_out_height(const WinState& window, const NodeRecord& record, int fallback) {
  const int laid_out = window.tree.render().local_bounds(record.id).height;
  return laid_out > 0 ? laid_out : wrapper_height(window, record, fallback);
}

// -- Menus (popups.cpp / menubar.cpp) -------------------------------------------

// The ui menu rows / per-row commands of MenuEntry rows.
[[nodiscard]] dg::MenuSpec menu_spec_of(const std::vector<dg::MenuEntry>& entries);
[[nodiscard]] std::vector<std::int32_t> commands_of(const std::vector<dg::MenuEntry>& entries);
// Forgets which menu is open (id, anchor, commands, bar) - after it closed.
void clear_menu_owner(WinState& window);
// Closes the open menu, reporting the dismissal.
void dismiss_open_menu(WinState& window);

void menubar_defaults(dg::BoxStyle& box, dg::NodeStyle& style);
void build_menubar(WinState& window, NodeRecord& record);
// An open bar menu's own pointer/key hooks (hovering or pressing another
// title, Left/Right); true = consumed.
bool menubar_menu_pointer(WinState& window, const dg::PointerEvent& event);
bool menubar_menu_key(WinState& window, const dg::KeyEvent& event);
// A bar menu closed: the bar stays in keyboard mode when `keep_active`.
void menubar_menu_closed(WinState& window, dg::NodeId bar, bool keep_active);
// F10 / Alt and the keyboard mode of a bar, before the controls.
bool menubar_route_key(WinState& window, const dg::KeyEvent& event);
// Closes `record`'s open menu, if any, without an event.
void menubar_forget(WinState& window, NodeRecord& record);

// -- Per-kind builders ----------------------------------------------------------

// basic.cpp: box, button, label, checkbox, image.
void basic_defaults(const AppState& app, NodeKind kind, dg::BoxStyle& box,
                    dg::NodeStyle& style);
void compose_basic(WinState& window, NodeRecord& record);
// The theme colours of a button, checkbox or radio (a no-op for other
// kinds); again after every theme change and caller colour write.
void style_control(WinState& window, NodeRecord& record);
// DG_PROP_CHECKED: a checkbox's mark, or (on a kButton) makes it a toggle
// button in that state.
dg::Status set_checked_prop(WinState& window, NodeRecord& record, const dg::PropValue& value);
[[nodiscard]] bool is_toggle_button(const NodeRecord& record);
// A completed click on a toggle button: flips and restyles it.
void flip_toggle_button(WinState& window, NodeRecord& record);
// editors.cpp: text field, text area.
void editor_defaults(const AppState& app, NodeKind kind, dg::BoxStyle& box,
                     dg::NodeStyle& style);
void compose_editor(WinState& window, NodeRecord& record);
// scroll_view.cpp / list.cpp.
void scroll_view_defaults(const WinState& window, dg::BoxStyle& box, dg::NodeStyle& style);
void compose_scroll_view(WinState& window, NodeRecord& record);
void list_defaults(const WinState& window, dg::BoxStyle& box, dg::NodeStyle& style);
void compose_list(WinState& window, NodeRecord& record);
// dropdown.cpp / progress.cpp / tabs.cpp / tree.cpp / table.cpp.
void dropdown_defaults(const AppState& app, dg::BoxStyle& box, dg::NodeStyle& style);
void control_defaults(NodeKind kind, dg::BoxStyle& box);
void compose_dropdown(WinState& window, NodeRecord& record);
void build_progress(WinState& window, NodeRecord& record);
void build_tabs(WinState& window, NodeRecord& record);
void build_tree(WinState& window, NodeRecord& record, std::optional<int> select = std::nullopt);
// The tree's items with their current expansion, for a rebuild.
void capture_tree(TreeState& state);
void build_table(WinState& window, NodeRecord& record,
                 std::optional<int> select = std::nullopt);
void paint_dropdown(WinState& window, NodeRecord& record);
// slider_radio.cpp.
inline constexpr int kRadioGroupLimit = 1 << 24;
void slider_radio_defaults(NodeKind kind, dg::BoxStyle& box, dg::NodeStyle& style);
void compose_slider(WinState& window, NodeRecord& record);
void compose_radio(WinState& window, NodeRecord& record);
// Selects radio `id`, repainting it and the group members it cleared;
// whether it was not selected before.
bool select_radio(WinState& window, dg::NodeId id);

// Per-state hooks, picked by std::visit; the templates are the defaults.
template <class S>
bool control_pointer(WinState& /*window*/, NodeRecord& /*record*/, S& /*state*/,
                     const dg::PointerEvent& /*event*/) {
  return false;
}
bool control_pointer(WinState& window, NodeRecord& record, TabsState& state,
                     const dg::PointerEvent& event);
bool control_pointer(WinState& window, NodeRecord& record, TreeState& state,
                     const dg::PointerEvent& event);
bool control_pointer(WinState& window, NodeRecord& record, TableState& state,
                     const dg::PointerEvent& event);
bool control_pointer(WinState& window, NodeRecord& record, SliderState& state,
                     const dg::PointerEvent& event);
bool control_pointer(WinState& window, NodeRecord& record, MenuBarState& state,
                     const dg::PointerEvent& event);

template <class S>
bool control_key(WinState& /*window*/, NodeRecord& /*record*/, S& /*state*/,
                 const dg::KeyEvent& /*event*/) {
  return false;
}
bool control_key(WinState& window, NodeRecord& record, TabsState& state,
                 const dg::KeyEvent& event);
bool control_key(WinState& window, NodeRecord& record, TreeState& state,
                 const dg::KeyEvent& event);
bool control_key(WinState& window, NodeRecord& record, TableState& state,
                 const dg::KeyEvent& event);
bool control_key(WinState& window, NodeRecord& record, SliderState& state,
                 const dg::KeyEvent& event);
bool control_key(WinState& window, NodeRecord& record, RadioState& state,
                 const dg::KeyEvent& event);

template <class S>
void sync_state(WinState& /*window*/, NodeRecord& /*record*/, S& /*state*/) {}
void sync_state(WinState& window, NodeRecord& record, ProgressState& state);
void sync_state(WinState& window, NodeRecord& record, TabsState& state);
void sync_state(WinState& window, NodeRecord& record, TreeState& state);
void sync_state(WinState& window, NodeRecord& record, TableState& state);

template <class S>
void restyle_state(WinState& /*window*/, NodeRecord& /*record*/, S& /*state*/) {}
void restyle_state(WinState& window, NodeRecord& record, DropdownState& state);
void restyle_state(WinState& window, NodeRecord& record, ProgressState& state);
void restyle_state(WinState& window, NodeRecord& record, TabsState& state);
void restyle_state(WinState& window, NodeRecord& record, TreeState& state);
void restyle_state(WinState& window, NodeRecord& record, TableState& state);
void restyle_state(WinState& window, NodeRecord& record, SliderState& state);
void restyle_state(WinState& window, NodeRecord& record, RadioState& state);
void restyle_state(WinState& window, NodeRecord& record, MenuBarState& state);

template <class S>
void tick_state(WinState& /*window*/, NodeRecord& /*record*/, S& /*state*/,
                dg::AnimTime /*now*/) {}
void tick_state(WinState& window, NodeRecord& record, ProgressState& state, dg::AnimTime now);

// Whether this state wants an animation frame (clamp_timeout()).
template <class S>
bool animating(const S& /*state*/) {
  return false;
}
bool animating(const ProgressState& state);

template <class S>
std::optional<dg::Status> set_prop_state(WinState& /*window*/, NodeRecord& /*record*/,
                                         S& /*state*/, std::uint16_t /*prop_id*/,
                                         const dg::PropValue& /*value*/) {
  return std::nullopt;
}
std::optional<dg::Status> set_prop_state(WinState& window, NodeRecord& record,
                                         ProgressState& state, std::uint16_t prop_id,
                                         const dg::PropValue& value);
std::optional<dg::Status> set_prop_state(WinState& window, NodeRecord& record,
                                         SliderState& state, std::uint16_t prop_id,
                                         const dg::PropValue& value);
std::optional<dg::Status> set_prop_state(WinState& window, NodeRecord& record,
                                         RadioState& state, std::uint16_t prop_id,
                                         const dg::PropValue& value);

template <class S>
void after_prop_state(WinState& /*window*/, NodeRecord& /*record*/, S& /*state*/,
                      std::uint16_t /*prop_id*/) {}
void after_prop_state(WinState& window, NodeRecord& record, ProgressState& state,
                      std::uint16_t prop_id);
void after_prop_state(WinState& window, NodeRecord& record, TreeState& state,
                      std::uint16_t prop_id);
void after_prop_state(WinState& window, NodeRecord& record, TableState& state,
                      std::uint16_t prop_id);
void after_prop_state(WinState& window, NodeRecord& record, SliderState& state,
                      std::uint16_t prop_id);

template <class S>
std::optional<dg::Status> set_text_state(WinState& /*window*/, NodeRecord& /*record*/,
                                         S& /*state*/, std::string_view /*text*/) {
  return std::nullopt;
}
std::optional<dg::Status> set_text_state(WinState& window, NodeRecord& record,
                                         DropdownState& state, std::string_view text);

template <class S>
dg::NodeId focus_target_state(const NodeRecord& record, const S& /*state*/) {
  return record.id;
}
dg::NodeId focus_target_state(const NodeRecord& record, const TabsState& state);
dg::NodeId focus_target_state(const NodeRecord& record, const TreeState& state);
dg::NodeId focus_target_state(const NodeRecord& record, const TableState& state);

}  // namespace dg::app_detail::components
