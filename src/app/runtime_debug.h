// Read-only access to runtime internals for the C ABI's dg_debug_*() hooks.
// Not public API.

#pragma once

#include "app/state.h"
#include "drawgui/app/window.h"
#include "drawgui/graphics/raster_surface.h"

namespace dg::app_detail {

// The surface `window` last painted into, or null before its first frame.
[[nodiscard]] inline const dg::RasterSurface* painted_surface(const dg::Window& window) {
  const WinState& state = window.impl();
  return state.surface.has_value() ? &*state.surface : nullptr;
}

}  // namespace dg::app_detail
