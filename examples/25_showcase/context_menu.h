#pragma once

#include <string>
#include <vector>

#include "drawgui/graphics/types.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/theme/theme.h"
#include "drawgui/widget/widget_set.h"

namespace context_menu {

inline constexpr int kRowHeight = 28;

[[nodiscard]] dg::PixelSize size_for(int count, int width);

std::vector<dg::NodeId> build(dg::RenderTree& tree, dg::WidgetSet& widgets, dg::NodeId parent,
                              const std::vector<std::string>& items, int width, dg::FontId font,
                              const dg::Theme& theme, dg::ThemeVariant variant);

}  // namespace context_menu
