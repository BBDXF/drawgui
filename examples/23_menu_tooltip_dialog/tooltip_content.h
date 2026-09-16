// A tooltip's own popup content: one plain, non-interactive text label -
// no kButton, no Widget attached at all, because a tooltip (SDL_WINDOW_
// TOOLTIP on the native branch) accepts NO input (doc/popup.md section 1),
// so nothing in its content could ever be clicked or focused regardless of
// what WidgetKind it carried.

#pragma once

#include <string>

#include "drawgui/graphics/types.h"
#include "drawgui/render/render_tree.h"

namespace tooltip_content {

[[nodiscard]] dg::PixelSize size_for(const std::string& text, int font_size_px);

void build(dg::RenderTree& tree, dg::NodeId parent, const std::string& text, dg::FontId font,
           float font_size, dg::PixelSize size);

}  // namespace tooltip_content
