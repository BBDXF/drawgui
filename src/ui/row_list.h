// Behaviour TreeView and Table share over their kList: pointer-to-row
// mapping, wheel/scrollbar routing, keeping a row in view and the key ->
// target-row table. Internal to src/ui/.

#pragma once

#include <optional>

#include "drawgui/ui/control_context.h"
#include "drawgui/window/window_manager.h"

namespace dg::row_list {

// The visible extent of `list` (its border box height; it has no vertical
// padding).
[[nodiscard]] int viewport(const ControlContext& ctx, NodeId list);

// The logical row under absolute `point`, or nothing (outside the row area,
// in the gutter, or past the last row).
[[nodiscard]] std::optional<int> row_at(const ControlContext& ctx, NodeId list,
                                        PixelPoint point, int count);

// Wheel over `root`, or a press/drag/release on `list`'s scrollbar. True when
// consumed; `scrolled` is set when the offset moved (repaint the rows).
// `dragging` is the control's own "thumb drag in progress" bit.
bool scroll_pointer(ControlContext& ctx, NodeId root, NodeId list, const PointerEvent& event,
                    bool& dragging, bool& scrolled);

// Scrolls the minimum needed so `row` is fully visible. True when it moved.
bool reveal(ControlContext& ctx, NodeId list, int row);

// Up/Down/Home/End/PageUp/PageDown from `current` over `count` rows (no
// selection yet: Down/Home start at 0, Up/End at the end). Nothing for any
// other key or an empty list.
[[nodiscard]] std::optional<int> nav_target(const ControlContext& ctx, NodeId list, Key key,
                                            std::optional<int> current, int count);

// Whether `root` should take a key event: it has focus, is enabled, and the
// event is a key-down.
[[nodiscard]] bool owns_key(const ControlContext& ctx, NodeId root, const KeyEvent& event);

}  // namespace dg::row_list
