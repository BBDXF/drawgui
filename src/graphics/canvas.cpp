#include "drawgui/graphics/canvas.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"

namespace dg {
namespace {

// SkColor is also straight (unpremultiplied) 0xAARRGGBB, so this is a
// reinterpretation rather than a conversion. design.md section 5.11.3 rule 1
// requires the premultiplication to happen inside Skia, once, at this entry
// point - which is exactly what passing a straight SkColor to SkPaint does.
SkColor to_sk_color(Color color) {
  return static_cast<SkColor>(color.argb());
}

SkRect to_sk_rect(const Rect& rect) {
  return SkRect::MakeLTRB(rect.left, rect.top, rect.right, rect.bottom);
}

SkRRect to_sk_rrect(const Rect& rect, const Radii& radii) {
  // SkRRect corner order is upper-left, upper-right, lower-right, lower-left,
  // which is the field order of dg::Radii.
  const SkVector corners[4] = {
      {radii.top_left, radii.top_left},
      {radii.top_right, radii.top_right},
      {radii.bottom_right, radii.bottom_right},
      {radii.bottom_left, radii.bottom_left},
  };
  SkRRect rrect;
  rrect.setRectRadii(to_sk_rect(rect), corners);
  return rrect;
}

// design.md section 5.3.4: quality parameters are fixed, not exposed.
// Analytic anti-aliasing is always on - Skia's is good enough that MSAA buys
// nothing but memory.
SkPaint make_paint(Color color) {
  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setColor(to_sk_color(color));
  return paint;
}

SkPaint make_stroke_paint(Color color, float stroke_width) {
  SkPaint paint = make_paint(color);
  paint.setStroke(true);
  paint.setStrokeWidth(stroke_width);
  return paint;
}

}  // namespace

void Canvas::clear(Color color) {
  canvas_->clear(to_sk_color(color));
}

void Canvas::fill_rect(const Rect& rect, Color color) {
  canvas_->drawRect(to_sk_rect(rect), make_paint(color));
}

void Canvas::stroke_rect(const Rect& rect, Color color, float stroke_width) {
  canvas_->drawRect(to_sk_rect(rect), make_stroke_paint(color, stroke_width));
}

void Canvas::fill_rrect(const Rect& rect, const Radii& radii, Color color) {
  canvas_->drawRRect(to_sk_rrect(rect, radii), make_paint(color));
}

void Canvas::stroke_rrect(const Rect& rect, const Radii& radii, Color color,
                          float stroke_width) {
  canvas_->drawRRect(to_sk_rrect(rect, radii), make_stroke_paint(color, stroke_width));
}

}  // namespace dg
