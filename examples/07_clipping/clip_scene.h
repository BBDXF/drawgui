// The scene examples/07_clipping puts on screen, and the handles a check needs.
//
// It is built from the LAYOUT tree rather than by placing rectangles, because
// the amount by which a child overflows its container has to change when the
// window is resized - a scene with hard-coded overflow would look the same at
// every size and would prove nothing about a clip that has to follow a moving
// boundary.
//
// Five panels, each one a shape the clip code branches on:
//
//   open      a container of oversized children with overflow = visible, so
//             the overflow is visible on screen next to the clipped one
//   cut       the same container with overflow = clip
//   round     a clipped container with border_radius, which is the case that
//             is not clip-invariant and the case a card or an avatar is
//   nest      a clipped container holding a clipped container, whose child
//             reaches past both
//   gone      a clipped container whose child is entirely outside it
//
// `open` and `cut` are deliberately built from ONE description with one flag
// different, so that what is on screen between them is the property and
// nothing else.

#pragma once

#include <string>
#include <vector>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/font_catalog.h"

namespace clip_scene {

struct Handles {
  dg::NodeId open;
  dg::NodeId open_child;
  dg::NodeId cut;
  dg::NodeId cut_child;
  dg::NodeId round;
  dg::NodeId round_child;
  dg::NodeId nest_outer;
  dg::NodeId nest_inner;
  dg::NodeId nest_child;
  dg::NodeId gone;
  dg::NodeId gone_child;

  // Every node whose visibility a clip decides, which is what the check
  // sweeps and what the demo reports a hover against.
  std::vector<dg::NodeId> clipped;
};

struct Scene {
  dg::LayoutTree tree;
  Handles handles;
};

Scene build(const dg::TreeSpec& spec);

// How far the widest child overruns its container, in pixels. The demo prints
// it and the check requires a resize ladder to produce several different
// values - a ladder that produced one is six runs of the same case.
[[nodiscard]] int overflow_amount(const Scene& scene);

[[nodiscard]] std::string describe(const Scene& scene, dg::NodeId id);

}  // namespace clip_scene
