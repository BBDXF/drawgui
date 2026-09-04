#include "golden_image.h"

#include <algorithm>
#include <fstream>
#include <mutex>
#include <sstream>

#include "include/codec/SkCodec.h"
#include "include/codec/SkPngDecoder.h"
#include "include/core/SkAlphaType.h"
#include "include/core/SkColorType.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkStream.h"
#include "include/encode/SkPngEncoder.h"

namespace dg::testing {
namespace {

constexpr int kChannels = 4;

// Skia does not auto-register codecs in a static build; the decoder has to be
// installed before the first decode. Doing it once, lazily, keeps callers
// from having to remember.
void ensure_png_decoder_registered() {
  static std::once_flag once;
  std::call_once(once, [] { SkCodecs::Register(SkPngDecoder::Decoder()); });
}

SkImageInfo comparison_info(int width, int height) {
  // RGBA8888 premultiplied is fixed here so that a comparison never depends
  // on the platform's native channel order.
  return SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
}

}  // namespace

std::optional<Image> decode_png(const std::vector<std::uint8_t>& png) {
  if (png.empty()) {
    return std::nullopt;
  }
  ensure_png_decoder_registered();

  sk_sp<SkData> data = SkData::MakeWithCopy(png.data(), png.size());
  sk_sp<SkImage> image = SkImages::DeferredFromEncodedData(std::move(data));
  if (!image) {
    return std::nullopt;
  }

  Image out;
  out.width = image->width();
  out.height = image->height();
  if (out.empty()) {
    return std::nullopt;
  }

  const auto pixel_count = static_cast<std::size_t>(out.width) *
                           static_cast<std::size_t>(out.height) *
                           static_cast<std::size_t>(kChannels);
  out.pixels.resize(pixel_count);

  const SkImageInfo info = comparison_info(out.width, out.height);
  const auto row_bytes =
      static_cast<std::size_t>(out.width) * static_cast<std::size_t>(kChannels);
  if (!image->readPixels(nullptr, info, out.pixels.data(), row_bytes, 0, 0)) {
    return std::nullopt;
  }
  return out;
}

std::vector<std::uint8_t> encode_png(const Image& image) {
  if (image.empty()) {
    return {};
  }
  const auto row_bytes =
      static_cast<std::size_t>(image.width) * static_cast<std::size_t>(kChannels);
  const SkPixmap pixmap{comparison_info(image.width, image.height), image.pixels.data(),
                        row_bytes};

  SkDynamicMemoryWStream stream;
  if (!SkPngEncoder::Encode(&stream, pixmap, SkPngEncoder::Options{})) {
    return {};
  }
  sk_sp<SkData> data = stream.detachAsData();
  if (!data || data->isEmpty()) {
    return {};
  }
  const auto* bytes = data->bytes();
  return std::vector<std::uint8_t>{bytes, bytes + data->size()};
}

Comparison compare(const Image& baseline, const Image& actual, int tolerance) {
  Comparison result;

  if (baseline.empty() || actual.empty()) {
    result.failure_reason = "one of the images is empty";
    return result;
  }

  if (baseline.width != actual.width || baseline.height != actual.height) {
    std::ostringstream message;
    message << "size mismatch: baseline is " << baseline.width << "x" << baseline.height
            << ", actual is " << actual.width << "x" << actual.height;
    result.failure_reason = message.str();
    return result;
  }

  // The diff image starts opaque black and gains a red pixel wherever the two
  // inputs disagree, scaled by how badly they disagree. Reading it tells you
  // where the regression is, which a scalar pixel count cannot.
  Image diff;
  diff.width = baseline.width;
  diff.height = baseline.height;
  diff.pixels.assign(baseline.pixels.size(), 0);

  const std::size_t count = baseline.pixels.size();
  for (std::size_t i = 0; i < count; i += kChannels) {
    int worst = 0;
    for (std::size_t c = 0; c < kChannels; ++c) {
      const int lhs = baseline.pixels[i + c];
      const int rhs = actual.pixels[i + c];
      worst = std::max(worst, std::abs(lhs - rhs));
    }

    result.max_channel_delta = std::max(result.max_channel_delta, worst);

    if (worst > tolerance) {
      ++result.differing_pixels;
      // Amplify so that a single-level difference is still visible.
      const int intensity = std::min(255, 64 + (worst * 4));
      diff.pixels[i + 0] = static_cast<std::uint8_t>(intensity);
      diff.pixels[i + 1] = 0;
      diff.pixels[i + 2] = 0;
      diff.pixels[i + 3] = 255;
    } else {
      // Unchanged pixels are kept as a dimmed greyscale backdrop so the red
      // marks can be located in the image.
      const int grey =
          (baseline.pixels[i + 0] + baseline.pixels[i + 1] + baseline.pixels[i + 2]) / 3 / 4;
      const auto dim = static_cast<std::uint8_t>(grey);
      diff.pixels[i + 0] = dim;
      diff.pixels[i + 1] = dim;
      diff.pixels[i + 2] = dim;
      diff.pixels[i + 3] = 255;
    }
  }

  if (result.differing_pixels == 0) {
    result.matched = true;
    return result;
  }

  std::ostringstream message;
  message << result.differing_pixels << " pixel(s) differ beyond tolerance " << tolerance
          << "; worst channel delta " << result.max_channel_delta;
  result.failure_reason = message.str();
  result.diff = std::move(diff);
  return result;
}

std::optional<std::vector<std::uint8_t>> read_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return std::nullopt;
  }
  return std::vector<std::uint8_t>{std::istreambuf_iterator<char>(in),
                                   std::istreambuf_iterator<char>()};
}

bool write_file(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);

  std::ofstream out(path, std::ios::binary);
  if (!out) {
    return false;
  }
  out.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(out);
}

}  // namespace dg::testing
