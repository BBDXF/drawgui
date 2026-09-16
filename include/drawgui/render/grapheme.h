// Grapheme-cluster boundaries: the font-INDEPENDENT half of 7-2b's cursor
// movement, kept deliberately separate from dg::Paragraph's caret_x()
// (the font/shaping-DEPENDENT half, paragraph.h).
//
// Why two seams rather than one: dg::Paragraph is skia::textlayout::
// Paragraph's own "editing API" (getGlyphClusterAt/getClosestGlyphClusterAt),
// and measuring it directly (doc/text-input.md's 7-2b cross-reference)
// found its clustering is a SHAPING concept - when the active font has no
// glyph for a codepoint sequence at all (no ligature, no colour glyph), each
// codepoint shapes as its own separate .notdef box and reports its own
// separate cluster, not one. That is the exact case the ZWJ family emoji /
// skin-tone modifier / regional-indicator flag acceptance criterion cannot
// tolerate - a TextField's content font may not carry emoji coverage at
// all, and correctness of BACKSPACE cannot depend on whether it does.
//
// design.md section 5.10.2 names the actual dependency directly: grapheme
// segmentation is `SkUnicode`'s job, not SkParagraph's - this file calls
// `SkUnicode::computeCodeUnitFlags()` (the exact primitive 7-1's smoke test
// already proved treats a ZWJ family emoji as one cluster), the same
// `SkUnicodes::Libgrapheme::Make()` backend paragraph_build.cpp already
// links, just a second call site for a capability already proven rather
// than a new dependency.
//
// No Skia type appears below, matching every other header at this layer.

#pragma once

#include <string_view>
#include <vector>

namespace dg {

// Byte offsets of every user-perceived-character (Unicode extended grapheme
// cluster, UAX #29) boundary in `utf8`, in ascending order - ALWAYS
// including 0 and utf8.size(), so N clusters produce a boundary vector of
// size N+1 (an empty string still produces {0}, its own only legal cursor
// position). `utf8` must already be well-formed (dg::sanitize_utf8()) -
// every caller in this codebase reaches this only through a TextField model
// string, which text_field_insert()'s sanitizer already guarantees.
[[nodiscard]] std::vector<int> grapheme_boundaries(std::string_view utf8);

}  // namespace dg
