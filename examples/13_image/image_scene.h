// The scene examples/13_image puts on screen, and the handles a check needs.
//
// FIVE PANELS, each a plain kLeaf sized so that its fit mode's arithmetic
// produces a visibly different answer - the demonstration IS the mismatch
// between the panel's own box and the source bitmap's shape, not a resize
// (design.md section 5.10.3's sizing rule is what makes that safe: every
// panel below is explicit width/height, so nothing here depends on the
// source having finished decoding before the box exists):
//
//   fill       192x96, a NON-square box against a square source - stretches
//              independently on each axis, visibly distorting the source's
//              own 1:1 aspect ratio.
//   contain    96x192 (tall) - scales uniformly to 96x96, letterboxed with
//              48px of background above and below.
//   cover      192x96 (wide) - scales uniformly to 192x192, centred and
//              cropped 48px off the top and bottom.
//   none       160x160 - the 64x64 source unscaled, centred with a 48px
//              margin of background on every side.
//   placeholder 128x128, no source at all - the plain configurable colour
//              design.md section 5.10.3 asks for in place of a hole.
//
// The source itself is SYNTHESIZED, not shipped as an asset: `quadrants()`
// draws four flat-coloured squares through the ordinary public
// dg::Canvas/dg::RasterSurface API, encodes them to a real PNG with
// RasterSurface::encode_png(), and image_scene::build() decodes those bytes
// through dg::ImageCatalog::decode() - the real SkCodec path, exercised
// without a checked-in binary or a version-pinned baseline. Both this file
// and tests/unit/test_image.cpp draw the same four squares from the same
// four dg::Canvas::fill_rect calls, so the two are the same formula rather
// than independently-invented ones that could quietly disagree.

#pragma once

#include <cstdint>
#include <vector>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/types.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/image_catalog.h"
#include "drawgui/render/render_tree.h"

namespace image_scene {

// The synthesized source's own size and the four quadrant colours, named so
// a check can recompute expected geometry instead of re-reading it off the
// scene it is checking.
inline constexpr int kSourceSize = 64;
inline constexpr dg::Color kTopLeft = dg::Color::rgba(0xE7, 0x4C, 0x3C);
inline constexpr dg::Color kTopRight = dg::Color::rgba(0x2E, 0xCC, 0x71);
inline constexpr dg::Color kBottomLeft = dg::Color::rgba(0x2E, 0x86, 0xDE);
inline constexpr dg::Color kBottomRight = dg::Color::rgba(0xF1, 0xC4, 0x0F);
inline constexpr dg::Color kPlaceholderColor = dg::Color::rgba(0x55, 0x5B, 0x66);
inline constexpr dg::Color kFrameFill = dg::Color::from_argb(0xFF20262F);

// Wide and tall enough that the row of five panels (768px of panels, 96px of
// gaps, 48px of padding = 912 wide; the tallest panel at 192 plus 48 of
// padding = 240 tall) fits with room to spare - a window this demo opens
// narrower than this would overrun the row's main axis, which is a real
// LayoutTree diagnostic (design.md section 5.4.7) and not a bug in the scene.
inline constexpr dg::PixelSize kDemoViewport{960, 300};

// Four flat quadrants, `size` pixels square. The formula is the four
// dg::Canvas::fill_rect calls themselves - reproducible from source, no
// checked-in asset, no pinned hash.
[[nodiscard]] std::vector<std::uint8_t> quadrants(int size);

struct Handles {
  dg::NodeId row;
  dg::NodeId fill_panel;
  dg::NodeId contain_panel;
  dg::NodeId cover_panel;
  dg::NodeId none_panel;
  dg::NodeId placeholder_panel;
};

struct Scene {
  dg::LayoutTree tree;
  dg::ImageCatalog images;
  dg::ImageId source;
  Handles handles;
};

// `spec.images` is overwritten with a catalog holding one decoded entry - the
// synthesized source above - so a caller only has to supply the viewport and
// background.
Scene build(dg::TreeSpec spec);

}  // namespace image_scene
