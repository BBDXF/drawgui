// Turning a TextStyle into a laid-out skia::textlayout::Paragraph.
//
// The one place this project builds a FontCollection/ParagraphBuilder. Both
// dg::Paragraph (the public measurement API, paragraph.cpp) and skia_paint.
// cpp's paragraph paint path call this, so the two cannot disagree about how
// a paragraph is built - the same "one function, not two copies" argument
// WidgetSet::text_field_replace_range already makes for insert/backspace/
// delete's shared projection logic.

#pragma once

#include <memory>

#include "drawgui/render/font_catalog.h"
#include "drawgui/render/render_tree.h"

namespace skia::textlayout {
class Paragraph;
}  // namespace skia::textlayout

namespace dg::detail {

// Null when `text.font` is invalid or `text.text` is empty - the same "draw
// nothing" outcome the plain SkFont path already gives paint_text() for the
// identical inputs, so a caller does not have to special-case this seam
// differently from the one it replaces.
[[nodiscard]] std::unique_ptr<skia::textlayout::Paragraph> build_paragraph(
    const FontCatalog& fonts, const TextStyle& text, float width);

}  // namespace dg::detail
