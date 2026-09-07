// Internal access to a FontCatalog's typefaces and to the runs a string breaks
// into.
//
// The public header names no Skia type, and the paint path needs one. This
// header is the seam between those two facts: it lives under src/, nothing
// outside the library can include it, and it exists so that skia_paint.cpp can
// turn a FontId into an SkTypeface, and a TextStyle into a sequence of faces,
// without FontCatalog growing a public accessor that leaks Skia into include/.

#pragma once

#include <cstddef>
#include <vector>

#include "drawgui/render/font_catalog.h"
#include "drawgui/render/render_tree.h"

#include "include/core/SkRefCnt.h"
#include "include/core/SkTypeface.h"

namespace dg {

// A maximal stretch of one string drawn by one face.
struct TextRun {
  // Never null. A run that nothing covers carries the PRIMARY typeface, so the
  // .notdef box it draws has the metrics of the font the text asked for.
  sk_sp<SkTypeface> typeface;

  // Byte offsets into TextStyle::text.
  std::size_t begin = 0;
  std::size_t end = 0;

  // How many codepoints the run spans. Only the missing-glyph path needs it -
  // it draws one .notdef per codepoint - but it is filled in either way so the
  // two run kinds have one shape.
  std::size_t codepoints = 0;

  // True when no face on this machine covers these codepoints, including when
  // the bytes are not valid UTF-8. The paint path draws glyph 0 rather than
  // the string's own glyphs, which is the deliberate, visible outcome: a
  // missing glyph must never be an invisible one.
  bool missing = false;
};

struct FontAccess {
  // Null when `id` names nothing in `catalog`, including the zero id. The
  // caller draws no text rather than substituting a family.
  [[nodiscard]] static sk_sp<SkTypeface> typeface(const FontCatalog& catalog, FontId id);

  // Empty when the style names no valid font or carries no text. Otherwise
  // every byte of the string is covered by exactly one run, in order.
  [[nodiscard]] static std::vector<TextRun> runs(const FontCatalog& catalog,
                                                 const TextStyle& text);
};

}  // namespace dg
