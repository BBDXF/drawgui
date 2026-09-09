// The scene examples/09_sizing puts on screen, and the handles a check needs.
//
// FOUR ROWS, one per thing the second sizing stage does, chosen so that
// resizing the window drives each of them through the transition that matters
// rather than merely changing its numbers:
//
//   toolbar   three buttons of the same declared width and DIFFERENT shrink
//             weights. Wide, they sit at 200 each and nothing shrinks; narrow,
//             the deficit is split by `shrink x base` and they compress at
//             visibly different rates. Dragging the window across that point
//             is the surplus-to-deficit transition.
//
//   ratio     two thumbnails with no size of their own, stretched to the row's
//             height, deriving their WIDTH from it. The row takes the leftover
//             vertical space, so dragging the window taller changes their
//             cross axis and their main axis has to follow - the
//             cross-to-main dependency doc/sizing.md section 4.1 is written
//             around.
//
//   chips     four declared bases that add up to more than a narrow window
//             offers, so a basis that fitted stops fitting.
//
//   footer    two items pinned to the right edge, which they reach only
//             because the row FILLS the main axis. With main_size at its
//             default the row would hug its content and `justify` would have
//             nothing to place.
//
// The buttons carry the same base and differ only in weight, deliberately.
// Three buttons that also differed in width would compress at different rates
// for two reasons at once, and a reader could not tell which one they were
// watching.

#pragma once

#include <string>
#include <vector>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/render_tree.h"

namespace sizing_scene {

// The buttons' shared base, and their three weights. Named because the check
// re-derives the expected widths from them rather than from the tree it is
// checking.
inline constexpr int kButtonBase = 200;
inline constexpr int kButtonWeights[] = {1, 2, 3};

inline constexpr int kChipBases[] = {220, 170, 130, 90};

inline constexpr float kWideRatio = 16.0F / 9.0F;
inline constexpr float kSquareRatio = 1.0F;

struct Handles {
  dg::NodeId body;

  dg::NodeId toolbar;
  std::vector<dg::NodeId> buttons;

  dg::NodeId ratio_row;
  dg::NodeId wide_thumb;
  dg::NodeId square_thumb;
  dg::NodeId ratio_filler;

  // Takes three quarters of the leftover vertical space, so the ratio row
  // above it takes a quarter. That is what keeps the derived 16:9 width inside
  // the window: a `max_height` on the ratio row would be overridden by the
  // tight main constraint its grow weight earns it.
  dg::NodeId content_panel;

  dg::NodeId chip_row;
  std::vector<dg::NodeId> chips;

  dg::NodeId footer;
  std::vector<dg::NodeId> footer_items;
};

struct Scene {
  dg::LayoutTree tree;
  Handles handles;
};

Scene build(const dg::TreeSpec& spec);

[[nodiscard]] std::string describe(const Scene& scene, dg::NodeId id);

}  // namespace sizing_scene
