#include "drawgui/render/image_catalog.h"

#include <cmath>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "include/codec/SkCodec.h"
#include "include/codec/SkJpegDecoder.h"
#include "include/codec/SkPngDecoder.h"
#include "include/codec/SkWebpDecoder.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkSize.h"
#include "include/core/SkStream.h"

#include "modules/svg/include/SkSVGDOM.h"
#include "modules/svg/include/SkSVGRenderContext.h"
#include "modules/svg/include/SkSVGSVG.h"

#include "render/image_catalog_impl.h"

namespace dg {
namespace {

// Skia's static build registers no codecs at all until asked - measured
// already by examples/02_skia_cpu_gallery/resources.cpp. The prebuilt
// archive carries PNG, JPEG (libjpeg-turbo) and WebP (libwebp) decoders;
// those three are registered and tested. std::call_once keeps this a
// one-time global mutation rather than one per decode() call.
void ensure_codecs_registered() {
  static std::once_flag registered;
  std::call_once(registered, [] {
    SkCodecs::Register(SkPngDecoder::Decoder());
    SkCodecs::Register(SkJpegDecoder::Decoder());
    SkCodecs::Register(SkWebpDecoder::Decoder());
  });
}

// SVG has no magic number; an XML document starts with '<' (an <?xml
// prolog, a comment, a doctype or <svg itself) after an optional UTF-8 BOM
// and whitespace. None of PNG/JPEG/WebP can start that way.
bool looks_like_svg(const std::uint8_t* bytes, std::size_t size) {
  std::size_t i = 0;
  if (size >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) {
    i = 3;
  }
  while (i < size && std::isspace(bytes[i]) != 0) {
    ++i;
  }
  return i < size && bytes[i] == '<';
}

// width/height when the document gives absolute ones, else the viewBox.
SkSize svg_intrinsic_size(const SkSVGDOM& dom) {
  const SkSVGSVG* root = dom.getRoot();
  if (root == nullptr) {
    return SkSize::MakeEmpty();
  }
  const SkSize sized = root->intrinsicSize(SkSVGLengthContext(SkSize::Make(0, 0)));
  if (sized.width() > 0 && sized.height() > 0) {
    return sized;
  }
  const std::optional<SkSVGViewBoxType>& view_box = root->getViewBox();
  if (view_box.has_value() && view_box->width() > 0 && view_box->height() > 0) {
    return SkSize::Make(view_box->width(), view_box->height());
  }
  return SkSize::MakeEmpty();
}

}  // namespace

ImageCatalog::ImageCatalog() : impl_(std::make_shared<Impl>()) {}

Expected<ImageId, ImageError> ImageCatalog::decode(const std::uint8_t* encoded,
                                                   std::size_t size) {
  if (encoded == nullptr || size == 0) {
    return Unexpected{ImageError{"image: decode() was given no bytes"}};
  }
  ensure_codecs_registered();

  sk_sp<SkData> data = SkData::MakeWithCopy(encoded, size);
  std::unique_ptr<SkCodec> codec = SkCodec::MakeFromData(data);
  if (!codec) {
    return Unexpected{ImageError{"image: no registered codec (PNG/JPEG/WebP) accepted these " +
                                 std::to_string(size) + " bytes"}};
  }

  auto [decoded, result] = codec->getImage();
  if (!decoded) {
    return Unexpected{ImageError{"image: decode failed with SkCodec::Result " +
                                 std::to_string(static_cast<int>(result))}};
  }
  if (decoded->width() <= 0 || decoded->height() <= 0) {
    return Unexpected{ImageError{"image: decoded to a non-positive size"}};
  }

  ImageEntry entry;
  entry.bitmap = std::move(decoded);
  impl_->images.push_back(std::move(entry));
  return ImageId{static_cast<std::uint16_t>(impl_->images.size())};
}

Expected<ImageId, ImageError> ImageCatalog::decode_any(const std::uint8_t* encoded,
                                                       std::size_t size,
                                                       const SvgOptions& options) {
  if (encoded != nullptr && looks_like_svg(encoded, size)) {
    return decode_svg(encoded, size, options);
  }
  return decode(encoded, size);
}

Expected<ImageId, ImageError> ImageCatalog::decode_svg(const std::uint8_t* encoded,
                                                       std::size_t size,
                                                       const SvgOptions& options) {
  if (encoded == nullptr || size == 0) {
    return Unexpected{ImageError{"image: decode_svg() was given no bytes"}};
  }
  SkMemoryStream stream(encoded, size, /*copyData=*/false);
  sk_sp<SkSVGDOM> dom = SkSVGDOM::Builder().make(stream);
  if (!dom || dom->getRoot() == nullptr) {
    return Unexpected{ImageError{"image: bytes are not a parseable SVG document"}};
  }
  const SkSize intrinsic = svg_intrinsic_size(*dom);
  if (intrinsic.isEmpty()) {
    return Unexpected{
        ImageError{"image: SVG has no positive intrinsic size (width/height or viewBox)"}};
  }
  dom->setContainerSize(intrinsic);

  ImageEntry entry;
  entry.svg = std::move(dom);
  entry.intrinsic = intrinsic;
  entry.tint = options.tint;
  impl_->images.push_back(std::move(entry));
  return ImageId{static_cast<std::uint16_t>(impl_->images.size())};
}

Expected<ImageId, ImageError> ImageCatalog::decode_file(const std::string& path,
                                                        const SvgOptions& options) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return Unexpected{ImageError{"image: cannot open " + path}};
  }
  const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file),
                                        std::istreambuf_iterator<char>()};
  if (bytes.empty()) {
    return Unexpected{ImageError{"image: " + path + " is empty"}};
  }
  return decode_any(bytes.data(), bytes.size(), options);
}

bool ImageCatalog::is_vector(ImageId id) const {
  return holds(id) && impl_->images[id.value - 1].svg != nullptr;
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
  const ImageEntry& entry = impl_->images[id.value - 1];
  if (entry.svg) {
    return PixelSize{static_cast<int>(std::ceil(entry.intrinsic.width())),
                     static_cast<int>(std::ceil(entry.intrinsic.height()))};
  }
  return PixelSize{entry.bitmap->width(), entry.bitmap->height()};
}

}  // namespace dg
