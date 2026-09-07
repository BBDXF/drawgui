// The scene examples/08_opacity puts on screen, and the handles a check needs.
//
// FOUR PANELS, and the first two are the whole point:
//
//   per_object   three overlapping chips, each drawn at alpha 128/255
//   grouped      the same three chips at full alpha, inside a stage whose
//                `opacity` is 128/255
//   nested       a faded stage inside a faded stage, so two layers multiply
//   fading       a stage whose opacity the demo animates from 1 to 0 and back
//
// The first two are built from ONE description with ONE difference - where the
// translucency is asked for - so that what is between them on screen is the
// property and nothing else. That matters more here than anywhere else in this
// project, because per-object alpha and group opacity look IDENTICAL wherever
// content does not overlap, and a demo whose chips did not overlap would show
// two panels that agree and prove nothing.
//
// The chips overlap in pairs on purpose rather than all three at one point:
// two layers of per-object alpha over the backdrop is a different number from
// three, so the per-object panel shows a staircase while the grouped one shows
// one flat tone, which is a difference a human reads without a colour picker.
//
// Built from the LAYOUT tree, like examples/07_clipping, so the panels resize
// with the window. A layer's extent is a function of where its children ended
// up, so a scene at one fixed size would never exercise an extent that moves.

#pragma once

#include <string>
#include <vector>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/render_tree.h"

namespace opacity_scene {

// The alpha every panel fades by, as an exact eight-bit fraction.
//
// 128/255 rather than 0.5 so that a screenshot's numbers are the ones the
// arithmetic predicts: 255 * 128/255 is exactly 128, while 255 * 0.5 lands
// between two integers and the expected value becomes an argument about
// rounding rather than about compositing.
inline constexpr float kHalf = 128.0F / 255.0F;

struct Handles {
  dg::NodeId per_object_stage;
  dg::NodeId grouped_stage;
  dg::NodeId nested_outer;
  dg::NodeId nested_inner;
  dg::NodeId fading_stage;

  // The chips of the two comparable panels, in the same order, so a check can
  // walk them in pairs rather than trusting two hand-written lists to line up.
  std::vector<dg::NodeId> per_object_chips;
  std::vector<dg::NodeId> grouped_chips;
  std::vector<dg::NodeId> nested_chips;
  std::vector<dg::NodeId> fading_chips;
};

struct Scene {
  dg::LayoutTree tree;
  Handles handles;
};

Scene build(const dg::TreeSpec& spec);

// The rectangle two adjacent chips of `chips` share, in absolute pixels. It is
// the only place per-object alpha and group opacity can disagree, so a check
// that cannot find one is a check running against the wrong scene.
[[nodiscard]] dg::PixelRect overlap_of(const Scene& scene, const std::vector<dg::NodeId>& chips,
                                       std::size_t first);

// The part of `chips[index]` no sibling covers, which is where the two panels
// must AGREE - and which is also the only place the grouped panel's own
// uniformity can be stated against a value rather than against itself. Empty
// when the window is too narrow for the chips to keep one, which is a
// condition a caller must refuse rather than skip.
[[nodiscard]] dg::PixelRect solo_band(const Scene& scene, const std::vector<dg::NodeId>& chips,
                                      std::size_t index);

void set_opacity(Scene& scene, dg::NodeId node, float opacity);

[[nodiscard]] std::string describe(const Scene& scene, dg::NodeId id);

}  // namespace opacity_scene
