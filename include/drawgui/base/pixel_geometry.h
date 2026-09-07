// Integer geometry in physical device pixels.
//
// These two types started life inside include/drawgui/window/window_manager.h,
// which was their first consumer. They moved down here when the render layer
// became their second: a render tree that tracks damage speaks in whole
// device pixels for the same reason a framebuffer does, and it has no
// business including the window layer's header to say so.
//
// The direction of the move matters. Nothing was invented here for a caller
// that does not exist - the file contains exactly the two structs that were
// already shipping plus the four rectangle operations damage accumulation
// turned out to need, and every one of those is exercised by a unit test.
//
// dg::Rect (graphics/types.h) is the float, logical-pixel rectangle layout
// will use. It is deliberately a different type: a coordinate that has been
// through DPI scaling and one that has not are not interchangeable, and
// design.md section 5.4.9 keeps the two apart on purpose.

#pragma once

#include <algorithm>
#include <cstdint>

namespace dg {

// A size in physical device pixels, which is what a frame must be rasterized
// at. Integral, unlike dg::Size, because a framebuffer is allocated in whole
// pixels and rounding it at the point of use is how off-by-one edges happen.
struct PixelSize {
  int width = 0;
  int height = 0;

  friend bool operator==(PixelSize, PixelSize) = default;
};

// A position in physical device pixels, relative to the top-left of the image
// it describes.
//
// Integral for the same reason PixelSize is: a pointer position arrives from
// the platform as a coordinate inside a framebuffer, and hit testing has to
// agree with the rasterizer about which pixel that is. A float here would put
// "which pixel is 10.5 in" between the two.
struct PixelPoint {
  int x = 0;
  int y = 0;

  friend bool operator==(PixelPoint, PixelPoint) = default;
};

// A region in physical device pixels, relative to the top-left of the image
// it describes.
//
// Half-open: a rectangle at x = 4 with width 2 covers columns 4 and 5. Every
// operation below preserves that, which is what makes two rectangles sharing
// an edge count as disjoint rather than as overlapping by a row of pixels.
struct PixelRect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;

  [[nodiscard]] static constexpr PixelRect from_edges(int left, int top, int right,
                                                      int bottom) {
    if (right <= left || bottom <= top) {
      return PixelRect{};
    }
    return PixelRect{left, top, right - left, bottom - top};
  }

  [[nodiscard]] constexpr int left() const { return x; }
  [[nodiscard]] constexpr int top() const { return y; }
  [[nodiscard]] constexpr int right() const { return x + width; }
  [[nodiscard]] constexpr int bottom() const { return y + height; }

  [[nodiscard]] constexpr bool is_empty() const { return width <= 0 || height <= 0; }

  // 64-bit because a 4K rectangle is 8.3 million pixels and the merge
  // heuristic in DamageRegion adds several of them together; 32-bit would
  // overflow on a plausible multi-monitor bounding box rather than on an
  // absurd one.
  [[nodiscard]] constexpr std::int64_t area() const {
    if (is_empty()) {
      return 0;
    }
    return static_cast<std::int64_t>(width) * static_cast<std::int64_t>(height);
  }

  [[nodiscard]] constexpr PixelRect offset_by(int dx, int dy) const {
    return PixelRect{x + dx, y + dy, width, height};
  }

  // Grown by `amount` on every side. An empty rectangle stays empty: there is
  // no position to grow around.
  [[nodiscard]] constexpr PixelRect inflated_by(int amount) const {
    if (is_empty()) {
      return PixelRect{};
    }
    return PixelRect::from_edges(x - amount, y - amount, right() + amount, bottom() + amount);
  }

  friend bool operator==(PixelRect, PixelRect) = default;
};

// The overlap of two rectangles, or an empty rectangle when they do not
// overlap. An empty input yields an empty result.
[[nodiscard]] constexpr PixelRect intersect(const PixelRect& a, const PixelRect& b) {
  if (a.is_empty() || b.is_empty()) {
    return PixelRect{};
  }
  return PixelRect::from_edges(std::max(a.left(), b.left()), std::max(a.top(), b.top()),
                               std::min(a.right(), b.right()),
                               std::min(a.bottom(), b.bottom()));
}

[[nodiscard]] constexpr bool intersects(const PixelRect& a, const PixelRect& b) {
  return !intersect(a, b).is_empty();
}

// The smallest rectangle containing both. An empty operand contributes
// nothing, so joining onto an empty rectangle returns the other one rather
// than a box stretching back to the origin.
[[nodiscard]] constexpr PixelRect join(const PixelRect& a, const PixelRect& b) {
  if (a.is_empty()) {
    return b.is_empty() ? PixelRect{} : b;
  }
  if (b.is_empty()) {
    return a;
  }
  return PixelRect::from_edges(std::min(a.left(), b.left()), std::min(a.top(), b.top()),
                               std::max(a.right(), b.right()),
                               std::max(a.bottom(), b.bottom()));
}

// True when every pixel of `inner` is also a pixel of `outer`. An empty
// `inner` is contained in anything, since it has no pixels to place.
[[nodiscard]] constexpr bool contains(const PixelRect& outer, const PixelRect& inner) {
  if (inner.is_empty()) {
    return true;
  }
  if (outer.is_empty()) {
    return false;
  }
  return outer.left() <= inner.left() && outer.top() <= inner.top() &&
         outer.right() >= inner.right() && outer.bottom() >= inner.bottom();
}

// True when the pixel at `point` belongs to `rect`. Half-open on the right and
// bottom edges, exactly as PixelRect is everywhere else: the pixel a rectangle
// ends at belongs to whatever comes next, so two rectangles sharing an edge
// never both claim the pointer.
[[nodiscard]] constexpr bool contains(const PixelRect& rect, PixelPoint point) {
  return !rect.is_empty() && point.x >= rect.left() && point.x < rect.right() &&
         point.y >= rect.top() && point.y < rect.bottom();
}

}  // namespace dg
