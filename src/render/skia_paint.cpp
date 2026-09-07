#include "render/skia_paint.h"

#include <algorithm>

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRRect.h"

namespace dg::detail {
namespace {

SkRRect to_sk_rrect(const SkRect& rect, const Radii& radii) {
  const SkVector corners[4] = {
      {radii.top_left, radii.top_left},
      {radii.top_right, radii.top_right},
      {radii.bottom_right, radii.bottom_right},
      {radii.bottom_left, radii.bottom_left},
  };
  SkRRect rrect;
  rrect.setRectRadii(rect, corners);
  return rrect;
}

Radii shrink(const Radii& radii, float amount) {
  return Radii{
      std::max(0.0F, radii.top_left - amount), std::max(0.0F, radii.top_right - amount),
      std::max(0.0F, radii.bottom_right - amount), std::max(0.0F, radii.bottom_left - amount)};
}

// dg::Color is unpremultiplied 0xAARRGGBB at the API boundary (design.md
// section 5.11.3 rule 1) and SkColor is the same 32-bit layout, so this is a
// reinterpretation rather than a conversion. Skia premultiplies internally
// when it fills.
SkColor to_sk_color(Color color) {
  return static_cast<SkColor>(color.argb());
}

void fill_shape(SkCanvas& canvas, const SkRect& rect, const Radii& radii, SkPaint& paint) {
  if (radii.is_zero()) {
    canvas.drawRect(rect, paint);
    return;
  }
  canvas.drawRRect(to_sk_rrect(rect, radii), paint);
}

}  // namespace

SkRect to_sk_rect(const PixelRect& rect) {
  return SkRect::MakeXYWH(static_cast<float>(rect.x), static_cast<float>(rect.y),
                          static_cast<float>(rect.width), static_cast<float>(rect.height));
}

void paint_node(SkCanvas& canvas, const PixelRect& bounds, const NodeStyle& style) {
  if (bounds.is_empty()) {
    return;
  }
  const SkRect rect = to_sk_rect(bounds);

  SkPaint paint;
  paint.setAntiAlias(true);

  if (style.fill.alpha() != 0) {
    paint.setStyle(SkPaint::kFill_Style);
    paint.setColor(to_sk_color(style.fill));
    fill_shape(canvas, rect, style.radii, paint);
  }

  if (style.border_width <= 0.0F || style.border_color.alpha() == 0) {
    return;
  }

  // Skia centres a stroke on the path it is given, so stroking the node's own
  // rectangle would put half the border width outside the bounds the node
  // declared - and those pixels would then never be invalidated when the node
  // changes. Insetting by half the width lands the whole border inside.
  const float inset = style.border_width * 0.5F;
  const SkRect inner = rect.makeInset(inset, inset);
  if (inner.isEmpty()) {
    return;
  }
  paint.setStyle(SkPaint::kStroke_Style);
  paint.setStrokeWidth(style.border_width);
  paint.setColor(to_sk_color(style.border_color));
  fill_shape(canvas, inner, shrink(style.radii, inset), paint);
}

}  // namespace dg::detail
