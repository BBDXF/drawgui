// Which face draws this codepoint.
//
// The whole of drawgui's answer to design.md sections 5.10.4 and 5.13.5 lives
// here, because the font manager this project links has none: slice 2 measured
// matchFamilyStyleCharacter() returning null for every codepoint on
// SkFontMgr_New_Custom_Directory, and slice 4-1 measured it again, with and
// without a bcp47 hint, unchanged.
//
// The walk, in order, and every step of it is a decision recorded in
// doc/font-fallback.md:
//
//   1. Colour emoji jump the queue. A monochrome primary that happens to carry
//      an outline for U+1F600 must not win over a colour font that has it.
//   2. The family the caller named. An explicit choice is honoured whenever it
//      can be.
//   3. The language chain, most specific rule first. This is the ONLY step
//      that can distinguish Han unification, and it is a table rather than an
//      inference, because the fonts themselves cannot tell zh from ja.
//   4. The whole pool in family-name order. The last resort, sorted so that
//      two machines with the same fonts answer the same way.
//   5. Nothing. Reported as glyph 0, drawn as the primary's .notdef box.

#include "drawgui/render/font_catalog.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "include/core/SkFontStyle.h"
#include "include/core/SkString.h"
#include "include/core/SkTypeface.h"

#include "drawgui/base/utf8.h"
#include "drawgui/render/render_tree.h"
#include "render/font_access.h"
#include "render/font_catalog_impl.h"

namespace dg {
namespace {

// What one codepoint resolved to, internally: the face rather than its name.
struct Hit {
  // Null when nothing covers the codepoint. Owning rather than a raw pointer
  // so the run it ends up in keeps the face alive without a cast.
  sk_sp<SkTypeface> face;
  std::string family;
  SkGlyphID glyph = 0;
  bool from_primary = false;
  bool colour_glyphs = false;
};

const FaceEntry* find_family(const FontChain& chain, std::string_view family) {
  for (const FaceEntry& entry : chain.pool) {
    if (entry.family == family) {
      return &entry;
    }
  }
  return nullptr;
}

Hit hit_for(const FaceEntry& entry, char32_t codepoint) {
  const SkGlyphID glyph = entry.face->unicharToGlyph(static_cast<SkUnichar>(codepoint));
  if (glyph == 0) {
    return Hit{};
  }
  return Hit{entry.face, entry.family, glyph, false, entry.colour_glyphs};
}

// Step 1. Only faces that actually carry colour tables are considered, so a
// machine with no colour emoji font falls straight through to the ordinary
// walk instead of picking an arbitrary monochrome face here.
Hit colour_first(const FontChain& chain, char32_t codepoint) {
  for (const FontFallbackRule& rule : chain.rules) {
    for (const std::string& family : rule.families) {
      const FaceEntry* entry = find_family(chain, family);
      if (entry == nullptr || !entry->colour_glyphs) {
        continue;
      }
      const Hit hit = hit_for(*entry, codepoint);
      if (hit.face != nullptr) {
        return hit;
      }
    }
  }
  for (const FaceEntry& entry : chain.pool) {
    if (!entry.colour_glyphs) {
      continue;
    }
    const Hit hit = hit_for(entry, codepoint);
    if (hit.face != nullptr) {
      return hit;
    }
  }
  return Hit{};
}

// Step 3. Rules are already ordered most-specific-first by
// set_fallback_rules(), so this is a plain walk and the precedence is a
// property of the table rather than of this loop.
Hit language_chain(const FontChain& chain, std::string_view language, char32_t codepoint) {
  for (const FontFallbackRule& rule : chain.rules) {
    if (!language_matches(rule.language, language)) {
      continue;
    }
    for (const std::string& family : rule.families) {
      const FaceEntry* entry = find_family(chain, family);
      if (entry == nullptr) {
        continue;
      }
      const Hit hit = hit_for(*entry, codepoint);
      if (hit.face != nullptr) {
        return hit;
      }
    }
  }
  return Hit{};
}

// `from_primary` is decided here, at the end, rather than by whichever step
// produced the hit. It answers "did the family the caller named supply this
// glyph", and that has to stay true when the caller named the colour emoji
// font and step 1 found it in the pool - otherwise a caller asking a font
// about its own coverage is told no.
Hit with_primary_flag(Hit hit, const sk_sp<SkTypeface>& primary) {
  hit.from_primary = hit.face != nullptr && hit.face == primary;
  return hit;
}

Hit resolve_hit(const FontChain& chain, const sk_sp<SkTypeface>& primary,
                std::string_view language, char32_t codepoint) {
  if (prefers_colour_font(codepoint)) {
    const Hit hit = colour_first(chain, codepoint);
    if (hit.face != nullptr) {
      return with_primary_flag(hit, primary);
    }
  }

  if (primary) {
    const SkGlyphID glyph = primary->unicharToGlyph(static_cast<SkUnichar>(codepoint));
    if (glyph != 0) {
      SkString name;
      primary->getFamilyName(&name);
      return Hit{primary, std::string{name.c_str()}, glyph, true, has_colour_glyphs(*primary)};
    }
  }

  const Hit chained = language_chain(chain, language, codepoint);
  if (chained.face != nullptr) {
    return with_primary_flag(chained, primary);
  }

  for (const FaceEntry& entry : chain.pool) {
    const Hit hit = hit_for(entry, codepoint);
    if (hit.face != nullptr) {
      return with_primary_flag(hit, primary);
    }
  }
  return Hit{};
}

FontResolution to_resolution(const Hit& hit) {
  return FontResolution{hit.family, hit.glyph, hit.from_primary, hit.colour_glyphs};
}

}  // namespace

FontResolution FontCatalog::resolve(FontId primary, std::string_view language,
                                    char32_t codepoint) const {
  const sk_sp<SkTypeface> face = FontAccess::typeface(*this, primary);
  return to_resolution(resolve_hit(impl_->chain, face, language, codepoint));
}

std::vector<FontResolution> FontCatalog::resolve_text(FontId primary, std::string_view language,
                                                      std::string_view utf8) const {
  const sk_sp<SkTypeface> face = FontAccess::typeface(*this, primary);
  std::vector<FontResolution> out;
  std::size_t offset = 0;
  while (offset < utf8.size()) {
    const Utf8Step step = utf8_decode(utf8, offset);
    offset += step.length;
    if (!step.valid) {
      // design.md section 5.13.3: reported, not rewritten to U+FFFD. An
      // invalid byte gets the same shape as an uncovered codepoint because it
      // has the same outcome - something visible that is not a character.
      out.emplace_back();
      continue;
    }
    out.push_back(to_resolution(resolve_hit(impl_->chain, face, language, step.codepoint)));
  }
  return out;
}

// Groups consecutive codepoints that resolve to the same face into one run, so
// that the paint path issues one draw call per face rather than per character.
//
// The single-run case is byte-for-byte what the previous, fallback-free paint
// path did: one run spanning the whole string, drawn with drawSimpleText at
// the same origin. That equivalence is what keeps every existing text-bearing
// scene rendering identically.
std::vector<TextRun> FontAccess::runs(const FontCatalog& catalog, const TextStyle& text) {
  const sk_sp<SkTypeface> primary = FontAccess::typeface(catalog, text.font);
  std::vector<TextRun> out;
  if (!primary || text.text.empty()) {
    return out;
  }

  const std::string_view source{text.text};
  std::size_t offset = 0;
  while (offset < source.size()) {
    const Utf8Step step = utf8_decode(source, offset);
    const Hit hit =
        step.valid ? resolve_hit(catalog.impl_->chain, primary, text.language, step.codepoint)
                   : Hit{};
    // A run that nothing covers carries the PRIMARY typeface, because the box
    // that gets drawn should have the metrics of the font the text asked for.
    const bool missing = hit.face == nullptr;
    const sk_sp<SkTypeface>& face = missing ? primary : hit.face;

    if (!out.empty() && out.back().typeface == face && out.back().missing == missing) {
      out.back().end = offset + step.length;
      ++out.back().codepoints;
    } else {
      out.push_back(TextRun{face, offset, offset + step.length, 1, missing});
    }
    offset += step.length;
  }
  return out;
}

}  // namespace dg
