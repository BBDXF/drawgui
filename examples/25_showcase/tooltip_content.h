#pragma once

#include <string>

#include "drawgui/graphics/types.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/theme/theme.h"

namespace tooltip_content {

[[nodiscard]] dg::PixelSize size_for(const std::string& text, int font_size_px);

void build(dg::RenderTree& tree, dg::NodeId parent, const std::string& text, dg::FontId font,
           float font_size, dg::PixelSize size, const dg::Theme& theme,
           dg::ThemeVariant variant);

}  // namespace tooltip_content
