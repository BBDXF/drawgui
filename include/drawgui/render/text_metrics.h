// ASCII-only text measurement for cursor positioning and click-to-offset hit
// testing - the two questions a TextField needs answered that a Label never
// asks.
//
// Built directly on SkFont::measureText, the same primitive
// src/render/skia_paint.cpp paints every string through, so a measured width
// always agrees with what will actually be rasterized - never an
// independently-derived metric that could disagree with the rasterizer's own
// idea of a glyph's advance.
//
// Scoped to ASCII (bytes 0x20-0x7E) on purpose: doc/text-input.md section 1
// records why. In short, a TextField's content boundary is ASCII for this
// slice, and for ASCII a byte offset, a codepoint offset and a
// grapheme-cluster boundary are the same number, so measuring by BYTE OFFSET
// needs none of skia_paint.cpp's run-splitting/fallback machinery (built for
// multi-script Labels, doc/font-fallback.md) - a single face covers the whole
// string by construction. Passing non-ASCII text measures whatever SkFont
// does with it, but nothing in this library's TextField widget ever calls
// here with any - WidgetSet::text_field_insert() filters at the boundary.
//
// No Skia type appears below, matching every other header at this layer.

#pragma once

#include <cstddef>
#include <string_view>

#include "drawgui/render/font_catalog.h"

namespace dg {

// The width, in device pixels, that `ascii_text` occupies when painted with
// `font` at `size`. Zero when `font` is invalid, `size` is non-positive or
// the text is empty.
[[nodiscard]] float measure_ascii_width(const FontCatalog& fonts, FontId font, float size,
                                        std::string_view ascii_text);

// The byte offset in [0, ascii_text.size()] whose glyph boundary is nearest
// `local_x` device pixels from the text's own left edge (the caller has
// already subtracted any scroll offset and left inset). Snaps to whichever
// side of a glyph's advance `local_x` is closer to the midpoint of, which is
// what makes clicking a character's left half land the cursor before it and
// its right half land the cursor after it.
[[nodiscard]] std::size_t ascii_offset_at_x(const FontCatalog& fonts, FontId font, float size,
                                            std::string_view ascii_text, float local_x);

}  // namespace dg
