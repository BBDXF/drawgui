// Internal seams between src/abi/abi_impl.cpp (the node/window/pump core)
// and src/abi/abi_components.cpp (ABI 0.2 phase B: the src/ui component
// layer - menus, dropdowns, tooltips, dialogs, progress bars, tabs, trees,
// tables). Nothing here is exported; the dg_*() entry points of both files
// are declared in abi_impl.h.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "abi_types.h"

namespace dg::abi {

// -- Implemented in abi_impl.cpp ----------------------------------------------

AppImpl* app_of(dg_app_t* handle);
WindowImpl* window_of(dg_window_t* handle);
NodeSlot* slot_of(dg_node_t* handle);
// The nearest ancestor-or-self of `id` that has a dg_node_t handle, or null.
dg_node_t* handle_near(AppImpl& impl, WindowImpl& window, dg::NodeId id);
// Applies a focus change's editor side effects and queues
// DG_EVENT_FOCUS_CHANGED for it.
void report_focus(AppImpl& impl, WindowImpl& window, const dg::FocusChange& change);
// The app's active theme and variant, else the builtin theme (dark).
std::pair<const dg::Theme*, dg::ThemeVariant> active_theme_of(const AppImpl& impl);
// Makes the pending node `child_index` live under `parent_id`.
std::int32_t attach_node(AppImpl& impl, std::size_t window_index, dg::NodeId parent_id,
                         std::size_t child_index);
float default_text_size();

// -- Implemented in abi_components.cpp ----------------------------------------

namespace components {

[[nodiscard]] bool is_component_type(std::uint16_t type_id);
// The default box/style of a component node, before it is added.
void defaults(const AppImpl& impl, std::uint16_t type_id, dg::BoxStyle& box,
              dg::NodeStyle& style);
// Builds the component inside the freshly added node `slot.node_id`.
void compose(AppImpl& impl, WindowImpl& window, NodeSlot& slot);
void init_window(AppImpl& impl, WindowImpl& window);

// Event routing, before the core's own handling. True = consumed.
bool route_pointer(AppImpl& impl, WindowImpl& window, const dg::PointerEvent& event);
bool route_key(AppImpl& impl, WindowImpl& window, const dg::KeyEvent& event);
bool route_text(AppImpl& impl, WindowImpl& window, const dg::TextInputEvent& event);
// Queues DG_EVENT_FOCUS_CHANGED when a component moved focus by itself.
void report_focus_drift(AppImpl& impl, WindowImpl& window);

// `timeout_ms` cut to the nearest tooltip deadline / animation frame.
[[nodiscard]] int clamp_timeout(const AppImpl& impl, int timeout_ms);
void tick(AppImpl& impl, WindowImpl& window);
// After a layout pass that re-placed nodes.
void sync(AppImpl& impl, WindowImpl& window);
void restyle(AppImpl& impl, WindowImpl& window);

// Before dg_node_remove() detaches `subtree`; after it marked slots removed.
void before_remove(AppImpl& impl, WindowImpl& window, const std::vector<dg::NodeId>& subtree);
void after_remove(AppImpl& impl);

// Component-specific property/text writes; nullopt = not handled here.
std::optional<std::int32_t> set_prop(AppImpl& impl, WindowImpl& window, NodeSlot& slot,
                                     std::uint16_t prop_id, const dg_value& value);
void after_prop(AppImpl& impl, WindowImpl& window, NodeSlot& slot, std::uint16_t prop_id);
std::optional<std::int32_t> set_text(AppImpl& impl, WindowImpl& window, NodeSlot& slot,
                                     std::string_view text);
// The node keyboard focus goes to for dg_node_focus(slot).
[[nodiscard]] dg::NodeId focus_target(const WindowImpl& window, const NodeSlot& slot);

}  // namespace components
}  // namespace dg::abi
