// The mouse cursor shapes drawgui can show.
//
// Shared by the render tree (NodeStyle::cursor, DG_PROP_CURSOR), the widget
// layer (dg::resolve_cursor()) and the window layer (WindowManager::
// set_cursor()), which is why it lives down here rather than in any of them.
//
// The enumerator order IS the DG_PROP_CURSOR ordinal order
// (props/drawgui.props.toml's `values`, DG_CURSOR_* in
// render/prop_ids.generated.h); src/props/node_props.cpp static_asserts it.

#pragma once

#include <cstdint>

namespace dg {

enum class Cursor : std::uint8_t {
  // Only meaningful on a node: "no opinion, ask the ancestors". The window
  // layer shows it as kDefault.
  kAuto,
  kDefault,
  kPointer,
  kText,
  kCrosshair,
  kMove,
  kNotAllowed,
  kWait,
  kProgress,
  kResizeEw,
  kResizeNs,
  kResizeNwse,
  kResizeNesw,
};

inline constexpr std::uint32_t kCursorCount = 13;

}  // namespace dg
