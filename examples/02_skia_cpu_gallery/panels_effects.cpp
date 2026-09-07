// Compositing, gradients, blur and images - the group that is cheap on a GPU
// and is the reason this measurement exists on a CPU.

#include "gallery.h"

#include <vector>

#include "include/core/SkBlendMode.h"
#include "include/core/SkBlurTypes.h"
#include "include/core/SkColor.h"
#include "include/core/SkFont.h"
#include "include/core/SkMaskFilter.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkScalar.h"
#include "include/core/SkShader.h"
#include "include/core/SkTileMode.h"
#include "include/effects/SkGradient.h"
#include "include/effects/SkImageFilters.h"

#include "panel_support.h"

namespace gallery {
namespace {

void caption(SkCanvas& canvas, const Resources& resources, float x, float y, const char* text) {
  if (resources.sans) {
    canvas.drawString(text, x, y, text_font(resources.sans, 10.0F), fill_paint(kInkDim));
  }
}

void three_discs(SkCanvas& canvas, float origin_x, float origin_y, const SkPaint& paint) {
  SkPaint blue = paint;
  blue.setColor(SkColorSetA(kBlue, paint.getAlpha()));
  SkPaint amber = paint;
  amber.setColor(SkColorSetA(kAmber, paint.getAlpha()));
  SkPaint green = paint;
  green.setColor(SkColorSetA(kGreen, paint.getAlpha()));

  canvas.drawCircle(origin_x, origin_y, 22.0F, blue);
  canvas.drawCircle(origin_x + 24.0F, origin_y, 22.0F, amber);
  canvas.drawCircle(origin_x + 12.0F, origin_y + 20.0F, 22.0F, green);
}

// The difference this panel exists to show: three half-transparent shapes
// blend with each other, whereas one half-transparent GROUP does not. The
// second is what a fade animation on a widget subtree has to do, and it is
// why saveLayer is both necessary and expensive.
void panel_alpha(SkCanvas& canvas, const Resources& resources, const SkRect& body) {
  SkPaint opaque = fill_paint(SK_ColorWHITE);
  three_discs(canvas, 34.0F, 32.0F, opaque);
  caption(canvas, resources, 8.0F, 82.0F, "opaque");

  SkPaint half = fill_paint(SK_ColorWHITE);
  half.setAlpha(128);
  three_discs(canvas, 118.0F, 32.0F, half);
  caption(canvas, resources, 92.0F, 82.0F, "alpha 0.5 each");

  const SkRect group = SkRect::MakeXYWH(180.0F, 4.0F, 90.0F, 76.0F);
  canvas.saveLayerAlphaf(&group, 0.5F);
  three_discs(canvas, 202.0F, 32.0F, opaque);
  canvas.restore();
  caption(canvas, resources, 178.0F, 82.0F, "group alpha 0.5");

  caption(canvas, resources, 8.0F, body.height() - 4.0F,
          "the middle group blends with itself; the right one does not");
}

void panel_blend(SkCanvas& canvas, const Resources& resources, const SkRect& body) {
  const SkBlendMode modes[4] = {SkBlendMode::kSrcOver, SkBlendMode::kMultiply,
                                SkBlendMode::kScreen, SkBlendMode::kOverlay};
  const char* names[4] = {"SrcOver", "Multiply", "Screen", "Overlay"};
  const float width = body.width() / 4.0F;

  for (int i = 0; i < 4; ++i) {
    const float x = static_cast<float>(i) * width;
    canvas.drawRect(SkRect::MakeXYWH(x + 4.0F, 6.0F, width - 12.0F, 54.0F), fill_paint(kBlue));
    SkPaint paint = fill_paint(kAmber);
    paint.setBlendMode(modes[i]);
    canvas.drawCircle(x + (width * 0.5F), 44.0F, 22.0F, paint);
    caption(canvas, resources, x + 6.0F, 78.0F, names[i]);
  }
  caption(canvas, resources, 4.0F, body.height() - 4.0F,
          "design.md 5.3.3 admits only these four");
}

void panel_gradient(SkCanvas& canvas, const Resources& resources, const SkRect& body) {
  const SkColor4f warm[3] = {
      SkColor4f::FromColor(0xFFE74C3C),
      SkColor4f::FromColor(0xFFF6C445),
      SkColor4f::FromColor(0xFF27AE60),
  };
  const SkGradient gradient{
      SkGradient::Colors{SkSpan<const SkColor4f>{warm, 3}, SkTileMode::kClamp},
      SkGradient::Interpolation{}};

  const SkRect linear_box = SkRect::MakeXYWH(6.0F, 6.0F, body.width() - 12.0F, 46.0F);
  const SkPoint across[2] = {{linear_box.left(), 0.0F}, {linear_box.right(), 0.0F}};
  SkPaint linear = fill_paint(SK_ColorWHITE);
  linear.setShader(SkShaders::LinearGradient(across, gradient));
  canvas.drawRRect(SkRRect::MakeRectXY(linear_box, 6.0F, 6.0F), linear);
  caption(canvas, resources, 6.0F, 64.0F, "linear gradient");

  const SkPoint centre{60.0F, body.height() - 46.0F};
  SkPaint radial = fill_paint(SK_ColorWHITE);
  radial.setShader(SkShaders::RadialGradient(centre, 38.0F, gradient));
  canvas.drawCircle(centre, 38.0F, radial);
  caption(canvas, resources, 24.0F, body.height() - 4.0F, "radial gradient");

  // A gradient under a rounded-rect clip is the shape of essentially every
  // modern button, so it is worth showing them composed rather than apart.
  canvas.save();
  const SkRect chip =
      SkRect::MakeXYWH(126.0F, body.height() - 78.0F, body.width() - 132.0F, 50.0F);
  canvas.clipRRect(SkRRect::MakeRectXY(chip, 25.0F, 25.0F), true);
  const SkPoint chip_axis[2] = {{chip.left(), chip.top()}, {chip.right(), chip.bottom()}};
  SkPaint chip_paint = fill_paint(SK_ColorWHITE);
  chip_paint.setShader(SkShaders::LinearGradient(chip_axis, gradient));
  canvas.drawRect(chip, chip_paint);
  canvas.restore();
  caption(canvas, resources, 126.0F, body.height() - 4.0F, "gradient + rounded clip");
}

// The expensive one. Both forms are shown because they cost very differently:
// a mask-filter blur blurs one shape's coverage, while an image-filter drop
// shadow forces a saveLayer and blurs a whole rasterized layer.
void panel_blur(SkCanvas& canvas, const Resources& resources, const SkRect& body) {
  SkPaint glow = fill_paint(kBlue);
  glow.setMaskFilter(SkMaskFilter::MakeBlur(kNormal_SkBlurStyle, 8.0F, false));
  canvas.drawRRect(
      SkRRect::MakeRectXY(SkRect::MakeXYWH(14.0F, 12.0F, 76.0F, 44.0F), 10.0F, 10.0F), glow);
  caption(canvas, resources, 14.0F, 72.0F, "mask-filter blur");

  SkPaint shadow = fill_paint(0xFF3E4756);
  shadow.setImageFilter(
      SkImageFilters::DropShadow(3.0F, 5.0F, 5.0F, 5.0F, 0xC0000000, nullptr));
  canvas.drawRRect(
      SkRRect::MakeRectXY(SkRect::MakeXYWH(116.0F, 12.0F, 90.0F, 44.0F), 10.0F, 10.0F), shadow);
  canvas.drawRRect(
      SkRRect::MakeRectXY(SkRect::MakeXYWH(116.0F, 12.0F, 90.0F, 44.0F), 10.0F, 10.0F),
      stroke_paint(kCardEdge, 1.0F));
  caption(canvas, resources, 116.0F, 72.0F, "drop shadow (image filter)");

  const SkRect frosted =
      SkRect::MakeXYWH(10.0F, body.height() - 74.0F, body.width() - 20.0F, 52.0F);
  SkPaint stripe = fill_paint(kViolet);
  for (int i = 0; i < 9; ++i) {
    canvas.drawRect(SkRect::MakeXYWH(frosted.left() + (static_cast<float>(i) * 26.0F),
                                     frosted.top(), 13.0F, frosted.height()),
                    stripe);
  }
  SkPaint blurred;
  blurred.setImageFilter(SkImageFilters::Blur(6.0F, 6.0F, nullptr));
  canvas.saveLayer(&frosted, &blurred);
  canvas.drawRect(
      SkRect::MakeXYWH(frosted.left(), frosted.top(), frosted.width() * 0.5F, frosted.height()),
      fill_paint(kAmber));
  canvas.restore();
  caption(canvas, resources, 10.0F, body.height() - 4.0F, "saveLayer + blur image filter");
}

void panel_image(SkCanvas& canvas, const Resources& resources, const SkRect& body) {
  if (!resources.photo) {
    caption(canvas, resources, 6.0F, 20.0F, "the PNG did not decode");
    return;
  }

  constexpr float kThumb = 74.0F;
  const SkRect nearest = SkRect::MakeXYWH(4.0F, 0.0F, kThumb, kThumb);
  const SkRect cubic = SkRect::MakeXYWH(kThumb + 12.0F, 0.0F, kThumb, kThumb);

  canvas.drawImageRect(resources.photo, nearest, SkSamplingOptions{SkFilterMode::kNearest},
                       nullptr);
  caption(canvas, resources, 4.0F, kThumb + 12.0F, "nearest");

  // Mitchell cubic is what design.md section 5.3.4 fixes for the project.
  canvas.drawImageRect(resources.photo, cubic, SkSamplingOptions{SkCubicResampler::Mitchell()},
                       nullptr);
  caption(canvas, resources, kThumb + 12.0F, kThumb + 12.0F, "Mitchell cubic");

  canvas.save();
  const SkRect avatar = SkRect::MakeXYWH(6.0F, kThumb + 22.0F, 44.0F, 44.0F);
  canvas.clipRRect(SkRRect::MakeOval(avatar), true);
  canvas.drawImageRect(resources.photo, avatar, SkSamplingOptions{SkCubicResampler::Mitchell()},
                       nullptr);
  canvas.restore();
  caption(canvas, resources, 6.0F, body.height() - 5.0F, "avatar");

  canvas.save();
  canvas.translate(104.0F, kThumb + 44.0F);
  canvas.rotate(-12.0F);
  canvas.drawImageRect(resources.photo, SkRect::MakeXYWH(-22.0F, -22.0F, 44.0F, 44.0F),
                       SkSamplingOptions{SkCubicResampler::Mitchell()}, nullptr);
  canvas.restore();
  caption(canvas, resources, 78.0F, body.height() - 5.0F, "rotated + sampled");
}

}  // namespace

std::vector<Panel> effect_panels() {
  return {
      {"COMPOSITING - object vs group alpha", Category::kEffects, panel_alpha},
      {"COMPOSITING - blend modes", Category::kEffects, panel_blend},
      {"SHADER - linear and radial gradients", Category::kEffects, panel_gradient},
      {"EFFECT - blur and drop shadow", Category::kEffects, panel_blur},
      {"IMAGE - decode, scale, sampling", Category::kEffects, panel_image},
  };
}

}  // namespace gallery
