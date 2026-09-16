// TextField: hand-derived cursor/selection geometry against the deterministic
// test font (DgTest Latin, 12px advance per glyph at size 20 - see
// tests/fonts/gen_test_fonts.py: ADVANCE=600, UNITS_PER_EM=1000, so every
// glyph is 0.6 * size pixels wide, monospaced), plus the model-level edit
// operations (insert/backspace/delete/move/click/selection).
//
// Built directly on RenderTree, mirroring test_form_controls.cpp's own
// precedent for exactly the same reason: LayoutTree's `kLeaf` child clamp
// (doc/form-controls.md section 4) is a real constraint for widgets whose
// children are sized independently of their parent, and this file wants
// exact, hand-derived pixel geometry rather than whatever a layout pass
// happens to produce.

#include <cstddef>
#include <limits>
#include <optional>
#include <string>

#include <doctest/doctest.h>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/types.h"
#include "drawgui/render/font_catalog.h"
#include "drawgui/render/grapheme.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/widget/focus.h"
#include "drawgui/widget/widget_set.h"

namespace {

using dg::Color;
using dg::FontCatalog;
using dg::FontId;
using dg::NodeId;
using dg::NodeStyle;
using dg::PixelRect;
using dg::PixelSize;
using dg::RenderTree;
using dg::TextAlign;
using dg::TextFieldMove;
using dg::TextSelection;
using dg::TreeSpec;
using dg::Widget;
using dg::WidgetKind;
using dg::WidgetSet;

// Every ASCII glyph in the generated test font is 600/1000 em wide - see
// this file's own top comment. At size 20 that is exactly 12 device pixels,
// chosen so every expected value below is an integer a reader can check by
// counting characters.
constexpr float kFontSize = 20.0F;
constexpr float kGlyphWidth = 12.0F;

TreeSpec spec_for(PixelSize size = PixelSize{400, 300}) {
  TreeSpec spec;
  spec.viewport = size;
  spec.background.fill = Color::from_argb(0xFF14171C);
  return spec;
}

FontCatalog test_fonts() {
  const dg::Expected<FontCatalog, dg::FontError> scanned = FontCatalog::scan(DG_TEST_FONT_DIR);
  REQUIRE(scanned.has_value());
  return scanned.value();
}

struct FieldScene {
  RenderTree tree;
  WidgetSet widgets;
  FontCatalog fonts;
  FontId font;
  NodeId field;
};

// `width`/`height` are the field's OWN local bounds, which is also the
// visible text area: like a slider's track (doc/form-controls.md section
// 1.4's own "no padding" argument), a TextField's own kLeaf carries no
// padding, so its local_bounds() IS the room text/caret/highlight have.
FieldScene build_field(int width, int height, const std::string& initial) {
  RenderTree tree{spec_for()};
  WidgetSet widgets;
  FontCatalog fonts = test_fonts();
  const dg::Expected<FontId, dg::FontError> font_id = fonts.add("DgTest Latin", false);
  REQUIRE(font_id.has_value());
  const FontId font = font_id.value();

  NodeStyle field_style;
  field_style.overflow = dg::Overflow::kClip;
  const NodeId field =
      tree.add_child(RenderTree::root(), PixelRect{0, 0, width, height}, field_style);

  const NodeId highlight = tree.add_child(field, PixelRect{0, 0, 0, height}, NodeStyle{});
  const NodeId underline = tree.add_child(field, PixelRect{0, 0, 0, height}, NodeStyle{});

  NodeStyle content_style;
  content_style.text.font = font;
  content_style.text.size = kFontSize;
  content_style.text.color = Color::from_argb(0xFFE8EDF4);
  content_style.text.align = TextAlign::kLeft;
  content_style.text.text = initial;
  const NodeId content = tree.add_child(field, PixelRect{0, 0, 1, height}, content_style);

  NodeStyle caret_style;
  caret_style.fill = Color::from_argb(0xFFFFFFFF);
  const NodeId caret = tree.add_child(field, PixelRect{0, 0, 2, height}, caret_style);

  Widget widget;
  widget.kind = WidgetKind::kTextField;
  widget.content = content;
  widget.caret = caret;
  widget.selection_highlight = highlight;
  widget.composition_underline = underline;
  widget.text = initial;
  widget.cursor = static_cast<int>(initial.size());
  widgets.attach(field, widget);

  return FieldScene{std::move(tree), std::move(widgets), std::move(fonts), font, field};
}

// Fails the test case and returns a dummy value when `field` has no active
// selection, otherwise returns it unwrapped - hoisted out of every case that
// needs one, for two reasons at once. First, test_hit_test.cpp's own
// documented one: clang-tidy cannot model doctest's REQUIRE, so a REQUIRE
// followed by a dereference reads to clang-analyzer as an unchecked optional
// access, and an ordinary `if`+`return` here (not a REQUIRE, and in a
// function clang-analyzer CAN see the check and the dereference inside of
// together) avoids that. Second: inlining even that `if` into three test
// cases already thick with CHECK-macro expansions pushed each one over
// clang-tidy's cognitive-complexity budget - the same reason
// test_font_coverage.cpp and test_clip.cpp hoist theirs.
TextSelection require_selection(const WidgetSet& widgets, NodeId field) {
  const std::optional<TextSelection> selection = widgets.text_field_selection(field);
  if (!selection.has_value()) {
    FAIL("expected an active selection");
    return TextSelection{};
  }
  return *selection;
}

// Fails the test case unless `text` is well-formed UTF-8 by dg::
// grapheme_boundaries()'s own definition of that (its first boundary is 0,
// its last is text.size()) - the one invariant the malformed-UTF-8 stress
// test below exists to check, after every single edit rather than once at
// the end.
void require_well_formed(const std::string& text) {
  const std::vector<int> boundaries = dg::grapheme_boundaries(text);
  CHECK(boundaries.front() == 0);
  CHECK(boundaries.back() == static_cast<int>(text.size()));
}

void move_cursor_to(FieldScene& scene, int offset) {
  scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field, TextFieldMove::kLineStart,
                                false);
  for (int step = 0; step < offset; ++step) {
    scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field,
                                  TextFieldMove::kCharRight, false);
  }
}

// One offset of the malformed-UTF-8 stress test below: insert `hostile` at
// `offset` into a fresh copy of `base`, check the model is still
// well-formed, then Backspace it away one grapheme cluster at a time -
// checking well-formedness after EVERY Backspace, not only at the end -
// until the field is empty again.
void stress_insert_and_backspace_at(const std::string& base, const std::string& hostile,
                                    int offset) {
  FieldScene scene = build_field(400, 30, base);
  move_cursor_to(scene, offset);
  scene.widgets.text_field_insert(scene.tree, scene.fonts, scene.field, hostile);
  const std::string after_insert = scene.widgets.text_field_text(scene.field);
  require_well_formed(after_insert);

  for (int i = 0; i < static_cast<int>(after_insert.size()) + 2; ++i) {
    scene.widgets.text_field_backspace(scene.tree, scene.fonts, scene.field);
    require_well_formed(scene.widgets.text_field_text(scene.field));
    scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field, TextFieldMove::kLineEnd,
                                  false);
  }
  CHECK(scene.widgets.text_field_text(scene.field).empty());
}

// The other half of the stress test: select across the ZWJ family emoji's
// own cluster boundary (byte range [5, base.size()) of `base` - the emoji
// sequence plus the trailing "C") and replace it with `hostile` in one
// text_field_insert() call, checking the result is well-formed and the
// selection was actually replaced rather than merged into.
void stress_select_and_replace(const std::string& base, const std::string& hostile) {
  FieldScene scene = build_field(400, 30, base);
  move_cursor_to(scene, 5);
  scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field, TextFieldMove::kLineEnd,
                                true);
  scene.widgets.text_field_insert(scene.tree, scene.fonts, scene.field, hostile);
  const std::string replaced = scene.widgets.text_field_text(scene.field);
  require_well_formed(replaced);
  CHECK(
      replaced.starts_with("A\xE4\xB8\xAD"
                           "B"));
}

}  // namespace

// ----------------------------------------------------------------------------
// The model: insert, backspace, delete, move, click.
// ----------------------------------------------------------------------------

TEST_CASE("insert appends at the cursor and advances it") {
  FieldScene scene = build_field(200, 30, "");
  CHECK(scene.widgets.text_field_insert(scene.tree, scene.fonts, scene.field, "ABC"));
  CHECK(scene.widgets.text_field_text(scene.field) == "ABC");
  CHECK(scene.widgets.text_field_cursor(scene.field) == 3);

  CHECK(scene.widgets.text_field_insert(scene.tree, scene.fonts, scene.field, "XY"));
  CHECK(scene.widgets.text_field_text(scene.field) == "ABCXY");
  CHECK(scene.widgets.text_field_cursor(scene.field) == 5);
}

TEST_CASE("insert accepts well-formed multi-byte UTF-8 whole, unlike 4-9's ASCII filter") {
  FieldScene scene = build_field(200, 30, "");
  // 0xC3 0xA9 is UTF-8 for U+00E9 (e-acute), well-formed and no longer
  // dropped - 7-2b lifted the printable-ASCII-only restriction.
  CHECK(scene.widgets.text_field_insert(scene.tree, scene.fonts, scene.field,
                                        "e\xC3\xA9"
                                        "e"));
  CHECK(scene.widgets.text_field_text(scene.field) ==
        "e\xC3\xA9"
        "e");
  CHECK(scene.widgets.text_field_cursor(scene.field) == 4);
}

TEST_CASE("insert strips ASCII control characters but keeps everything else") {
  FieldScene scene = build_field(200, 30, "");
  // A literal newline/tab/DEL a paste or IME might commit has no meaning in
  // this single-line field (multi-line editing stays out of 7-2b's scope);
  // everything else, including the multi-byte 'e-acute' either side, is
  // kept.
  CHECK(scene.widgets.text_field_insert(scene.tree, scene.fonts, scene.field,
                                        "e\x01\x09\x7F\xC3\xA9"));
  CHECK(scene.widgets.text_field_text(scene.field) == "e\xC3\xA9");

  // An all-filtered insert with no selection reports no change.
  CHECK_FALSE(
      scene.widgets.text_field_insert(scene.tree, scene.fonts, scene.field, "\x01\x02"));
  CHECK(scene.widgets.text_field_text(scene.field) == "e\xC3\xA9");
}

TEST_CASE("insert repairs malformed UTF-8 to U+FFFD rather than dropping it whole") {
  FieldScene scene = build_field(200, 30, "");
  // A lone continuation byte between two ASCII letters - the same
  // dg::sanitize_utf8() policy src/render/paragraph_build.cpp already uses
  // for SkParagraph::addText()'s boundary (doc/text-layout.md section 3),
  // reused here rather than a second hand-rolled substitution.
  CHECK(scene.widgets.text_field_insert(scene.tree, scene.fonts, scene.field,
                                        "A\x80"
                                        "B"));
  CHECK(scene.widgets.text_field_text(scene.field) ==
        "A\xEF\xBF\xBD"
        "B");
  CHECK(scene.widgets.text_field_cursor(scene.field) == 5);  // 'A' + 3-byte U+FFFD + 'B'
}

// The -DDG_SANITIZE=ON stress gate the task itself asks for: every one of
// design.md section 5.13.3's named ill-formed shapes (a lone continuation
// byte, a truncated multi-byte lead, an overlong encoding, a lone
// surrogate), inserted at EVERY byte offset of an existing string (not just
// the start/end this file's other tests already cover), then deleted again
// by Backspace at every offset, then replaced via a selection that spans a
// multi-byte grapheme-cluster boundary - through the real EDIT operations
// (text_field_insert/backspace/move/insert-over-selection), never a
// display-only path. Nothing here asserts a specific string back (that is
// what the hand-derived tests above and below are for); this test's whole
// job is to give ASan/UBSan every offset to find a bug at, the same
// discipline 7-2's own defect-injection campaign used to find the
// ill-formed-bytes-hang in the display path (doc/text-layout.md section 4)
// - reused here for the editing path rather than assumed safe by analogy.
TEST_CASE(
    "malformed UTF-8 injected at every insertion offset, backspaced at every offset, and "
    "selected-and-replaced across cluster boundaries never corrupts the model or crashes") {
  const std::vector<std::string> hostile_shapes = {
      std::string("\x80"),              // lone continuation byte
      std::string("\xE4\xB8"),          // truncated three-byte lead
      std::string("\xF0\x9F\x98"),      // truncated four-byte lead
      std::string("\xC0\x80"),          // overlong two-byte encoding of NUL
      std::string("\xE0\x80\xAF"),      // overlong three-byte encoding of '/'
      std::string("\xED\xA0\x80"),      // lone (unpaired) surrogate U+D800
      std::string("\xF5\x80\x80\x80"),  // beyond U+10FFFF
  };
  const std::string base =
      "A\xE4\xB8\xAD"
      "B"
      "\U0001F468\u200D\U0001F469\u200D\U0001F467\u200D\U0001F466"
      "C";

  for (const std::string& hostile : hostile_shapes) {
    CAPTURE(hostile);
    // Insert at every offset of `base` in turn, always starting from a
    // fresh, well-formed copy - the "at every offset" the task asks for,
    // not merely the boundary offsets this file's other tests already
    // exercise.
    for (int offset = 0; offset <= static_cast<int>(base.size()); ++offset) {
      stress_insert_and_backspace_at(base, hostile, offset);
    }
    stress_select_and_replace(base, hostile);
  }
}

TEST_CASE("insert with an active selection replaces it") {
  FieldScene scene = build_field(200, 30, "ABCDE");
  scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field, TextFieldMove::kLineStart,
                                false);
  scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field, TextFieldMove::kCharRight,
                                true);
  scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field, TextFieldMove::kCharRight,
                                true);
  // Selection is now [0, 2) - "AB".
  CHECK(scene.widgets.text_field_insert(scene.tree, scene.fonts, scene.field, "X"));
  CHECK(scene.widgets.text_field_text(scene.field) == "XCDE");
  CHECK(scene.widgets.text_field_cursor(scene.field) == 1);
  CHECK_FALSE(scene.widgets.text_field_selection(scene.field).has_value());
}

TEST_CASE("backspace deletes one byte before the cursor, and is a no-op at position 0") {
  FieldScene scene = build_field(200, 30, "ABC");
  CHECK(scene.widgets.text_field_backspace(scene.tree, scene.fonts, scene.field));
  CHECK(scene.widgets.text_field_text(scene.field) == "AB");
  CHECK(scene.widgets.text_field_cursor(scene.field) == 2);

  scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field, TextFieldMove::kLineStart,
                                false);
  CHECK_FALSE(scene.widgets.text_field_backspace(scene.tree, scene.fonts, scene.field));
  CHECK(scene.widgets.text_field_text(scene.field) == "AB");
  CHECK(scene.widgets.text_field_cursor(scene.field) == 0);
}

TEST_CASE("delete-forward removes the byte at the cursor, and is a no-op at the end") {
  FieldScene scene = build_field(200, 30, "ABC");
  scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field, TextFieldMove::kLineStart,
                                false);
  CHECK(scene.widgets.text_field_delete_forward(scene.tree, scene.fonts, scene.field));
  CHECK(scene.widgets.text_field_text(scene.field) == "BC");
  CHECK(scene.widgets.text_field_cursor(scene.field) == 0);

  scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field, TextFieldMove::kLineEnd,
                                false);
  CHECK_FALSE(scene.widgets.text_field_delete_forward(scene.tree, scene.fonts, scene.field));
  CHECK(scene.widgets.text_field_text(scene.field) == "BC");
}

TEST_CASE("backspace/delete-forward with a selection delete the whole range") {
  FieldScene scene = build_field(200, 30, "ABCDE");
  scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field, TextFieldMove::kLineStart,
                                false);
  scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field, TextFieldMove::kCharRight,
                                true);
  scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field, TextFieldMove::kCharRight,
                                true);
  // Selection [0, 2).
  CHECK(scene.widgets.text_field_backspace(scene.tree, scene.fonts, scene.field));
  CHECK(scene.widgets.text_field_text(scene.field) == "CDE");
  CHECK(scene.widgets.text_field_cursor(scene.field) == 0);
}

// ----------------------------------------------------------------------------
// Grapheme-cluster editing (7-2b): backspace/delete/Left/Right move by
// user-perceived character, never by byte - the acceptance shape design.md
// section 5.10.2 itself names (a ZWJ family emoji, a skin-tone modifier, a
// regional-indicator flag pair each count as ONE cluster), pinned here at
// the TextField EDITING seam rather than only at the raw SkUnicode level
// test_grapheme.cpp and 7-1's smoke test already pin. DgTest Latin (this
// file's font throughout) has no glyph for any of these codepoints - on
// purpose: grapheme-cluster correctness must not depend on font coverage
// (doc/text-input.md's cross-reference to doc/text-layout.md section 2
// records the measurement that found SkParagraph's OWN glyph clustering is
// font-dependent, which is why cursor movement is built on
// dg::grapheme_boundaries() rather than on it).
// ----------------------------------------------------------------------------

TEST_CASE("backspace deletes an entire ZWJ family emoji in one step, not one codepoint") {
  const std::string family_emoji =
      "\U0001F468\u200D\U0001F469\u200D\U0001F467\u200D\U0001F466";  // 25 bytes, one cluster
  FieldScene scene = build_field(400, 30, "A" + family_emoji + "B");
  REQUIRE(scene.widgets.text_field_cursor(scene.field) ==
          static_cast<int>(2 + family_emoji.size()));

  scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field, TextFieldMove::kCharLeft,
                                false);
  // One Left step lands BEFORE "B" and AFTER the whole emoji sequence -
  // never inside it.
  REQUIRE(scene.widgets.text_field_cursor(scene.field) ==
          static_cast<int>(1 + family_emoji.size()));

  CHECK(scene.widgets.text_field_backspace(scene.tree, scene.fonts, scene.field));
  // The entire 25-byte sequence is gone in ONE backspace, leaving "AB".
  CHECK(scene.widgets.text_field_text(scene.field) == "AB");
  CHECK(scene.widgets.text_field_cursor(scene.field) == 1);
}

TEST_CASE("backspace deletes an entire skin-tone-modified emoji in one step") {
  const std::string thumbs_up_medium = "\U0001F44D\U0001F3FD";  // 8 bytes, one cluster
  FieldScene scene = build_field(400, 30, "X" + thumbs_up_medium);
  REQUIRE(scene.widgets.text_field_cursor(scene.field) ==
          static_cast<int>(1 + thumbs_up_medium.size()));
  CHECK(scene.widgets.text_field_backspace(scene.tree, scene.fonts, scene.field));
  CHECK(scene.widgets.text_field_text(scene.field) == "X");
}

TEST_CASE("backspace deletes an entire regional-indicator flag pair in one step") {
  const std::string flag_cn = "\U0001F1E8\U0001F1F3";  // 8 bytes, one cluster
  FieldScene scene = build_field(400, 30, "X" + flag_cn);
  REQUIRE(scene.widgets.text_field_cursor(scene.field) == static_cast<int>(1 + flag_cn.size()));
  CHECK(scene.widgets.text_field_backspace(scene.tree, scene.fonts, scene.field));
  CHECK(scene.widgets.text_field_text(scene.field) == "X");
}

TEST_CASE("delete-forward removes one CJK character at a time, not one byte") {
  // U+4E2D U+6587 - "中文", each 3 UTF-8 bytes, two clusters.
  FieldScene scene = build_field(400, 30, "\xE4\xB8\xAD\xE6\x96\x87");
  scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field, TextFieldMove::kLineStart,
                                false);
  CHECK(scene.widgets.text_field_delete_forward(scene.tree, scene.fonts, scene.field));
  CHECK(scene.widgets.text_field_text(scene.field) == "\xE6\x96\x87");
  CHECK(scene.widgets.text_field_cursor(scene.field) == 0);
}

TEST_CASE("Left/Right move by grapheme cluster across mixed ASCII and CJK text") {
  // "A" + "中" (3 bytes) + "B" - three clusters, boundaries at 0, 1, 4, 5.
  FieldScene scene = build_field(400, 30,
                                 "A\xE4\xB8\xAD"
                                 "B");
  scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field, TextFieldMove::kLineStart,
                                false);
  REQUIRE(scene.widgets.text_field_cursor(scene.field) == 0);
  scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field, TextFieldMove::kCharRight,
                                false);
  CHECK(scene.widgets.text_field_cursor(scene.field) == 1);
  scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field, TextFieldMove::kCharRight,
                                false);
  // One more Right clears the WHOLE 3-byte "中", landing at 4, not 2 or 3.
  CHECK(scene.widgets.text_field_cursor(scene.field) == 4);
  scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field, TextFieldMove::kCharLeft,
                                false);
  CHECK(scene.widgets.text_field_cursor(scene.field) == 1);
}

TEST_CASE("shift+arrow extends a selection; plain arrow collapses it to the near edge") {
  FieldScene scene = build_field(200, 30, "ABCDE");  // cursor starts at 5
  scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field, TextFieldMove::kCharLeft,
                                true);
  scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field, TextFieldMove::kCharLeft,
                                true);
  const TextSelection value = require_selection(scene.widgets, scene.field);
  CHECK(value.start == 3);
  CHECK(value.end == 5);

  // Plain (non-extending) Left with this selection active collapses to its
  // START, not to cursor-1 - the desktop-toolkit convention.
  scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field, TextFieldMove::kCharLeft,
                                false);
  CHECK(scene.widgets.text_field_cursor(scene.field) == 3);
  CHECK_FALSE(scene.widgets.text_field_selection(scene.field).has_value());
}

TEST_CASE("plain Left at offset 0 and Right at the end are clamped no-ops") {
  FieldScene scene = build_field(200, 30, "ABC");
  scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field, TextFieldMove::kLineStart,
                                false);
  CHECK(scene.widgets.text_field_cursor(scene.field) == 0);
  CHECK_FALSE(scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field,
                                            TextFieldMove::kCharLeft, false));
  CHECK(scene.widgets.text_field_cursor(scene.field) == 0);

  scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field, TextFieldMove::kLineEnd,
                                false);
  CHECK(scene.widgets.text_field_cursor(scene.field) == 3);
  CHECK_FALSE(scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field,
                                            TextFieldMove::kCharRight, false));
  CHECK(scene.widgets.text_field_cursor(scene.field) == 3);
}

TEST_CASE("Home/End always jump to the absolute ends, ignoring any active selection") {
  FieldScene scene = build_field(200, 30, "ABCDE");
  scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field, TextFieldMove::kLineStart,
                                false);
  scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field, TextFieldMove::kCharRight,
                                true);
  // Selection [0, 1).
  scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field, TextFieldMove::kLineEnd,
                                false);
  CHECK(scene.widgets.text_field_cursor(scene.field) == 5);
  CHECK_FALSE(scene.widgets.text_field_selection(scene.field).has_value());
}

TEST_CASE(
    "click positions the cursor at the byte offset nearest the click, and a drag extends "
    "a selection") {
  FieldScene scene = build_field(200, 30, "ABCDE");
  scene.widgets.text_field_click(scene.tree, scene.fonts, scene.field, /*pointer_x=*/26,
                                 /*extend_selection=*/false);
  // 26px is past the (12,24) midpoint 18, before the (24,36) midpoint 30 -
  // offset 2.
  CHECK(scene.widgets.text_field_cursor(scene.field) == 2);
  CHECK_FALSE(scene.widgets.text_field_selection(scene.field).has_value());

  scene.widgets.text_field_click(scene.tree, scene.fonts, scene.field, /*pointer_x=*/50,
                                 /*extend_selection=*/true);
  // 50px is past 4 glyphs' worth (48) - offset 4.
  const TextSelection value = require_selection(scene.widgets, scene.field);
  CHECK(value.start == 2);
  CHECK(value.end == 4);
}

// Regression: a click EXACTLY on a glyph's midpoint (not merely just past or
// just before it, which every other click test here uses) must resolve
// consistently to the far side, never the near side - `grapheme_offset_at_x`
// compares with a strict `<`, so a tie goes to the boundary AFTER the
// midpoint. Every prior click test in this file used an x a whole pixel away
// from any midpoint, so a defect that changes the comparison direction (a
// tie now resolving to the near side instead) would pass every one of them
// unnoticed - found by injecting exactly that (`<` weakened to `<=`) against
// the working tree, which survived every existing test and was closed by
// adding this one, pinning the tie-break directly at the pixel where it
// actually matters.
TEST_CASE("a click exactly on a glyph's midpoint resolves to the boundary after it") {
  FieldScene scene = build_field(200, 30, "ABCDE");
  // Glyph 0 spans [0, 12) - its exact midpoint is 6.
  scene.widgets.text_field_click(scene.tree, scene.fonts, scene.field, /*pointer_x=*/6,
                                 /*extend_selection=*/false);
  CHECK(scene.widgets.text_field_cursor(scene.field) == 1);
}

// Regression: a drag is a SEQUENCE of extend_selection==true clicks (one
// per pointer-move event), and the anchor from the FIRST one has to survive
// every later one in the sequence - only the cursor end should keep moving.
// Isolates a defect that re-anchors on every extending click (using the
// cursor from just before the CURRENT click rather than the drag's original
// starting point), which the two-click test above cannot see because its
// second click is the first (and only) extending one in that sequence, so
// "no prior anchor" and "preserve the prior anchor" produce the same anchor
// value there.
TEST_CASE("a drag's anchor is fixed at its first extending click, not re-set on every one") {
  FieldScene scene = build_field(200, 30, "ABCDE");
  scene.widgets.text_field_click(scene.tree, scene.fonts, scene.field, /*pointer_x=*/26,
                                 /*extend_selection=*/false);
  REQUIRE(scene.widgets.text_field_cursor(scene.field) == 2);

  scene.widgets.text_field_click(scene.tree, scene.fonts, scene.field, /*pointer_x=*/50,
                                 /*extend_selection=*/true);
  REQUIRE(scene.widgets.text_field_cursor(scene.field) == 4);

  // A third drag-continuation point, further right still. The anchor must
  // still be 2 (the drag's start), not 4 (the previous click's cursor).
  scene.widgets.text_field_click(scene.tree, scene.fonts, scene.field, /*pointer_x=*/58,
                                 /*extend_selection=*/true);
  const TextSelection value = require_selection(scene.widgets, scene.field);
  CHECK(value.start == 2);
  CHECK(value.end == 5);
}

// ----------------------------------------------------------------------------
// Geometry: the caret and selection-highlight rectangles, hand-derived
// against the monospaced test font rather than read back and trusted.
// ----------------------------------------------------------------------------

TEST_CASE("the caret sits at cursor_offset * glyph_width when the field has room") {
  FieldScene scene = build_field(200, 30, "ABCDE");
  scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field, TextFieldMove::kLineStart,
                                false);
  scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field, TextFieldMove::kCharRight,
                                false);
  scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field, TextFieldMove::kCharRight,
                                false);
  scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field, TextFieldMove::kCharRight,
                                false);
  // cursor == 3.
  const PixelRect caret = scene.tree.local_bounds(scene.widgets.at(scene.field).caret);
  CHECK(caret.x == static_cast<int>(3.0F * kGlyphWidth));
  CHECK(caret.width == 2);
}

// Regression: a caret move THAT LANDS BACK AT x == 0 has to update the
// caret's stored bounds exactly like any other move - x == 0 is not a
// sentinel for "leave the previous rectangle alone". Isolates a defect a
// naive "skip the update when the new x is 0" shortcut would introduce,
// which every OTHER caret test misses because none of them return to
// offset 0 after moving away from it.
TEST_CASE("the caret updates its bounds when moving back to offset 0, not only away from it") {
  FieldScene scene = build_field(200, 30, "ABCDE");
  scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field, TextFieldMove::kLineStart,
                                false);
  scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field, TextFieldMove::kCharRight,
                                false);
  scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field, TextFieldMove::kCharRight,
                                false);
  const PixelRect away = scene.tree.local_bounds(scene.widgets.at(scene.field).caret);
  REQUIRE(away.x == static_cast<int>(2.0F * kGlyphWidth));

  scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field, TextFieldMove::kLineStart,
                                false);
  const PixelRect back_at_zero = scene.tree.local_bounds(scene.widgets.at(scene.field).caret);
  CHECK(back_at_zero.x == 0);
}

TEST_CASE("the selection highlight spans exactly [start, end) in glyph widths") {
  FieldScene scene = build_field(200, 30, "ABCDE");
  scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field, TextFieldMove::kLineStart,
                                false);
  scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field, TextFieldMove::kCharRight,
                                true);
  scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field, TextFieldMove::kCharRight,
                                true);
  scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field, TextFieldMove::kCharRight,
                                true);
  // Selection [0, 3).
  const PixelRect highlight =
      scene.tree.local_bounds(scene.widgets.at(scene.field).selection_highlight);
  CHECK(highlight.x == 0);
  CHECK(highlight.width == static_cast<int>(3.0F * kGlyphWidth));
}

TEST_CASE("no selection collapses the highlight to zero width, hiding it") {
  FieldScene scene = build_field(200, 30, "ABCDE");
  const PixelRect highlight =
      scene.tree.local_bounds(scene.widgets.at(scene.field).selection_highlight);
  CHECK(highlight.width == 0);
}

TEST_CASE("overflow scrolls the content so the cursor stays inside the visible width") {
  // Field is 50px wide - 4 glyphs' worth (48px) fit, a 5th does not.
  FieldScene scene = build_field(50, 30, "");
  for (const char letter : {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'}) {
    scene.widgets.text_field_insert(scene.tree, scene.fonts, scene.field,
                                    std::string(1, letter));
  }
  // 8 glyphs, cursor at the end (8), total width 96px, visible 50px:
  // scroll_x = round(96) - 50 = 46, caret_x = clamp(96 - 46, 0, 50 - 2) = 48.
  const PixelRect caret = scene.tree.local_bounds(scene.widgets.at(scene.field).caret);
  CHECK(caret.x == 48);

  // The content child is shifted left by the same scroll amount.
  const PixelRect content = scene.tree.local_bounds(scene.widgets.at(scene.field).content);
  CHECK(content.x == -46);
}

// Regression: click-to-offset has to add the field's CURRENT scroll_x to the
// pointer position before resolving a glyph offset - a click always names an
// ABSOLUTE screen position, but the text underneath it has been shifted left
// by scroll_x, so the same screen x means a different byte offset once a
// field has scrolled. Isolates a defect that drops the scroll_x term
// entirely, which every OTHER click test misses because none of them click
// on a field whose content overflows and has therefore scrolled.
TEST_CASE("click-to-offset accounts for the field's current scroll position") {
  FieldScene scene = build_field(50, 30, "");
  for (const char letter : {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'}) {
    scene.widgets.text_field_insert(scene.tree, scene.fonts, scene.field,
                                    std::string(1, letter));
  }
  // scroll_x == 46 (asserted by the test above). A click at screen x == 10
  // resolves against local_x == 10 + 46 == 56, which falls past glyph 4's
  // midpoint (54) and before glyph 5's (66) - offset 5. Dropping scroll_x
  // would instead resolve local_x == 10 against glyph 0's midpoint (6) -
  // offset 1, a different and wrong answer.
  scene.widgets.text_field_click(scene.tree, scene.fonts, scene.field, /*pointer_x=*/10,
                                 /*extend_selection=*/false);
  CHECK(scene.widgets.text_field_cursor(scene.field) == 5);
}

TEST_CASE("a field whose content fits never scrolls, even after edits") {
  FieldScene scene = build_field(200, 30, "AB");
  scene.widgets.text_field_insert(scene.tree, scene.fonts, scene.field, "C");
  const PixelRect content = scene.tree.local_bounds(scene.widgets.at(scene.field).content);
  CHECK(content.x == 0);
}

// ----------------------------------------------------------------------------
// Focus: the unfocused ellipsis projection, and that it leaves the model
// untouched.
// ----------------------------------------------------------------------------

TEST_CASE("unfocused display ellipsizes an overflowing string without touching the model") {
  FieldScene scene = build_field(50, 30, "ABCDEFGH");  // 96px of text, 50px field
  scene.widgets.text_field_set_focus(scene.tree, scene.fonts, scene.field, false);

  CHECK(scene.widgets.text_field_text(scene.field) == "ABCDEFGH");
  const std::string displayed =
      scene.tree.style(scene.widgets.at(scene.field).content).text.text;
  CHECK(displayed != "ABCDEFGH");
  CHECK(displayed.ends_with("..."));
  // Hand-derived, not merely "ends in an ellipsis": the ellipsis itself is
  // 3 glyphs (36px). "A..." is 4 glyphs (48px), which fits the 50px field;
  // "AB..." is 5 glyphs (60px), which does not - so exactly one letter of
  // prefix survives. A truncation off by one glyph either way (keeping "AB"
  // or keeping none) would still end in "..." and would still differ from
  // the full string, so those two weaker checks above cannot tell a
  // one-glyph-too-wide ellipsis from a correct one; this can.
  CHECK(displayed == "A...");
  // The caret and highlight are hidden (zero width) while unfocused.
  CHECK(scene.tree.local_bounds(scene.widgets.at(scene.field).caret).width == 0);
}

TEST_CASE("unfocused display shows the string verbatim when it already fits") {
  FieldScene scene = build_field(200, 30, "AB");
  scene.widgets.text_field_set_focus(scene.tree, scene.fonts, scene.field, false);
  const std::string displayed =
      scene.tree.style(scene.widgets.at(scene.field).content).text.text;
  CHECK(displayed == "AB");
}

TEST_CASE("refocusing restores the full string, scrolled to keep the cursor visible") {
  FieldScene scene = build_field(50, 30, "ABCDEFGH");
  scene.widgets.text_field_set_focus(scene.tree, scene.fonts, scene.field, false);
  scene.widgets.text_field_set_focus(scene.tree, scene.fonts, scene.field, true);
  const std::string displayed =
      scene.tree.style(scene.widgets.at(scene.field).content).text.text;
  CHECK(displayed == "ABCDEFGH");
  CHECK(scene.tree.local_bounds(scene.widgets.at(scene.field).caret).width == 2);
}

// Regression: blurring a field with an active selection has to clear the
// selection, not merely hide its highlight while unfocused - otherwise the
// SAME selection reappears the moment the field is refocused, because
// text_field_refresh_display's focused branch draws whatever
// `selection_anchor` still holds. Isolates a defect that hides the
// highlight on blur (a correct-looking no-op) without clearing the model,
// which the blur-only tests above cannot see because none of them refocus
// afterward to check what came back.
TEST_CASE("blurring a field with a selection clears it, so refocusing does not resurrect it") {
  FieldScene scene = build_field(200, 30, "ABCDE");
  scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field, TextFieldMove::kLineStart,
                                false);
  scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field, TextFieldMove::kCharRight,
                                true);
  scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field, TextFieldMove::kCharRight,
                                true);
  REQUIRE(scene.widgets.text_field_selection(scene.field).has_value());

  scene.widgets.text_field_set_focus(scene.tree, scene.fonts, scene.field, false);
  scene.widgets.text_field_set_focus(scene.tree, scene.fonts, scene.field, true);
  CHECK_FALSE(scene.widgets.text_field_selection(scene.field).has_value());
  const PixelRect highlight =
      scene.tree.local_bounds(scene.widgets.at(scene.field).selection_highlight);
  CHECK(highlight.width == 0);
}

// ----------------------------------------------------------------------------
// 7-3: IME composition preview (doc/ime.md) - hand-derived against the same
// DgTest Latin font/geometry every earlier case in this file already uses
// (12px/glyph at size 20).
// ----------------------------------------------------------------------------

TEST_CASE("composition preview inserts inline without touching the committed model") {
  FieldScene scene = build_field(200, 30, "");
  scene.widgets.text_field_composition_update(scene.tree, scene.fonts, scene.field, "ni", 2, 0);

  CHECK(scene.widgets.text_field_is_composing(scene.field));
  CHECK(scene.widgets.text_field_composition_text(scene.field) == "ni");
  // The committed model is untouched by a preview - doc/ime.md's own
  // argument, checked directly rather than only inferred from the display.
  CHECK(scene.widgets.text_field_text(scene.field) == "");
  CHECK(scene.widgets.text_field_cursor(scene.field) == 0);

  const std::string displayed =
      scene.tree.style(scene.widgets.at(scene.field).content).text.text;
  CHECK(displayed == "ni");
  // start=2 (SDL's own "UTF-8 characters" unit, 2 ASCII bytes) places the
  // caret after both glyphs: 2 * 12 = 24px.
  const PixelRect caret = scene.tree.local_bounds(scene.widgets.at(scene.field).caret);
  CHECK(caret.x == 24);
  CHECK(caret.width == 2);
  // The underline spans the WHOLE preedit, byte [0, 2): 0px to 24px, 24px
  // wide.
  const PixelRect underline =
      scene.tree.local_bounds(scene.widgets.at(scene.field).composition_underline);
  CHECK(underline.x == 0);
  CHECK(underline.width == 24);
}

TEST_CASE("composition's focused-clause range highlights exactly [start, start+length)") {
  FieldScene scene = build_field(200, 30, "");
  // start=1, length=1: SDL's own convention for "the middle glyph of the
  // preedit is the clause currently being edited" - byte [1, 2), "Y".
  scene.widgets.text_field_composition_update(scene.tree, scene.fonts, scene.field, "XYZ", 1,
                                              1);

  const PixelRect highlight =
      scene.tree.local_bounds(scene.widgets.at(scene.field).selection_highlight);
  CHECK(highlight.x == 12);
  CHECK(highlight.width == 12);
}

TEST_CASE("a length of 0 (or SDL's -1 sentinel) shows no clause highlight, only a caret") {
  FieldScene scene = build_field(200, 30, "");
  scene.widgets.text_field_composition_update(scene.tree, scene.fonts, scene.field, "XYZ", 2,
                                              0);
  CHECK(scene.tree.local_bounds(scene.widgets.at(scene.field).selection_highlight).width == 0);

  scene.widgets.text_field_composition_update(scene.tree, scene.fonts, scene.field, "XYZ", -1,
                                              -1);
  CHECK(scene.tree.local_bounds(scene.widgets.at(scene.field).selection_highlight).width == 0);
}

TEST_CASE("composition commits through text_field_insert() unchanged - no parallel path") {
  FieldScene scene = build_field(200, 30, "");
  scene.widgets.text_field_composition_update(scene.tree, scene.fonts, scene.field, "XYZ", 3,
                                              0);
  REQUIRE(scene.widgets.text_field_is_composing(scene.field));

  // SDL delivers a REAL, separate SDL_EVENT_TEXT_INPUT at the moment the
  // IME commits - modelled here as an ordinary text_field_insert() call,
  // the exact same one every other insert in this file already uses.
  CHECK(scene.widgets.text_field_insert(scene.tree, scene.fonts, scene.field, "XYZ"));
  CHECK_FALSE(scene.widgets.text_field_is_composing(scene.field));
  CHECK(scene.widgets.text_field_composition_text(scene.field).empty());
  CHECK(scene.widgets.text_field_text(scene.field) == "XYZ");
  CHECK(scene.widgets.text_field_cursor(scene.field) == 3);
}

TEST_CASE("Escape (text_field_cancel_composition) discards the preview, committing nothing") {
  FieldScene scene = build_field(200, 30, "AB");
  scene.widgets.text_field_composition_update(scene.tree, scene.fonts, scene.field, "X", 1, 0);
  REQUIRE(scene.widgets.text_field_is_composing(scene.field));

  scene.widgets.text_field_cancel_composition(scene.tree, scene.fonts, scene.field);
  CHECK_FALSE(scene.widgets.text_field_is_composing(scene.field));
  CHECK(scene.widgets.text_field_composition_text(scene.field).empty());
  CHECK(scene.widgets.text_field_text(scene.field) == "AB");
  CHECK(scene.widgets.text_field_cursor(scene.field) == 2);
  const std::string displayed =
      scene.tree.style(scene.widgets.at(scene.field).content).text.text;
  CHECK(displayed == "AB");
  CHECK(scene.tree.local_bounds(scene.widgets.at(scene.field).composition_underline).width ==
        0);
}

TEST_CASE("losing focus mid-composition ends it without committing") {
  FieldScene scene = build_field(200, 30, "AB");
  scene.widgets.text_field_composition_update(scene.tree, scene.fonts, scene.field, "X", 1, 0);
  REQUIRE(scene.widgets.text_field_is_composing(scene.field));

  scene.widgets.text_field_set_focus(scene.tree, scene.fonts, scene.field, false);
  CHECK_FALSE(scene.widgets.text_field_is_composing(scene.field));
  CHECK(scene.widgets.text_field_text(scene.field) == "AB");
}

TEST_CASE(
    "a click mid-composition ends it without committing, then still positions the cursor") {
  FieldScene scene = build_field(200, 30, "AB");
  move_cursor_to(scene, 0);
  scene.widgets.text_field_composition_update(scene.tree, scene.fonts, scene.field, "X", 1, 0);
  REQUIRE(scene.widgets.text_field_is_composing(scene.field));

  // Click at x=26, past the midpoint of glyph index 2 (24px) - resolves to
  // the field's own end, offset 2, the same hand-derived midpoint math
  // every earlier click test in this file already uses.
  CHECK(scene.widgets.text_field_click(scene.tree, scene.fonts, scene.field, 26, false));
  CHECK_FALSE(scene.widgets.text_field_is_composing(scene.field));
  CHECK(scene.widgets.text_field_text(scene.field) == "AB");
  CHECK(scene.widgets.text_field_cursor(scene.field) == 2);
}

TEST_CASE("backspace/delete/move are suppressed while composing, not applied underneath it") {
  FieldScene scene = build_field(200, 30, "AB");
  scene.widgets.text_field_composition_update(scene.tree, scene.fonts, scene.field, "X", 1, 0);
  REQUIRE(scene.widgets.text_field_is_composing(scene.field));

  CHECK_FALSE(scene.widgets.text_field_backspace(scene.tree, scene.fonts, scene.field));
  CHECK_FALSE(scene.widgets.text_field_delete_forward(scene.tree, scene.fonts, scene.field));
  CHECK_FALSE(scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field,
                                            TextFieldMove::kCharLeft, false));
  CHECK(scene.widgets.text_field_is_composing(scene.field));
  CHECK(scene.widgets.text_field_text(scene.field) == "AB");
}

// Composition over an active selection (7-3's own named edge case): the
// selection is never touched by the PREVIEW - it is exactly what the
// eventual commit replaces, which is what lets the preview be nothing more
// than a display-time splice.
TEST_CASE(
    "composition over an active selection previews the splice, then replaces it on commit") {
  FieldScene scene = build_field(200, 30, "ABCDE");
  move_cursor_to(scene, 1);
  scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field, TextFieldMove::kCharRight,
                                true);
  scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field, TextFieldMove::kCharRight,
                                true);
  const TextSelection selected = require_selection(scene.widgets, scene.field);
  CHECK(selected.start == 1);
  CHECK(selected.end == 3);  // "BC"

  scene.widgets.text_field_composition_update(scene.tree, scene.fonts, scene.field, "XY", 2, 0);
  // The model is untouched while composing - "BC" is still there, not
  // deleted ahead of a commit that has not happened yet.
  CHECK(scene.widgets.text_field_text(scene.field) == "ABCDE");
  const std::string previewed =
      scene.tree.style(scene.widgets.at(scene.field).content).text.text;
  CHECK(previewed == "AXYDE");

  CHECK(scene.widgets.text_field_insert(scene.tree, scene.fonts, scene.field, "XY"));
  CHECK(scene.widgets.text_field_text(scene.field) == "AXYDE");
  CHECK(scene.widgets.text_field_cursor(scene.field) == 3);
}

TEST_CASE(
    "composition preview repairs malformed UTF-8 the same sanitize_utf8() insert() uses") {
  FieldScene scene = build_field(200, 30, "");
  // A lone continuation byte - the same hostile shape
  // "insert repairs malformed UTF-8 to U+FFFD" above already pins.
  scene.widgets.text_field_composition_update(scene.tree, scene.fonts, scene.field,
                                              "A\x80"
                                              "B",
                                              0, 0);
  require_well_formed(scene.widgets.text_field_composition_text(scene.field));
  CHECK(scene.widgets.text_field_composition_text(scene.field) ==
        "A\xEF\xBF\xBD"
        "B");
}

TEST_CASE("composition preview strips ASCII control characters, matching insert()'s policy") {
  FieldScene scene = build_field(200, 30, "");
  scene.widgets.text_field_composition_update(scene.tree, scene.fonts, scene.field, "A\nB", 0,
                                              0);
  CHECK(scene.widgets.text_field_composition_text(scene.field) == "AB");
}

// DG_SANITIZE-relevant: an absurd/hostile start or length must never read or
// write out of bounds - clamped by the byte-walk itself reaching the end of
// the string, not by trusting the caller's arithmetic.
TEST_CASE("an absurd start/length from a hostile or buggy IME clamps to the string's own end") {
  FieldScene scene = build_field(200, 30, "");
  const int huge = std::numeric_limits<int>::max();
  scene.widgets.text_field_composition_update(scene.tree, scene.fonts, scene.field, "XYZ", huge,
                                              huge);
  CHECK(scene.widgets.text_field_is_composing(scene.field));
  const PixelRect highlight =
      scene.tree.local_bounds(scene.widgets.at(scene.field).selection_highlight);
  // Both start and start+length clamp to the same point (the string's own
  // end, 3 bytes) - an empty range, so no clause highlight is shown.
  CHECK(highlight.width == 0);
  const PixelRect caret = scene.tree.local_bounds(scene.widgets.at(scene.field).caret);
  CHECK(caret.x == 36);  // 3 glyphs * 12px, the whole preedit's own width

  // A negative-but-not-(-1) value is exactly as hostile - clamps to 0, the
  // same way SDL's own "-1, not set" sentinel already does.
  scene.widgets.text_field_composition_update(scene.tree, scene.fonts, scene.field, "XYZ", -7,
                                              -7);
  CHECK(scene.tree.local_bounds(scene.widgets.at(scene.field).caret).x == 0);
}

TEST_CASE("composition previews CJK text inline, byte-correct, without corrupting the model") {
  FieldScene scene = build_field(400, 30, "AB");
  move_cursor_to(scene, 1);  // between A and B
  // "\xE4\xB8\xAD" is U+4E2D ("中"), not covered by DgTest Latin - this
  // checks byte-level model correctness, not glyph pixel geometry (the same
  // split the ZWJ-emoji tests above already draw for this font).
  const std::string cjk = "\xE4\xB8\xAD";
  scene.widgets.text_field_composition_update(scene.tree, scene.fonts, scene.field, cjk, 1, 0);
  CHECK(scene.widgets.text_field_is_composing(scene.field));
  CHECK(scene.widgets.text_field_text(scene.field) == "AB");  // untouched
  const std::string previewed =
      scene.tree.style(scene.widgets.at(scene.field).content).text.text;
  CHECK(previewed ==
        "A\xE4\xB8\xAD"
        "B");
  require_well_formed(previewed);

  CHECK(scene.widgets.text_field_insert(scene.tree, scene.fonts, scene.field, cjk));
  CHECK(scene.widgets.text_field_text(scene.field) ==
        "A\xE4\xB8\xAD"
        "B");
}

// ----------------------------------------------------------------------------
// dg::Focus: exclusivity and the atomic change shape.
// ----------------------------------------------------------------------------

TEST_CASE("dg::Focus reports blur and focus atomically, and is exclusive") {
  dg::Focus focus;
  const NodeId a{1};
  const NodeId b{2};

  const dg::FocusChange first = focus.set(a);
  CHECK(first.focused == a);
  CHECK_FALSE(first.blurred.has_value());
  CHECK(focus.is_focused(a));

  const dg::FocusChange second = focus.set(b);
  CHECK(second.blurred == a);
  CHECK(second.focused == b);
  CHECK_FALSE(focus.is_focused(a));
  CHECK(focus.is_focused(b));

  // Focusing the same widget again reports no change.
  const dg::FocusChange redundant = focus.set(b);
  CHECK_FALSE(redundant.any());

  const dg::FocusChange cleared = focus.set(std::nullopt);
  CHECK(cleared.blurred == b);
  CHECK_FALSE(cleared.focused.has_value());
  CHECK_FALSE(focus.current().has_value());
}
