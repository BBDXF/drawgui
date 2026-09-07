// Small shared helpers for the panel files. Not an abstraction layer - just
// the three or four lines each panel would otherwise repeat.

#pragma once

#include "include/core/SkColor.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkTypeface.h"

namespace gallery {

inline constexpr SkColor kBackground = 0xFF14171C;
inline constexpr SkColor kCard = 0xFF1E232B;
inline constexpr SkColor kCardEdge = 0xFF333B47;
inline constexpr SkColor kInk = 0xFFE6EAF0;
inline constexpr SkColor kInkDim = 0xFF95A1B2;
inline constexpr SkColor kBlue = 0xFF2E86DE;
inline constexpr SkColor kGreen = 0xFF27AE60;
inline constexpr SkColor kRed = 0xFFE74C3C;
inline constexpr SkColor kAmber = 0xFFF6C445;
inline constexpr SkColor kViolet = 0xFF9B59B6;

// Anti-aliased with subpixel positioning, which is the quality setting
// design.md section 5.3.4 fixes for the whole project. The one panel that
// shows aliased text overrides it locally, on purpose.
inline SkFont text_font(const sk_sp<SkTypeface>& typeface, float size) {
  SkFont font{typeface, size};
  font.setEdging(SkFont::Edging::kAntiAlias);
  font.setSubpixel(true);
  return font;
}

inline SkPaint fill_paint(SkColor color, bool anti_alias = true) {
  SkPaint paint;
  paint.setAntiAlias(anti_alias);
  paint.setColor(color);
  return paint;
}

inline SkPaint stroke_paint(SkColor color, float width, bool anti_alias = true) {
  SkPaint paint = fill_paint(color, anti_alias);
  paint.setStroke(true);
  paint.setStrokeWidth(width);
  return paint;
}

}  // namespace gallery
