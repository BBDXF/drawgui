#include "render/skia_paint.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkFontTypes.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRefCnt.h"
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

// The radii of the shape the four border widths inset the border box to.
//
// A corner is bounded by two sides of possibly different thickness, and this
// type carries one circular radius per corner rather than an elliptical pair,
// so the two have to collapse to one number. The THICKER side wins, and that
// choice is a containment proof rather than a preference: with
// `inner = r - max(wa, wb)` the inner corner's centre sits `|wa - wb|` away
// from the outer corner's centre, and `|wa - wb| + (r - max(wa, wb)) <= r`
// holds for every non-negative pair - so the inner shape is always inside the
// outer one, which is what drawDRRect requires and what keeps the border
// inside the rectangle the node declared.
Radii inset_radii(const Radii& radii, const BorderWidths& widths) {
  const auto corner = [](float radius, float a, float b) {
    return std::max(0.0F, radius - std::max(std::max(a, 0.0F), std::max(b, 0.0F)));
  };
  return Radii{corner(radii.top_left, widths.left, widths.top),
               corner(radii.top_right, widths.right, widths.top),
               corner(radii.bottom_right, widths.right, widths.bottom),
               corner(radii.bottom_left, widths.left, widths.bottom)};
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

// Subpixel positioning OFF. With it on, the same string at the same integer
// origin can rasterize differently depending on the canvas translation in
// force, and a damage repaint and a full repaint do not share one - which
// would break the byte-identity comparison that is this project's whole
// acceptance technique for partial repaint.
SkFont run_font(const sk_sp<SkTypeface>& typeface, float size) {
  SkFont font{typeface, size};
  font.setEdging(SkFont::Edging::kAntiAlias);
  font.setSubpixel(false);
  return font;
}

// A run nothing covers is drawn as one .notdef box per codepoint, in the font
// the text asked for. That is the decision, not an accident: an uncovered
// codepoint that drew nothing would be indistinguishable from a string that
// was never set, and the difference between "this machine has no font for
// this" and "the label is empty" is the whole diagnosis.
float missing_run_width(const SkFont& font, std::size_t codepoints) {
  const SkGlyphID notdef = 0;
  return font.getWidth(notdef) * static_cast<float>(codepoints);
}

void draw_missing_run(SkCanvas& canvas, const TextRun& run, const SkFont& font,
                      const SkPaint& paint, SkPoint origin) {
  std::vector<SkGlyphID> glyphs(run.codepoints, 0);
  std::vector<SkPoint> positions(run.codepoints);
  font.getPos(glyphs, positions, origin);
  canvas.drawGlyphs(glyphs, positions, SkPoint{0.0F, 0.0F}, font, paint);
}

float run_width(const TextRun& run, const SkFont& font, const std::string& source) {
  if (run.missing) {
    return missing_run_width(font, run.codepoints);
  }
  return font.measureText(source.data() + run.begin, run.end - run.begin,
                          SkTextEncoding::kUTF8);
}

void paint_text(SkCanvas& canvas, const PixelRect& bounds, const TextStyle& text,
                const FontCatalog* fonts) {
  if (text.text.empty() || text.size <= 0.0F || text.color.alpha() == 0 || fonts == nullptr) {
    return;
  }
  sk_sp<SkTypeface> primary = FontAccess::typeface(*fonts, text.font);
  if (!primary) {
    return;
  }
  const std::vector<TextRun> runs = FontAccess::runs(*fonts, text);
  if (runs.empty()) {
    return;
  }

  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setColor(to_sk_color(text.color));

  std::vector<SkFont> fonts_per_run;
  fonts_per_run.reserve(runs.size());
  float width = 0.0F;
  for (const TextRun& run : runs) {
    fonts_per_run.push_back(run_font(run.typeface, text.size));
    width += run_width(run, fonts_per_run.back(), text.text);
  }

  // The baseline comes from the PRIMARY font, not from each run's own. A
  // fallback face has its own ascent and descent, and letting each run place
  // itself would make one string sit on several baselines - the CJK half of a
  // mixed label would step up or down mid-sentence.
  const SkFont primary_font = run_font(primary, text.size);
  const float baseline = text_baseline_y(bounds, primary_font);
  float pen = text_origin_x(bounds, text, width);

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
  for (std::size_t index = 0; index < runs.size(); ++index) {
    const TextRun& run = runs[index];
    const SkFont& font = fonts_per_run[index];
    if (run.missing) {
      draw_missing_run(canvas, run, font, paint, SkPoint{pen, baseline});
    } else {
      canvas.drawSimpleText(text.text.data() + run.begin, run.end - run.begin,
                            SkTextEncoding::kUTF8, pen, baseline, font, paint);
    }
    pen += run_width(run, font, text.text);
  }
  canvas.restore();
}

// Two routes, chosen by whether the four widths agree.
//
// UNIFORM is the original one, kept byte for byte: Skia centres a stroke on
// the path it is given, so stroking the node's own rectangle would put half
// the width outside the bounds the node declared, and those pixels would then
// never be invalidated when the node changes. Insetting by half lands the
// whole border inside. Every measurement and every golden baseline this
// project holds was produced by this path, so an unequal-border feature must
// not reroute it.
//
// UNEQUAL fills the ring between the border box and the box the four widths
// inset it to. One filled annulus rather than four stroked edges, because the
// table gives all four sides ONE colour - so mitre joints are invisible, and
// four overlapping bands would double-blend a translucent border at the
// corners while a single fill cannot.
void paint_border(SkCanvas& canvas, const SkRect& rect, const NodeStyle& style,
                  SkPaint& paint) {
  const BorderWidths& widths = style.border_width;
  paint.setColor(to_sk_color(style.border_color));

  if (widths.is_uniform()) {
    const float inset = widths.left * 0.5F;
    const SkRect centred = rect.makeInset(inset, inset);
    if (centred.isEmpty()) {
      return;
    }
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setStrokeWidth(widths.left);
    fill_shape(canvas, centred, shrink(style.radii, inset), paint);
    return;
  }

  paint.setStyle(SkPaint::kFill_Style);
  const SkRect hole = SkRect::MakeLTRB(
      rect.fLeft + std::max(0.0F, widths.left), rect.fTop + std::max(0.0F, widths.top),
      rect.fRight - std::max(0.0F, widths.right), rect.fBottom - std::max(0.0F, widths.bottom));

  // Widths that meet or cross in the middle leave no content box at all, so
  // the "ring" is the whole shape. Handing drawDRRect an inverted inner
  // rectangle would be undefined rather than merely wrong.
  if (hole.isEmpty()) {
    fill_shape(canvas, rect, style.radii, paint);
    return;
  }
  canvas.drawDRRect(to_sk_rrect(rect, style.radii),
                    to_sk_rrect(hole, inset_radii(style.radii, widths)), paint);
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

  if (style.border_width.is_zero() || style.border_color.alpha() == 0) {
    paint_text(canvas, bounds, style.text, fonts);
    return;
  }

  paint_border(canvas, rect, style, paint);

  // Text last, so a label reads over its own border rather than under it.
  paint_text(canvas, bounds, style.text, fonts);
}

}  // namespace dg::detail
