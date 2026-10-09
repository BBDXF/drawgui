// Seams between the src/app translation units. Nothing here is public API.

#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "app/state.h"
#include "drawgui/props/node_props.h"
#include "drawgui/widget/widget_set.h"

namespace dg::app_detail {

// -- window.cpp: records and events -------------------------------------------

[[nodiscard]] NodeRecord* record_of(WinState& window, dg::NodeId id);
[[nodiscard]] const NodeRecord* record_of(const WinState& window, dg::NodeId id);
// Every record of `window`, in creation order.
[[nodiscard]] std::vector<NodeRecord*> records_in_order(WinState& window);
// `id` when it has a record, else none.
[[nodiscard]] std::optional<dg::NodeId> exact_node(const WinState& window, dg::NodeId id);
// The nearest ancestor-or-self of `id` that has a record.
[[nodiscard]] std::optional<dg::NodeId> nearest_node(const WinState& window, dg::NodeId id);
void emit(AppState& app, const dg::Event& event);
[[nodiscard]] dg::Status to_status(dg::PropStatus status);

[[nodiscard]] bool is_text_editor(const WinState& window, dg::NodeId id);
[[nodiscard]] bool is_text_area(const WinState& window, dg::NodeId id);
// Adds a node of `kind` under `parent_id` (any node, composed or not), with
// its default box and composed children, and records it.
dg::NodeId attach_new(WinState& window, NodeKind kind, dg::NodeId parent_id);

// -- theme.cpp ------------------------------------------------------------------

// The app's active theme and variant, else the builtin theme (dark).
[[nodiscard]] std::pair<const dg::Theme*, dg::ThemeVariant> theme_for(const AppState& app);
[[nodiscard]] dg::DisabledPalette disabled_palette_for(const AppState& app);
[[nodiscard]] dg::ScrollbarStyle scrollbar_style_for(const AppState& app);
// Re-applies the active theme to every open window.
void reapply_theme(AppState& app);

// -- focus.cpp --------------------------------------------------------------------

// Every focus change ends here: the editors' side effects, then one
// FocusChangedEvent.
void apply_focus_change(WinState& window, const dg::FocusChange& change);
void set_editor_focus(WinState& window, dg::NodeId id, bool focused);
// Points the platform IME's candidate window at a focused text area's caret.
void follow_caret(WinState& window, dg::NodeId area);
void sync_disabled(WinState& window, dg::NodeId node);
// Reports a focus move a component made by itself.
void report_focus_drift(WinState& window);

// -- pointer.cpp / keys.cpp ---------------------------------------------------------

void process_pointer_event(WinState& window, const dg::PointerEvent& event);
void process_key_event(WinState& window, const dg::KeyEvent& event);
[[nodiscard]] bool is_scroller(const WinState& window, dg::NodeId id);
[[nodiscard]] int viewport_extent(const WinState& window, dg::NodeId owner);

// -- pump.cpp ---------------------------------------------------------------------

void process_pump(AppState& app, int timeout_ms);
void relayout(WinState& window);
bool ensure_surface_current(WinState& window);

// -- components/editors.cpp -------------------------------------------------------

void restyle_editors(WinState& window);
bool insert_into_editor(WinState& window, dg::NodeId editor, const std::string& text);
void refresh_editor_text(WinState& window, const NodeRecord& record);
// Shows/hides and places an editor's placeholder (empty text, not composing).
void sync_placeholder(WinState& window, const NodeRecord& record);
// DG_PROP_PADDING_* on a text editor: its symmetric text inset.
dg::Status set_editor_padding(WinState& window, const NodeRecord& record,
                              const dg::PropValue& value);

// -- components/list.cpp ----------------------------------------------------------

void paint_list_rows(WinState& window, const NodeRecord& record,
                     const std::vector<dg::ListSlot>& rows);
void repaint_list(WinState& window, const NodeRecord& record);
void apply_scroll_result(WinState& window, dg::NodeId owner, const dg::ScrollResult& result);
bool press_list_row(WinState& window, dg::PixelPoint at, int clicks);
dg::Status set_list_prop(WinState& window, const NodeRecord& record, std::uint16_t prop_id,
                         const dg::PropValue& value);
void resync_list(WinState& window, const NodeRecord& record);

// -- components/scroll_view.cpp ---------------------------------------------------

void follow_scroll_axis(WinState& window, const NodeRecord& record);

}  // namespace dg::app_detail
