// dg::Paragraph's multi-line editing geometry (offset_at/caret_rect/
// range_rects/lines/line_of/word_at) and the paint-side paragraph cache
// (doc/limitations.md A1), against the generated test fonts.
//
// DgTest Latin is monospaced: at size 20 every glyph is EXACTLY 12px wide
// and every line box is 20px tall (baseline 16), so expected positions are
// counted by hand.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/anim/animation_engine.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/render/font_catalog.h"
#include "drawgui/render/node_lifecycle.h"
#include "drawgui/render/paragraph.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/shortcuts/action_scopes.h"
#include "drawgui/theme/theme_bindings.h"
#include "drawgui/widget/focus.h"
#include "drawgui/widget/interaction.h"
#include "drawgui/widget/widget_set.h"

#include "modules/skparagraph/include/Paragraph.h"
#include "render/paragraph_build.h"

namespace {

using dg::FontCatalog;
using dg::FontId;
using dg::Paragraph;
using dg::TextStyle;

struct Fonts {
  FontCatalog catalog;
  FontId latin;
};

Fonts make_fonts() {
  dg::Expected<FontCatalog, dg::FontError> scanned = FontCatalog::scan(DG_TEST_FONT_DIR);
  REQUIRE(scanned.has_value());
  FontCatalog catalog = std::move(scanned).value();
  dg::Expected<FontId, dg::FontError> latin = catalog.add("DgTest Latin", false);
  REQUIRE(latin.has_value());
  return Fonts{std::move(catalog), latin.value()};
}

TextStyle style_of(FontId font, std::string text) {
  TextStyle style;
  style.text = std::move(text);
  style.font = font;
  style.size = 20.0F;
  style.color = dg::Color::from_argb(0xFFFFFFFF);
  style.align = dg::TextAlign::kLeft;
  style.wrap = true;
  return style;
}

Paragraph build(const Fonts& fonts, const std::string& text, float width) {
  dg::Expected<Paragraph, dg::FontError> built =
      Paragraph::build(fonts.catalog, style_of(fonts.latin, text), width);
  REQUIRE(built.has_value());
  return std::move(built).value();
}

}  // namespace

TEST_CASE(
    "paragraph geometry: SkParagraph's hit-test/range/line/word APIs speak UTF-16 in this "
    "build, its glyph-cluster API UTF-8 - CJK + emoji offsets") {
  const Fonts fonts = make_fonts();
  // Bytes: A[0] U+4E2D[1,4) U+1F600[4,8) B[8]. UTF-16: A0 U+4E2D 1 U+1F600 2,3 B4.
  const std::string text = "A\u4e2d\U0001F600B";
  std::unique_ptr<skia::textlayout::Paragraph> raw =
      dg::detail::build_paragraph(fonts.catalog, style_of(fonts.latin, text), 1000.0F);
  REQUIRE(raw);
  const std::vector<skia::textlayout::TextBox> b_box =
      raw->getRectsForRange(4, 5, skia::textlayout::RectHeightStyle::kTight,
                            skia::textlayout::RectWidthStyle::kTight);
  REQUIRE(b_box.size() == 1);
  CHECK(b_box[0].rect.left() == doctest::Approx(36.0));  // UTF-16 index 4 is "B"
  CHECK(raw->getGlyphPositionAtCoordinate(100.0F, 5.0F).position == 5);  // 5 UTF-16 units
  std::vector<skia::textlayout::LineMetrics> lines;
  raw->getLineMetrics(lines);
  REQUIRE(lines.size() == 1);
  CHECK(lines[0].fEndIndex == 5);
  skia::textlayout::Paragraph::GlyphClusterInfo cluster;
  REQUIRE(raw->getGlyphClusterAt(8, &cluster));  // byte 8 is "B"
  CHECK(cluster.fBounds.left() == doctest::Approx(36.0));

  // dg::Paragraph hides all of that behind byte offsets.
  const Paragraph para = build(fonts, text, 1000.0F);
  CHECK(para.caret_rect(1).x == doctest::Approx(12.0));
  CHECK(para.caret_rect(4).x == doctest::Approx(24.0));
  CHECK(para.caret_rect(8).x == doctest::Approx(36.0));
  CHECK(para.caret_rect(9).x == doctest::Approx(48.0));
  CHECK(para.offset_at(26.0F, 5.0F) == 4);
  CHECK(para.offset_at(34.0F, 5.0F) == 8);
  CHECK(para.offset_at(500.0F, 5.0F) == 9);
  const std::vector<dg::PixelRectF> emoji = para.range_rects(4, 8);
  REQUIRE(emoji.size() == 1);
  CHECK(emoji[0].x == doctest::Approx(24.0));
  CHECK(emoji[0].width == doctest::Approx(12.0));
  REQUIRE(para.lines().size() == 1);
  CHECK(para.lines()[0].end_incl == 9);
}

TEST_CASE("paragraph geometry: offset_at/caret_rect round-trip over three soft-wrapped lines") {
  const Fonts fonts = make_fonts();
  // "AB " is 36px, so a 40px width holds one word per line.
  const Paragraph para = build(fonts, "AB CD EF", 40.0F);
  const std::vector<dg::LineInfo> lines = para.lines();
  REQUIRE(lines.size() == 3);
  CHECK(lines[0].start == 0);
  CHECK(lines[0].end_incl == 3);
  CHECK(lines[0].end_excl_newline == 3);
  CHECK(lines[1].start == 3);
  CHECK(lines[1].end_incl == 6);
  CHECK(lines[2].start == 6);
  CHECK(lines[2].end_incl == 8);
  CHECK(lines[0].top == doctest::Approx(0.0));
  CHECK(lines[1].top == doctest::Approx(20.0));
  CHECK(lines[2].top == doctest::Approx(40.0));
  CHECK(lines[1].height == doctest::Approx(20.0));
  CHECK(lines[1].baseline == doctest::Approx(36.0));

  for (int offset = 0; offset <= 8; ++offset) {
    CAPTURE(offset);
    const dg::CaretRect caret = para.caret_rect(offset);
    CHECK(caret.line == para.line_of(offset));
    CHECK(para.offset_at(caret.x, caret.top + (caret.height / 2.0F)) == offset);
  }

  // The wrap point has two carets: downstream at the start of line 1,
  // upstream at the end of line 0 (after the space).
  const dg::CaretRect down = para.caret_rect(3);
  CHECK(down.line == 1);
  CHECK(down.x == doctest::Approx(0.0));
  const dg::CaretRect up = para.caret_rect(3, true);
  CHECK(up.line == 0);
  CHECK(up.x == doctest::Approx(36.0));
  CHECK(up.top == doctest::Approx(0.0));

  // A selection across the wrap is one rectangle per line.
  CHECK(para.range_rects(1, 7).size() == 3);
  CHECK(para.word_at(4) == std::pair<int, int>{3, 5});
}

TEST_CASE("paragraph geometry: empty lines and a trailing newline own a caret") {
  const Fonts fonts = make_fonts();
  const Paragraph para = build(fonts, "ab\n\ncd\n", 1000.0F);
  const std::vector<dg::LineInfo> lines = para.lines();
  REQUIRE(lines.size() == 4);
  CHECK(lines[0].end_excl_newline == 2);
  CHECK(lines[0].end_incl == 3);
  CHECK(lines[1].start == 3);
  CHECK(lines[1].end_excl_newline == 3);
  CHECK(lines[1].end_incl == 4);
  CHECK(lines[2].start == 4);
  CHECK(lines[2].end_excl_newline == 6);
  CHECK(lines[2].end_incl == 7);
  CHECK(lines[3].start == 7);
  CHECK(lines[3].end_incl == 7);
  CHECK(lines[3].top == doctest::Approx(60.0));

  CHECK(para.line_of(2) == 0);
  CHECK(para.line_of(3) == 1);
  CHECK(para.line_of(7) == 3);

  const dg::CaretRect at_newline = para.caret_rect(2);
  CHECK(at_newline.line == 0);
  CHECK(at_newline.x == doctest::Approx(24.0));
  const dg::CaretRect empty = para.caret_rect(3);
  CHECK(empty.line == 1);
  CHECK(empty.x == doctest::Approx(0.0));
  CHECK(empty.top == doctest::Approx(20.0));
  // Upstream changes nothing after a HARD break.
  CHECK(para.caret_rect(3, true).line == 1);
  const dg::CaretRect tail = para.caret_rect(7);
  CHECK(tail.line == 3);
  CHECK(tail.x == doctest::Approx(0.0));
  CHECK(tail.top == doctest::Approx(60.0));

  CHECK(para.offset_at(100.0F, 5.0F) == 2);   // never past line 0's '\n'
  CHECK(para.offset_at(100.0F, 30.0F) == 3);  // the empty line
  CHECK(para.offset_at(100.0F, 50.0F) == 6);
  CHECK(para.offset_at(5.0F, 70.0F) == 7);  // the line after the trailing '\n'
  CHECK(para.offset_at(5.0F, 500.0F) == 7);
  CHECK(para.offset_at(-5.0F, -50.0F) == 0);
}

TEST_CASE("paragraph geometry: offset_at snaps to a grapheme boundary inside a ZWJ sequence") {
  const Fonts fonts = make_fonts();
  const std::string family = "\U0001F468\u200D\U0001F469\u200D\U0001F467\u200D\U0001F466";
  const std::string text = "A" + family + "B";
  const Paragraph para = build(fonts, text, 1000.0F);
  const int end_of_family = 1 + static_cast<int>(family.size());
  const float right = para.caret_rect(end_of_family).x;
  for (float x = 0.0F; x <= right + 30.0F; x += 1.0F) {
    CAPTURE(x);
    const int offset = para.offset_at(x, 5.0F);
    CHECK((offset == 0 || offset == 1 || offset == end_of_family ||
           offset == end_of_family + 1));
  }
}

namespace {

struct CacheScene {
  dg::RenderTree tree;
  dg::NodeId label;
  std::optional<dg::RasterSurface> surface = dg::RasterSurface::create(200, 120);

  static dg::TreeSpec spec_of(FontCatalog fonts) {
    dg::TreeSpec spec;
    spec.viewport = dg::PixelSize{200, 120};
    spec.background.fill = dg::Color::from_argb(0xFF000000);
    spec.fonts = std::move(fonts);
    return spec;
  }

  explicit CacheScene(Fonts fonts) : tree(spec_of(std::move(fonts.catalog))) {
    dg::NodeStyle style;
    style.text = style_of(fonts.latin, "AB CD EF GH");
    label = tree.add_child(tree.root(), dg::PixelRect{10, 10, 60, 100}, style);
    REQUIRE(surface.has_value());
  }

  std::uint64_t paint() {
    const std::uint64_t before = dg::detail::paragraph_build_count();
    tree.repaint_full(*surface);
    return dg::detail::paragraph_build_count() - before;
  }
};

}  // namespace

TEST_CASE("paragraph cache: a wrapped node is laid out once across repeated paints") {
  Fonts fonts = make_fonts();
  const FontId latin = fonts.latin;
  CacheScene scene{std::move(fonts)};

  CHECK(scene.paint() == 1);
  CHECK(scene.tree.paragraph_cache_size() == 1);
  CHECK(scene.paint() == 0);
  CHECK(scene.paint() == 0);

  // Moving without resizing keeps the layout.
  scene.tree.set_local_bounds(scene.label, dg::PixelRect{20, 10, 60, 100});
  CHECK(scene.paint() == 0);

  scene.tree.set_text(scene.label, style_of(latin, "AB CD"));
  CHECK(scene.paint() == 1);
  CHECK(scene.paint() == 0);

  scene.tree.set_local_bounds(scene.label, dg::PixelRect{20, 10, 90, 100});
  CHECK(scene.paint() == 1);
  CHECK(scene.paint() == 0);

  dg::NodeStyle restyled = scene.tree.style(scene.label);
  restyled.text.size = 24.0F;
  scene.tree.set_style(scene.label, restyled);
  CHECK(scene.paint() == 1);
}

TEST_CASE("paragraph cache: removing the node drops its entry") {
  CacheScene scene{make_fonts()};
  CHECK(scene.paint() == 1);
  REQUIRE(scene.tree.paragraph_cache_size() == 1);

  dg::WidgetSet widgets;
  dg::ThemeBindings theme_bindings;
  dg::Focus focus;
  dg::AnimationEngine animation;
  dg::ActionScopes action_scopes;
  dg::Interaction interaction;
  const dg::NodeSideTables tables{widgets,       theme_bindings, focus,
                                  animation,     action_scopes,  interaction};
  REQUIRE(dg::remove_subtree(scene.tree, scene.label, tables));
  CHECK(scene.tree.paragraph_cache_size() == 0);
  CHECK(scene.paint() == 0);
}
