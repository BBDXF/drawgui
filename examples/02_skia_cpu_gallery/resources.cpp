#include "gallery.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "include/codec/SkCodec.h"
#include "include/codec/SkPngDecoder.h"
#include "include/core/SkColor.h"
#include "include/core/SkData.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkFontStyle.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkPoint.h"
#include "include/core/SkString.h"
#include "include/core/SkSurface.h"
#include "include/core/SkTileMode.h"
#include "include/effects/SkGradient.h"
#include "include/encode/SkPngEncoder.h"
#include "include/ports/SkFontMgr_directory.h"

namespace gallery {
namespace {

constexpr int kPhotoSize = 96;

// Tried in order. The custom-directory font manager exposes families by the
// name inside the font file, so these are the names Skia reports, not the
// file names.
constexpr const char* kCjkFamilies[] = {
    "WenQuanYi Zen Hei",
    "WenQuanYi Micro Hei",
    "Droid Sans Fallback",
};

sk_sp<SkTypeface> match(const sk_sp<SkFontMgr>& manager, const char* family,
                        const SkFontStyle& style) {
  return manager ? manager->matchFamilyStyle(family, style) : nullptr;
}

sk_sp<SkTypeface> match_cjk(const sk_sp<SkFontMgr>& manager, std::vector<std::string>& notes) {
  for (const char* family : kCjkFamilies) {
    sk_sp<SkTypeface> typeface = match(manager, family, SkFontStyle::Normal());
    if (typeface) {
      notes.emplace_back(std::string{"CJK typeface: "} + family);
      return typeface;
    }
  }
  notes.emplace_back("CJK typeface: NONE FOUND - Chinese panels will be empty");
  return nullptr;
}

// The image the gallery draws is generated, encoded to PNG and decoded back,
// rather than shipped as an asset. That keeps the example self-contained and
// deterministic, and it exercises the decode path for real - which matters,
// because a static Skia build registers no codecs at all until asked.
sk_sp<SkImage> make_photo(std::vector<std::string>& notes) {
  sk_sp<SkSurface> surface =
      SkSurfaces::Raster(SkImageInfo::MakeN32Premul(kPhotoSize, kPhotoSize));
  if (!surface) {
    notes.emplace_back("image: could not allocate the source surface");
    return nullptr;
  }

  SkCanvas* canvas = surface->getCanvas();
  canvas->clear(SK_ColorWHITE);

  const SkPoint diagonal[2] = {
      {0.0F, 0.0F}, {static_cast<float>(kPhotoSize), static_cast<float>(kPhotoSize)}};
  const SkColor4f sweep[3] = {
      SkColor4f::FromColor(0xFF1B3A6B),
      SkColor4f::FromColor(0xFF2E86DE),
      SkColor4f::FromColor(0xFFF6C445),
  };
  const SkGradient gradient{
      SkGradient::Colors{SkSpan<const SkColor4f>{sweep, 3}, SkTileMode::kClamp},
      SkGradient::Interpolation{}};

  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setShader(SkShaders::LinearGradient(diagonal, gradient));
  canvas->drawRect(SkRect::MakeWH(kPhotoSize, kPhotoSize), paint);

  // Hard edges and thin lines on purpose: they are what makes a sampling
  // quality difference visible when the image is scaled up.
  paint.setShader(nullptr);
  paint.setColor(0xFFFFFFFF);
  for (int i = 0; i < 6; ++i) {
    const float offset = static_cast<float>(i) * 16.0F;
    canvas->drawRect(SkRect::MakeXYWH(offset, offset, 4.0F, kPhotoSize - (2.0F * offset)),
                     paint);
  }
  paint.setColor(0xFFE74C3C);
  canvas->drawCircle(kPhotoSize * 0.5F, kPhotoSize * 0.5F, kPhotoSize * 0.28F, paint);

  SkPixmap pixmap;
  if (!surface->peekPixels(&pixmap)) {
    notes.emplace_back("image: peekPixels failed on a raster surface");
    return nullptr;
  }
  sk_sp<SkData> png = SkPngEncoder::Encode(pixmap, SkPngEncoder::Options{});
  if (!png) {
    notes.emplace_back("image: PNG encode failed");
    return nullptr;
  }

  // Skia's static build does not auto-register codecs. Without this the
  // decode below returns kUnimplemented on a PNG the same process just wrote.
  SkCodecs::Register(SkPngDecoder::Decoder());

  std::unique_ptr<SkCodec> codec = SkCodec::MakeFromData(png);
  if (!codec) {
    notes.emplace_back("image: no codec for the PNG that was just encoded");
    return nullptr;
  }
  auto [decoded, result] = codec->getImage();
  if (!decoded) {
    notes.emplace_back("image: decode failed with result " +
                       std::to_string(static_cast<int>(result)));
    return nullptr;
  }

  notes.emplace_back(
      "image: " + std::to_string(png->size()) + " byte PNG encoded and decoded back to " +
      std::to_string(decoded->width()) + "x" + std::to_string(decoded->height()));
  return decoded;
}

}  // namespace

bool Resources::complete() const {
  return sans && sans_bold && serif && mono && cjk && photo;
}

Resources load_resources(const std::string& font_dir) {
  Resources resources;

  // A directory scan rather than fontconfig. Both font managers are present
  // in the prebuilt archive, but the fontconfig one would add -lfontconfig to
  // a link line that is currently one static archive plus -lfreetype, and the
  // directory scanner found every family this gallery needs. The cost is
  // real and recorded: it has no fallback chain, so CJK has to be selected by
  // name below rather than discovered from the character.
  sk_sp<SkFontMgr> manager = SkFontMgr_New_Custom_Directory(font_dir.c_str());
  if (!manager) {
    resources.notes.emplace_back("font manager: could not scan " + font_dir);
    return resources;
  }
  resources.notes.emplace_back("font manager: scanned " + font_dir + ", " +
                               std::to_string(manager->countFamilies()) + " families");

  resources.sans = match(manager, "DejaVu Sans", SkFontStyle::Normal());
  resources.sans_bold = match(manager, "DejaVu Sans", SkFontStyle::Bold());
  resources.serif = match(manager, "DejaVu Serif", SkFontStyle::Normal());
  resources.mono = match(manager, "DejaVu Sans Mono", SkFontStyle::Normal());
  resources.cjk = match_cjk(manager, resources.notes);

  if (!resources.sans) {
    resources.notes.emplace_back("font: 'DejaVu Sans' not found - text panels will be empty");
  }
  if (manager->matchFamilyStyleCharacter(nullptr, SkFontStyle::Normal(), nullptr, 0, 0x4F60) ==
      nullptr) {
    resources.notes.emplace_back(
        "font fallback: matchFamilyStyleCharacter() returns null - this font manager has no "
        "fallback chain, so every script must be selected by family name");
  }

  resources.photo = make_photo(resources.notes);
  return resources;
}

}  // namespace gallery
