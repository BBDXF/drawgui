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
#include <optional>
#include <string>

#include <doctest/doctest.h>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/types.h"
#include "drawgui/render/font_catalog.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/render/text_metrics.h"
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

}  // namespace

// ----------------------------------------------------------------------------
// dg::measure_ascii_width / dg::ascii_offset_at_x - the primitives everything
// else is built on.
// ----------------------------------------------------------------------------

TEST_CASE("measure_ascii_width is exactly glyph_width * length on the test font") {
  FontCatalog fonts = test_fonts();
  const FontId font = fonts.add("DgTest Latin", false).value();
  // Exact rather than Approx: every glyph's advance (600/1000 em, ADVANCE in
  // tests/fonts/gen_test_fonts.py) times a small integer size (20) has an
  // exact float representation, so there is no rounding error to tolerate.
  CHECK(dg::measure_ascii_width(fonts, font, kFontSize, "") == 0.0F);
  CHECK(dg::measure_ascii_width(fonts, font, kFontSize, "A") == kGlyphWidth);
  CHECK(dg::measure_ascii_width(fonts, font, kFontSize, "ABCDE") == 5.0F * kGlyphWidth);
}

TEST_CASE("ascii_offset_at_x snaps to the nearer side of each glyph's advance") {
  FontCatalog fonts = test_fonts();
  const FontId font = fonts.add("DgTest Latin", false).value();
  const std::string text = "ABCDE";  // 5 glyphs, 12px each, midpoints at 6/18/30/42/54

  CHECK(dg::ascii_offset_at_x(fonts, font, kFontSize, text, -5.0F) == 0);
  CHECK(dg::ascii_offset_at_x(fonts, font, kFontSize, text, 0.0F) == 0);
  CHECK(dg::ascii_offset_at_x(fonts, font, kFontSize, text, 5.9F) == 0);
  CHECK(dg::ascii_offset_at_x(fonts, font, kFontSize, text, 6.1F) == 1);
  CHECK(dg::ascii_offset_at_x(fonts, font, kFontSize, text, 17.9F) == 1);
  CHECK(dg::ascii_offset_at_x(fonts, font, kFontSize, text, 18.1F) == 2);
  CHECK(dg::ascii_offset_at_x(fonts, font, kFontSize, text, 60.0F) == 5);
  CHECK(dg::ascii_offset_at_x(fonts, font, kFontSize, text, 1000.0F) == 5);
}

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

TEST_CASE("insert filters non-ASCII bytes and control characters, keeping the rest") {
  FieldScene scene = build_field(200, 30, "");
  // 0xC3 0xA9 is UTF-8 for U+00E9 (e-acute) - both bytes are outside
  // 0x20-0x7E and are dropped; 'e' either side survives.
  CHECK(scene.widgets.text_field_insert(scene.tree, scene.fonts, scene.field,
                                        "e\xC3\xA9"
                                        "e"));
  CHECK(scene.widgets.text_field_text(scene.field) == "ee");
  CHECK(scene.widgets.text_field_cursor(scene.field) == 2);

  // An all-filtered insert with no selection reports no change.
  CHECK_FALSE(scene.widgets.text_field_insert(scene.tree, scene.fonts, scene.field,
                                              "\xC3"
                                              "\xA9"));
  CHECK(scene.widgets.text_field_text(scene.field) == "ee");
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
