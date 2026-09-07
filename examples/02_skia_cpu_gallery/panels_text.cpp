// Text. The requirement a GUI toolkit lives or dies on, and the one where a
// naive stack passes every ASCII test and then draws boxes.

#include "gallery.h"

#include <cstring>
#include <string>
#include <vector>

#include "include/core/SkColor.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkFontTypes.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"
#include "include/core/SkScalar.h"

#include "panel_support.h"

namespace gallery {
namespace {

// Chinese, because the project's author works in Chinese and CJK is where a
// text stack that only ever saw Latin falls over. Held as UTF-8 in the
// source, which is the only encoding that crosses drawgui's boundary
// (design.md section 5.13.1).
constexpr const char* kCjkSample = "你好，世界";
constexpr const char* kCjkSentence = "自绘 GUI 内核，用 C ABI 嵌入任何语言。";
constexpr const char* kCjkMixed = "中文 mixed with English 123";

void caption(SkCanvas& canvas, const Resources& resources, float x, float y, const char* text) {
  if (resources.sans) {
    canvas.drawString(text, x, y, text_font(resources.sans, 10.0F), fill_paint(kInkDim));
  }
}

void panel_sizes(SkCanvas& canvas, const Resources& resources, const SkRect& body) {
  if (!resources.sans) {
    return;
  }
  float y = 14.0F;
  for (const float size : {10.0F, 12.0F, 14.0F, 18.0F, 24.0F, 32.0F}) {
    const SkFont font = text_font(resources.sans, size);
    canvas.drawString("Handgloves 0123", 6.0F, y + size, font, fill_paint(kInk));
    canvas.drawString(std::to_string(static_cast<int>(size)).c_str(), body.width() - 20.0F,
                      y + size, text_font(resources.sans, 10.0F), fill_paint(kInkDim));
    y += size + 6.0F;
  }
}

void panel_styles(SkCanvas& canvas, const Resources& resources, const SkRect& /*body*/) {
  struct Sample {
    const sk_sp<SkTypeface>* typeface;
    const char* label;
  };
  const Sample samples[4] = {
      {&resources.sans, "sans regular"},
      {&resources.sans_bold, "sans bold"},
      {&resources.serif, "serif"},
      {&resources.mono, "monospace"},
  };

  float y = 14.0F;
  for (const Sample& sample : samples) {
    if (*sample.typeface) {
      canvas.drawString("Quick brown fox", 6.0F, y + 16.0F, text_font(*sample.typeface, 17.0F),
                        fill_paint(kInk));
    }
    caption(canvas, resources, 6.0F, y + 30.0F, sample.label);
    y += 38.0F;
  }
}

// Layout in step 3 depends on being able to ask how wide a string is before
// drawing it, so the answer is drawn on top of the string it describes rather
// than merely printed.
void panel_metrics(SkCanvas& canvas, const Resources& resources, const SkRect& /*body*/) {
  if (!resources.sans) {
    return;
  }
  const char* sample = "Measure me";
  const SkFont font = text_font(resources.sans, 26.0F);

  SkRect bounds{};
  const SkScalar advance =
      font.measureText(sample, std::strlen(sample), SkTextEncoding::kUTF8, &bounds);

  SkFontMetrics metrics{};
  const SkScalar line_spacing = font.getMetrics(&metrics);

  const float origin_x = 12.0F;
  const float baseline = 52.0F;

  // The advance box (what layout reserves) and the tight ink bounds (what is
  // actually marked) are different rectangles, and confusing them is how text
  // ends up clipped by one pixel on descenders.
  canvas.drawRect(SkRect::MakeXYWH(origin_x, baseline + metrics.fAscent, advance,
                                   metrics.fDescent - metrics.fAscent),
                  fill_paint(0x332E86DE));
  canvas.drawRect(bounds.makeOffset(origin_x, baseline), stroke_paint(kAmber, 1.0F));
  canvas.drawLine(origin_x - 6.0F, baseline, origin_x + advance + 6.0F, baseline,
                  stroke_paint(kRed, 1.0F));
  canvas.drawString(sample, origin_x, baseline, font, fill_paint(kInk));

  caption(canvas, resources, origin_x, baseline + 26.0F, "red = baseline, amber = ink bounds");
  caption(canvas, resources, origin_x, baseline + 38.0F, "blue = advance x ascent..descent");

  const SkFont small = text_font(resources.sans, 11.0F);
  float y = baseline + 58.0F;
  for (const std::string& line :
       {"advance   " + std::to_string(advance),
        "bounds    " + std::to_string(bounds.width()) + " x " + std::to_string(bounds.height()),
        "ascent    " + std::to_string(metrics.fAscent),
        "descent   " + std::to_string(metrics.fDescent),
        "spacing   " + std::to_string(line_spacing)}) {
    canvas.drawString(line.c_str(), origin_x, y, small, fill_paint(kInkDim));
    y += 13.0F;
  }
}

void panel_cjk(SkCanvas& canvas, const Resources& resources, const SkRect& body) {
  if (!resources.cjk) {
    caption(canvas, resources, 6.0F, 20.0F, "no CJK typeface was found on this host");
    return;
  }

  canvas.drawString(kCjkSample, 6.0F, 34.0F, text_font(resources.cjk, 28.0F), fill_paint(kInk));
  canvas.drawString(kCjkSample, 6.0F, 60.0F, text_font(resources.cjk, 18.0F),
                    fill_paint(kBlue));
  canvas.drawString(kCjkSentence, 6.0F, 84.0F, text_font(resources.cjk, 14.0F),
                    fill_paint(kInk));
  canvas.drawString(kCjkMixed, 6.0F, 106.0F, text_font(resources.cjk, 14.0F),
                    fill_paint(kGreen));

  // Measuring CJK matters as much as drawing it: a wrapper that assumes one
  // glyph is one byte gets the width of this string wrong by a factor of
  // three.
  const SkFont font = text_font(resources.cjk, 14.0F);
  const SkScalar advance =
      font.measureText(kCjkSentence, std::strlen(kCjkSentence), SkTextEncoding::kUTF8, nullptr);
  const std::string note = std::to_string(std::strlen(kCjkSentence)) + " UTF-8 bytes, " +
                           std::to_string(static_cast<int>(advance)) + " px wide";
  caption(canvas, resources, 6.0F, 124.0F, note.c_str());
  caption(canvas, resources, 6.0F, body.height() - 4.0F,
          "selected by family name - no fallback chain here");
}

}  // namespace

std::vector<Panel> text_panels() {
  return {
      {"TEXT - sizes 10 to 32", Category::kText, panel_sizes},
      {"TEXT - weights and families", Category::kText, panel_styles},
      {"TEXT - measurement (measureText)", Category::kText, panel_metrics},
      {"TEXT - CJK 中文", Category::kText, panel_cjk},
  };
}

}  // namespace gallery
