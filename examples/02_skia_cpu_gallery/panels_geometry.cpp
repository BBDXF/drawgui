// Shapes, strokes, anti-aliasing, transforms and clipping - the parts of a
// widget's appearance that are pure geometry.

#include "gallery.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "include/core/SkColor.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPathTypes.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkScalar.h"
#include "include/effects/SkDashPathEffect.h"

#include "panel_support.h"

namespace gallery {
namespace {

void caption(SkCanvas& canvas, const Resources& resources, float x, float y, const char* text) {
  if (resources.sans) {
    canvas.drawString(text, x, y, text_font(resources.sans, 10.0F), fill_paint(kInkDim));
  }
}

// A five-pointed star. SkPath is immutable in this Skia revision, so every
// path is assembled in an SkPathBuilder and detached; there is no moveTo on
// SkPath at all.
SkPath star(SkPoint centre, float outer, float inner) {
  SkPathBuilder builder;
  for (int i = 0; i < 10; ++i) {
    const float radius = (i % 2 == 0) ? outer : inner;
    const float angle = (static_cast<float>(i) * 3.14159265F / 5.0F) - (3.14159265F / 2.0F);
    const SkPoint point{centre.fX + (radius * std::cos(angle)),
                        centre.fY + (radius * std::sin(angle))};
    if (i == 0) {
      builder.moveTo(point);
    } else {
      builder.lineTo(point);
    }
  }
  return builder.close().detach();
}

void panel_shapes(SkCanvas& canvas, const Resources& resources, const SkRect& body) {
  const float row = body.height() * 0.5F;
  canvas.drawRect(SkRect::MakeXYWH(4.0F, 8.0F, 56.0F, 40.0F), fill_paint(kBlue));
  caption(canvas, resources, 4.0F, 62.0F, "rect");

  canvas.drawRRect(
      SkRRect::MakeRectXY(SkRect::MakeXYWH(72.0F, 8.0F, 56.0F, 40.0F), 12.0F, 12.0F),
      fill_paint(kGreen));
  caption(canvas, resources, 72.0F, 62.0F, "rounded rect");

  canvas.drawCircle(168.0F, 28.0F, 20.0F, fill_paint(kAmber));
  caption(canvas, resources, 148.0F, 62.0F, "circle");

  canvas.drawOval(SkRect::MakeXYWH(200.0F, 12.0F, 64.0F, 32.0F), fill_paint(kViolet));
  caption(canvas, resources, 200.0F, 62.0F, "oval");

  const SkRect arc_box = SkRect::MakeXYWH(6.0F, row + 6.0F, 52.0F, 52.0F);
  canvas.drawArc(arc_box, -90.0F, 250.0F, true, fill_paint(kRed));
  caption(canvas, resources, 4.0F, row + 72.0F, "arc (wedge)");

  canvas.drawArc(SkRect::MakeXYWH(78.0F, row + 6.0F, 52.0F, 52.0F), -90.0F, 250.0F, false,
                 stroke_paint(kBlue, 6.0F));
  caption(canvas, resources, 76.0F, row + 72.0F, "arc (stroked)");

  // An outer rounded rect minus an inner one, in a single call: the shape a
  // ring-style focus indicator or a donut chart is made of.
  canvas.drawDRRect(
      SkRRect::MakeRectXY(SkRect::MakeXYWH(152.0F, row + 6.0F, 56.0F, 52.0F), 14.0F, 14.0F),
      SkRRect::MakeRectXY(SkRect::MakeXYWH(164.0F, row + 18.0F, 32.0F, 28.0F), 8.0F, 8.0F),
      fill_paint(kGreen));
  caption(canvas, resources, 152.0F, row + 72.0F, "drawDRRect");
}

void panel_path(SkCanvas& canvas, const Resources& resources, const SkRect& body) {
  canvas.drawPath(star(SkPoint{44.0F, 44.0F}, 36.0F, 15.0F), fill_paint(kAmber));
  caption(canvas, resources, 8.0F, 92.0F, "star (winding)");

  SkPath even_odd = star(SkPoint{132.0F, 44.0F}, 36.0F, 15.0F);
  even_odd.setFillType(SkPathFillType::kEvenOdd);
  canvas.drawPath(even_odd, fill_paint(kViolet));
  caption(canvas, resources, 92.0F, 92.0F, "star (even-odd)");

  SkPathBuilder curve;
  curve.moveTo(8.0F, body.height() - 16.0F)
      .cubicTo(60.0F, body.height() - 92.0F, 140.0F, body.height() + 12.0F, body.width() - 8.0F,
               body.height() - 54.0F);
  canvas.drawPath(curve.detach(), stroke_paint(kBlue, 3.0F));
  caption(canvas, resources, 8.0F, body.height() - 4.0F, "cubic bezier, stroked");

  SkPathBuilder quad;
  quad.moveTo(180.0F, 16.0F).quadTo(body.width() - 4.0F, 44.0F, 180.0F, 76.0F);
  canvas.drawPath(quad.detach(), stroke_paint(kGreen, 3.0F));
  caption(canvas, resources, 196.0F, 92.0F, "quad");
}

void panel_stroke(SkCanvas& canvas, const Resources& resources, const SkRect& body) {
  float y = 12.0F;
  for (const float width : {1.0F, 2.0F, 4.0F, 8.0F}) {
    canvas.drawLine(6.0F, y, 96.0F, y, stroke_paint(kBlue, width));
    y += 18.0F;
  }
  caption(canvas, resources, 6.0F, y + 2.0F, "widths 1 / 2 / 4 / 8");

  const SkPaint::Cap caps[3] = {SkPaint::kButt_Cap, SkPaint::kRound_Cap, SkPaint::kSquare_Cap};
  const char* cap_names[3] = {"butt", "round", "square"};
  y = 12.0F;
  for (int i = 0; i < 3; ++i) {
    SkPaint paint = stroke_paint(kAmber, 11.0F);
    paint.setStrokeCap(caps[i]);
    canvas.drawLine(126.0F, y, 186.0F, y, paint);
    caption(canvas, resources, 192.0F, y + 4.0F, cap_names[i]);
    y += 24.0F;
  }

  const SkPaint::Join joins[3] = {SkPaint::kMiter_Join, SkPaint::kRound_Join,
                                  SkPaint::kBevel_Join};
  const char* join_names[3] = {"miter", "round", "bevel"};
  for (int i = 0; i < 3; ++i) {
    SkPaint paint = stroke_paint(kGreen, 9.0F);
    paint.setStrokeJoin(joins[i]);
    const float x = 12.0F + (static_cast<float>(i) * 74.0F);
    SkPathBuilder corner;
    corner.moveTo(x, body.height() - 16.0F)
        .lineTo(x + 24.0F, body.height() - 52.0F)
        .lineTo(x + 48.0F, body.height() - 16.0F);
    canvas.drawPath(corner.detach(), paint);
    caption(canvas, resources, x + 6.0F, body.height() - 2.0F, join_names[i]);
  }
}

void panel_dash(SkCanvas& canvas, const Resources& resources, const SkRect& body) {
  const SkScalar patterns[3][2] = {{10.0F, 6.0F}, {2.0F, 4.0F}, {18.0F, 5.0F}};
  const char* names[3] = {"10 on / 6 off", "2 on / 4 off (dotted)", "18 on / 5 off"};
  float y = 14.0F;
  for (int i = 0; i < 3; ++i) {
    SkPaint paint = stroke_paint(kBlue, 3.0F);
    paint.setPathEffect(SkDashPathEffect::Make({patterns[i], 2}, 0.0F));
    canvas.drawLine(6.0F, y, body.width() - 6.0F, y, paint);
    caption(canvas, resources, 6.0F, y + 14.0F, names[i]);
    y += 30.0F;
  }

  // Round caps on a dotted line is how a real dotted border is drawn; the
  // square-capped version above is a dashed one.
  SkPaint dotted = stroke_paint(kAmber, 4.0F);
  dotted.setStrokeCap(SkPaint::kRound_Cap);
  const SkScalar dots[2] = {0.0F, 10.0F};
  dotted.setPathEffect(SkDashPathEffect::Make({dots, 2}, 0.0F));
  canvas.drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(8.0F, y + 4.0F, body.width() - 16.0F,
                                                        body.height() - y - 24.0F),
                                       10.0F, 10.0F),
                   dotted);
  caption(canvas, resources, 8.0F, body.height() - 4.0F, "dashed rounded-rect border");
}

void panel_antialias(SkCanvas& canvas, const Resources& resources, const SkRect& body) {
  const float half = body.width() * 0.5F;
  for (int side = 0; side < 2; ++side) {
    const bool anti_alias = side == 1;
    const float x = static_cast<float>(side) * half;
    canvas.save();
    canvas.clipRect(SkRect::MakeXYWH(x, 0.0F, half, body.height()), false);

    canvas.drawCircle(x + 34.0F, 34.0F, 26.0F, fill_paint(kBlue, anti_alias));
    canvas.drawPath(star(SkPoint{x + 96.0F, 34.0F}, 28.0F, 12.0F),
                    fill_paint(kAmber, anti_alias));

    SkPaint diagonal = stroke_paint(kGreen, 3.0F, anti_alias);
    canvas.drawLine(x + 10.0F, 76.0F, x + half - 14.0F, 106.0F, diagonal);

    if (resources.sans) {
      SkFont font = text_font(resources.sans, 15.0F);
      font.setEdging(anti_alias ? SkFont::Edging::kAntiAlias : SkFont::Edging::kAlias);
      canvas.drawString("Handgloves", x + 10.0F, 130.0F, font, fill_paint(kInk));
    }
    caption(canvas, resources, x + 10.0F, body.height() - 4.0F,
            anti_alias ? "anti-aliased" : "aliased");
    canvas.restore();
  }
  canvas.drawLine(half, 0.0F, half, body.height(), stroke_paint(kCardEdge, 1.0F));
}

void panel_transform(SkCanvas& canvas, const Resources& resources, const SkRect& body) {
  const SkRect unit = SkRect::MakeXYWH(-18.0F, -12.0F, 36.0F, 24.0F);

  canvas.save();
  canvas.translate(34.0F, 30.0F);
  canvas.drawRect(unit, fill_paint(kBlue));
  canvas.restore();
  caption(canvas, resources, 10.0F, 54.0F, "translate");

  canvas.save();
  canvas.translate(112.0F, 30.0F);
  canvas.scale(1.6F, 0.9F);
  canvas.drawRect(unit, fill_paint(kGreen));
  canvas.restore();
  caption(canvas, resources, 88.0F, 54.0F, "scale");

  canvas.save();
  canvas.translate(200.0F, 30.0F);
  canvas.rotate(24.0F);
  canvas.drawRect(unit, fill_paint(kAmber));
  canvas.restore();
  caption(canvas, resources, 178.0F, 54.0F, "rotate");

  // The save/restore stack is how a widget tree renders: each level composes
  // its own transform onto its parent's and pops it again on the way out.
  canvas.save();
  canvas.translate(40.0F, body.height() - 56.0F);
  for (int depth = 0; depth < 5; ++depth) {
    canvas.translate(38.0F, 0.0F);
    canvas.rotate(14.0F);
    canvas.scale(0.94F, 0.94F);
    canvas.save();
    canvas.drawRRect(SkRRect::MakeRectXY(unit, 5.0F, 5.0F), fill_paint(kViolet));
    canvas.restore();
  }
  canvas.restore();
  caption(canvas, resources, 6.0F, body.height() - 4.0F, "nested save / restore stack");
}

void panel_clip(SkCanvas& canvas, const Resources& resources, const SkRect& body) {
  const float width = (body.width() - 16.0F) / 3.0F;
  const float height = body.height() - 22.0F;

  const auto stripes = [&canvas](float origin_x, float extent) {
    for (int i = 0; i < 22; ++i) {
      const float y = static_cast<float>(i) * 8.0F;
      canvas.drawRect(SkRect::MakeXYWH(origin_x, y, extent, 4.0F),
                      fill_paint((i % 2 == 0) ? kBlue : kAmber));
    }
  };

  canvas.save();
  canvas.clipRect(SkRect::MakeXYWH(0.0F, 0.0F, width, height), true);
  stripes(0.0F, width);
  canvas.restore();
  caption(canvas, resources, 0.0F, body.height() - 4.0F, "rect clip");

  // The clip a scroll container, a card and an avatar all need.
  canvas.save();
  canvas.clipRRect(
      SkRRect::MakeRectXY(SkRect::MakeXYWH(width + 8.0F, 0.0F, width, height), 16.0F, 16.0F),
      true);
  stripes(width + 8.0F, width);
  canvas.restore();
  caption(canvas, resources, width + 8.0F, body.height() - 4.0F, "rounded-rect clip");

  canvas.save();
  canvas.clipPath(star(SkPoint{(2.0F * width) + 16.0F + (width * 0.5F), height * 0.5F},
                       std::min(width, height) * 0.5F, std::min(width, height) * 0.22F),
                  true);
  stripes((2.0F * width) + 16.0F, width);
  canvas.restore();
  caption(canvas, resources, (2.0F * width) + 16.0F, body.height() - 4.0F, "path clip");
}

}  // namespace

std::vector<Panel> geometry_panels() {
  return {
      {"GEOMETRY - rect, rrect, circle, oval, arc", Category::kGeometry, panel_shapes},
      {"GEOMETRY - paths, fill types, beziers", Category::kGeometry, panel_path},
      {"PAINT - stroke width, caps, joins", Category::kGeometry, panel_stroke},
      {"PAINT - dashes (SkDashPathEffect)", Category::kGeometry, panel_dash},
      {"PAINT - anti-aliasing: off | on", Category::kGeometry, panel_antialias},
      {"TRANSFORM - translate, scale, rotate", Category::kGeometry, panel_transform},
      {"CLIP - rect, rounded rect, path", Category::kGeometry, panel_clip},
  };
}

}  // namespace gallery
