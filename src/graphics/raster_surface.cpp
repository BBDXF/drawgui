#include "drawgui/graphics/raster_surface.h"

#include <utility>

#include "include/core/SkCanvas.h"
#include "include/core/SkColorType.h"
#include "include/core/SkData.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkSurface.h"
#include "include/encode/SkPngEncoder.h"

namespace dg {

struct RasterSurface::Impl {
  sk_sp<SkSurface> surface;
};

RasterSurface::RasterSurface(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

RasterSurface::RasterSurface(RasterSurface&&) noexcept = default;
RasterSurface& RasterSurface::operator=(RasterSurface&&) noexcept = default;

// Out of line because Impl is incomplete in the header.
RasterSurface::~RasterSurface() = default;

std::optional<RasterSurface> RasterSurface::create(int width, int height) {
  if (width <= 0 || height <= 0) {
    return std::nullopt;
  }

  // N32Premul is Skia's native surface format: 32-bit, premultiplied, in
  // whichever channel order is fastest on this platform. It is the only
  // raster configuration this project uses, per design.md section 5.3.4.
  sk_sp<SkSurface> surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(width, height));
  if (!surface) {
    return std::nullopt;
  }

  auto impl = std::make_unique<Impl>();
  impl->surface = std::move(surface);
  return RasterSurface{std::move(impl)};
}

int RasterSurface::width() const {
  return impl_->surface->width();
}

int RasterSurface::height() const {
  return impl_->surface->height();
}

Canvas RasterSurface::canvas() {
  return Canvas{impl_->surface->getCanvas()};
}

SkCanvas* RasterSurface::sk_canvas() {
  return impl_->surface->getCanvas();
}

PixelView RasterSurface::peek_pixels() const {
  SkPixmap pixmap;
  if (!impl_->surface->peekPixels(&pixmap)) {
    return PixelView{};
  }

  // Asked of the surface rather than compared against kN32_SkColorType. That
  // constant is a macro expansion over SK_R32_SHIFT, which a consumer of the
  // prebuilt archive computes from its own (absent) build settings: it reads
  // as kRGBA_8888 here while every surface the library hands back is
  // kBGRA_8888. Trusting it would swap red and blue in every blitted frame.
  return PixelView{
      static_cast<const std::uint8_t*>(pixmap.addr()),
      pixmap.width(),
      pixmap.height(),
      pixmap.rowBytes(),
      pixmap.colorType() == kBGRA_8888_SkColorType,
  };
}

std::vector<std::uint8_t> RasterSurface::encode_png() const {
  // peekPixels gives direct access to the raster backing store, so encoding
  // costs no copy of the surface itself. It only succeeds for raster
  // surfaces, which is all this class ever creates.
  SkPixmap pixmap;
  if (!impl_->surface->peekPixels(&pixmap)) {
    return {};
  }

  // SkImage::encodeToData was removed in this Skia revision; the encoder
  // namespaces are the supported entry point.
  sk_sp<SkData> png = SkPngEncoder::Encode(pixmap, SkPngEncoder::Options{});
  if (!png || png->isEmpty()) {
    return {};
  }

  const auto* bytes = png->bytes();
  return std::vector<std::uint8_t>{bytes, bytes + png->size()};
}

}  // namespace dg
