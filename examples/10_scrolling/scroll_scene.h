// The scene examples/10_scrolling puts on screen, and the handles a check
// needs.
//
// TWO VIEWPORTS, one per scroll_axis ordinal, so both are exercised on
// screen rather than one being asserted only in tests/unit/test_scroll.cpp:
//
//   vertical    a column of kVerticalItemCount fixed-height chips inside a
//               kLeaf whose scroll_axis is kVertical. Content is
//               kVerticalItemCount * kVerticalItemHeight tall against a
//               kVerticalViewportHeight-tall viewport - enough overflow to
//               make the clamp at both ends visible on a real drag.
//
//   horizontal  the same shape, rotated: a row of chips inside a
//               scroll_axis: kHorizontal leaf.
//
// NEITHER VIEWPORT IS A NEW NODE KIND. Each is composed from three existing
// mechanisms - a kLeaf's `scroll_axis` (this slice), `overflow: kClip`
// (doc/clipping.md), and `RenderTree::set_scroll_offset` (runtime state) -
// plus a plain kColumn/kRow holding the items. `doc/scrolling.md` section 1
// is the argument for why that composition is the whole feature.
//
// NO TEXT. Items are distinguished by a colour ramp rather than a label, the
// same simplification examples/09_sizing made for the same reason: a font
// catalog is machinery this scene does not need to exercise scrolling.

#pragma once

#include <string>
#include <vector>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/widget/widget_set.h"

namespace scroll_scene {

inline constexpr int kVerticalItemCount = 24;
inline constexpr int kVerticalItemHeight = 44;
inline constexpr int kVerticalViewportWidth = 320;
inline constexpr int kVerticalViewportHeight = 440;

inline constexpr int kHorizontalItemCount = 14;
inline constexpr int kHorizontalItemWidth = 140;
inline constexpr int kHorizontalViewportHeight = 110;

// How many pixels one wheel notch or drag pixel moves the content. Named so
// the check can derive an expected offset from a scripted input rather than
// reading it back from the very code under test.
inline constexpr int kPixelsPerWheelNotch = 48;

struct Handles {
  dg::NodeId body;

  dg::NodeId vertical_viewport;
  dg::NodeId vertical_content;
  std::vector<dg::NodeId> vertical_items;

  dg::NodeId horizontal_viewport;
  dg::NodeId horizontal_content;
  std::vector<dg::NodeId> horizontal_items;
};

struct Scene {
  dg::LayoutTree tree;
  dg::WidgetSet widgets;
  Handles handles;
};

Scene build(const dg::TreeSpec& spec);

// Which viewport (if either) the pointer is over, climbing from the hit node
// exactly as an event loop would - the same call `scroll_window.cpp` makes
// for a real wheel or drag event, exposed here so a headless check can drive
// it without opening a window.
[[nodiscard]] std::string describe(const Scene& scene, dg::NodeId id);

}  // namespace scroll_scene
