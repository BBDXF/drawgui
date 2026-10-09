// JPEG / WebP decode and SVG icons through ImageCatalog.
//
// Sources are synthesized at test time (the Skia encoders are in the same
// prebuilt archive the decoders are), so no binary fixture is committed: a
// 16x16 four-quadrant bitmap is encoded to JPEG (quality 100) and lossless
// WebP, decoded back through the public, format-agnostic decode(), and
// spot-checked at quadrant centres. JPEG is lossy, so its checks allow a
// small per-channel tolerance; lossless WebP must match exactly.
//
// decode() stays raster-only; decode_any() sniffs SVG. SVG is checked at two node sizes (the vector-at-paint-time decision), with
// a tint, from a committed text fixture via decode_file(), and with bad data.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/base/expected.h"
#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/graphics/types.h"
#include "drawgui/layout/box.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/image_catalog.h"
#include "drawgui/render/render_tree.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkColor.h"
#include "include/core/SkData.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkRect.h"
#include "include/encode/SkJpegEncoder.h"
#include "include/encode/SkWebpEncoder.h"

namespace {

using dg::Color;
using dg::ImageCatalog;
using dg::ImageError;
using dg::ImageFit;
using dg::ImageId;
using dg::LayoutTree;
using dg::NodeStyle;
using dg::PixelSize;
using dg::RasterSurface;

constexpr int kSource = 16;
constexpr SkColor kRed = SkColorSetRGB(0xE7, 0x4C, 0x3C);
constexpr SkColor kGreen = SkColorSetRGB(0x2E, 0xCC, 0x71);
constexpr SkColor kBlue = SkColorSetRGB(0x2E, 0x86, 0xDE);
constexpr SkColor kYellow = SkColorSetRGB(0xF1, 0xC4, 0x0F);

SkBitmap quadrant_bitmap() {
  SkBitmap bitmap;
  bitmap.allocPixels(SkImageInfo::MakeN32Premul(kSource, kSource));
  constexpr int kHalf = kSource / 2;
  bitmap.erase(kRed, SkIRect::MakeXYWH(0, 0, kHalf, kHalf));
  bitmap.erase(kGreen, SkIRect::MakeXYWH(kHalf, 0, kHalf, kHalf));
  bitmap.erase(kBlue, SkIRect::MakeXYWH(0, kHalf, kHalf, kHalf));
  bitmap.erase(kYellow, SkIRect::MakeXYWH(kHalf, kHalf, kHalf, kHalf));
  return bitmap;
}

std::vector<std::uint8_t> to_bytes(const sk_sp<SkData>& data) {
  if (!data) {
    return {};
  }
  const auto* begin = static_cast<const std::uint8_t*>(data->data());
  return {begin, begin + data->size()};
}

std::vector<std::uint8_t> quadrant_jpeg() {
  SkJpegEncoder::Options options;
  options.fQuality = 100;
  return to_bytes(SkJpegEncoder::Encode(quadrant_bitmap().pixmap(), options));
}

std::vector<std::uint8_t> quadrant_webp() {
  SkWebpEncoder::Options options;
  options.fCompression = SkWebpEncoder::Compression::kLossless;
  options.fQuality = 100;
  return to_bytes(SkWebpEncoder::Encode(quadrant_bitmap().pixmap(), options));
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

bool near(std::uint32_t actual, SkColor expected, int tolerance) {
  const auto channel_ok = [&](unsigned shift) {
    const int a = static_cast<int>((actual >> shift) & 0xFFU);
    const int e = static_cast<int>((expected >> shift) & 0xFFU);
    return std::abs(a - e) <= tolerance;
  };
  return channel_ok(24U) && channel_ok(16U) && channel_ok(8U) && channel_ok(0U);
}

constexpr Color kBackground = Color::rgba(0x11, 0x22, 0x33);

// One node exactly filling `viewport`, painting `id` from `catalog` under
// `fit`; returns the painted surface (or nothing on allocation failure).
std::optional<RasterSurface> paint_scene(ImageCatalog catalog, ImageId id, PixelSize viewport,
                                         ImageFit fit) {
  dg::TreeSpec spec;
  spec.viewport = viewport;
  spec.background.fill = kBackground;
  spec.images = std::move(catalog);
  LayoutTree tree{spec};

  dg::BoxStyle box;
  box.kind = dg::LayoutKind::kLeaf;
  box.width = viewport.width;
  box.height = viewport.height;
  NodeStyle style;
  style.image.source = id;
  style.image.fit = fit;
  tree.add_child(LayoutTree::root(), box, style);
  tree.layout_full();

  std::optional<RasterSurface> surface = RasterSurface::create(viewport.width, viewport.height);
  if (surface.has_value()) {
    tree.render().repaint_full(*surface);
  }
  return surface;
}

std::vector<std::uint32_t> decode_and_sample(const std::vector<std::uint8_t>& bytes) {
  ImageCatalog catalog;
  const dg::Expected<ImageId, ImageError> id = catalog.decode(bytes.data(), bytes.size());
  if (!id.has_value() || catalog.pixel_size(id.value()) != PixelSize{kSource, kSource} ||
      catalog.is_vector(id.value())) {
    return {};
  }
  const std::optional<RasterSurface> surface =
      paint_scene(catalog, id.value(), PixelSize{kSource, kSource}, ImageFit::kFill);
  if (!surface.has_value()) {
    return {};
  }
  return {pixel_at(*surface, 4, 4), pixel_at(*surface, 12, 4), pixel_at(*surface, 4, 12),
          pixel_at(*surface, 12, 12)};
}

// A 10x10 SVG: left half opaque red, right half nothing.
const std::string kHalfRedSvg =
    R"(<svg xmlns="http://www.w3.org/2000/svg" width="10" height="10" viewBox="0 0 10 10">)"
    R"(<rect x="0" y="0" width="5" height="10" fill="#ff0000"/></svg>)";

std::vector<std::uint8_t> bytes_of(const std::string& text) {
  return {text.begin(), text.end()};
}

// Paints the half-red SVG under kFill at `side` x `side` and samples the
// left-half centre, the right-half centre and the pixel just left of the
// half-way edge.
std::vector<std::uint32_t> svg_samples(int side, std::optional<Color> tint) {
  ImageCatalog catalog;
  const std::vector<std::uint8_t> bytes = bytes_of(kHalfRedSvg);
  const dg::Expected<ImageId, ImageError> id =
      catalog.decode_svg(bytes.data(), bytes.size(), dg::SvgOptions{tint});
  if (!id.has_value()) {
    return {};
  }
  const std::optional<RasterSurface> surface =
      paint_scene(catalog, id.value(), PixelSize{side, side}, ImageFit::kFill);
  if (!surface.has_value()) {
    return {};
  }
  const int half = side / 2;
  return {pixel_at(*surface, half / 2, half), pixel_at(*surface, half + (half / 2), half),
          pixel_at(*surface, half - 1, half)};
}

}  // namespace

TEST_CASE("JPEG decodes through the format-agnostic decode() with correct size and colours") {
  const std::vector<std::uint8_t> jpeg = quadrant_jpeg();
  REQUIRE_FALSE(jpeg.empty());
  const std::vector<std::uint32_t> samples = decode_and_sample(jpeg);
  REQUIRE(samples.size() == 4);
  CHECK(near(samples[0], kRed, 12));
  CHECK(near(samples[1], kGreen, 12));
  CHECK(near(samples[2], kBlue, 12));
  CHECK(near(samples[3], kYellow, 12));
}

TEST_CASE("lossless WebP decodes with exact colours") {
  const std::vector<std::uint8_t> webp = quadrant_webp();
  REQUIRE_FALSE(webp.empty());
  const std::vector<std::uint32_t> samples = decode_and_sample(webp);
  REQUIRE(samples.size() == 4);
  CHECK(samples[0] == kRed);
  CHECK(samples[1] == kGreen);
  CHECK(samples[2] == kBlue);
  CHECK(samples[3] == kYellow);
}

TEST_CASE("truncated JPEG and WebP are rejected rather than half-decoded") {
  for (std::vector<std::uint8_t> bytes : {quadrant_jpeg(), quadrant_webp()}) {
    REQUIRE(bytes.size() > 16);
    bytes.resize(12);
    ImageCatalog catalog;
    CHECK_FALSE(catalog.decode(bytes.data(), bytes.size()).has_value());
    CHECK(catalog.size() == 0);
  }
}

TEST_CASE("decode() never parses SVG; the format-agnostic decode_any() does") {
  ImageCatalog catalog;
  const std::vector<std::uint8_t> bytes = bytes_of("\n  " + kHalfRedSvg);
  CHECK_FALSE(catalog.decode(bytes.data(), bytes.size()).has_value());
  const dg::Expected<ImageId, ImageError> id = catalog.decode_any(bytes.data(), bytes.size());
  REQUIRE(id.has_value());
  CHECK(catalog.is_vector(id.value()));
  CHECK(catalog.pixel_size(id.value()) == PixelSize{10, 10});
}

TEST_CASE("SVG viewBox alone supplies the intrinsic size") {
  ImageCatalog catalog;
  const std::vector<std::uint8_t> bytes = bytes_of(
      R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 12"><rect width="24" height="12"/></svg>)");
  const dg::Expected<ImageId, ImageError> id = catalog.decode_svg(bytes.data(), bytes.size());
  REQUIRE(id.has_value());
  CHECK(catalog.pixel_size(id.value()) == PixelSize{24, 12});
}

TEST_CASE("SVG renders as vectors at two node sizes, crisp at the half-way edge") {
  for (const int side : {20, 80}) {
    CAPTURE(side);
    const std::vector<std::uint32_t> samples = svg_samples(side, std::nullopt);
    REQUIRE(samples.size() == 3);
    CHECK(samples[0] == 0xFFFF0000U);
    CHECK(samples[1] == kBackground.argb());
    // The pixel just inside the edge is fully red at both scales - a vector
    // draw, not an upscaled 10x10 raster (which would blur it at 80).
    CHECK(samples[2] == 0xFFFF0000U);
  }
}

TEST_CASE("SVG tint recolours the icon and keeps transparent areas transparent") {
  const std::vector<std::uint32_t> samples = svg_samples(40, Color::rgba(0x00, 0xFF, 0x00));
  REQUIRE(samples.size() == 3);
  CHECK(samples[0] == 0xFF00FF00U);
  CHECK(samples[1] == kBackground.argb());
}

TEST_CASE("decode_any() routes raster bytes to the codecs") {
  ImageCatalog catalog;
  const std::vector<std::uint8_t> webp = quadrant_webp();
  const dg::Expected<ImageId, ImageError> id = catalog.decode_any(webp.data(), webp.size());
  REQUIRE(id.has_value());
  CHECK_FALSE(catalog.is_vector(id.value()));
}

TEST_CASE("decode_file loads a committed SVG fixture") {
  ImageCatalog catalog;
  const dg::Expected<ImageId, ImageError> id =
      catalog.decode_file(std::string{DG_TEST_FIXTURE_DIR} + "/icon_square.svg");
  REQUIRE(id.has_value());
  CHECK(catalog.is_vector(id.value()));
  CHECK(catalog.pixel_size(id.value()) == PixelSize{10, 10});
  CHECK_FALSE(catalog.decode_file(std::string{DG_TEST_FIXTURE_DIR} + "/missing.svg").has_value());
}

TEST_CASE("bad SVG data is rejected") {
  ImageCatalog catalog;
  for (const std::string& text :
       {std::string{"<not-svg"}, std::string{"<html><body/></html>"},
        std::string{R"(<svg xmlns="http://www.w3.org/2000/svg"></svg>)"}}) {
    CAPTURE(text);
    const std::vector<std::uint8_t> bytes = bytes_of(text);
    CHECK_FALSE(catalog.decode_any(bytes.data(), bytes.size()).has_value());
  }
  CHECK(catalog.size() == 0);
}
