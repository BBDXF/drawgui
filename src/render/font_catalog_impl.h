// The state behind FontCatalog, shared by the table and the fallback chain.
//
// Two translation units need it: font_catalog.cpp owns the table and the pool,
// font_fallback.cpp owns the question "which face draws this codepoint". They
// were split because those are two different jobs, not because either is large.

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "drawgui/render/font_catalog.h"

#include "include/core/SkFontMgr.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkTypeface.h"

namespace dg {

// One family's representative face.
//
// ONE face per family, not one per style, and that is a decision. Coverage is
// read from this face, and this face is what draws - so the index cannot
// disagree with the result. Choosing a weight-matched face at draw time would
// reintroduce that gap: DejaVu Sans regular has 6253 glyphs and DejaVu Sans
// Bold has 6196, so a family selected because the regular covered a codepoint
// can hand back a bold face that does not.
//
// The cost is stated rather than hidden: a bold run that falls through to the
// chain is drawn in the fallback family's representative weight, not in bold.
struct FaceEntry {
  std::string family;
  sk_sp<SkTypeface> face;

  // CBDT/CBLC, sbix or COLR present. Detected from the font's own tables
  // rather than from its name, so a machine whose colour emoji font is called
  // something else still works.
  bool colour_glyphs = false;
};

// Everything the fallback walk reads. A type of its own rather than four
// members of Impl, because Impl is private to FontCatalog and the walk lives in
// its own translation unit - and because "the chain" is a thing with its own
// invariants: the pool is sorted by family name, the rules by specificity.
struct FontChain {
  std::vector<FaceEntry> pool;
  std::vector<FontFallbackRule> rules;
};

struct FontCatalog::Impl {
  sk_sp<SkFontMgr> manager;

  // The families the caller named, one-based ids into this vector.
  std::vector<sk_sp<SkTypeface>> typefaces;

  FontChain chain;
};

// CBDT/CBLC, sbix or COLR present. Read off the font's own tables rather than
// its name, so a machine whose colour emoji font is called something else
// still works.
[[nodiscard]] bool has_colour_glyphs(const SkTypeface& face);

// True when a BCP 47 rule prefix applies to a tag, on subtag boundaries and
// case-insensitively. An empty rule matches everything.
[[nodiscard]] bool language_matches(std::string_view rule, std::string_view tag);

// The codepoints routed to a colour font BEFORE the family the caller named.
//
// design.md section 5.10.4 makes emoji a consumer of the fallback chain, and
// it has to jump the queue: DejaVu Sans carries a monochrome outline for
// U+1F600, so "the primary wins" would answer every emoji with a line-art face
// on a machine that has a colour one.
//
// The range is the plane-1 emoji blocks and nothing else. The BMP symbols that
// Unicode also gives an emoji presentation - U+2714, U+2B50 and their
// neighbours - are deliberately left to the ordinary chain: routing them to a
// colour font changes how existing symbol text renders, and doing it correctly
// needs the real Emoji_Presentation property table, which this project does
// not ship. doc/font-fallback.md records the consequence.
[[nodiscard]] bool prefers_colour_font(char32_t codepoint);

}  // namespace dg
