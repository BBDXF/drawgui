#include "render/paragraph_build.h"

#include <utility>

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

  sk_sp<skia::textlayout::FontCollection> collection =
      sk_make_sp<skia::textlayout::FontCollection>();
  // ASSET, not "default": FontCollection only consults its "default" manager
  // for automatic fallback search (defaultFallback()/defaultEmojiFallback()),
  // never for an explicit family-name lookup - findTypefaces() walks asset,
  // dynamic and test managers instead. Every run below names an explicit
  // family (paragraph_runs.h), so this is the manager that has to be
  // registered here, not the other one.
  collection->setAssetFontManager(FontAccess::font_manager(fonts));

  // No FontCollection-driven fallback search: every run below already names
  // its own family, resolved by FontCatalog::resolve() the same way the
  // plain SkFont paint path resolves it (paragraph_runs.h). Leaving fallback
  // enabled would ask this project's SkFontMgr for
  // matchFamilyStyleCharacter(), which doc/font-fallback.md already measured
  // as always null here - so it would find nothing SkParagraph's own search
  // could not, and would cost a search on every run for it.
  collection->disableFontFallback();

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
      // One U+FFFD (the well-formed 3-byte UTF-8 encoding \xEF\xBF\xBD) per
      // invalid byte, never the raw bytes themselves - see ParagraphRun::
      // invalid's comment for why the raw bytes are not safe to hand
      // SkParagraph. This is an internal substitution to keep a third-party
      // shaping library from hanging on ill-formed input, not the ABI-level
      // "silently rewritten to U+FFFD" design.md section 5.13.3 forbids:
      // nothing here reports back to a caller, because nothing in this
      // measurement/paint path has an error channel to report through - the
      // plain SkFont path already draws a comparable stand-in (the primary's
      // .notdef box) for the identical input.
      std::string replacement;
      replacement.reserve((run.end - run.begin) * 3);
      for (std::size_t i = run.begin; i < run.end; ++i) {
        replacement.append("\xEF\xBF\xBD");
      }
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
