#include "render/paragraph_build.h"

#include <cstdint>
#include <string>
#include <utility>

#include "drawgui/base/utf8.h"

#include "include/core/SkColor.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkString.h"
#include "modules/skparagraph/include/FontCollection.h"
#include "modules/skparagraph/include/Paragraph.h"
#include "modules/skparagraph/include/ParagraphBuilder.h"
#include "modules/skparagraph/include/ParagraphStyle.h"
#include "modules/skunicode/include/SkUnicode.h"
#include "modules/skunicode/include/SkUnicode_libgrapheme.h"

#include "render/font_access.h"
#include "render/paragraph_runs.h"

namespace dg::detail {
namespace {

std::uint64_t& build_counter() {
  static std::uint64_t count = 0;
  return count;
}

SkColor to_sk_color(Color color) {
  return static_cast<SkColor>(color.argb());
}

skia::textlayout::TextAlign to_sk_text_align(TextAlign align) {
  switch (align) {
    case TextAlign::kLeft:
      return skia::textlayout::TextAlign::kLeft;
    case TextAlign::kRight:
      return skia::textlayout::TextAlign::kRight;
    case TextAlign::kCenter:
      return skia::textlayout::TextAlign::kCenter;
  }
  return skia::textlayout::TextAlign::kLeft;
}

}  // namespace

ParagraphCacheEntry::ParagraphCacheEntry() = default;
ParagraphCacheEntry::~ParagraphCacheEntry() = default;

std::uint64_t paragraph_build_count() {
  return build_counter();
}

std::unique_ptr<skia::textlayout::Paragraph> build_paragraph(const FontCatalog& fonts,
                                                             const TextStyle& text,
                                                             float width) {
  if (text.text.empty() || !fonts.holds(text.font) || text.size <= 0.0F) {
    return nullptr;
  }

  sk_sp<SkUnicode> unicode = SkUnicodes::Libgrapheme::Make();
  if (!unicode) {
    return nullptr;
  }

  // One collection per FontCatalog, shared by every paragraph built over it
  // (FontAccess::paragraph_collection()), so its typeface lookups and
  // shaping cache survive from one build to the next.
  sk_sp<skia::textlayout::FontCollection> collection = FontAccess::paragraph_collection(fonts);
  ++build_counter();

  skia::textlayout::TextStyle base_style;
  base_style.setFontSize(text.size);
  base_style.setColor(to_sk_color(text.color));
  if (!text.language.empty()) {
    base_style.setLocale(SkString(text.language));
  }

  skia::textlayout::ParagraphStyle paragraph_style;
  paragraph_style.setTextStyle(base_style);
  paragraph_style.setTextAlign(to_sk_text_align(text.align));
  if (text.max_lines > 0) {
    paragraph_style.setMaxLines(static_cast<std::size_t>(text.max_lines));
    paragraph_style.setEllipsis(SkString("..."));
  }

  std::unique_ptr<skia::textlayout::ParagraphBuilder> builder =
      skia::textlayout::ParagraphBuilder::make(paragraph_style, collection, unicode);
  if (!builder) {
    return nullptr;
  }

  for (const ParagraphRun& run : paragraph_runs(fonts, text)) {
    skia::textlayout::TextStyle run_style = base_style;
    run_style.setFontFamilies({SkString(run.family)});
    builder->pushStyle(run_style);
    if (run.invalid) {
      // dg::sanitize_utf8() (include/drawgui/base/utf8.h) is what does the
      // substitution now - one U+FFFD (the well-formed 3-byte UTF-8
      // encoding \xEF\xBF\xBD) per invalid byte, never the raw bytes
      // themselves, see ParagraphRun::invalid's comment for why the raw
      // bytes are not safe to hand SkParagraph. 7-2b promoted this from an
      // inline loop here into that shared function so its OWN new boundary
      // (WidgetSet::text_field_insert()) calls the identical policy rather than a second one -
      // this is an internal substitution to keep a third-party shaping
      // library from hanging on ill-formed input, not the ABI-level
      // "silently rewritten to U+FFFD" design.md section 5.13.3 forbids:
      // nothing here reports back to a caller, because nothing in this
      // measurement/paint path has an error channel to report through - the
      // plain SkFont path already draws a comparable stand-in (the primary's
      // .notdef box) for the identical input.
      const std::string replacement =
          sanitize_utf8(std::string_view{text.text}.substr(run.begin, run.end - run.begin));
      builder->addText(replacement.data(), replacement.size());
    } else {
      builder->addText(text.text.data() + run.begin, run.end - run.begin);
    }
    builder->pop();
  }

  std::unique_ptr<skia::textlayout::Paragraph> paragraph = builder->Build();
  if (!paragraph) {
    return nullptr;
  }
  paragraph->layout(width);
  return paragraph;
}

}  // namespace dg::detail
