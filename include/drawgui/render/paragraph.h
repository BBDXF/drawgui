// Measuring how tall a wrapped paragraph of text will be, before any node
// carrying it is laid out.
//
// The full argument, in short: this
// project's LayoutTree (include/drawgui/layout/box.h) knows nothing about
// text at all - BoxStyle carries no font, no string, nothing render_tree.h's
// TextStyle owns - and 7-2 keeps it that way rather than teaching it to ask a
// FontCatalog a question mid-layout. A wrapping paragraph's height is a
// function of the WIDTH it is given, exactly the same shape design.md
// section 5.10.3 already forces on a decoded image: the box must have a
// determinate size before its content is resolved, never the other way
// round. Paragraph::build() is what a caller runs BEFORE constructing (or
// resizing) a node that wraps text, so the resulting height can be handed to
// BoxStyle::height/RenderTree the same way an image's aspect_ratio already
// is - LayoutTree itself never measures a byte of text.
//
// No Skia type appears below, for the identical reason font_catalog.h names
// none: the pimpl hides skia::textlayout::Paragraph entirely.

#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "drawgui/base/expected.h"
#include "drawgui/render/font_catalog.h"
#include "drawgui/render/render_tree.h"

namespace dg {

// What a laid-out paragraph reports about itself.
struct ParagraphMetrics {
  // Device pixels, rounded up so a caller that hands this straight to
  // BoxStyle::height never clips the last fractional row of text.
  int height = 0;

  int line_count = 0;

  // True when `TextStyle::max_lines` cut real content off the end - the
  // multi-line counterpart of the single-line TextField's ellipsize(),
  // reported rather than silently absorbed.
  bool exceeded_max_lines = false;
};

// A float rectangle in a paragraph's own coordinates (its top-left is 0,0).
struct PixelRectF {
  float x = 0.0F;
  float y = 0.0F;
  float width = 0.0F;
  float height = 0.0F;
};

// Where a caret at some offset is drawn: a vertical bar at `x` spanning
// [top, top + height), on line `line` (an index into Paragraph::lines()).
struct CaretRect {
  float x = 0.0F;
  float top = 0.0F;
  float height = 0.0F;
  int line = 0;
};

// One visual line. `start`..`end_incl` is every byte the line owns,
// including a terminating '\n'; `end_excl_newline` stops before that '\n'
// (equal to `end_incl` on a soft-wrapped or final line). Lines tile the
// text: each `start` is the previous line's `end_incl`. A text ending in
// '\n' has a final EMPTY line (start == end_incl == text size) - the line
// a caret after that newline sits on.
struct LineInfo {
  int start = 0;
  int end_excl_newline = 0;
  int end_incl = 0;
  float top = 0.0F;
  float height = 0.0F;
  float baseline = 0.0F;
};

// A laid-out paragraph, built once against a fixed width. Immutable: a
// caller who wants a different width builds a new one, matching every other
// measurement primitive in this project (dg::measure_ascii_width() is the
// same shape one field over) rather than growing an in-place re-layout this
// slice's scope does not need.
class Paragraph {
 public:
  // `width` is device pixels, the same unit every other measurement in this
  // project uses. Fails the same way the plain SkFont path already silently
  // draws nothing for: an invalid `text.font`, an empty `text.text`, or a
  // non-positive `text.size`.
  [[nodiscard]] static Expected<Paragraph, FontError> build(const FontCatalog& fonts,
                                                            const TextStyle& text, float width);

  Paragraph(Paragraph&&) noexcept;
  Paragraph& operator=(Paragraph&&) noexcept;
  Paragraph(const Paragraph&) = delete;
  Paragraph& operator=(const Paragraph&) = delete;
  ~Paragraph();

  [[nodiscard]] ParagraphMetrics metrics() const;

  // The pixel x-position (relative to this paragraph's own left edge) where
  // a caret sitting at BYTE offset `offset` would be drawn - the
  // shaping-aware replacement for 4-9's measure_ascii_width()/
  // ascii_offset_at_x() per-substring SkFont::measureText calls
  // (7-2b's own investigation), built on skia::textlayout::
  // Paragraph's own byte-offset-native "Editing API"
  // (getGlyphClusterAt) - confirmed byte-offset-native (not the UTF-16-
  // offset legacy Flutter API design.md section 5.13.2 warns about) by
  // direct measurement against the linked archive, not assumed from Skia's
  // own naming.
  //
  // `offset` must be in [0, text length]. It is not required to land on a
  // grapheme-cluster boundary (dg::grapheme_boundaries()) for this call
  // itself to return a sensible pixel value - every CALLER in this codebase
  // only ever passes one, because that is the only place a caret is ever
  // drawn, but this function does no snapping of its own, matching the "one
  // function does one thing" shape every other primitive at this layer
  // already has.
  [[nodiscard]] float caret_x(int offset) const;

  // Multi-line editing geometry. Every offset here is a UTF-8 BYTE offset
  // into the TextStyle::text the paragraph was built from, exactly like
  // caret_x(). SkParagraph's own hit-test/range/line/word queries
  // (getGlyphPositionAtCoordinate, getRectsForRange, getLineMetrics,
  // getWordBoundary) were MEASURED to speak UTF-16 code units in the linked
  // archive (tests/unit/test_paragraph.cpp, "CJK + emoji offsets") - only
  // the getGlyphCluster* family is UTF-8 - so these convert both ways
  // internally rather than leaking a second offset unit to callers.

  // The grapheme-cluster boundary nearest the point (x, y), in paragraph
  // coordinates. `y` picks the line (clamped to the first/last); `x` is then
  // resolved within that line and never lands past its '\n'.
  [[nodiscard]] int offset_at(float x, float y) const;

  // The caret for `offset` (clamped to the text). An offset that is both
  // the end of one soft-wrapped line and the start of the next is drawn at
  // the START of the next line unless `upstream` asks for the end of the
  // previous one. Empty lines (including the one after a trailing '\n')
  // get a caret from their own line metrics.
  [[nodiscard]] CaretRect caret_rect(int offset, bool upstream = false) const;

  // The highlight rectangles covering [start, end) - one or more per line.
  [[nodiscard]] std::vector<PixelRectF> range_rects(int start, int end) const;

  // Every visual line, top to bottom. Never empty for a built paragraph.
  [[nodiscard]] std::vector<LineInfo> lines() const;

  // The index into lines() of the line containing `offset` (downstream: a
  // soft-wrap boundary belongs to the later line).
  [[nodiscard]] int line_of(int offset) const;

  // The [start, end) byte range of the word (or run of whitespace/
  // punctuation, by SkParagraph's own word segmentation) containing
  // `offset`.
  [[nodiscard]] std::pair<int, int> word_at(int offset) const;

 private:
  struct Impl;
  explicit Paragraph(std::unique_ptr<Impl> impl);

  std::unique_ptr<Impl> impl_;
};

}  // namespace dg
