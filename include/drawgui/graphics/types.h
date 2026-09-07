// Geometry and color primitives shared by the graphics layer.
//
// These are drawgui's own types. Skia equivalents (SkRect, SkColor, SkRRect)
// never appear above the graphics layer - design.md section 5.3 keeps Skia's
// API surface contained, not because a backend swap is planned, but because
// Skia exports thousands of symbols and this project uses under 2% of them.

#pragma once

#include <cstdint>

namespace dg {

// A color at the API boundary.
//
// design.md section 5.11.3 rule 1: the boundary format is unpremultiplied
// (straight) 0xAARRGGBB. Skia stores premultiplied internally; the single
// conversion happens at the graphics layer entry point, never here.
class Color {
 public:
  constexpr Color() = default;

  static constexpr Color from_argb(std::uint32_t argb) { return Color{argb}; }

  static constexpr Color rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b,
                              std::uint8_t a = 0xFF) {
    return Color{(static_cast<std::uint32_t>(a) << 24) | (static_cast<std::uint32_t>(r) << 16) |
                 (static_cast<std::uint32_t>(g) << 8) | static_cast<std::uint32_t>(b)};
  }

  [[nodiscard]] constexpr std::uint32_t argb() const { return argb_; }

  [[nodiscard]] constexpr std::uint8_t alpha() const {
    return static_cast<std::uint8_t>(argb_ >> 24);
  }
  [[nodiscard]] constexpr std::uint8_t red() const {
    return static_cast<std::uint8_t>(argb_ >> 16);
  }
  [[nodiscard]] constexpr std::uint8_t green() const {
    return static_cast<std::uint8_t>(argb_ >> 8);
  }
  [[nodiscard]] constexpr std::uint8_t blue() const { return static_cast<std::uint8_t>(argb_); }

  friend constexpr bool operator==(Color, Color) = default;

 private:
  constexpr explicit Color(std::uint32_t argb) : argb_(argb) {}

  std::uint32_t argb_ = 0;  // fully transparent
};

// All coordinates are logical pixels (dp). design.md section 5.4.9: DPI
// scaling is applied once, as a canvas transform at the render root, and
// never enters layout.
struct Point {
  float x = 0;
  float y = 0;

  friend constexpr bool operator==(const Point&, const Point&) = default;
};

struct Size {
  float width = 0;
  float height = 0;

  friend constexpr bool operator==(const Size&, const Size&) = default;
};

// Half-open rectangle in logical pixels.
struct Rect {
  float left = 0;
  float top = 0;
  float right = 0;
  float bottom = 0;

  static constexpr Rect from_xywh(float x, float y, float w, float h) {
    return Rect{x, y, x + w, y + h};
  }

  [[nodiscard]] constexpr float width() const { return right - left; }
  [[nodiscard]] constexpr float height() const { return bottom - top; }
  [[nodiscard]] constexpr bool is_empty() const { return right <= left || bottom <= top; }

  friend constexpr bool operator==(const Rect&, const Rect&) = default;
};

// Per-corner radii, matching the border_radius_{tl,tr,br,bl} property group
// in design.md section 5.9.5. A single radius per corner - elliptical corners
// with independent x and y radii are not exposed, as nothing consumes them.
struct Radii {
  float top_left = 0;
  float top_right = 0;
  float bottom_right = 0;
  float bottom_left = 0;

  static constexpr Radii all(float r) { return Radii{r, r, r, r}; }

  [[nodiscard]] constexpr bool is_zero() const {
    return top_left == 0 && top_right == 0 && bottom_right == 0 && bottom_left == 0;
  }

  friend constexpr bool operator==(const Radii&, const Radii&) = default;
};

// How thick the border is on each of the four sides, matching the
// border_width_{l,t,r,b} property group.
//
// Four floats rather than one, because the property table has always had four
// and the painter used to collapse them to their minimum - which was exact
// only when all four agreed. doc/properties.md section 3.3 recorded that as a
// table-versus-engine disagreement; this type is what removes it.
//
// `is_uniform()` is not a convenience. The painter takes a different route for
// a uniform border (one centred stroke, inset by half the width) than for an
// unequal one (a filled ring between two shapes), and the uniform route is the
// one every existing pixel in this project was produced by.
struct BorderWidths {
  float left = 0;
  float top = 0;
  float right = 0;
  float bottom = 0;

  static constexpr BorderWidths all(float width) {
    return BorderWidths{width, width, width, width};
  }

  [[nodiscard]] constexpr bool is_zero() const {
    return left <= 0 && top <= 0 && right <= 0 && bottom <= 0;
  }

  [[nodiscard]] constexpr bool is_uniform() const {
    return left == top && top == right && right == bottom;
  }

  friend constexpr bool operator==(const BorderWidths&, const BorderWidths&) = default;
};

}  // namespace dg
