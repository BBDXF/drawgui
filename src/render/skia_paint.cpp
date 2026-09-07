#include "render/skia_paint.h"

#include <algorithm>
#include <utility>

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkFontTypes.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkTypeface.h"

#include "render/font_access.h"

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

// Where the run starts, given how wide it turned out to be. `inset` keeps
// left- and right-aligned text off the node's own border; centred text is
// already clear of both, so it ignores it - a centred run that also honoured
// the inset would drift off centre whenever the two edges were treated
// differently.
float text_origin_x(const PixelRect& bounds, const TextStyle& text, float width) {
  const auto left = static_cast<float>(bounds.left());
  const auto right = static_cast<float>(bounds.right());
  const auto inset = static_cast<float>(text.inset);
  switch (text.align) {
    case TextAlign::kLeft:
      return left + inset;
    case TextAlign::kRight:
      return right - inset - width;
    case TextAlign::kCenter:
      break;
  }
  return left + ((static_cast<float>(bounds.width) - width) * 0.5F);
}

// Centred on the ASCENT-TO-DESCENT box rather than on the em box or on the
// glyphs actually present. Centring on the glyphs would move a button's label
// when its text changed from "OK" to "Apply", because the second has a
// descender and the first does not; centring on the font's own metrics keeps
// every label in a row sitting on the same line whatever it says.
float text_baseline_y(const PixelRect& bounds, const SkFont& font) {
  SkFontMetrics metrics{};
  font.getMetrics(&metrics);
  const float span = metrics.fDescent - metrics.fAscent;
  return static_cast<float>(bounds.top()) +
         ((static_cast<float>(bounds.height) - span) * 0.5F) - metrics.fAscent;
}

void paint_text(SkCanvas& canvas, const PixelRect& bounds, const TextStyle& text,
                const FontCatalog* fonts) {
  if (text.text.empty() || text.size <= 0.0F || text.color.alpha() == 0 || fonts == nullptr) {
    return;
  }
  sk_sp<SkTypeface> typeface = FontAccess::typeface(*fonts, text.font);
  if (!typeface) {
    return;
  }

  SkFont font{std::move(typeface), text.size};
  font.setEdging(SkFont::Edging::kAntiAlias);

  // Subpixel positioning OFF. With it on, the same string at the same integer
  // origin can rasterize differently depending on the canvas translation in
  // force, and a damage repaint and a full repaint do not share one - which
  // would break the byte-identity comparison that is this project's whole
  // acceptance technique for partial repaint.
  font.setSubpixel(false);

  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setColor(to_sk_color(text.color));

  const float width =
      font.measureText(text.text.data(), text.text.size(), SkTextEncoding::kUTF8);

  // Clipped to the node. A string is the only thing in NodeStyle that is not
  // naturally contained by the box it was given, and a glyph escaping that box
  // leaves pixels no invalidation will ever reach.
  //
  // This clip nests inside whatever damage clip is already in force, and the
  // two intersect exactly - which is safe because glyph rasterization is
  // clip-invariant (measured; see clips_atomically). A text node is therefore
  // free to be cut in half by a damage rectangle, and this containment clip is
  // still what keeps it inside its own box.
  canvas.save();
  canvas.clipRect(to_sk_rect(bounds), false);
  canvas.drawSimpleText(text.text.data(), text.text.size(), SkTextEncoding::kUTF8,
                        text_origin_x(bounds, text, width), text_baseline_y(bounds, font), font,
                        paint);
  canvas.restore();
}

}  // namespace

SkRect to_sk_rect(const PixelRect& rect) {
  return SkRect::MakeXYWH(static_cast<float>(rect.x), static_cast<float>(rect.y),
                          static_cast<float>(rect.width), static_cast<float>(rect.height));
}

void paint_node(SkCanvas& canvas, const PixelRect& bounds, const NodeStyle& style,
                const FontCatalog* fonts) {
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
    paint_text(canvas, bounds, style.text, fonts);
    return;
  }

  // Skia centres a stroke on the path it is given, so stroking the node's own
  // rectangle would put half the border width outside the bounds the node
  // declared - and those pixels would then never be invalidated when the node
  // changes. Insetting by half the width lands the whole border inside.
  const float inset = style.border_width * 0.5F;
  const SkRect inner = rect.makeInset(inset, inset);
  if (!inner.isEmpty()) {
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setStrokeWidth(style.border_width);
    paint.setColor(to_sk_color(style.border_color));
    fill_shape(canvas, inner, shrink(style.radii, inset), paint);
  }

  // Text last, so a label reads over its own border rather than under it.
  paint_text(canvas, bounds, style.text, fonts);
}

}  // namespace dg::detail
