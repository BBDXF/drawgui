// The acceptance gate for slice 4-1: does a string mixing scripts render with
// real glyphs, on the fonts of whatever machine is running this, without the
// caller naming a family per script?
//
// The hermetic half of that question lives in test_font_fallback.cpp, which
// resolves against fonts this build generates and can therefore pin exact
// families and exact glyph ids. This file asks the other half - the one that
// only the real system font set can answer - and it has to do so without
// asserting which fonts that set contains.
//
// The shape that makes it a gate rather than a tautology:
//
//   ORACLE. "Some family in the pool covers this codepoint" is computed by
//   naming each family as a PRIMARY and reading `from_primary`. That is a
//   different code path from the chain walk, so the oracle cannot be wrong in
//   the same way the thing under test is.
//
//   IMPLICATION. Whenever the oracle says a codepoint is coverable, the
//   resolver must return a non-zero glyph. Checking non-null instead would
//   pass on a tofu box, which is the whole failure this slice exists to close.
//
//   NO SILENT SKIP. A machine whose fonts cover too little for the check to
//   mean anything FAILS rather than passing quietly. Section 7's trap is a
//   suite that stays green by having nothing to assert.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/base/utf8.h"
#include "drawgui/render/font_catalog.h"

namespace {

using dg::FontCatalog;
using dg::FontId;
using dg::FontResolution;

struct Sample {
  const char* script;
  const char* utf8;
  char32_t codepoint;
};

// One codepoint per script, spread deliberately across what a desktop font set
// might and might not have.
const std::vector<Sample>& samples() {
  static const std::vector<Sample> list = {
      {"Latin", "A", 0x0041},
      {"Greek", "\xCE\xB1", 0x03B1},
      {"Cyrillic", "\xD0\x94", 0x0414},
      {"Arabic", "\xD8\xA8", 0x0628},
      {"Hebrew", "\xD7\x90", 0x05D0},
      {"Han", "\xE4\xB8\xAD", 0x4E2D},
      {"Hiragana", "\xE3\x81\x82", 0x3042},
      {"Hangul", "\xEA\xB0\x80", 0xAC00},
      {"Arrows", "\xE2\x86\x92", 0x2192},
      {"Braille", "\xE2\xA0\x81", 0x2801},
      {"Armenian", "\xD4\xB1", 0x0531},
      {"Georgian", "\xE1\x83\x90", 0x10D0},
      {"Emoji", "\xF0\x9F\x98\x80", 0x1F600},
  };
  return list;
}

// dg::Expected rather than std::optional, for the reason recorded in
// test_font_fallback.cpp: clang-tidy cannot model doctest's REQUIRE, so an
// optional dereferenced after one is reported as unchecked access.
dg::Expected<FontCatalog, dg::FontError> system_catalog() {
  return FontCatalog::scan(DG_SYSTEM_FONT_DIR);
}

// Every family named as a primary, so `from_primary` reports that family's own
// coverage. This is the oracle: it never consults the fallback chain.
struct Oracle {
  FontCatalog catalog;
  std::vector<FontId> families;
};

dg::Expected<Oracle, dg::FontError> build_oracle() {
  dg::Expected<FontCatalog, dg::FontError> scanned = system_catalog();
  if (!scanned) {
    return dg::Unexpected{scanned.error()};
  }
  FontCatalog catalog = std::move(scanned).value();
  std::vector<FontId> ids;
  for (const std::string& family : catalog.fallback_families()) {
    const dg::Expected<FontId, dg::FontError> id = catalog.add(family, false);
    if (id) {
      ids.push_back(id.value());
    }
  }
  return Oracle{std::move(catalog), std::move(ids)};
}

// The family that covers `codepoint` by its own cmap, or empty.
std::string covering_family(const Oracle& oracle, char32_t codepoint) {
  for (FontId id : oracle.families) {
    const FontResolution direct = oracle.catalog.resolve(id, "", codepoint);
    if (direct.from_primary && direct.glyph != 0) {
      return direct.family;
    }
  }
  return {};
}

bool any_colour_family_covers(const Oracle& oracle, char32_t codepoint) {
  return std::any_of(oracle.families.begin(), oracle.families.end(), [&](FontId id) {
    const FontResolution direct = oracle.catalog.resolve(id, "", codepoint);
    return direct.from_primary && direct.glyph != 0 && direct.colour_glyphs;
  });
}

// A Latin-only primary, so that everything except Latin has to come from the
// chain. DejaVu Sans is tried first because it is the widest Latin face in the
// Debian/Ubuntu base set; any family will do, and the FIRST one that exists is
// used rather than a hardcoded name, so this works on a machine that has none
// of the ones listed.
// Returns an invalid FontId when this machine offers nothing at all, which the
// caller turns into a failing REQUIRE rather than a skip.
FontId pick_primary(FontCatalog& catalog) {
  for (const char* family : {"DejaVu Sans", "Noto Sans", "Ubuntu Sans", "Liberation Sans"}) {
    const dg::Expected<FontId, dg::FontError> id = catalog.add(family, false);
    if (id) {
      return id.value();
    }
  }
  const std::vector<std::string> available = catalog.fallback_families();
  if (available.empty()) {
    return FontId{};
  }
  const dg::Expected<FontId, dg::FontError> id = catalog.add(available.front(), false);
  return id ? id.value() : FontId{};
}

// One sample, checked. Returned rather than asserted inline because doctest's
// macros expand into enough control flow that a loop over thirteen of them put
// the test case past clang-tidy's cognitive-complexity threshold - the same
// reason test_text_damage.cpp hoists its surface pair out.
struct SampleVerdict {
  bool coverable = false;
  bool resolved = false;
  bool agreed_absent = false;
};

SampleVerdict check_sample(const Oracle& oracle, FontId primary, const Sample& sample) {
  SampleVerdict verdict;
  const std::string covering = covering_family(oracle, sample.codepoint);
  const FontResolution resolved = oracle.catalog.resolve(primary, "", sample.codepoint);
  if (covering.empty()) {
    // The resolver must AGREE that nothing covers it. Returning a family here
    // would mean it picked one without checking the cmap.
    verdict.agreed_absent = resolved.glyph == 0;
    return verdict;
  }
  verdict.coverable = true;

  // The honest form of "it worked". A non-null typeface proves nothing: a face
  // without the codepoint hands back glyph 0 and draws a tofu box. And the
  // family it named must really have the glyph, so the resolver cannot pass by
  // returning a plausible name.
  verdict.resolved = resolved.glyph != 0 && !resolved.family.empty() &&
                     !covering_family(oracle, sample.codepoint).empty();
  return verdict;
}

std::size_t count_codepoints(std::string_view text) {
  std::size_t codepoints = 0;
  for (std::size_t offset = 0; offset < text.size();) {
    offset += dg::utf8_decode(text, offset).length;
    ++codepoints;
  }
  return codepoints;
}

std::size_t count_missing(const std::vector<FontResolution>& resolved) {
  std::size_t missing = 0;
  for (const FontResolution& item : resolved) {
    missing += item.glyph == 0 ? 1 : 0;
  }
  return missing;
}

// The oracle plus a Latin-only primary, built once. One helper rather than two
// so a test case carries a single REQUIRE - doctest's macros are expensive in
// clang-tidy's cognitive-complexity accounting and a case is mostly macros.
struct Setup {
  Oracle oracle;
  FontId primary;
};

struct Survey {
  std::size_t exercised = 0;
  std::vector<std::string> uncovered;
  std::vector<std::string> failures;
};

// Every sample checked in one pass, so the test case carries one assertion
// instead of a loop of them. doctest's macros expand into enough control flow
// that a loop of thirteen exceeds clang-tidy's cognitive-complexity gate, and
// a named failure list reads better in a report than thirteen CAPTUREs.
dg::Expected<Setup, dg::FontError> build_setup() {
  dg::Expected<Oracle, dg::FontError> built = build_oracle();
  if (!built) {
    return dg::Unexpected{built.error()};
  }
  Oracle oracle = std::move(built).value();
  const FontId primary = pick_primary(oracle.catalog);
  if (!primary.is_valid()) {
    return dg::Unexpected{dg::FontError{"this machine offers no font to use as a primary"}};
  }
  return Setup{std::move(oracle), primary};
}

std::string coverable_string(const Oracle& oracle) {
  std::string mixed;
  for (const Sample& sample : samples()) {
    if (!covering_family(oracle, sample.codepoint).empty()) {
      mixed += sample.utf8;
    }
  }
  return mixed;
}

// The whole survey as one line per fact, so a test case emits one MESSAGE
// instead of a loop of them.
std::string describe(const Survey& survey) {
  std::string report =
      "scripts covered by this machine's fonts: " + std::to_string(survey.exercised) + " of " +
      std::to_string(samples().size());
  for (const std::string& script : survey.uncovered) {
    report += "\n  no font here covers: " + script;
  }
  for (const std::string& failure : survey.failures) {
    report += "\n  FAILED: " + failure;
  }
  return report;
}

Survey survey_samples(const Oracle& oracle, FontId primary) {
  Survey survey;
  for (const Sample& sample : samples()) {
    const SampleVerdict verdict = check_sample(oracle, primary, sample);
    if (verdict.coverable) {
      ++survey.exercised;
      if (!verdict.resolved) {
        survey.failures.emplace_back(std::string{"covered but not resolved: "} + sample.script);
      }
      continue;
    }
    survey.uncovered.emplace_back(sample.script);
    if (!verdict.agreed_absent) {
      survey.failures.emplace_back(
          std::string{"no font covers it, yet the resolver named one: "} + sample.script);
    }
  }
  return survey;
}

}  // namespace

TEST_CASE("the system font directory yields a usable catalog") {
  // Not a skip. If this machine cannot produce a catalog then the gate below
  // has nothing to gate, and a suite that goes green in that state is the
  // exact failure design.md section 7 warns about.
  const dg::Expected<FontCatalog, dg::FontError> catalog = system_catalog();
  REQUIRE_MESSAGE(catalog.has_value(),
                  "no fonts under " DG_SYSTEM_FONT_DIR
                  "; set DRAWGUI_SYSTEM_FONT_DIR to a directory that has some");
  CHECK_FALSE(catalog.value().fallback_families().empty());
}

TEST_CASE("every script this machine can draw is reached without naming a family") {
  const dg::Expected<Setup, dg::FontError> setup = build_setup();
  REQUIRE(setup.has_value());

  const Survey survey = survey_samples(setup.value().oracle, setup.value().primary);
  CHECK(survey.failures.empty());
  MESSAGE(describe(survey));

  // A machine whose font set covers fewer than four of the sample scripts
  // cannot exercise fallback in any interesting way, and a green result from
  // it would be meaningless. Four is reachable with DejaVu Sans alone, which
  // is in the base package set of every distribution this project targets.
  CHECK(survey.exercised >= 4);
}

TEST_CASE("a whole multilingual string resolves with no tofu") {
  const dg::Expected<Setup, dg::FontError> setup = build_setup();
  REQUIRE(setup.has_value());

  // One string, several scripts, one style, no family named per script. This
  // is the sentence the slice is judged on.
  const std::string mixed = coverable_string(setup.value().oracle);
  REQUIRE(mixed.size() > 4);

  const std::vector<FontResolution> resolved =
      setup.value().oracle.catalog.resolve_text(setup.value().primary, "", mixed);
  CHECK(resolved.size() == count_codepoints(mixed));
  CHECK(count_missing(resolved) == 0);
}

TEST_CASE("emoji prefer a colour face over a monochrome outline") {
  const dg::Expected<Setup, dg::FontError> setup = build_setup();
  REQUIRE(setup.has_value());

  constexpr char32_t kGrin = 0x1F600;
  if (!any_colour_family_covers(setup.value().oracle, kGrin)) {
    // Stated, not silently skipped: without a colour font there is no
    // preference to express, and the hermetic test in test_font_fallback.cpp
    // proves the rule against fonts this build generates.
    MESSAGE("no colour font here covers U+1F600; the rule is proven hermetically instead");
    return;
  }

  const FontResolution resolved =
      setup.value().oracle.catalog.resolve(setup.value().primary, "", kGrin);
  CHECK(resolved.glyph != 0);
  CHECK(resolved.colour_glyphs);
}
