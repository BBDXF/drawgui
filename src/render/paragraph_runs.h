// Splitting a TextStyle's string into family-name runs for SkParagraph.
//
// SkParagraph resolves a font by asking its FontCollection for a family NAME;
// this project's FontMgr (SkFontMgr_New_Custom_Directory) cannot answer
// matchFamilyStyleCharacter() (doc/font-fallback.md), so handing SkParagraph
// one family and letting it search is not an option here the way it would be
// on a fontconfig-backed manager. Instead this file reuses the per-codepoint
// decision FontCatalog::resolve() already makes for the plain SkFont paint
// path (font_fallback.cpp) and turns it into per-run family names up front,
// so SkParagraph never has to search at all - it is TOLD which family to use
// for each stretch of text, the same "the chain drawgui builds" argument
// doc/font-fallback.md already made, extended to a second consumer rather
// than re-derived for it.
//
// Skia-free: this header and its .cpp name no Skia type, because everything
// it needs (utf8_decode, FontCatalog::resolve/family_name) is already public.
// paragraph_build.h is the seam that turns these runs into SkParagraph calls.

#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "drawgui/render/font_catalog.h"
#include "drawgui/render/render_tree.h"

namespace dg::detail {

// One maximal stretch of `TextStyle::text` that SkParagraph should shape with
// one family. `family` is never empty: a codepoint nothing covers still gets
// the PRIMARY font's own family name, the same "draw the primary's .notdef
// box" decision src/render/font_fallback.cpp's TextRun already makes for the
// non-wrapped path.
struct ParagraphRun {
  std::string family;
  std::size_t begin = 0;
  std::size_t end = 0;

  // True when every byte in [begin, end) failed dg::utf8_decode - a lone
  // continuation byte, a truncated multi-byte lead, an overlong encoding, a
  // surrogate, or anything past U+10FFFF (design.md section 5.13.3's list).
  // paragraph_build.cpp reads this to decide what bytes it may safely hand
  // SkParagraph's own addText(): the RAW invalid bytes themselves, never -
  // HarfBuzz/libgrapheme assume well-formed UTF-8 input and this project's
  // own defect-injection campaign (a truncated three-byte lead handed
  // through unchanged) found that assumption violated turns into a hang, not
  // a crash, which is a strictly worse failure mode for ASan/UBSan to catch.
  bool invalid = false;
};

// Empty when `text.text` is empty or `text.font` is invalid.
[[nodiscard]] std::vector<ParagraphRun> paragraph_runs(const FontCatalog& fonts,
                                                       const TextStyle& text);

}  // namespace dg::detail
