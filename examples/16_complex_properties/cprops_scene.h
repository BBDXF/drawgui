// The scene examples/16_complex_properties puts on screen, and the handles a
// check needs.
//
// THREE PANELS, one per property the dedicated-setter channel (design.md
// section 5.9.5) lands this slice, each built through the CHANNEL itself -
// dg::set_gradient()/dg::set_shadow()/dg::set_image() - rather than by
// writing NodeStyle fields directly, because the thing this demo exists to
// show is that the id-based door works end to end, not merely that the
// fields paint correctly (tests/unit/test_complex_props.cpp already proves
// the door's own validation; this proves the door on a real scene):
//
//   gradient   220x110, three stops (red/green/blue) at offsets 0/0.5/1,
//              angle 0 (horizontal) - the gradient LINE spans exactly the
//              panel's own left/right edges, which is what makes its pixels
//              hand-derivable rather than merely plausible.
//   shadow     120x120, offset (14, 10), blur_radius 0 and spread 0 - a
//              HARD-EDGED shadow, deliberately, so the sliver it casts past
//              the panel's own right and bottom edges is one solid colour at
//              byte precision rather than a blurred gradient a check could
//              only bound. The budget cap and a real blurred shadow are
//              exercised by tests/unit/test_complex_props.cpp instead, where
//              an inequality-based oracle is the right tool.
//   image      128x128, a two-colour source (top half / bottom half)
//              synthesized in-process exactly as examples/13_image's own
//              quadrants are, decoded through the real SkCodec path and
//              attached through dg::set_image() - the channel's PROTOTYPE
//              client, proving the id-based door reaches the same paint path
//              RenderTree::set_image() already proved in slice 5-1.
//
// `transform` has no panel: dg::set_transform() always reports kUnsupported,
// so there is nothing to paint. cprops_check.cpp calls it anyway and prints
// the status, which is the whole demonstration for that property.

#pragma once

#include <cstdint>
#include <vector>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/types.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/props/node_props.h"
#include "drawgui/render/image_catalog.h"
#include "drawgui/render/render_tree.h"

namespace cprops_scene {

inline constexpr dg::Color kPanelFill = dg::Color::from_argb(0xFF20262F);
inline constexpr dg::Color kFrameEdge = dg::Color::from_argb(0xFF5A6472);

// The gradient's three stops, named so a check can recompute the expected
// pixel from the same formula the scene built it with, rather than reading
// the value off a previous run.
inline constexpr dg::Color kGradientStart = dg::Color::from_argb(0xFFFF0000);
inline constexpr dg::Color kGradientMid = dg::Color::from_argb(0xFF00FF00);
inline constexpr dg::Color kGradientEnd = dg::Color::from_argb(0xFF0000FF);

// The shadow's own geometry, named for the same reason.
inline constexpr int kShadowOffsetX = 14;
inline constexpr int kShadowOffsetY = 10;
inline constexpr dg::Color kShadowColor = dg::Color::from_argb(0xFFAA3355);

inline constexpr int kImageSourceSize = 64;
inline constexpr dg::Color kImageTop = dg::Color::from_argb(0xFFE7A23C);
inline constexpr dg::Color kImageBottom = dg::Color::from_argb(0xFF3C86E7);

inline constexpr dg::PixelSize kDemoViewport{640, 220};

// The two-colour source: top half `kImageTop`, bottom half `kImageBottom`,
// `size` pixels square - reproducible from the two dg::Canvas::fill_rect
// calls alone, no checked-in asset, matching examples/13_image's own solution
// to the golden-source-image problem.
[[nodiscard]] std::vector<std::uint8_t> halves(int size);

struct Handles {
  dg::NodeId row;
  dg::NodeId gradient_panel;
  dg::NodeId shadow_panel;
  dg::NodeId image_panel;
};

struct Scene {
  dg::LayoutTree tree;
  dg::ImageCatalog images;
  Handles handles;

  // What dg::set_transform() answered when this scene was built - always
  // kUnsupported, carried here so a check or the window's own console log
  // can report it without rebuilding the call.
  dg::PropWrite transform_result;
};

// `spec.images` is overwritten with a catalog holding the decoded two-colour
// source, matching examples/13_image's own Scene::build() contract.
Scene build(dg::TreeSpec spec);

}  // namespace cprops_scene
