#include "render/skia_paint.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkFontTypes.h"
#include "include/core/SkImage.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkTileMode.h"
#include "include/core/SkTypeface.h"
#include "include/effects/SkGradient.h"
#include "include/effects/SkImageFilters.h"

#include "render/clip_shape.h"
#include "render/font_access.h"
#include "render/image_access.h"

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

// Painted BEFORE `fill`/`background_gradient`, so it sits behind everything
// else the node paints - design.md section 5.9.5's decoration order.
//
// `DropShadowOnly` rather than `DropShadow`: the latter also draws the shape
// it is given, in the paint's own colour, which here would double-paint the
// node's border box before `fill` gets to it. `DropShadowOnly` produces only
// the blurred, offset, coloured shadow - the shape drawn to carry the filter
// (an opaque fill, `SK_ColorBLACK`) never reaches the canvas itself, only its
// COVERAGE does, which is what the filter reads to know where the shadow is
// dense.
//
// `spread` grows the shape BEFORE the blur is applied - `SkRect::makeOutset`,
// CSS's own spread semantics - and a spread negative enough to invert the
// rectangle collapses it to an empty one rather than a shape with negative
// area, which `fill_shape` would hand Skia as undefined input.
void paint_shadow(SkCanvas& canvas, const SkRect& rect, const Radii& radii,
                  const std::optional<ShadowStyle>& shadow) {
  if (!shadow.has_value() || shadow->color.alpha() == 0) {
    return;
  }
  const SkRect spread_rect = rect.makeOutset(shadow->spread, shadow->spread);
  if (spread_rect.isEmpty()) {
    return;
  }
  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setStyle(SkPaint::kFill_Style);
  paint.setColor(SK_ColorBLACK);
  paint.setImageFilter(SkImageFilters::DropShadowOnly(shadow->offset_x, shadow->offset_y,
                                                      shadow->blur_radius, shadow->blur_radius,
                                                      to_sk_color(shadow->color), nullptr));
  fill_shape(canvas, spread_rect, radii, paint);
}

// The gradient AXIS: CSS's own `linear-gradient(<angle>, ...)` convention
// (0deg = bottom-to-top is CSS's own choice; this project instead follows
// the more common "0 = left-to-right, clockwise" convention already
// documented on LinearGradientStyle::angle_deg, so the two must not be
// confused) turned into two points long enough that the gradient line spans
// the whole box regardless of which corner it exits through - the standard
// "gradient line length" formula: project the half-width and half-height
// onto the axis and sum their magnitudes.
sk_sp<SkShader> gradient_shader(const SkRect& rect, const LinearGradientStyle& gradient) {
  std::vector<SkColor4f> colors;
  std::vector<float> positions;
  colors.reserve(gradient.stops.size());
  positions.reserve(gradient.stops.size());
  for (const GradientStop& stop : gradient.stops) {
    colors.push_back(SkColor4f::FromColor(to_sk_color(stop.color)));
    positions.push_back(stop.offset);
  }

  constexpr float kDegToRad = 3.14159265358979323846F / 180.0F;
  const float angle = gradient.angle_deg * kDegToRad;
  const float dx = std::cos(angle);
  const float dy = std::sin(angle);
  const float half_w = rect.width() * 0.5F;
  const float half_h = rect.height() * 0.5F;
  const float half_extent = (std::abs(dx) * half_w) + (std::abs(dy) * half_h);
  const SkPoint centre{rect.centerX(), rect.centerY()};
  const SkPoint points[2] = {
      {centre.x() - (dx * half_extent), centre.y() - (dy * half_extent)},
      {centre.x() + (dx * half_extent), centre.y() + (dy * half_extent)},
  };

  const SkGradient description{
      SkGradient::Colors{SkSpan<const SkColor4f>{colors.data(), colors.size()},
                         SkSpan<const float>{positions.data(), positions.size()},
                         SkTileMode::kClamp},
      SkGradient::Interpolation{}};
  return SkShaders::LinearGradient(points, description);
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

// Where the decoded bitmap lands inside `bounds`, given its own pixel size
// and the requested fit - the arithmetic examples/13_image's oracle checks by
// hand, so it lives here rather than inline where only pixels could verify
// it (doc/clipping.md's fit_radii() precedent: two consumers that both
// degrade a bad answer the same way cannot disagree with it, so the bug has
// no test).
SkRect image_dst_rect(const SkRect& bounds, float source_width, float source_height,
                      ImageFit fit) {
  switch (fit) {
    case ImageFit::kFill:
      return bounds;
    case ImageFit::kNone: {
      const float x = bounds.fLeft + ((bounds.width() - source_width) * 0.5F);
      const float y = bounds.fTop + ((bounds.height() - source_height) * 0.5F);
      return SkRect::MakeXYWH(x, y, source_width, source_height);
    }
    case ImageFit::kContain:
    case ImageFit::kCover: {
      const float scale_x = bounds.width() / source_width;
      const float scale_y = bounds.height() / source_height;
      const float scale =
          fit == ImageFit::kContain ? std::min(scale_x, scale_y) : std::max(scale_x, scale_y);
      const float width = source_width * scale;
      const float height = source_height * scale;
      const float x = bounds.fLeft + ((bounds.width() - width) * 0.5F);
      const float y = bounds.fTop + ((bounds.height() - height) * 0.5F);
      return SkRect::MakeXYWH(x, y, width, height);
    }
  }
  return bounds;
}

// The placeholder colour, filling the whole node the same way a background
// fill does - design.md section 5.10.3's "占位色，不留空洞" (a placeholder
// colour, never a hole).
void draw_placeholder(SkCanvas& canvas, const SkRect& rect, Color placeholder) {
  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setStyle(SkPaint::kFill_Style);
  paint.setColor(to_sk_color(placeholder));
  canvas.drawRect(rect, paint);
}

// Clipped to `bounds` and `radii` exactly like the fill is (apply_clip() is
// shared with the overflow clip below), so an image respects rounded corners
// the same way a background colour already does.
//
// NOT a fit-mode "does the whole image show" question at the DECODE side:
// this only ever runs after LayoutTree has already settled `bounds` (design.md
// section 5.10.3's mandatory sizing rule, enforced in
// LayoutTree::Impl::measure(), src/layout/box_layout.cpp) - fit only decides
// what is drawn inside a box whose size was never in question.
void paint_image(SkCanvas& canvas, const PixelRect& bounds, const ImageStyle& image,
                 const Radii& radii, const ImageCatalog* images) {
  if (!carries_image(image) || bounds.is_empty()) {
    return;
  }
  const SkRect rect = to_sk_rect(bounds);

  sk_sp<SkImage> decoded = (image.source.is_valid() && images != nullptr)
                               ? ImageAccess::image(*images, image.source)
                               : nullptr;
  if (!decoded) {
    if (image.placeholder.alpha() == 0) {
      return;
    }
    canvas.save();
    apply_clip(canvas, bounds, radii);
    draw_placeholder(canvas, rect, image.placeholder);
    canvas.restore();
    return;
  }

  const auto source_width = static_cast<float>(decoded->width());
  const auto source_height = static_cast<float>(decoded->height());
  if (source_width <= 0.0F || source_height <= 0.0F) {
    return;
  }

  canvas.save();
  apply_clip(canvas, bounds, radii);
  SkPaint paint;
  paint.setAntiAlias(true);
  canvas.drawImageRect(decoded, image_dst_rect(rect, source_width, source_height, image.fit),
                       SkSamplingOptions{}, &paint);
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

void apply_clip(SkCanvas& canvas, const PixelRect& bounds, const Radii& radii) {
  const SkRect rect = to_sk_rect(bounds);
  const Radii fitted = fit_radii(bounds, radii);
  if (fitted.is_zero()) {
    canvas.clipRect(rect, false);
    return;
  }
  canvas.clipRRect(to_sk_rrect(rect, fitted), true);
}

void paint_node(SkCanvas& canvas, const PixelRect& bounds, const NodeStyle& style,
                const FontCatalog* fonts, const ImageCatalog* images) {
  if (bounds.is_empty()) {
    return;
  }
  const SkRect rect = to_sk_rect(bounds);

  paint_shadow(canvas, rect, style.radii, style.shadow);

  SkPaint paint;
  paint.setAntiAlias(true);

  // A gradient REPLACES the flat fill rather than layering over it - one
  // background-colour decoration layer, matching design.md section 5.9.5's
  // own "不支持多重背景" (no multiple backgrounds) stance for the analogous
  // background_image property. `>= 2` mirrors dg::set_gradient()'s own
  // validation (doc/complex-properties.md); a style built by hand rather
  // than through the setter still cannot paint an under-specified gradient.
  if (style.background_gradient.has_value() && style.background_gradient->stops.size() >= 2) {
    paint.setStyle(SkPaint::kFill_Style);
    paint.setShader(gradient_shader(rect, *style.background_gradient));
    fill_shape(canvas, rect, style.radii, paint);
    paint.setShader(nullptr);
  } else if (style.fill.alpha() != 0) {
    paint.setStyle(SkPaint::kFill_Style);
    paint.setColor(to_sk_color(style.fill));
    fill_shape(canvas, rect, style.radii, paint);
  }

  paint_image(canvas, bounds, style.image, style.radii, images);

  if (style.border_width.is_zero() || style.border_color.alpha() == 0) {
    paint_text(canvas, bounds, style.text, fonts);
    return;
  }

  paint_border(canvas, rect, style, paint);

  // Text last, so a label reads over its own border rather than under it.
  paint_text(canvas, bounds, style.text, fonts);
}

}  // namespace dg::detail
