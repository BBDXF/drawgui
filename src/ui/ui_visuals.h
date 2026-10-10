// Node building and painting for the popup components - every colour,
// size and node shape Menu/Tooltip/Dialog put on screen lives here, apart
// from their behaviour (menu.cpp, tooltip_popup.cpp, dialog.cpp), so a
// visual pass touches only this file and ui_context.cpp's
// popup_style_from_theme().

#pragma once

#include <string>
#include <vector>

#include "drawgui/ui/dialog.h"
#include "drawgui/ui/menu.h"
#include "drawgui/ui/ui_context.h"

namespace dg::ui_visuals {

[[nodiscard]] PixelSize menu_size(const UiContext& ctx, const MenuSpec& spec);

// One node per item, children of `parent` (the popup's content_root). Item
// rows get a kButton widget; separator rows get none; disabled rows carry
// NodeStyle::disabled.
std::vector<NodeId> build_menu(UiContext& ctx, NodeId parent, const MenuSpec& spec, int width);

// Repaints one item row as highlighted or not.
void paint_menu_item(UiContext& ctx, NodeId row, const MenuItem& item, bool highlighted);

[[nodiscard]] PixelSize tooltip_size(const UiContext& ctx, const std::string& text);
void build_tooltip(UiContext& ctx, NodeId parent, const std::string& text, PixelSize size);

struct DialogNodes {
  NodeId panel;
  NodeId body;
  int body_content_top = 0;
  std::vector<NodeId> buttons;
};
DialogNodes build_dialog(UiContext& ctx, NodeId parent, PixelSize window,
                         const DialogSpec& spec);
// The usable width inside the dialog panel build_dialog() makes.
[[nodiscard]] int dialog_inner_width(const UiContext& ctx, PixelSize window,
                                     const DialogSpec& spec);
// Repaints one dialog button enabled or disabled (dimmed; NodeStyle::
// disabled, so Tab skips it).
void paint_dialog_button(UiContext& ctx, NodeId button, bool is_default, bool disabled);

}  // namespace dg::ui_visuals
