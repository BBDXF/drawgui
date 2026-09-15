#include "drawgui/render/image_catalog.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include "include/codec/SkCodec.h"
#include "include/codec/SkPngDecoder.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkRefCnt.h"

#include "render/image_catalog_impl.h"

namespace dg {
namespace {

// Skia's static build registers no codecs at all until asked - measured
// already by examples/02_skia_cpu_gallery/resources.cpp, whose make_photo()
// this decode path mirrors. Registering more than once is harmless (SkCodecs
// keeps a list, and this project only ever needs PNG - design.md section
// 5.10.2's other five decode formats are accepted "for free" only in the
// sense that nothing here refuses their bytes; nothing in this slice's own
// tests or examples exercises them), but std::call_once is what keeps this
// engine from repeating a global mutation on every ImageCatalog::decode()
// call for no reason.
void ensure_png_codec_registered() {
  static std::once_flag registered;
  std::call_once(registered, [] { SkCodecs::Register(SkPngDecoder::Decoder()); });
}

}  // namespace

ImageCatalog::ImageCatalog() : impl_(std::make_shared<Impl>()) {}

Expected<ImageId, ImageError> ImageCatalog::decode(const std::uint8_t* encoded,
                                                   std::size_t size) {
  if (encoded == nullptr || size == 0) {
    return Unexpected{ImageError{"image: decode() was given no bytes"}};
  }

  ensure_png_codec_registered();

  sk_sp<SkData> data = SkData::MakeWithCopy(encoded, size);
  std::unique_ptr<SkCodec> codec = SkCodec::MakeFromData(data);
  if (!codec) {
    return Unexpected{
        ImageError{"image: no registered codec accepted these " + std::to_string(size) +
                   " bytes (design.md section 5.10.2 names the default codec set)"}};
  }

  auto [decoded, result] = codec->getImage();
  if (!decoded) {
    return Unexpected{ImageError{"image: decode failed with SkCodec::Result " +
                                 std::to_string(static_cast<int>(result))}};
  }
  if (decoded->width() <= 0 || decoded->height() <= 0) {
    return Unexpected{ImageError{"image: decoded to a non-positive size"}};
  }

  impl_->images.push_back(std::move(decoded));
  return ImageId{static_cast<std::uint16_t>(impl_->images.size())};
}

std::size_t ImageCatalog::size() const {
  return impl_->images.size();
}

bool ImageCatalog::holds(ImageId id) const {
  return id.is_valid() && id.value <= impl_->images.size();
}

std::optional<PixelSize> ImageCatalog::pixel_size(ImageId id) const {
  if (!holds(id)) {
    return std::nullopt;
  }
  const sk_sp<SkImage>& image = impl_->images[id.value - 1];
  return PixelSize{image->width(), image->height()};
}

}  // namespace dg
