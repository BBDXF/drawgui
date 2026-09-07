// Font fallback, resolved against fonts THIS BUILD GENERATED.
//
// Every assertion here would be host-dependent if it named a system font, and
// the usual escape - "the call returned non-null" - is exactly the assertion a
// tofu box also satisfies. So tests/fonts/gen_test_fonts.py emits four tiny
// fonts with coverage written down in one place, and this file pins exact
// family names, exact glyph ids and exact pixels against them.
//
// The pair that carries the weight is DgTest Han Hans and DgTest Han Ja. They
// cover EXACTLY the same four codepoints with DIFFERENT outlines - a low bar
// and a high bar. If the language tag ever stopped selecting the chain, both
// would draw the same rectangle and the Han-unification case below would fail
// on pixels rather than on a family name that happened to change.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/render/font_catalog.h"
#include "drawgui/render/render_tree.h"

namespace {

using dg::FontCatalog;
using dg::FontFallbackRule;
using dg::FontId;
using dg::FontResolution;

constexpr int kWidth = 96;
constexpr int kHeight = 96;
constexpr std::uint32_t kBackground = 0xFF000000;
constexpr std::uint32_t kInk = 0xFFFFFFFF;

// The codepoints the generated fonts cover, named so an assertion reads.
constexpr char32_t kLatinA = U'A';
constexpr char32_t kGreekAlpha = 0x03B1;
constexpr char32_t kHanZhong = 0x4E2D;
constexpr char32_t kRunicFehu = 0x16A0;

// Emoji-range, covered by DgTest Rare only, which is NOT a colour font. This
// is the codepoint that exercises falling out of the colour-first step.
constexpr char32_t kAstralNonColour = 0x1F900;

// Emoji-range, covered by BOTH the primary (monochrome) and DgTest Colour.
// The colour face must win - this is the DejaVu Sans / Noto Color Emoji trap
// in miniature.
constexpr char32_t kEmojiGrin = 0x1F600;

// Covered by nothing in the generated set. Cyrillic Komi De: deliberately not
// a codepoint any of the four fonts has, and deliberately not in the emoji
// range either, so it exercises the plain end of the walk.
constexpr char32_t kUncovered = 0x0500;

// Glyph ids are pinned rather than merely checked non-zero. The generator
// assigns ids in sorted codepoint order starting at 1, so DgTest Latin covers
// U+0020..U+007E as glyphs 1..95 and U+03B1 as 96. If the resolver ever
// returned the right FAMILY but the wrong FACE, a non-zero check would pass
// and these would not.
constexpr std::uint16_t kLatinAGlyph = 34;  // 'A' is 0x41, the 34th of 0x20..
constexpr std::uint16_t kGreekAlphaGlyph = 96;
constexpr std::uint16_t kHanZhongGlyph = 1;  // first of the four Han codepoints
constexpr std::uint16_t kRunicGlyph = 1;     // DgTest Rare: U+16A0 then U+1F900
constexpr std::uint16_t kAstralGlyph = 2;
constexpr std::uint16_t kColourEmojiGlyph = 1;  // DgTest Colour has only it

// dg::Expected rather than std::optional throughout this file, for a reason
// that is not style: clang-tidy's bugprone-unchecked-optional-access cannot
// model doctest's REQUIRE, so every `fonts.` after a REQUIRE is reported as an
// unchecked dereference. Expected is this project's own result type, the check
// is the same shape, and value() on an error state aborts loudly rather than
// fabricating one.
dg::Expected<FontCatalog, dg::FontError> test_catalog() {
  return FontCatalog::scan(DG_TEST_FONT_DIR);
}

// The rules the hermetic cases use. zh-Hans and ja are routed to two fonts
// with identical coverage, which is what makes the Han-unification assertion
// falsifiable rather than decorative.
std::vector<FontFallbackRule> test_rules() {
  return {
      FontFallbackRule{"zh-Hans", {"DgTest Han Hans"}},
      FontFallbackRule{"ja", {"DgTest Han Ja"}},
      FontFallbackRule{"", {"DgTest Latin", "DgTest Rare"}},
  };
}

struct Fixture {
  FontCatalog catalog;
  FontId latin;
};

dg::Expected<Fixture, dg::FontError> fixture() {
  dg::Expected<FontCatalog, dg::FontError> scanned = test_catalog();
  if (!scanned) {
    return dg::Unexpected{scanned.error()};
  }
  FontCatalog catalog = std::move(scanned).value();
  catalog.set_fallback_rules(test_rules());
  const dg::Expected<FontId, dg::FontError> latin = catalog.add("DgTest Latin", false);
  if (!latin) {
    return dg::Unexpected{latin.error()};
  }
  return Fixture{std::move(catalog), latin.value()};
}

dg::TextStyle text_style(const std::string& content, FontId font, const std::string& language) {
  dg::TextStyle text;
  text.text = content;
  text.font = font;
  text.language = language;
  text.size = 64.0F;
  text.color = dg::Color::from_argb(kInk);
  text.align = dg::TextAlign::kLeft;
  return text;
}

// Renders one label onto a black surface and returns the pixels.
std::vector<std::uint32_t> render(const Fixture& setup, const dg::TextStyle& text) {
  dg::TreeSpec spec;
  spec.viewport = dg::PixelSize{kWidth, kHeight};
  spec.background.fill = dg::Color::from_argb(kBackground);
  spec.fonts = setup.catalog;

  dg::RenderTree tree{spec};
  dg::NodeStyle style;
  style.text = text;
  tree.add_child(dg::RenderTree::root(), dg::PixelRect{0, 0, kWidth, kHeight}, style);

  std::optional<dg::RasterSurface> surface = dg::RasterSurface::create(kWidth, kHeight);
  std::vector<std::uint32_t> pixels;
  if (!surface.has_value()) {
    return pixels;
  }
  tree.repaint_full(*surface);

  const dg::PixelView view = surface->peek_pixels();
  pixels.resize(static_cast<std::size_t>(kWidth) * kHeight);
  for (int y = 0; y < kHeight; ++y) {
    const std::uint8_t* row = view.pixels + (static_cast<std::size_t>(y) * view.row_bytes);
    for (int x = 0; x < kWidth; ++x) {
      std::uint32_t value = 0;
      std::memcpy(&value, row + (static_cast<std::size_t>(x) * 4), 4);
      pixels[(static_cast<std::size_t>(y) * kWidth) + static_cast<std::size_t>(x)] = value;
    }
  }
  return pixels;
}

// Ink is any pixel that is not the background, counted over the top half and
// the bottom half separately - which is the whole difference between the two
// generated Han fonts.
struct Ink {
  std::size_t top = 0;
  std::size_t bottom = 0;

  [[nodiscard]] std::size_t total() const { return top + bottom; }
};

// The top-left pixel is background by construction: the label is left-aligned
// and its glyphs sit on a baseline well below the first row. Comparing against
// it rather than against a literal avoids depending on the surface's channel
// order, which slice 2 measured is NOT what kN32_SkColorType claims.
Ink measure_ink(const std::vector<std::uint32_t>& pixels) {
  const std::uint32_t background = pixels[0];
  Ink ink;
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      const std::uint32_t value =
          pixels[(static_cast<std::size_t>(y) * kWidth) + static_cast<std::size_t>(x)];
      if (value != background) {
        (y < kHeight / 2 ? ink.top : ink.bottom) += 1;
      }
    }
  }
  return ink;
}

// The first (codepoint, language) pair on which two catalogs disagree, or
// empty. Hoisted out of the test case because doctest's macros expand into
// enough control flow that a nested loop of them exceeds clang-tidy's
// cognitive-complexity gate - the same reason test_text_damage.cpp hoists its
// surface pair.
std::string first_disagreement(const Fixture& a, const Fixture& b) {
  for (char32_t codepoint : {kLatinA, kGreekAlpha, kHanZhong, kRunicFehu, kAstralNonColour,
                             kEmojiGrin, kUncovered}) {
    for (const char* language : {"", "ja", "zh-Hans"}) {
      const FontResolution first = a.catalog.resolve(a.latin, language, codepoint);
      const FontResolution second = b.catalog.resolve(b.latin, language, codepoint);
      if (first.family != second.family || first.glyph != second.glyph ||
          first.from_primary != second.from_primary) {
        return std::string{"U+"} + std::to_string(static_cast<std::uint32_t>(codepoint)) +
               " lang='" + language + "': '" + first.family + "' vs '" + second.family + "'";
      }
    }
  }
  return {};
}

// True when every entry has a real glyph and a family. A non-null typeface
// proves nothing - a face without the codepoint hands back glyph 0.
bool all_resolved(const std::vector<FontResolution>& resolved) {
  return std::all_of(resolved.begin(), resolved.end(), [](const FontResolution& item) {
    return item.glyph != 0 && !item.family.empty();
  });
}

}  // namespace

TEST_CASE("the generated test fonts are present and enumerate deterministically") {
  const dg::Expected<FontCatalog, dg::FontError> catalog = test_catalog();
  REQUIRE(catalog.has_value());

  // Sorted by family name, not by readdir order. Skia's directory manager
  // enumerates in filesystem order - measured: these come back as Colour,
  // Rare, Han Ja, Han Hans, Latin - so an unsorted last-resort chain would
  // answer differently on two machines carrying identical fonts.
  const std::vector<std::string> expected = {"DgTest Colour", "DgTest Han Hans",
                                             "DgTest Han Ja", "DgTest Latin", "DgTest Rare"};
  CHECK(catalog.value().fallback_families() == expected);
}

TEST_CASE("two catalogs built from the same directory answer identically") {
  const dg::Expected<Fixture, dg::FontError> a = fixture();
  const dg::Expected<Fixture, dg::FontError> b = fixture();
  REQUIRE(a.has_value());
  REQUIRE(b.has_value());

  CHECK(a.value().catalog.fallback_families() == b.value().catalog.fallback_families());
  CHECK(first_disagreement(a.value(), b.value()).empty());
}

TEST_CASE("the family the caller named answers for what it covers") {
  const dg::Expected<Fixture, dg::FontError> setup = fixture();
  REQUIRE(setup.has_value());
  const Fixture& fonts = setup.value();

  const FontResolution latin = fonts.catalog.resolve(fonts.latin, "", kLatinA);
  CHECK(latin.family == "DgTest Latin");
  CHECK(latin.glyph == kLatinAGlyph);
  CHECK(latin.from_primary);

  // Not Latin script, still the primary's job because the primary has it.
  const FontResolution greek = fonts.catalog.resolve(fonts.latin, "", kGreekAlpha);
  CHECK(greek.family == "DgTest Latin");
  CHECK(greek.glyph == kGreekAlphaGlyph);
  CHECK(greek.from_primary);
}

TEST_CASE("a codepoint the named family lacks comes from the chain") {
  const dg::Expected<Fixture, dg::FontError> setup = fixture();
  REQUIRE(setup.has_value());
  const Fixture& fonts = setup.value();

  SUBCASE("a BMP codepoint") {
    const FontResolution rare = fonts.catalog.resolve(fonts.latin, "", kRunicFehu);
    CHECK(rare.family == "DgTest Rare");
    CHECK(rare.glyph == kRunicGlyph);
    CHECK_FALSE(rare.from_primary);
  }
  SUBCASE("an astral codepoint, through the cmap format 12 subtable") {
    const FontResolution astral = fonts.catalog.resolve(fonts.latin, "", kAstralNonColour);
    CHECK(astral.family == "DgTest Rare");
    CHECK(astral.glyph == kAstralGlyph);
    CHECK_FALSE(astral.from_primary);
    CHECK_FALSE(astral.colour_glyphs);
  }
}

// design.md section 5.10.4: emoji is a consumer of this chain, and it has to
// jump the queue. DgTest Latin carries a monochrome outline for U+1F600, so
// "the family the caller named always wins" would answer with line art on a
// machine that has a colour font - which is exactly what DejaVu Sans and Noto
// Color Emoji do on this one.
TEST_CASE("a colour face beats the named family's own monochrome emoji") {
  const dg::Expected<Fixture, dg::FontError> setup = fixture();
  REQUIRE(setup.has_value());
  const Fixture& fonts = setup.value();

  // The trap really is set: the primary DOES cover it.
  const FontResolution direct = fonts.catalog.resolve(fonts.latin, "", kGreekAlpha);
  CHECK(direct.from_primary);

  const FontResolution emoji = fonts.catalog.resolve(fonts.latin, "", kEmojiGrin);
  CHECK(emoji.family == "DgTest Colour");
  CHECK(emoji.glyph == kColourEmojiGlyph);
  CHECK(emoji.colour_glyphs);
  CHECK_FALSE(emoji.from_primary);
}

// design.md section 5.13.5. This is the case the whole slice exists for.
TEST_CASE("Han unification: one codepoint, two languages, two families") {
  const dg::Expected<Fixture, dg::FontError> setup = fixture();
  REQUIRE(setup.has_value());
  const Fixture& fonts = setup.value();

  const FontResolution hans = fonts.catalog.resolve(fonts.latin, "zh-Hans", kHanZhong);
  const FontResolution japanese = fonts.catalog.resolve(fonts.latin, "ja", kHanZhong);

  CHECK(hans.family == "DgTest Han Hans");
  CHECK(japanese.family == "DgTest Han Ja");
  CHECK(hans.glyph == kHanZhongGlyph);
  CHECK(japanese.glyph == kHanZhongGlyph);
  CHECK(hans.family != japanese.family);
}

TEST_CASE("Han unification reaches the pixels, not only the family name") {
  const dg::Expected<Fixture, dg::FontError> setup = fixture();
  REQUIRE(setup.has_value());
  const Fixture& fonts = setup.value();

  const std::string zhong = "\xE4\xB8\xAD";
  const std::vector<std::uint32_t> hans =
      render(fonts, text_style(zhong, fonts.latin, "zh-Hans"));
  const std::vector<std::uint32_t> japanese =
      render(fonts, text_style(zhong, fonts.latin, "ja"));

  CHECK(hans != japanese);

  // And in the direction the two generated fonts describe: Hans is a low bar,
  // Ja is a high bar, both the same area. Checking only `!=` would pass if the
  // two renders differed for any reason at all, including a bug that drew
  // nothing in one of them.
  const Ink hans_ink = measure_ink(hans);
  const Ink ja_ink = measure_ink(japanese);
  CHECK(hans_ink.total() > 0);
  CHECK(hans_ink.total() == ja_ink.total());
  CHECK(hans_ink.top == 0);
  CHECK(ja_ink.top > 0);
  CHECK(ja_ink.bottom == 0);
}

// Found by injection, not by design. Collapsing the run splitter so that every
// codepoint uses the FIRST run's face left all ten CTest entries green: every
// rendering case above draws a string that needs exactly ONE face, and
// resolve_text() does not go through the splitter at all. The shape that was
// missing is "a rendered string that needs two faces", and this is it.
//
// The assertion works because the generated fonts have disjoint vertical
// extents. DgTest Han Hans draws U+4E2D as a bar that lies ENTIRELY BELOW the
// midline, so appending it to a Latin string must not add a single pixel above
// the midline. If the string were drawn with the Latin face throughout, that
// face's .notdef box - which spans the full ascender - would appear up there.
TEST_CASE("a string needing two faces is drawn with both of them") {
  const dg::Expected<Fixture, dg::FontError> setup = fixture();
  REQUIRE(setup.has_value());
  const Fixture& fonts = setup.value();

  const Ink latin_only = measure_ink(render(fonts, text_style("A", fonts.latin, "zh-Hans")));
  const Ink mixed =
      measure_ink(render(fonts, text_style("A\xE4\xB8\xAD", fonts.latin, "zh-Hans")));

  REQUIRE(latin_only.top > 0);
  CHECK(mixed.top == latin_only.top);
  CHECK(mixed.bottom > latin_only.bottom);
}

TEST_CASE("language tags match on subtag boundaries and ignore case") {
  const dg::Expected<Fixture, dg::FontError> setup = fixture();
  REQUIRE(setup.has_value());
  const Fixture& fonts = setup.value();

  CHECK(fonts.catalog.resolve(fonts.latin, "ja-JP", kHanZhong).family == "DgTest Han Ja");
  CHECK(fonts.catalog.resolve(fonts.latin, "JA", kHanZhong).family == "DgTest Han Ja");
  CHECK(fonts.catalog.resolve(fonts.latin, "zh-Hans-CN", kHanZhong).family ==
        "DgTest Han Hans");

  // "jav" is Javanese, not Japanese. A naive prefix match would route it to
  // the Japanese chain; the boundary check sends it to the generic one, which
  // in this rule set names neither Han font and therefore ends at the sorted
  // pool - whose first covering entry is DgTest Han Hans.
  CHECK(fonts.catalog.resolve(fonts.latin, "jav", kHanZhong).family == "DgTest Han Hans");
}

// Also found by injection. Deleting the specificity sort left everything green,
// because both rule tables in the codebase - the shipped default and the one
// these tests use - are ALREADY written most-specific-first, so the sort had
// nothing to reorder. The missing shape is a badly ordered table, which is
// precisely the input the sort exists for: FontFallbackRule's contract says
// precedence is a property of the table's content, not of the order a caller
// happened to list it in.
TEST_CASE("a specific rule beats a generic one listed before it") {
  dg::Expected<FontCatalog, dg::FontError> scanned = test_catalog();
  REQUIRE(scanned.has_value());
  FontCatalog catalog = std::move(scanned).value();

  // Generic FIRST, and it names a family that covers U+4E2D. An unsorted walk
  // takes it and never reaches the ja rule.
  catalog.set_fallback_rules({
      FontFallbackRule{"", {"DgTest Han Hans"}},
      FontFallbackRule{"ja", {"DgTest Han Ja"}},
  });
  const dg::Expected<FontId, dg::FontError> latin = catalog.add("DgTest Latin", false);
  REQUIRE(latin.has_value());

  CHECK(catalog.resolve(latin.value(), "ja", kHanZhong).family == "DgTest Han Ja");
  CHECK(catalog.resolve(latin.value(), "de", kHanZhong).family == "DgTest Han Hans");
}

TEST_CASE("a codepoint nothing covers is reported, not silently dropped") {
  const dg::Expected<Fixture, dg::FontError> setup = fixture();
  REQUIRE(setup.has_value());
  const Fixture& fonts = setup.value();

  const FontResolution missing = fonts.catalog.resolve(fonts.latin, "", kUncovered);
  CHECK(missing.family.empty());
  CHECK(missing.glyph == 0);
  CHECK_FALSE(missing.from_primary);
}

TEST_CASE("a codepoint nothing covers still puts ink on the surface") {
  const dg::Expected<Fixture, dg::FontError> setup = fixture();
  REQUIRE(setup.has_value());
  const Fixture& fonts = setup.value();

  // U+0500 as UTF-8. The decision (doc/font-fallback.md) is that this draws
  // the primary's .notdef box: invisible would be indistinguishable from an
  // empty label, and "the label is empty" and "this machine has no font for
  // this character" need different fixes.
  const std::vector<std::uint32_t> pixels =
      render(fonts, text_style("\xD4\x80", fonts.latin, ""));
  CHECK(measure_ink(pixels).total() > 0);
}

TEST_CASE("every codepoint of a mixed-script string resolves to a real glyph") {
  const dg::Expected<Fixture, dg::FontError> setup = fixture();
  REQUIRE(setup.has_value());
  const Fixture& fonts = setup.value();

  // Latin, Greek, Han, Runic, emoji - five scripts, one style, no family named
  // per script by the caller.
  const std::string mixed = "Ab\xCE\xB1\xE4\xB8\xAD\xE1\x9A\xA0\xF0\x9F\xA4\x80";
  const std::vector<FontResolution> resolved =
      fonts.catalog.resolve_text(fonts.latin, "zh-Hans", mixed);

  REQUIRE(resolved.size() == 6);
  CHECK(all_resolved(resolved));
  CHECK(resolved[0].family == "DgTest Latin");
  CHECK(resolved[2].family == "DgTest Latin");
  CHECK(resolved[3].family == "DgTest Han Hans");
  CHECK(resolved[4].family == "DgTest Rare");
  CHECK(resolved[5].family == "DgTest Rare");
}

TEST_CASE("ill-formed UTF-8 is reported the same way an uncovered codepoint is") {
  const dg::Expected<Fixture, dg::FontError> setup = fixture();
  REQUIRE(setup.has_value());
  const Fixture& fonts = setup.value();

  const std::vector<FontResolution> resolved = fonts.catalog.resolve_text(fonts.latin, "",
                                                                          std::string("A\xFF"
                                                                                      "B"));
  REQUIRE(resolved.size() == 3);
  CHECK(resolved[0].glyph == kLatinAGlyph);
  CHECK(resolved[1].glyph == 0);
  CHECK(resolved[1].family.empty());
  CHECK(resolved[2].glyph != 0);
}

TEST_CASE("naming a family this machine does not have still fails") {
  dg::Expected<FontCatalog, dg::FontError> scanned = test_catalog();
  REQUIRE(scanned.has_value());
  FontCatalog catalog = std::move(scanned).value();

  // The contract slice 3 established, and the one the fallback chain must not
  // quietly repeal: the chain answers for CODEPOINTS, never for a family the
  // caller asked for by name.
  const dg::Expected<FontId, dg::FontError> missing = catalog.add("Inter", false);
  CHECK_FALSE(missing.has_value());
  CHECK(missing.error().message.find("Inter") != std::string::npos);
}
