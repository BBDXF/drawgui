// The context menu's own popup content: one interactive kButton row per
// item label, stacked in a plain kColumn - identical shape and identical
// argument to examples/22_dropdown_menu/dropdown_options.{h,cpp} (kList's
// pool nodes carry no attached Widget, so they cannot be focused/clicked -
// doc/menus.md section 3 - rows are plain kButtons instead). A second copy
// rather than reusing dropdown_options.{h,cpp} directly: that file's own
// `size_for()`/`build()` are dropdown-shaped by name and by directory
// (examples/22_dropdown_menu/), and this project's own precedent is a
// per-example content builder (dropdown_options itself is not shared with
// examples/21_focus's own popup_menu.cpp, a near-identical file) rather
// than promoting either to a shared library the day a second consumer
// appears with no third yet asking for one.

#pragma once

#include <string>
#include <vector>

#include "drawgui/graphics/types.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/widget/widget_set.h"

namespace menu_rows {

inline constexpr int kRowHeight = 28;
inline constexpr int kRowPad = 10;

[[nodiscard]] dg::PixelSize size_for(int count, int width);

std::vector<dg::NodeId> build(dg::RenderTree& tree, dg::WidgetSet& widgets, dg::NodeId parent,
                              const std::vector<std::string>& items, int width, dg::FontId font,
                              float font_size);

}  // namespace menu_rows
