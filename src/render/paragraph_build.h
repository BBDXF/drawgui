// Turning a TextStyle into a laid-out skia::textlayout::Paragraph.
//
// The one place this project builds a ParagraphBuilder (the FontCollection
// it draws on is built once per FontCatalog, FontAccess::
// paragraph_collection()). Both
// dg::Paragraph (the public measurement API, paragraph.cpp) and skia_paint.
// cpp's paragraph paint path call this, so the two cannot disagree about how
// a paragraph is built - the same "one function, not two copies" argument
// WidgetSet::text_field_commit() already makes for the text field's shared
// projection logic.

#pragma once

#include <cstdint>
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

// One wrapped text node's laid-out paragraph, kept between paints. Valid
// exactly while `fonts`, `text` and `width` still match what the node would
// build now - RenderTree also drops it eagerly on set_text()/set_style()/a
// width change and on removal, but paint re-checks the key regardless, so a
// style change through any other path can never paint a stale layout.
struct ParagraphCacheEntry {
  ParagraphCacheEntry();
  ParagraphCacheEntry(const ParagraphCacheEntry&) = delete;
  ParagraphCacheEntry& operator=(const ParagraphCacheEntry&) = delete;
  ParagraphCacheEntry(ParagraphCacheEntry&&) = delete;
  ParagraphCacheEntry& operator=(ParagraphCacheEntry&&) = delete;
  ~ParagraphCacheEntry();

  const FontCatalog* fonts = nullptr;
  TextStyle text;
  float width = 0.0F;
  std::unique_ptr<skia::textlayout::Paragraph> paragraph;
};

// How many times build_paragraph() has run in this process - a debug/test
// probe for the paint-side paragraph cache (RenderTree's Node::
// paragraph_cache), whose whole point is that repeated paints of an
// unchanged wrapped node stop calling build_paragraph() at all.
[[nodiscard]] std::uint64_t paragraph_build_count();

}  // namespace dg::detail
