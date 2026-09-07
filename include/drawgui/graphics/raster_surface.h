// RasterSurface - a CPU-rasterized drawing target.
//
// design.md section 5.3.2 is emphatic that this is test infrastructure, not a
// degraded fallback: GPU rasterization differs by driver, so anti-aliased
// edges vary by several grey levels between Intel, NVIDIA and Mali. Pixel
// comparison against GPU output produces perpetual false failures. Every
// golden-image test therefore runs here, where the result is deterministic
// and reproducible, and CI needs no GPU.
//
// That makes CPU raster a permanently maintained first-class path. It must
// not regress when GPU backends arrive in P1.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "drawgui/graphics/canvas.h"

class SkCanvas;

namespace dg {

// Read access to a raster surface's backing store, in the terms something
// that copies pixels needs: where they are, how many, and how far apart the
// rows sit. Valid only while the surface is alive and only until the next
// draw.
struct PixelView {
  const std::uint8_t* pixels = nullptr;
  int width = 0;
  int height = 0;

  // Bytes from the start of one row to the start of the next. Not
  // width * 4: Skia is free to pad rows, and assuming it does not is the
  // classic way to produce a sheared image on exactly one machine.
  std::size_t row_bytes = 0;

  // True when one pixel is four bytes in memory order blue, green, red,
  // alpha. That is what this platform's raster surfaces turn out to be, and
  // it is also what a 32-bit XRGB window surface expects, which is why a
  // frame can be copied across without conversion.
  //
  // A bool rather than a format enum because there is exactly one format any
  // caller has ever needed and one it must refuse. Note that this is measured
  // from the live surface rather than derived from `kN32_SkColorType`: that
  // constant is computed by a macro in Skia's headers from build settings a
  // consumer of the prebuilt archive does not have, and it disagrees with the
  // library it came with.
  bool is_bgra8888 = false;
};

class RasterSurface {
 public:
  // Returns nullopt for a non-positive size or if the allocation fails.
  [[nodiscard]] static std::optional<RasterSurface> create(int width, int height);

  RasterSurface(RasterSurface&&) noexcept;
  RasterSurface& operator=(RasterSurface&&) noexcept;
  RasterSurface(const RasterSurface&) = delete;
  RasterSurface& operator=(const RasterSurface&) = delete;
  ~RasterSurface();

  [[nodiscard]] int width() const;
  [[nodiscard]] int height() const;

  [[nodiscard]] Canvas canvas();

  // The SkCanvas behind canvas(), for code whose subject is Skia itself.
  //
  // dg::Canvas is deliberately tiny and grows one operation at a time as a
  // render object needs it. examples/02_skia_cpu_gallery is not such a
  // consumer: its job is to exercise and time Skia's own drawing surface so
  // that the layout and widget layer can be designed against measured costs.
  // Routing it through a wrapper designed today would mean inventing forty
  // wrapper methods shaped by "what Skia offers" instead of by what a widget
  // needs - which is the mistake this project already made once with the
  // platform headers.
  //
  // Not for application code. When step 3 knows which operations widgets
  // actually use, those become dg::Canvas methods and this stays where it is.
  [[nodiscard]] SkCanvas* sk_canvas();

  // Direct read access to the pixels, without a copy.
  [[nodiscard]] PixelView peek_pixels() const;

  // Encodes the current contents as PNG. Returns an empty vector on failure.
  //
  // PNG is lossless and deterministic, which is what makes it usable as a
  // golden baseline format.
  [[nodiscard]] std::vector<std::uint8_t> encode_png() const;

 private:
  struct Impl;

  explicit RasterSurface(std::unique_ptr<Impl> impl);

  std::unique_ptr<Impl> impl_;
};

}  // namespace dg
