#include "popup_scene.h"

namespace popup_scene {

void build_menu_content(dg::RenderTree& tree, dg::NodeId parent, dg::PixelSize size) {
  dg::NodeStyle background_style;
  background_style.fill = kBackground;
  tree.add_child(parent, dg::PixelRect{0, 0, size.width, size.height}, background_style);

  int top = kPadding;
  for (const dg::Color& color : kRowColors) {
    dg::NodeStyle row_style;
    row_style.fill = color;
    const dg::PixelRect row_bounds{kPadding, top, size.width - (2 * kPadding), kRowHeight};
    tree.add_child(parent, row_bounds, row_style);
    top += kRowHeight + kRowGap;
  }
}

}  // namespace popup_scene
