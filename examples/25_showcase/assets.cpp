// The sample bitmaps of the 图片 page, painted with Skia at start-up and
// encoded as PNG, JPEG and WebP so the runtime decodes all three formats
// for real (no fixture files to ship).

#include <array>
#include <cstdint>
#include <vector>

#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkData.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkStream.h"
#include "include/effects/SkGradient.h"
#include "include/encode/SkJpegEncoder.h"
#include "include/encode/SkPngEncoder.h"
#include "include/encode/SkWebpEncoder.h"
#include "showcase.h"

namespace showcase {
namespace {

constexpr int kImageW = 240;
constexpr int kImageH = 150;

struct Scene {
  SkColor sky_top;
  SkColor sky_bottom;
  SkColor sun;
  SkColor far_hill;
  SkColor near_hill;
};

// A small landscape: gradient sky, sun, two hills. PNG keeps rounded,
// transparent corners to show off its alpha channel.
SkBitmap paint_scene(const Scene& scene, bool rounded) {
  SkBitmap bitmap;
  bitmap.allocPixels(SkImageInfo::MakeN32Premul(kImageW, kImageH));
  bitmap.eraseColor(SK_ColorTRANSPARENT);
  SkCanvas canvas(bitmap);
  const SkRect bounds = SkRect::MakeWH(kImageW, kImageH);
  if (rounded) {
    canvas.clipRRect(SkRRect::MakeRectXY(bounds, 18, 18), true);
  }
  const std::array<SkPoint, 2> points = {SkPoint::Make(0, 0), SkPoint::Make(0, kImageH)};
  const std::array<SkColor4f, 2> colors = {SkColor4f::FromColor(scene.sky_top),
                                           SkColor4f::FromColor(scene.sky_bottom)};
  SkPaint sky;
  sky.setShader(SkShaders::LinearGradient(
      points.data(), SkGradient(SkGradient::Colors(colors, SkTileMode::kClamp), {})));
  canvas.drawRect(bounds, sky);
  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setColor(scene.sun);
  canvas.drawCircle(170, 52, 22, paint);
  SkPathBuilder far;
  far.moveTo(0, 110);
  far.cubicTo(60, 70, 110, 80, 150, 104);
  far.cubicTo(190, 86, 220, 84, kImageW, 96);
  far.lineTo(kImageW, kImageH);
  far.lineTo(0, kImageH);
  far.close();
  paint.setColor(scene.far_hill);
  canvas.drawPath(far.detach(), paint);
  SkPathBuilder near;
  near.moveTo(0, 130);
  near.cubicTo(70, 104, 140, 118, kImageW, 122);
  near.lineTo(kImageW, kImageH);
  near.lineTo(0, kImageH);
  near.close();
  paint.setColor(scene.near_hill);
  canvas.drawPath(near.detach(), paint);
  return bitmap;
}

std::vector<std::uint8_t> bytes_of(SkDynamicMemoryWStream& stream) {
  const sk_sp<SkData> data = stream.detachAsData();
  const auto* begin = static_cast<const std::uint8_t*>(data->data());
  return {begin, begin + data->size()};
}

}  // namespace

std::array<std::vector<std::uint8_t>, 3> sample_images() {
  std::array<std::vector<std::uint8_t>, 3> out;
  {
    const SkBitmap dusk =
        paint_scene({SkColorSetRGB(0x3B, 0x2A, 0x6B), SkColorSetRGB(0xF2, 0x8C, 0x6B),
                     SkColorSetRGB(0xFF, 0xD8, 0x7A), SkColorSetRGB(0x6B, 0x4C, 0x8C),
                     SkColorSetRGB(0x2E, 0x22, 0x4D)},
                    true);
    SkDynamicMemoryWStream stream;
    if (SkPngEncoder::Encode(&stream, dusk.pixmap(), {})) {
      out[0] = bytes_of(stream);
    }
  }
  {
    const SkBitmap day =
        paint_scene({SkColorSetRGB(0x4F, 0x9D, 0xE8), SkColorSetRGB(0xC9, 0xE8, 0xFF),
                     SkColorSetRGB(0xFF, 0xF4, 0xB8), SkColorSetRGB(0x6F, 0xB5, 0x6A),
                     SkColorSetRGB(0x3C, 0x8A, 0x4A)},
                    false);
    SkJpegEncoder::Options options;
    options.fQuality = 88;
    SkDynamicMemoryWStream stream;
    if (SkJpegEncoder::Encode(&stream, day.pixmap(), options)) {
      out[1] = bytes_of(stream);
    }
  }
  {
    const SkBitmap night =
        paint_scene({SkColorSetRGB(0x0B, 0x1D, 0x3A), SkColorSetRGB(0x1F, 0x6F, 0x8B),
                     SkColorSetRGB(0xE8, 0xF1, 0xF5), SkColorSetRGB(0x1A, 0x4B, 0x5C),
                     SkColorSetRGB(0x0E, 0x2A, 0x36)},
                    false);
    SkWebpEncoder::Options options;
    options.fCompression = SkWebpEncoder::Compression::kLossy;
    options.fQuality = 85;
    SkDynamicMemoryWStream stream;
    if (SkWebpEncoder::Encode(&stream, night.pixmap(), options)) {
      out[2] = bytes_of(stream);
    }
  }
  return out;
}

}  // namespace showcase
