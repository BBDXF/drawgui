// 7-2: multi-line paragraph layout, CJK line breaking, and the "did this
// break L3" question, against fonts THIS BUILD GENERATES rather than the
// host's - the identical reason test_font_fallback.cpp gives, extended to a
// second consumer of the same generated font set.
//
// DgTest Latin is a monospaced test font: 600/1000 em advance at any size,
// so at size 20 every glyph (including a space) is EXACTLY 12 device pixels
// wide - doc/text-input.md section 6.2 already established this is what
// makes an expected pixel value something a reader can check by counting
// characters rather than trusting a rendered screenshot.

#include <cstddef>
#include <string>

#include <doctest/doctest.h>

#include "drawgui/render/font_catalog.h"
#include "drawgui/render/paragraph.h"
#include "drawgui/render/render_tree.h"

#include "render/paragraph_runs.h"

namespace {

using dg::FontCatalog;
using dg::FontId;
using dg::Paragraph;
using dg::TextAlign;
using dg::TextStyle;

FontCatalog make_catalog() {
  dg::Expected<FontCatalog, dg::FontError> catalog = FontCatalog::scan(DG_TEST_FONT_DIR);
  REQUIRE(catalog.has_value());
  return std::move(catalog).value();
}

TextStyle make_style(FontId font, std::string text, float size = 20.0F) {
  TextStyle style;
  style.text = std::move(text);
  style.font = font;
  style.size = size;
  style.color = dg::Color::from_argb(0xFFFFFFFF);
  style.align = TextAlign::kLeft;
  style.wrap = true;
  return style;
}

}  // namespace

TEST_CASE("paragraph: a string narrower than the width lays out on one line") {
  FontCatalog fonts = make_catalog();
  dg::Expected<FontId, dg::FontError> latin = fonts.add("DgTest Latin", false);
  REQUIRE(latin.has_value());

  const TextStyle style = make_style(latin.value(), "AB");
  dg::Expected<Paragraph, dg::FontError> built = Paragraph::build(fonts, style, 200.0F);
  REQUIRE(built.has_value());
  const dg::ParagraphMetrics metrics = built.value().metrics();
  CHECK(metrics.line_count == 1);
  CHECK_FALSE(metrics.exceeded_max_lines);
}

TEST_CASE("paragraph: a string wider than the width wraps to more than one line") {
  FontCatalog fonts = make_catalog();
  dg::Expected<FontId, dg::FontError> latin = fonts.add("DgTest Latin", false);
  REQUIRE(latin.has_value());

  // Ten "AB " triples (space-separated words), 8 glyphs of 12px = 96px each -
  // far more than fits on one 100px-wide line, so UAX#14's ordinary
  // space-based break must fire more than once.
  const TextStyle style = make_style(latin.value(), "AB AB AB AB AB AB AB AB AB AB");
  dg::Expected<Paragraph, dg::FontError> built = Paragraph::build(fonts, style, 100.0F);
  REQUIRE(built.has_value());
  const dg::ParagraphMetrics metrics = built.value().metrics();
  CHECK(metrics.line_count > 1);

  // The height grows with the line count - the "reflowing text is height-
  // determined-by-width" shape doc/text-layout.md section 2 measures. One
  // line at this font/size is a single, fixed line-box height; N lines is
  // N times that height (SkParagraph does not add inter-line leading here),
  // so a caller can predict the box height from the width alone.
  const TextStyle one_line_style = make_style(latin.value(), "AB");
  dg::Expected<Paragraph, dg::FontError> one_line =
      Paragraph::build(fonts, one_line_style, 100.0F);
  REQUIRE(one_line.has_value());
  const int single_line_height = one_line.value().metrics().height;
  CHECK(single_line_height > 0);
  CHECK(metrics.height == single_line_height * metrics.line_count);
}

TEST_CASE("paragraph: max_lines truncates and reports it was exceeded") {
  FontCatalog fonts = make_catalog();
  dg::Expected<FontId, dg::FontError> latin = fonts.add("DgTest Latin", false);
  REQUIRE(latin.has_value());

  TextStyle style = make_style(latin.value(), "AB AB AB AB AB AB AB AB AB AB");
  style.max_lines = 1;
  dg::Expected<Paragraph, dg::FontError> built = Paragraph::build(fonts, style, 100.0F);
  REQUIRE(built.has_value());
  const dg::ParagraphMetrics metrics = built.value().metrics();
  CHECK(metrics.line_count == 1);
  CHECK(metrics.exceeded_max_lines);
}

TEST_CASE(
    "paragraph: 16 unspaced Han characters wrap without a single space, at a "
    "width that forces more than one line (UAX#14, not whitespace)") {
  FontCatalog fonts = make_catalog();
  dg::Expected<FontId, dg::FontError> hans = fonts.add("DgTest Han Hans", false);
  REQUIRE(hans.has_value());

  // The same 16-codepoint string 7-1's smoke test already proved produces
  // more than 4 UAX#14 soft breaks at the SkUnicode level - this is that
  // capability consumed by a real paragraph rather than asserted on raw
  // flags. DgTest Han Hans covers exactly these four codepoints; the string
  // repeats them so the paragraph has enough width to wrap.
  const std::string cjk_text =
      "\u4e2d\u6d77\u76f4\u9aa8\u4e2d\u6d77\u76f4\u9aa8\u4e2d\u6d77\u76f4\u9aa8\u4e2d\u6d77"
      "\u76f4\u9aa8";
  const TextStyle style = make_style(hans.value(), cjk_text);
  // Narrow enough that a 16-character run cannot fit one line, wide enough
  // that at least two characters (24px) fit - so a rule table that were
  // silently absent (which would report zero break opportunities among CJK
  // text with no spaces) could not produce more than one line by accident.
  dg::Expected<Paragraph, dg::FontError> built = Paragraph::build(fonts, style, 60.0F);
  REQUIRE(built.has_value());
  CHECK(built.value().metrics().line_count > 1);
}

TEST_CASE(
    "paragraph: build() reports a fillable error rather than crashing on an "
    "invalid font id, empty text, or non-positive size") {
  FontCatalog fonts = make_catalog();
  dg::Expected<FontId, dg::FontError> latin = fonts.add("DgTest Latin", false);
  REQUIRE(latin.has_value());

  TextStyle invalid_font = make_style(FontId{}, "hi");
  CHECK_FALSE(Paragraph::build(fonts, invalid_font, 100.0F).has_value());

  TextStyle empty_text = make_style(latin.value(), "");
  CHECK_FALSE(Paragraph::build(fonts, empty_text, 100.0F).has_value());

  TextStyle zero_size = make_style(latin.value(), "hi", 0.0F);
  CHECK_FALSE(Paragraph::build(fonts, zero_size, 100.0F).has_value());
}

TEST_CASE(
    "paragraph: malformed UTF-8 is degraded to the primary family's run rather "
    "than crashing - a lone continuation byte, a truncated multi-byte lead, and "
    "an overlong encoding, each fed through Paragraph::build() directly") {
  FontCatalog fonts = make_catalog();
  dg::Expected<FontId, dg::FontError> latin = fonts.add("DgTest Latin", false);
  REQUIRE(latin.has_value());

  // A lone continuation byte (0x80..0xBF with nothing before it).
  const TextStyle lone_continuation = make_style(latin.value(), std::string{"A\x80"
                                                                            "B"});
  CHECK(Paragraph::build(fonts, lone_continuation, 100.0F).has_value());

  // A truncated three-byte lead (0xE0 wants two continuation bytes; none
  // follow before the string ends).
  const TextStyle truncated = make_style(latin.value(), std::string{"A\xE0"});
  CHECK(Paragraph::build(fonts, truncated, 100.0F).has_value());

  // An overlong two-byte encoding of NUL (0xC0 0x80) - a codepoint that has a
  // shorter valid encoding, which utf8_decode() rejects per design.md
  // section 5.13.3 rather than accepting.
  const TextStyle overlong = make_style(latin.value(), std::string{"A\xC0\x80"
                                                                   "B"});
  CHECK(Paragraph::build(fonts, overlong, 100.0F).has_value());
}

TEST_CASE(
    "paragraph_runs: groups by resolved family name, not by codepoint, and a "
    "codepoint nothing covers still gets the primary's own family") {
  FontCatalog fonts = make_catalog();
  dg::Expected<FontId, dg::FontError> latin = fonts.add("DgTest Latin", false);
  REQUIRE(latin.has_value());

  TextStyle style;
  style.font = latin.value();
  style.language = "zh-Hans";
  // "AB" (DgTest Latin covers it) + U+4E2D (DgTest Latin does NOT cover it;
  // nothing is registered under zh-Hans in this scan, so it falls through to
  // the pool - or, if nothing in the pool covers it either, to "missing",
  // which must still carry the PRIMARY's family name rather than an empty
  // one).
  style.text = "AB\u4e2d";

  const std::vector<dg::detail::ParagraphRun> runs = dg::detail::paragraph_runs(fonts, style);
  REQUIRE(runs.size() == 2);
  CHECK(runs[0].family == "DgTest Latin");
  CHECK(runs[0].begin == 0);
  CHECK(runs[0].end == 2);
  // Whatever face ends up covering (or not covering) U+4E2D, the run's
  // family name is never empty - the whole point of family_name() as the
  // fallback for the "missing" case.
  CHECK_FALSE(runs[1].family.empty());
  CHECK(runs[1].begin == 2);
  CHECK(runs[1].end == 5);
}

TEST_CASE("paragraph_runs: a valid byte immediately beside an invalid one, both "
         "resolving to the same (primary) family, does NOT merge into a single "
         "run marked valid") {
  // Defect-injection finding: the merge condition once compared only
  // `family`, not `invalid` too. Because an invalid step's family defaults to
  // the PRIMARY (the same family "A" already resolves to), "A" followed by a
  // lone continuation byte merged into ONE run whose `invalid` flag was
  // whatever the FIRST character set - false - so build_paragraph() handed
  // the raw invalid byte straight to SkParagraph and hung (see doc/text-
  // layout.md section 5). This is the direct, function-level regression test
  // for that boundary; the malformed-UTF-8 Paragraph::build() test above
  // caught the SAME defect only through the hang it causes two layers up.
  FontCatalog fonts = make_catalog();
  dg::Expected<FontId, dg::FontError> latin = fonts.add("DgTest Latin", false);
  REQUIRE(latin.has_value());

  TextStyle style;
  style.font = latin.value();
  style.text = std::string{"A\x80" "B"};

  const std::vector<dg::detail::ParagraphRun> runs = dg::detail::paragraph_runs(fonts, style);
  REQUIRE(runs.size() == 3);
  CHECK_FALSE(runs[0].invalid);
  CHECK(runs[0].begin == 0);
  CHECK(runs[0].end == 1);
  CHECK(runs[1].invalid);
  CHECK(runs[1].begin == 1);
  CHECK(runs[1].end == 2);
  CHECK_FALSE(runs[2].invalid);
  CHECK(runs[2].begin == 2);
  CHECK(runs[2].end == 3);
}
