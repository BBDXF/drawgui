// Image content: decode, the size-before-decode constraint, the image-swap-
// does-not-relayout property, fit-mode placement, and the placeholder.
//
// design.md section 5.10.3 states the load-bearing rule in one sentence:
// `RenderImage` must be able to size itself before its content has finished
// decoding, through explicit width/height, aspect_ratio, or a parent
// constraint (grow, stretch, main_size: max) - never through the decoded
// content itself, because that would make every finished load a reflow. This
// file's central test proves the property the rule buys rather than merely
// asserting the rule fired: decode a second bitmap of a DIFFERENT pixel size,
// swap it in, and show LayoutStats reports zero layout work, not merely that
// the numbers happened to agree.
//
// THE SOURCE IMAGE PROBLEM. Every existing golden scene in this project is
// vector fills/borders/text with a checked-in PNG baseline compared byte for
// byte. An image test needs a decode step with no such precedent, and
// depending on a checked-in binary PNG would tie this file to whichever
// libpng shipped in this build's Skia. The synthesized-source technique
// examples/02_skia_cpu_gallery/resources.cpp already established for the
// identical reason (see its make_photo()) is reused here: `quadrant_png()`
// below draws four flat-coloured quadrants through the ordinary, already-
// public dg::Canvas/dg::RasterSurface API - itself a documented pixel
// formula, not a binary blob - encodes it to a real PNG with
// RasterSurface::encode_png(), and ImageCatalog::decode() runs the REAL
// SkCodec PNG decode path over those bytes. The assertions below are then
// specific decoded-and-painted pixel colours at hand-computed coordinates,
// the same oracle technique examples 09-12's `--verify-*` modes and
// tests/unit/test_opacity.cpp already use instead of a second checked-in
// baseline - so this is a NEW oracle, not a modification of the existing
// byte-exact golden suite, which stays untouched.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/base/expected.h"
#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/canvas.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/graphics/types.h"
#include "drawgui/layout/box.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/image_catalog.h"
#include "drawgui/render/render_tree.h"

namespace {

using dg::BoxStyle;
using dg::Canvas;
using dg::Color;
using dg::CrossAlign;
using dg::ImageCatalog;
using dg::ImageError;
using dg::ImageFit;
using dg::ImageId;
using dg::ImageStyle;
using dg::LayoutKind;
using dg::LayoutStats;
using dg::LayoutTree;
using dg::NodeId;
using dg::NodeStyle;
using dg::PixelPoint;
using dg::PixelRect;
using dg::PixelSize;
using dg::RasterSurface;
using dg::Rect;

// Four flat-coloured quadrants, `size` pixels square (`size` must be even).
// The formula IS the four fill_rect calls below - nothing about the source
// bitmap depends on anything not written out here, so it needs no checked-in
// asset and no pinned hash to be reproducible from source alone.
std::vector<std::uint8_t> quadrant_png(int size) {
  std::optional<RasterSurface> surface = RasterSurface::create(size, size);
  if (!surface.has_value()) {
    FAIL("could not allocate a raster surface");
    return {};
  }
  Canvas canvas = surface->canvas();
  const auto half = static_cast<float>(size) / 2.0F;
  canvas.fill_rect(Rect::from_xywh(0, 0, half, half), Color::rgba(0xE7, 0x4C, 0x3C));  // TL red
  canvas.fill_rect(Rect::from_xywh(half, 0, half, half),
                   Color::rgba(0x2E, 0xCC, 0x71));  // TR green
  canvas.fill_rect(Rect::from_xywh(0, half, half, half),
                   Color::rgba(0x2E, 0x86, 0xDE));  // BL blue
  canvas.fill_rect(Rect::from_xywh(half, half, half, half),
                   Color::rgba(0xF1, 0xC4, 0x0F));  // BR yellow
  return surface->encode_png();
}

std::uint32_t pixel_at(const RasterSurface& surface, int x, int y) {
  const dg::PixelView view = surface.peek_pixels();
  const std::size_t offset =
      (static_cast<std::size_t>(y) * view.row_bytes) + (static_cast<std::size_t>(x) * 4);
  return (static_cast<std::uint32_t>(view.pixels[offset + 3]) << 24U) |
         (static_cast<std::uint32_t>(view.pixels[offset + 2]) << 16U) |
         (static_cast<std::uint32_t>(view.pixels[offset + 1]) << 8U) |
         static_cast<std::uint32_t>(view.pixels[offset]);
}

std::uint32_t solid(Color color) {
  return color.argb();
}

// An explicit, opaque background for every fit-mode paint test below, so a
// sample landing outside the drawn image reads a known colour rather than
// whatever a freshly allocated raster surface happens to hold uninitialized.
constexpr Color kBackground = Color::rgba(0x11, 0x22, 0x33);

// ---------------------------------------------------------------------------
// Decode.
// ---------------------------------------------------------------------------

TEST_CASE("image catalog decode grows the table and reports a valid, held id") {
  const std::vector<std::uint8_t> png = quadrant_png(8);
  CHECK_FALSE(png.empty());

  ImageCatalog catalog;
  CHECK(catalog.size() == 0);
  const dg::Expected<ImageId, ImageError> decoded = catalog.decode(png.data(), png.size());
  REQUIRE(decoded.has_value());
  const ImageId id = decoded.value();
  CHECK(id.is_valid());
  CHECK(catalog.holds(id));
  CHECK(catalog.size() == 1);
}

// A plain int pair rather than std::optional<PixelSize> in the signature, on
// purpose: every caller of this helper wants two ordinary numbers, and having
// the "was this even decoded" question answered right here - inside a
// function with no CHECK/REQUIRE macros in it at all - is what keeps that
// question out of the TEST_CASE below, where clang-tidy's cognitive-
// complexity budget is tightest (doctest expands each assertion into its own
// branch, so the same logic reads as far more complex inside a TEST_CASE
// than as a plain function - the same reason tests/unit/test_clip.cpp and
// test_font_coverage.cpp already hoist their own sweeps out of their cases).
struct DecodedSize {
  bool ok = false;
  int width = 0;
  int height = 0;
};

DecodedSize decode_and_measure(int source_size) {
  ImageCatalog catalog;
  const std::vector<std::uint8_t> png = quadrant_png(source_size);
  const dg::Expected<ImageId, ImageError> decoded = catalog.decode(png.data(), png.size());
  if (!decoded.has_value()) {
    return DecodedSize{};
  }
  const std::optional<PixelSize> size = catalog.pixel_size(decoded.value());
  if (!size.has_value()) {
    return DecodedSize{};
  }
  return DecodedSize{true, size->width, size->height};
}

TEST_CASE("a decoded id's pixel_size reports the source's own dimensions") {
  const DecodedSize measured = decode_and_measure(8);
  REQUIRE(measured.ok);
  CHECK(measured.width == 8);
  CHECK(measured.height == 8);
}

TEST_CASE("image catalog decode fails on bytes no codec accepts") {
  const std::vector<std::uint8_t> garbage{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
  ImageCatalog catalog;
  dg::Expected<ImageId, ImageError> decoded = catalog.decode(garbage.data(), garbage.size());
  REQUIRE_FALSE(decoded.has_value());
  CHECK_FALSE(decoded.error().message.empty());
  CHECK(catalog.size() == 0);
}

TEST_CASE("image catalog decode fails on no bytes") {
  ImageCatalog catalog;
  dg::Expected<ImageId, ImageError> decoded = catalog.decode(nullptr, 0);
  REQUIRE_FALSE(decoded.has_value());
}

TEST_CASE("an id from a different catalog is not held") {
  ImageCatalog first;
  ImageCatalog second;
  const std::vector<std::uint8_t> png = quadrant_png(4);
  const ImageId id = first.decode(png.data(), png.size()).value();
  CHECK(first.holds(id));
  CHECK_FALSE(second.holds(id));
  CHECK_FALSE(second.pixel_size(id).has_value());
}

// ---------------------------------------------------------------------------
// carries_image().
// ---------------------------------------------------------------------------

TEST_CASE("carries_image is false for a default style, true for a source or a placeholder") {
  CHECK_FALSE(dg::carries_image(ImageStyle{}));

  ImageStyle with_source;
  with_source.source = ImageId{1};
  CHECK(dg::carries_image(with_source));

  ImageStyle with_placeholder;
  with_placeholder.placeholder = Color::rgba(0x80, 0x80, 0x80);
  CHECK(dg::carries_image(with_placeholder));

  ImageStyle transparent_placeholder;
  transparent_placeholder.placeholder = Color::rgba(0x80, 0x80, 0x80, 0x00);
  CHECK_FALSE(dg::carries_image(transparent_placeholder));
}

// ---------------------------------------------------------------------------
// design.md section 5.10.3's mandatory sizing rule: three legal routes, and
// the illegal fourth reported as a diagnostic rather than asserted or
// silently sized from content.
// ---------------------------------------------------------------------------

BoxStyle image_leaf() {
  BoxStyle box;
  box.kind = LayoutKind::kLeaf;
  return box;
}

NodeStyle image_bearing_style() {
  NodeStyle style;
  style.image.source = ImageId{1};  // need not resolve in a catalog for a layout-only test
  return style;
}

bool has_sizing_diagnostic(const LayoutTree& tree) {
  return std::ranges::any_of(tree.diagnostics(), [](const std::string& line) {
    return line.find("image has no determinate size before decode") != std::string::npos;
  });
}

TEST_CASE("illegal fourth case: no width/height, no aspect_ratio, no parent constraint") {
  dg::TreeSpec spec;
  spec.viewport = PixelSize{200, 200};
  LayoutTree tree{spec};

  // The root is a kColumn (LayoutTree::Impl::Impl); an ordinary child with no
  // sizing property of its own is measured loose on both axes, which is
  // exactly the "no legal route fired" case.
  tree.add_child(LayoutTree::root(), image_leaf(), image_bearing_style());

  tree.layout_full();
  CHECK(has_sizing_diagnostic(tree));
}

TEST_CASE("legal route 1: explicit width and height settle the image leaf") {
  dg::TreeSpec spec;
  spec.viewport = PixelSize{200, 200};
  LayoutTree tree{spec};

  BoxStyle box = image_leaf();
  box.width = 40;
  box.height = 30;
  const NodeId node = tree.add_child(LayoutTree::root(), box, image_bearing_style());

  tree.layout_full();
  CHECK_FALSE(has_sizing_diagnostic(tree));
  CHECK(tree.bounds(node).width == 40);
  CHECK(tree.bounds(node).height == 30);
}

TEST_CASE("legal route 2: aspect_ratio derives the second axis from a settled one") {
  dg::TreeSpec spec;
  spec.viewport = PixelSize{200, 200};
  LayoutTree tree{spec};

  BoxStyle box = image_leaf();
  box.width = 60;
  box.aspect_ratio = 2.0F;  // width / height == 2, so height derives to 30
  const NodeId node = tree.add_child(LayoutTree::root(), box, image_bearing_style());

  tree.layout_full();
  CHECK_FALSE(has_sizing_diagnostic(tree));
  CHECK(tree.bounds(node).width == 60);
  CHECK(tree.bounds(node).height == 30);
}

TEST_CASE("legal route 3: a parent constraint (grow + stretch) settles both axes") {
  dg::TreeSpec spec;
  spec.viewport = PixelSize{200, 100};
  LayoutTree tree{spec};

  BoxStyle row = image_leaf();
  row.kind = LayoutKind::kRow;
  row.width = 200;
  row.height = 50;
  row.cross_align = CrossAlign::kStretch;
  const NodeId row_id = tree.add_child(LayoutTree::root(), row, NodeStyle{});

  BoxStyle grown = image_leaf();
  grown.grow = 1;
  const NodeId node = tree.add_child(row_id, grown, image_bearing_style());

  tree.layout_full();
  CHECK_FALSE(has_sizing_diagnostic(tree));
  CHECK(tree.bounds(node).width == 200);
  CHECK(tree.bounds(node).height == 50);
}

// ---------------------------------------------------------------------------
// THE central claim: swapping the decoded image for one of a completely
// different pixel size must not move a single node. Measured with
// LayoutStats, the way 4-6/4-7/4-9 measured their own relayout claims,
// rather than asserted.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// THE central claim: swapping the decoded image for one of a completely
// different pixel size must not move a single node. Measured with
// LayoutStats, the way 4-6/4-7/4-9 measured their own relayout claims,
// rather than asserted.
//
// All of the setup - decode, build, first layout, swap, second layout -
// lives in measure_swap() rather than in the TEST_CASE below, for the same
// cognitive-complexity reason decode_and_measure() above does: a plain
// function's control flow is cheap, the same logic re-read through
// CHECK/REQUIRE macros is not.
// ---------------------------------------------------------------------------

struct SwapMeasurement {
  bool decoded = false;
  int small_width = 0;
  int large_width = 0;
  PixelRect before;
  PixelRect after;
  LayoutStats after_swap;
};

SwapMeasurement measure_swap() {
  SwapMeasurement result;
  ImageCatalog catalog;
  const ImageId small = catalog.decode(quadrant_png(8).data(), quadrant_png(8).size()).value();
  const ImageId large =
      catalog.decode(quadrant_png(400).data(), quadrant_png(400).size()).value();
  const std::optional<PixelSize> small_size = catalog.pixel_size(small);
  const std::optional<PixelSize> large_size = catalog.pixel_size(large);
  if (!small_size.has_value() || !large_size.has_value()) {
    return result;
  }
  result.decoded = true;
  result.small_width = small_size->width;
  result.large_width = large_size->width;

  dg::TreeSpec spec;
  spec.viewport = PixelSize{200, 200};
  spec.images = catalog;
  LayoutTree tree{spec};

  BoxStyle box = image_leaf();
  box.width = 50;
  box.height = 50;
  NodeStyle style;
  style.image.source = small;
  const NodeId node = tree.add_child(LayoutTree::root(), box, style);

  tree.layout_full();
  result.before = tree.bounds(node);

  // A wildly different decoded size - 400x400 against the 8x8 the box was
  // laid out against - swapped in through the SAME call an eventual async
  // decode would use once its result arrives.
  NodeStyle swapped = style;
  swapped.image.source = large;
  tree.render().set_image(node, swapped.image);

  result.after_swap = tree.layout();
  result.after = tree.bounds(node);
  return result;
}

TEST_CASE("swapping a decoded image for a different pixel size relays out nothing") {
  const SwapMeasurement result = measure_swap();
  REQUIRE(result.decoded);
  CHECK(result.small_width == 8);
  CHECK(result.large_width == 400);
  CHECK(result.before.width == 50);
  CHECK(result.before.height == 50);
  CHECK(result.after_swap.nodes_visited == 0);
  CHECK(result.after_swap.nodes_relaid_out == 0);
  CHECK(result.after_swap.dirty_roots == 0);
  CHECK(result.after == result.before);
}

// ---------------------------------------------------------------------------
// Fit modes, against the real decode-then-paint path. One 8x8 source (4x4
// quadrants) painted at a node whose own dimensions are chosen per case, so
// every fit mode's arithmetic produces a distinct, hand-computable answer.
// ---------------------------------------------------------------------------

// Builds an 8x8-quadrant-source scene at `viewport`, one node exactly filling
// it under `fit`, repaints it and returns the colour at each of `points`, in
// order - empty on any allocation/decode failure. Returning plain sampled
// colours rather than invoking a CHECK-laden callback is what keeps this
// helper's own complexity low AND keeps every TEST_CASE below to a flat list
// of CHECKs with no nested lambda, for the identical cognitive-complexity
// reason decode_and_measure() and measure_swap() above are shaped this way.
std::vector<std::uint32_t> sample_fit_scene(PixelSize viewport, ImageFit fit,
                                            const std::vector<PixelPoint>& points) {
  ImageCatalog catalog;
  const std::vector<std::uint8_t> png = quadrant_png(8);
  const dg::Expected<ImageId, ImageError> decoded = catalog.decode(png.data(), png.size());
  if (!decoded.has_value()) {
    return {};
  }

  dg::TreeSpec spec;
  spec.viewport = viewport;
  spec.background.fill = kBackground;
  spec.images = std::move(catalog);
  LayoutTree tree{spec};

  BoxStyle box = image_leaf();
  box.width = viewport.width;
  box.height = viewport.height;
  NodeStyle style;
  style.image.source = decoded.value();
  style.image.fit = fit;
  tree.add_child(LayoutTree::root(), box, style);
  tree.layout_full();

  std::optional<RasterSurface> surface = RasterSurface::create(viewport.width, viewport.height);
  if (!surface.has_value()) {
    return {};
  }
  tree.render().repaint_full(*surface);

  std::vector<std::uint32_t> samples;
  samples.reserve(points.size());
  for (const PixelPoint& point : points) {
    samples.push_back(pixel_at(*surface, point.x, point.y));
  }
  return samples;
}

TEST_CASE("fit: fill stretches the 8x8 source across a 16x16 node, quadrant for quadrant") {
  const std::vector<std::uint32_t> pixels = sample_fit_scene(
      PixelSize{16, 16}, ImageFit::kFill, {{2, 2}, {13, 2}, {2, 13}, {13, 13}});
  REQUIRE(pixels.size() == 4);
  CHECK(pixels[0] == solid(Color::rgba(0xE7, 0x4C, 0x3C)));  // TL red
  CHECK(pixels[1] == solid(Color::rgba(0x2E, 0xCC, 0x71)));  // TR green
  CHECK(pixels[2] == solid(Color::rgba(0x2E, 0x86, 0xDE)));  // BL blue
  CHECK(pixels[3] == solid(Color::rgba(0xF1, 0xC4, 0x0F)));  // BR yellow
}

TEST_CASE("fit: none draws the source unscaled and centred, leaving the node's own edge bare") {
  // An 8x8 source inside a 16x16 node, unscaled, sits at [4, 12) on both
  // axes; a corner of the NODE outside that centred square is background,
  // never a quadrant colour.
  const std::vector<std::uint32_t> pixels =
      sample_fit_scene(PixelSize{16, 16}, ImageFit::kNone, {{5, 5}, {1, 1}});
  REQUIRE(pixels.size() == 2);
  CHECK(pixels[0] == solid(Color::rgba(0xE7, 0x4C, 0x3C)));  // TL red, centred
  CHECK(pixels[1] == solid(kBackground));
}

TEST_CASE("fit: contain letterboxes a taller-than-wide node without cropping") {
  // A 16-wide, 32-tall node: contain scales the 8x8 source by min(16/8,
  // 32/8) = 2, to 16x16, centred vertically - so it occupies y in [8, 24)
  // and the top/bottom 8-pixel bands stay background.
  const std::vector<std::uint32_t> pixels = sample_fit_scene(
      PixelSize{16, 32}, ImageFit::kContain, {{2, 2}, {2, 10}, {13, 21}, {2, 29}});
  REQUIRE(pixels.size() == 4);
  CHECK(pixels[0] == solid(kBackground));                    // top band: background
  CHECK(pixels[1] == solid(Color::rgba(0xE7, 0x4C, 0x3C)));  // TL, letterboxed
  CHECK(pixels[2] == solid(Color::rgba(0xF1, 0xC4, 0x0F)));  // BR yellow
  CHECK(pixels[3] == solid(kBackground));                    // bottom band: background
}

TEST_CASE("fit: cover fills a wider-than-tall node, cropping the source's own edges") {
  // A 32-wide, 16-tall node: cover scales by max(32/8, 16/8) = 4, to 32x32,
  // centred - so 8 columns overshoot on each side and are cropped away, and
  // only a vertical strip of each quadrant survives across the node's width.
  const std::vector<std::uint32_t> pixels = sample_fit_scene(
      PixelSize{32, 16}, ImageFit::kCover, {{4, 4}, {27, 4}, {4, 11}, {27, 11}});
  REQUIRE(pixels.size() == 4);
  CHECK(pixels[0] == solid(Color::rgba(0xE7, 0x4C, 0x3C)));  // TL red survives
  CHECK(pixels[1] == solid(Color::rgba(0x2E, 0xCC, 0x71)));  // TR green survives
  CHECK(pixels[2] == solid(Color::rgba(0x2E, 0x86, 0xDE)));  // BL blue survives
  CHECK(pixels[3] == solid(Color::rgba(0xF1, 0xC4, 0x0F)));  // BR yellow survives
}

// ---------------------------------------------------------------------------
// The placeholder: never a hole, and never drawn once the real image is
// there.
// ---------------------------------------------------------------------------

// Plain sampled colours out of a small helper, for the identical cognitive-
// complexity reason every other multi-step test above hoists its setup: a
// TEST_CASE that only lists CHECKs stays well under clang-tidy's budget, a
// TEST_CASE that also builds the tree and repaints it does not.
std::optional<std::uint32_t> sample_placeholder_only() {
  dg::TreeSpec spec;
  spec.viewport = PixelSize{20, 20};
  LayoutTree tree{spec};

  BoxStyle box = image_leaf();
  box.width = 20;
  box.height = 20;
  NodeStyle style;
  style.image.placeholder = Color::rgba(0x99, 0x99, 0x99);
  tree.add_child(LayoutTree::root(), box, style);
  tree.layout_full();

  std::optional<RasterSurface> surface = RasterSurface::create(20, 20);
  if (!surface.has_value()) {
    return std::nullopt;
  }
  tree.render().repaint_full(*surface);
  return pixel_at(*surface, 10, 10);
}

TEST_CASE("placeholder paints while source is invalid, never a hole") {
  const std::optional<std::uint32_t> pixel = sample_placeholder_only();
  if (!pixel.has_value()) {
    FAIL("could not allocate a raster surface");
    return;
  }
  CHECK(*pixel == solid(Color::rgba(0x99, 0x99, 0x99)));
}

// Rebuilding the tree with the catalog attached, matching how a real caller
// would construct one TreeSpec up front; what is under test is
// paint_image()'s branch choice, not catalog lifetime.
std::optional<std::uint32_t> sample_real_image_over_placeholder() {
  ImageCatalog catalog;
  const std::vector<std::uint8_t> png = quadrant_png(4);
  const dg::Expected<ImageId, ImageError> decoded = catalog.decode(png.data(), png.size());
  if (!decoded.has_value()) {
    return std::nullopt;
  }

  dg::TreeSpec spec;
  spec.viewport = PixelSize{20, 20};
  spec.images = std::move(catalog);
  LayoutTree tree{spec};

  BoxStyle box = image_leaf();
  box.width = 20;
  box.height = 20;
  NodeStyle style;
  style.image.source = decoded.value();
  style.image.placeholder = Color::rgba(0x99, 0x99, 0x99);
  tree.add_child(LayoutTree::root(), box, style);
  tree.layout_full();

  std::optional<RasterSurface> surface = RasterSurface::create(20, 20);
  if (!surface.has_value()) {
    return std::nullopt;
  }
  tree.render().repaint_full(*surface);
  return pixel_at(*surface, 5, 5);
}

TEST_CASE("a real image replaces the placeholder entirely, once decoded") {
  const std::optional<std::uint32_t> pixel = sample_real_image_over_placeholder();
  if (!pixel.has_value()) {
    FAIL("could not decode or allocate for this scene");
    return;
  }
  CHECK(*pixel == solid(Color::rgba(0xE7, 0x4C, 0x3C)));
  CHECK(*pixel != solid(Color::rgba(0x99, 0x99, 0x99)));
}

}  // namespace
