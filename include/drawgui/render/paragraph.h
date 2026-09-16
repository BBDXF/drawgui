// Measuring how tall a wrapped paragraph of text will be, before any node
// carrying it is laid out.
//
// doc/text-layout.md section 2 is the full argument; the short version: this
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
  // (doc/text-input.md's 7-2b cross-reference), built on skia::textlayout::
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

 private:
  struct Impl;
  explicit Paragraph(std::unique_ptr<Impl> impl);

  std::unique_ptr<Impl> impl_;
};

}  // namespace dg
