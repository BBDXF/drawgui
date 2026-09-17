// The sort-order dropdown's own popup content - identical shape to
// examples/22_dropdown_menu/dropdown_options.{h,cpp}, kept as its own file
// per this project's own established precedent (dropdown_options.cpp is
// not shared with examples/21_focus's near-identical popup_menu.cpp
// either): one content builder per example, promoted to a shared file only
// the day a third consumer asks for one.

#pragma once

#include <string>
#include <vector>

#include "drawgui/graphics/types.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/theme/theme.h"
#include "drawgui/widget/widget_set.h"

namespace sort_options {

inline constexpr int kRowHeight = 28;

[[nodiscard]] dg::PixelSize size_for(int count, int width);

std::vector<dg::NodeId> build(dg::RenderTree& tree, dg::WidgetSet& widgets, dg::NodeId parent,
                              const std::vector<std::string>& options, int width,
                              dg::FontId font, const dg::Theme& theme,
                              dg::ThemeVariant variant);

}  // namespace sort_options
