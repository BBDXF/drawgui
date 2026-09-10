#include "drawgui/render/text_metrics.h"

#include <algorithm>
#include <cstddef>

#include "include/core/SkFont.h"
#include "include/core/SkFontTypes.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkTypeface.h"

#include "render/font_access.h"

namespace dg {
namespace {

// The identical construction src/render/skia_paint.cpp's run_font() uses -
// subpixel positioning off, for the same determinism reason: two calls at
// different translations must measure and paint the same string identically,
// which is this project's whole partial-repaint acceptance technique.
SkFont build_font(const sk_sp<SkTypeface>& typeface, float size) {
  SkFont font{typeface, size};
  font.setEdging(SkFont::Edging::kAntiAlias);
  font.setSubpixel(false);
  return font;
}

}  // namespace

float measure_ascii_width(const FontCatalog& fonts, FontId font, float size,
                          std::string_view ascii_text) {
  if (ascii_text.empty() || size <= 0.0F) {
    return 0.0F;
  }
  const sk_sp<SkTypeface> typeface = FontAccess::typeface(fonts, font);
  if (!typeface) {
    return 0.0F;
  }
  const SkFont sk_font = build_font(typeface, size);
  return sk_font.measureText(ascii_text.data(), ascii_text.size(), SkTextEncoding::kUTF8);
}

std::size_t ascii_offset_at_x(const FontCatalog& fonts, FontId font, float size,
                              std::string_view ascii_text, float local_x) {
  if (ascii_text.empty() || size <= 0.0F) {
    return 0;
  }
  const sk_sp<SkTypeface> typeface = FontAccess::typeface(fonts, font);
  if (!typeface) {
    return 0;
  }
  if (local_x <= 0.0F) {
    return 0;
  }
  const SkFont sk_font = build_font(typeface, size);

  // One measureText call per prefix length rather than a single pass over
  // per-glyph advances: O(n^2) in the string length, which this project's
  // performance policy explicitly does not gate (a TextField's content is
  // short, and every prior slice's stated position is "measured when there
  // is a measurement to justify one" - doc/font-fallback.md's cost section
  // makes the identical trade for its own resolver).
  float previous_width = 0.0F;
  for (std::size_t i = 0; i < ascii_text.size(); ++i) {
    const float width_through_i =
        sk_font.measureText(ascii_text.data(), i + 1, SkTextEncoding::kUTF8);
    const float midpoint = (previous_width + width_through_i) * 0.5F;
    if (local_x < midpoint) {
      return i;
    }
    previous_width = width_through_i;
  }
  return ascii_text.size();
}

}  // namespace dg
