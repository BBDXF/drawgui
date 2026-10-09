// Internal seams between src/abi/abi_impl.cpp (handles, nodes, windows,
// events) and src/abi/abi_components.cpp (the phase B component entry
// points). Nothing here is exported; the dg_*() entry points of both files
// are declared in abi_impl.h. Both are thin shells over dg::App/dg::Window
// (src/app): they resolve handles, validate C arguments, and map
// dg::Status to DG_ERR_*.

#pragma once

#include <cstdint>

#include "abi_types.h"
#include "drawgui/app/events.h"
#include "drawgui/app/status.h"

namespace dg::abi {

AppImpl* app_of(dg_app_t* handle);
WindowImpl* window_of(dg_window_t* handle);
NodeSlot* slot_of(dg_node_t* handle);

// The DG_ERR_* a runtime Status means.
[[nodiscard]] std::int32_t to_error_code(dg::Status status);

// Moves every event the runtime queued into AppImpl::pending_events,
// converting each to a dg_event. Called before a node can stop resolving
// (removal) so an event names the handle it was queued for.
void drain_events(AppImpl& impl);

// The DG_EVENT_* kind of `event` - the one mapping drain_events() uses.
[[nodiscard]] std::uint32_t dg_event_kind(const dg::Event& event);

// Creates the dg_node_t handle of a node the runtime already created (a
// dg_tabs_add() panel): live from the start.
dg_node_t* adopt_node(dg_app_t* app, std::size_t window_index, dg::NodeId id,
                      std::uint16_t type_id);

}  // namespace dg::abi
