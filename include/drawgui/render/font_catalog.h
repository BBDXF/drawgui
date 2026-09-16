// The fonts a render tree is allowed to draw with, named once and referred to
// by number afterwards - and the fallback chain that covers everything the
// named family does not.
//
// This exists because sub-step 3 needed a label, a label needs text, and text
// is the first thing a node can paint that is not a rectangle. Slice 2 chose
// SkFontMgr_New_Custom_Directory to keep the link line at one archive plus
// -lfreetype, and paid for it:
//
//   matchFamilyStyleCharacter() returns null on it, for every codepoint, with
//   or without a bcp47 hint. Measured again in slice 4-1, unchanged.
//
// So the fallback chain is built here rather than borrowed from the font
// manager. doc/font-fallback.md records why that was chosen over taking the
// fontconfig dependency, what it costs, and what would force the other
// choice. The short version: fontconfig's per-language answers come from
// /etc/fonts on the host, and a text renderer whose glyph selection is a
// property of the machine cannot keep this project's zero-tolerance golden
// guarantee.
//
// Two contracts that look similar and are not:
//
//   add("Inter") FAILS when the machine has no Inter. Naming a family that is
//   not there is a startup error, still, exactly as before - substituting for
//   it would look right on the developer's machine and wrong everywhere else.
//
//   A codepoint the named family does not cover goes to the fallback chain.
//   That is not a substitution for a family the caller asked for; it is the
//   answer to a question the named family cannot answer at all.
//
// No Skia type appears below. The typefaces live behind the pimpl, exactly as
// they do for RasterSurface.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "drawgui/base/expected.h"

namespace dg {

// Why a font operation failed, as text. The same reasoning as WindowError: no
// caller branches on the reason, and the reason ("no family named 'Inter' in
// /usr/share/fonts") is the whole of what makes it fixable.
struct FontError {
  std::string message;
};

// Names one entry of a FontCatalog.
//
// Zero is not a font. A default-constructed NodeStyle therefore carries no
// font, which is what makes "I forgot to set the font" and "I asked for a font
// that is not there" the same visible outcome - nothing drawn - rather than a
// silent fall back onto whichever family happened to be first.
struct FontId {
  std::uint16_t value = 0;

  [[nodiscard]] constexpr bool is_valid() const { return value != 0; }

  friend bool operator==(FontId, FontId) = default;
};

// One rule of the language-keyed fallback chain (design.md section 5.13.5).
//
// `language` is matched as a BCP 47 PREFIX on subtag boundaries: "zh-Hans"
// matches "zh-Hans" and "zh-Hans-CN" but not "zh-Hant". An empty `language`
// matches everything and is therefore the generic chain. There is deliberately
// no BCP 47 canonicalization engine here - "zh-CN" does not become "zh-Hans"
// by magic, it becomes it by being listed, which is a table a reader can
// check rather than an algorithm they have to trust.
//
// `families` are tried in order and skipped when this machine does not have
// them, so one table can name the good font for every platform and still work
// on a machine that has none of them.
struct FontFallbackRule {
  std::string language;
  std::vector<std::string> families;
};

// What one codepoint resolved to.
//
// `glyph` is the honest form of "did this work". A non-null typeface proves
// nothing - a face that lacks the codepoint returns glyph 0 and draws a tofu
// box, which is exactly what a caller checking for null would call a success.
struct FontResolution {
  // Empty when nothing on this machine covers the codepoint.
  std::string family;

  // Zero when nothing covers the codepoint. Non-zero means a real glyph.
  std::uint16_t glyph = 0;

  // True when the family the caller named supplied the glyph itself, so a
  // test can tell "the primary already had it" from "the chain found it".
  bool from_primary = false;

  // True when the chosen face carries CBDT/CBLC, sbix or COLR. design.md
  // section 5.10.4 makes emoji a consumer of this chain, and the rule it needs
  // - a colour face beats a monochrome outline for the same emoji - is only
  // checkable if a caller can see which kind it got.
  bool colour_glyphs = false;
};

// A shared, append-only table of typefaces plus a fallback chain over every
// font the scan found.
//
// Copyable, and a copy names the same fonts: this is a handle to one table,
// not a container that can be duplicated into two that disagree. A render tree
// holds one, every node that draws text holds an index into it, and the
// typefaces outlive both because the table is reference counted.
class FontCatalog {
 public:
  // Scans `directory` recursively for font files, then builds the fallback
  // pool: one representative face per family, SORTED BY FAMILY NAME.
  //
  // The sort is load-bearing. Skia's directory manager enumerates in
  // readdir order, which is a property of the filesystem rather than of the
  // fonts, so an unsorted last-resort chain would answer differently on two
  // machines carrying identical font packages.
  [[nodiscard]] static Expected<FontCatalog, FontError> scan(const std::string& directory);

  // Resolves one family and returns the id that names it from now on.
  //
  // Fails rather than substituting. A GUI that silently swaps DejaVu Sans for
  // whatever was nearest looks fine on the developer's machine and wrong
  // everywhere else. This is unchanged by fallback: the chain answers for
  // codepoints, never for a family the caller asked for by name.
  [[nodiscard]] Expected<FontId, FontError> add(const std::string& family, bool bold);

  [[nodiscard]] std::size_t size() const;

  // The families this machine actually offers, for a diagnostic that can say
  // what was available instead of only what was missing.
  [[nodiscard]] std::string available_families() const;

  // True when `id` names an entry of THIS table. A node carrying an id from a
  // different catalog is a bug the paint path must not dereference.
  [[nodiscard]] bool holds(FontId id) const;

  // The family name `id` was added under, or empty when `id` names nothing in
  // this table. Added for 7-2's paragraph module: SkParagraph resolves fonts
  // by family-name string, never by the SkTypeface pointer this catalog
  // otherwise keeps behind FontId, and the resolution this table already
  // computes (resolve()/resolve_text()) reports family names for exactly this
  // reason - this accessor is the one additional case (the caller's OWN
  // primary family, not something the chain resolved) that FontResolution
  // does not already cover on its own.
  [[nodiscard]] std::string family_name(FontId id) const;

  // The chain drawgui ships. Names fonts from several platforms; the ones this
  // machine lacks are skipped, so the same table is correct everywhere.
  [[nodiscard]] static std::vector<FontFallbackRule> default_fallback_rules();

  // Replaces the rules. The host owns this decision - drawgui is a kernel
  // embedded through a C ABI, and a hardcoded, unoverridable idea of which
  // font is right for Japanese would be a policy the embedder cannot fix.
  void set_fallback_rules(std::vector<FontFallbackRule> rules);

  // The fallback pool in the order the last-resort walk uses. Exposed so that
  // determinism is checkable rather than asserted.
  [[nodiscard]] std::vector<std::string> fallback_families() const;

  // Which family will draw `codepoint`, for text tagged `language`, when the
  // caller named `primary`.
  //
  // `language` is a BCP 47 tag; empty means the generic chain.
  [[nodiscard]] FontResolution resolve(FontId primary, std::string_view language,
                                       char32_t codepoint) const;

  // The same answer for every codepoint of a UTF-8 string, in order.
  //
  // An ill-formed byte yields an entry with glyph 0 and an empty family - the
  // same shape as an uncovered codepoint, because the outcome is the same:
  // something visible that is not a character. design.md section 5.13.3
  // forbids silently rewriting it to U+FFFD, and this does not.
  [[nodiscard]] std::vector<FontResolution> resolve_text(FontId primary,
                                                         std::string_view language,
                                                         std::string_view utf8) const;

 private:
  struct Impl;

  explicit FontCatalog(std::shared_ptr<Impl> impl);

  std::shared_ptr<Impl> impl_;

  friend struct FontAccess;
};

}  // namespace dg
