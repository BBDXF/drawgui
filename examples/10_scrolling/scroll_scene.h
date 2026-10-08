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
// mechanisms - a kLeaf's `scroll_axis` (this slice), `overflow: kClip`,
// and `RenderTree::set_scroll_offset` (runtime state) -
// plus a plain kColumn/kRow holding the items. That composition is the
// whole feature.
//
// SCROLLBARS. Each viewport carries a scrollbar (WidgetSet::
// attach_scrollbar()): a track node added after the content and a thumb
// inside it, both coloured and sized from the builtin theme's
// color.scrollbar-* / size.scrollbar tokens, never a literal here. Each
// viewport reserves a gutter that thick on the bar's side (padding), and the
// items are that much narrower (vertical) or shorter (horizontal), so the
// bar never covers an item.
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
  dg::NodeId vertical_track;
  dg::NodeId vertical_thumb;

  dg::NodeId horizontal_viewport;
  dg::NodeId horizontal_content;
  std::vector<dg::NodeId> horizontal_items;
  dg::NodeId horizontal_track;
  dg::NodeId horizontal_thumb;

  // The scrollbar thickness the theme asked for, and so the gutter width.
  int gutter = 0;
};

struct Scene {
  dg::LayoutTree tree;
  dg::WidgetSet widgets;
  Handles handles;
};

Scene build(const dg::TreeSpec& spec);

// The visible length of `viewport` along the axis it scrolls - the
// `viewport_extent` every WidgetSet scrollbar call takes.
[[nodiscard]] int scroll_extent(const Scene& scene, dg::NodeId viewport);

// Re-places both scrollbars (WidgetSet::sync_scrollbar()) - after build, after
// any layout pass, and after a write to DG_PROP_SCROLLBAR.
void sync_scrollbars(Scene& scene);

// Which viewport (if either) the pointer is over, climbing from the hit node
// exactly as an event loop would - the same call `scroll_window.cpp` makes
// for a real wheel or drag event, exposed here so a headless check can drive
// it without opening a window.
[[nodiscard]] std::string describe(const Scene& scene, dg::NodeId id);

}  // namespace scroll_scene
