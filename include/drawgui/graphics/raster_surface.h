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

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "drawgui/graphics/canvas.h"

namespace dg {

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
