// A smoke test for slice 7-1, not a feature test.
//
// 7-1's entire job is to swap the prebuilt Skia asset for libskia2, which
// carries SkParagraph, SkShaper and SkUnicode(libgrapheme), and to prove the
// swap is a real foundation rather than archives sitting unused next to
// libskia.a. design.md's own rule ("接口必须在至少一个跑通的实现之后才写")
// applies to a dependency exactly as it does to an interface: this file is
// that one working call, not the multiline/BiDi/shaping feature that
// consumes it - those are 7-2 and stay out of this file on purpose.
//
// The backend is libgrapheme, not ICU: libskia2 does not ship a
// `libskunicode_icu.a` at all (see cmake/FetchSkia2.cmake / doc/skia-dependency.md),
// so `SkUnicodes::ICU::Make()` from an earlier, aborted draft of this file
// does not link against it. `SkUnicodes::Libgrapheme::Make()` is the real
// entry point this project's dependency actually provides.
//
// The font manager is `SkFontMgr_New_Custom_Directory` over the SAME
// generated test-font directory tests/fonts/gen_test_fonts.py already
// produces for test_font_fallback.cpp (DG_TEST_FONT_DIR), not a system font.
// A system font would make this test's answer a property of the machine
// running it - the reason the font-fallback tests never do that either.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "include/core/SkFontMgr.h"
#include "include/ports/SkFontMgr_directory.h"
#include "include/private/SkTArray.h"
#include "modules/skparagraph/include/FontCollection.h"
#include "modules/skparagraph/include/Paragraph.h"
#include "modules/skparagraph/include/ParagraphBuilder.h"
#include "modules/skparagraph/include/ParagraphStyle.h"
#include "modules/skunicode/include/SkUnicode.h"
#include "modules/skunicode/include/SkUnicode_libgrapheme.h"

namespace {

// Counts how many code units in `flags` carry `wanted`, mirroring the exact
// technique libskia2's own tests/smoke/smoke.cpp uses for the identical
// question. Duplicating it here rather than trusting that upstream file (or
// the README table it feeds) is the point: design.md section 12 open
// question 5 is settled against THIS built archive, first-party.
size_t count_flag(const skia_private::TArray<SkUnicode::CodeUnitFlags, true>& flags,
                   SkUnicode::CodeUnitFlags wanted) {
  size_t n = 0;
  for (int i = 0; i < flags.size(); ++i) {
    if (flags[i] & wanted) {
      ++n;
    }
  }
  return n;
}

}  // namespace

TEST_CASE("skia textlayout foundation: SkUnicode libgrapheme backend initializes") {
  sk_sp<SkUnicode> unicode = SkUnicodes::Libgrapheme::Make();
  REQUIRE(unicode != nullptr);
}

TEST_CASE("skia textlayout foundation: SkParagraph links, builds and lays out") {
  sk_sp<SkUnicode> unicode = SkUnicodes::Libgrapheme::Make();
  REQUIRE(unicode != nullptr);

  sk_sp<skia::textlayout::FontCollection> font_collection =
      sk_make_sp<skia::textlayout::FontCollection>();
  font_collection->setDefaultFontManager(SkFontMgr_New_Custom_Directory(DG_TEST_FONT_DIR));

  skia::textlayout::TextStyle text_style;
  text_style.setFontSize(16.0F);
  text_style.setFontFamilies({SkString("DgTest Latin")});

  skia::textlayout::ParagraphStyle paragraph_style;
  paragraph_style.setTextStyle(text_style);

  std::unique_ptr<skia::textlayout::ParagraphBuilder> builder =
      skia::textlayout::ParagraphBuilder::make(paragraph_style, font_collection, unicode);
  REQUIRE(builder != nullptr);

  builder->addText("Hello");
  std::unique_ptr<skia::textlayout::Paragraph> paragraph = builder->Build();
  REQUIRE(paragraph != nullptr);

  paragraph->layout(200.0F);

  // The proof this is a real dependency and not a linked-but-inert archive:
  // a genuine glyph run against a genuine (test) font produces a non-zero
  // line height and exactly one line at this width. Nothing here checks
  // multiline wrapping, shaping correctness or BiDi - that is 7-2's job.
  CHECK(paragraph->getHeight() > 0.0);
  CHECK(paragraph->lineNumber() == 1);
}

// The three cases below settle design.md section 12 open question 5 and the
// section 5.10.5-vs-5.13 tension against THIS built archive rather than
// against libskia2's own README claims. design.md section 5.13.6 states
// flatly that CJK line-break rule tables must not be trimmed away when ICU
// data is trimmed (section 5.10.5); libgrapheme dissolves the tension
// because there is no icudtl.dat to trim in the first place - see
// doc/skia-dependency.md for the full settlement writeup, including the
// runtime strace evidence that no icudtl.dat is ever opened.

TEST_CASE("skia textlayout foundation: grapheme clusters (ZWJ family emoji count as one)") {
  sk_sp<SkUnicode> unicode = SkUnicodes::Libgrapheme::Make();
  REQUIRE(unicode != nullptr);

  // design.md section 5.10.2 names this exact family emoji as the
  // grapheme-cluster requirement's own example: 7 code points joined by ZWJ,
  // one cursor/backspace unit. computeCodeUnitFlags marks kGraphemeStart at
  // every cluster boundary plus the string's end, so N clusters produce
  // N + 1 marks.
  const std::string family_emoji =
      "\U0001F468\u200D\U0001F469\u200D\U0001F467\u200D\U0001F466";
  skia_private::TArray<SkUnicode::CodeUnitFlags, true> flags;
  const bool ok = unicode->computeCodeUnitFlags(
      const_cast<char*>(family_emoji.data()), static_cast<int>(family_emoji.size()), false, &flags);
  REQUIRE(ok);
  CHECK(count_flag(flags, SkUnicode::kGraphemeStart) == 2);
}

TEST_CASE("skia textlayout foundation: CJK line breaking without spaces (UAX#14)") {
  sk_sp<SkUnicode> unicode = SkUnicodes::Libgrapheme::Make();
  REQUIRE(unicode != nullptr);

  // design.md section 5.13.6: CJK has no spaces to break on, so this is
  // entirely dependent on the UAX#14 rule table libgrapheme must carry
  // in-process (section 5.10.5's trim question). 16 unspaced Han characters;
  // a rule table that is silently absent gives at most a start/end mark and
  // nothing in between, not "more than a handful".
  const std::string cjk_text =
      "\u4e2d\u6587\u6ca1\u6709\u7a7a\u683c\u6240\u4ee5\u65ad\u884c\u5b8c\u5168\u4f9d\u8d56\u89c4\u5219\u8868";
  skia_private::TArray<SkUnicode::CodeUnitFlags, true> flags;
  const bool ok = unicode->computeCodeUnitFlags(
      const_cast<char*>(cjk_text.data()), static_cast<int>(cjk_text.size()), false, &flags);
  REQUIRE(ok);
  const size_t soft_breaks = count_flag(flags, SkUnicode::kSoftLineBreakBefore);
  CHECK(soft_breaks > 4);
}

TEST_CASE("skia textlayout foundation: BiDi produces an RTL region for Arabic-in-Latin text") {
  sk_sp<SkUnicode> unicode = SkUnicodes::Libgrapheme::Make();
  REQUIRE(unicode != nullptr);

  // design.md section 5.13.7: text BiDi is supported ("near-free" via
  // SkParagraph + the Unicode backend); what libgrapheme substitutes for
  // full ICU is its own bundled icu_bidi subset (see doc/skia-dependency.md
  // for the nm(1) evidence that no *external* ICU symbol is ever
  // undefined). A BidiLevel is RTL when it is odd.
  const std::string mixed_text = "hello \u0645\u0631\u062D\u0628\u0627 world";
  std::vector<SkUnicode::BidiRegion> regions;
  const bool ok = unicode->getBidiRegions(mixed_text.data(), static_cast<int>(mixed_text.size()),
                                           SkUnicode::TextDirection::kLTR, &regions);
  REQUIRE(ok);
  CHECK(regions.size() >= 2);
  bool has_rtl = false;
  for (const SkUnicode::BidiRegion& region : regions) {
    if (region.level % 2 == 1) {
      has_rtl = true;
    }
  }
  CHECK(has_rtl);
}
