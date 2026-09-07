#include "render/clip_shape.h"

#include <algorithm>

namespace dg::detail {
namespace {

// One edge's worth of the uniform-scale rule: two radii sharing an edge may
// not together exceed it.
void limit_scale(double& scale, float first, float second, int edge) {
  const double sum = static_cast<double>(first) + static_cast<double>(second);
  if (sum > static_cast<double>(edge)) {
    scale = std::min(scale, static_cast<double>(edge) / sum);
  }
}

[[nodiscard]] float non_negative(float value) {
  return std::max(0.0F, value);
}

// True when the point lies outside the quarter-circle at a corner. `cx`/`cy`
// are the corner circle's centre, which is `radius` inside the box on both
// axes, so a point beyond the centre on both axes is in the corner's quadrant
// and is inside the shape only if it is within the radius.
[[nodiscard]] bool within_corner(float px, float py, float cx, float cy, float radius) {
  const float dx = px - cx;
  const float dy = py - cy;
  return ((dx * dx) + (dy * dy)) <= (radius * radius);
}

}  // namespace

Radii fit_radii(const PixelRect& rect, const Radii& radii) {
  if (rect.is_empty()) {
    return Radii{};
  }
  const Radii clamped{non_negative(radii.top_left), non_negative(radii.top_right),
                      non_negative(radii.bottom_right), non_negative(radii.bottom_left)};
  if (clamped.is_zero()) {
    return clamped;
  }

  double scale = 1.0;
  limit_scale(scale, clamped.top_left, clamped.top_right, rect.width);
  limit_scale(scale, clamped.top_right, clamped.bottom_right, rect.height);
  limit_scale(scale, clamped.bottom_right, clamped.bottom_left, rect.width);
  limit_scale(scale, clamped.bottom_left, clamped.top_left, rect.height);
  if (scale >= 1.0) {
    return clamped;
  }

  const auto scaled = [scale](float radius) {
    return static_cast<float>(static_cast<double>(radius) * scale);
  };
  return Radii{scaled(clamped.top_left), scaled(clamped.top_right),
               scaled(clamped.bottom_right), scaled(clamped.bottom_left)};
}

bool clip_contains(const PixelRect& rect, const Radii& radii, PixelPoint point) {
  if (!contains(rect, point)) {
    return false;
  }
  const Radii fitted = fit_radii(rect, radii);
  if (fitted.is_zero()) {
    return true;
  }

  const auto px = static_cast<float>(point.x) + 0.5F;
  const auto py = static_cast<float>(point.y) + 0.5F;
  const auto left = static_cast<float>(rect.left());
  const auto top = static_cast<float>(rect.top());
  const auto right = static_cast<float>(rect.right());
  const auto bottom = static_cast<float>(rect.bottom());

  if (px < left + fitted.top_left && py < top + fitted.top_left) {
    return within_corner(px, py, left + fitted.top_left, top + fitted.top_left,
                         fitted.top_left);
  }
  if (px > right - fitted.top_right && py < top + fitted.top_right) {
    return within_corner(px, py, right - fitted.top_right, top + fitted.top_right,
                         fitted.top_right);
  }
  if (px > right - fitted.bottom_right && py > bottom - fitted.bottom_right) {
    return within_corner(px, py, right - fitted.bottom_right, bottom - fitted.bottom_right,
                         fitted.bottom_right);
  }
  if (px < left + fitted.bottom_left && py > bottom - fitted.bottom_left) {
    return within_corner(px, py, left + fitted.bottom_left, bottom - fitted.bottom_left,
                         fitted.bottom_left);
  }
  return true;
}

}  // namespace dg::detail
