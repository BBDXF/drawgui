// Editor polish shared by TextField and TextArea: undo/redo in the editing
// core (merge rules over the AnimTime seam, depth cap, redo invalidation),
// word segmentation and word moves/deletes, max length in grapheme
// clusters, multi-click counting, and double/triple-click selection through
// WidgetSet against the generated "DgTest Latin" font (every glyph 12px wide
// at 20px).

#include <string>

#include <doctest/doctest.h>

#include "drawgui/anim/clock.h"
#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/types.h"
#include "drawgui/render/font_catalog.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/widget/click_counter.h"
#include "drawgui/widget/text_edit.h"
#include "drawgui/widget/widget_set.h"

namespace {

namespace te = dg::text_edit;
using dg::AnimTime;
using dg::FontCatalog;
using dg::NodeId;
using dg::NodeStyle;
using dg::PixelPoint;
using dg::PixelRect;
using dg::RenderTree;
using dg::TextEditState;
using dg::TextSelection;
using dg::Widget;
using dg::WidgetKind;
using dg::WidgetSet;

constexpr float kFontSize = 20.0F;

// Types `text` one cluster per call, `step_ms` apart starting at `start_ms`.
void type(TextEditState& state, const std::string& text, std::int64_t start_ms,
          std::int64_t step_ms = 100) {
  std::int64_t now = start_ms;
  for (const char c : text) {
    state.history.now = AnimTime{now};
    te::insert(state, std::string(1, c));
    now += step_ms;
  }
}

bool same(TextSelection a, TextSelection b) {
  return a.start == b.start && a.end == b.end;
}

struct Scene {
  RenderTree tree;
  WidgetSet widgets;
  FontCatalog fonts;
  NodeId editor;
};

Scene build(WidgetKind kind, const std::string& text) {
  dg::TreeSpec spec;
  spec.viewport = dg::PixelSize{600, 400};
  const dg::Expected<FontCatalog, dg::FontError> scanned = FontCatalog::scan(DG_TEST_FONT_DIR);
  REQUIRE(scanned.has_value());
  Scene scene{RenderTree{spec}, WidgetSet{}, scanned.value(), NodeId{}};
  const dg::Expected<dg::FontId, dg::FontError> font = scene.fonts.add("DgTest Latin", false);
  REQUIRE(font.has_value());
  RenderTree& tree = scene.tree;
  NodeStyle style;
  style.overflow = dg::Overflow::kClip;
  scene.editor = tree.add_child(RenderTree::root(), PixelRect{0, 0, 400, 200}, style);

  Widget widget;
  widget.kind = kind;
  NodeStyle content;
  content.text.font = font.value();
  content.text.size = kFontSize;
  content.text.align = dg::TextAlign::kLeft;
  if (kind == WidgetKind::kTextArea) {
    widget.selection_layer = tree.add_child(scene.editor, PixelRect{}, NodeStyle{});
    for (int i = 0; i < 8; ++i) {
      widget.highlight_pool.push_back(tree.add_child(widget.selection_layer, PixelRect{}, {}));
    }
    content.text.wrap = true;
  } else {
    widget.selection_highlight = tree.add_child(scene.editor, PixelRect{}, NodeStyle{});
    widget.composition_underline = tree.add_child(scene.editor, PixelRect{}, NodeStyle{});
    content.text.text = text;
  }
  widget.content = tree.add_child(scene.editor, PixelRect{}, content);
  widget.caret = tree.add_child(scene.editor, PixelRect{}, NodeStyle{});
  widget.edit.text = text;
  scene.widgets.attach(scene.editor, widget);
  if (kind == WidgetKind::kTextArea) {
    scene.widgets.text_area_set_focus(tree, scene.fonts, scene.editor, true);
  } else {
    scene.widgets.text_field_set_focus(tree, scene.fonts, scene.editor, true);
  }
  return scene;
}

TextSelection selection_of(const Scene& scene) {
  const TextEditState* state = scene.widgets.editor_state(scene.editor);
  REQUIRE(state != nullptr);
  const std::optional<TextSelection> sel = te::selection(*state);
  return sel.value_or(TextSelection{-1, -1});
}

}  // namespace

TEST_SUITE("editor polish") {
  TEST_CASE("undo: a typed word is one step, a space starts the next") {
    TextEditState state;
    type(state, "hello world", 0);
    CHECK(state.history.undo.size() == 2);
    REQUIRE(te::undo(state));
    CHECK(state.text == "hello");
    CHECK(state.cursor == 5);
    REQUIRE(te::undo(state));
    CHECK(state.text.empty());
    CHECK_FALSE(te::undo(state));
    REQUIRE(te::redo(state));
    REQUIRE(te::redo(state));
    CHECK(state.text == "hello world");
    CHECK_FALSE(te::redo(state));
  }

  TEST_CASE("undo: a pause, a caret move or a delete break the typing run") {
    TextEditState state;
    type(state, "ab", 0);
    type(state, "cd", 5000);  // > 1s after "b"
    CHECK(state.history.undo.size() == 2);

    te::move_horizontal(state, true, false);  // caret jump
    te::move_horizontal(state, false, false);
    type(state, "e", 5200);
    CHECK(state.history.undo.size() == 3);

    state.history.now = AnimTime{5300};
    te::backspace(state);
    state.history.now = AnimTime{5400};
    te::backspace(state);  // consecutive backspaces merge
    CHECK(state.history.undo.size() == 4);
    CHECK(state.text == "abc");
    REQUIRE(te::undo(state));
    CHECK(state.text == "abcde");
  }

  TEST_CASE("undo: a new edit clears redo") {
    TextEditState state;
    type(state, "abc", 0);
    REQUIRE(te::undo(state));
    CHECK(state.history.redo.size() == 1);
    type(state, "x", 100);
    CHECK(state.history.redo.empty());
    CHECK_FALSE(te::redo(state));
    CHECK(state.text == "x");
  }

  TEST_CASE("undo: paste, cut and an IME commit are one step each; preedit is never recorded") {
    TextEditState state;
    type(state, "ab", 0);
    state.history.now = AnimTime{50};
    te::insert(state, "PASTED");  // a paste merges with nothing
    CHECK(state.text == "abPASTED");
    CHECK(state.history.undo.size() == 2);

    // Cut: select "PASTED" and replace with nothing.
    te::select_range(state, 2, 8);
    te::insert(state, "");
    CHECK(state.text == "ab");
    CHECK(state.history.undo.size() == 3);

    te::composition_update(state, "ni", 2, 0);
    te::composition_update(state, "nih", 3, 0);
    CHECK(state.history.undo.size() == 3);
    te::insert(state, "\u4F60");  // the commit: one cluster, still its own step
    CHECK(state.history.undo.size() == 4);
    type(state, "c", 60);
    CHECK(state.history.undo.size() == 5);

    REQUIRE(te::undo(state));
    REQUIRE(te::undo(state));
    CHECK(state.text == "ab");
    REQUIRE(te::undo(state));
    CHECK(state.text == "abPASTED");
    CHECK(state.selection_anchor == 2);  // the snapshot restores the selection too
    CHECK(state.cursor == 8);
    REQUIRE(te::undo(state));
    CHECK(state.text == "ab");
  }

  TEST_CASE("undo is suppressed while composing") {
    TextEditState state;
    type(state, "a", 0);
    te::composition_update(state, "x", 1, 0);
    CHECK_FALSE(te::undo(state));
    CHECK(state.text == "a");
  }

  TEST_CASE("undo depth is capped") {
    TextEditState state;
    for (int i = 0; i < 150; ++i) {
      state.history.now = AnimTime{i * 10'000};
      te::insert(state, "x");
    }
    CHECK(state.history.undo.size() == dg::TextEditHistory::kMaxDepth);
    int undone = 0;
    while (te::undo(state)) {
      ++undone;
    }
    CHECK(undone == 100);
    CHECK(state.text == std::string(50, 'x'));
  }

  TEST_CASE("word moves over ASCII words and punctuation") {
    const std::string text = "foo, bar_baz  qux";
    CHECK(te::word_right(text, 0) == 3);
    CHECK(te::word_right(text, 3) == 4);  // the comma is its own run
    CHECK(te::word_right(text, 4) == 12);
    CHECK(te::word_right(text, 12) == 17);
    CHECK(te::word_right(text, 17) == 17);
    CHECK(te::word_left(text, 17) == 14);
    CHECK(te::word_left(text, 14) == 5);
    CHECK(te::word_left(text, 5) == 3);
    CHECK(te::word_left(text, 3) == 0);
    CHECK(te::word_left(text, 0) == 0);
  }

  TEST_CASE("word moves over CJK, CJK punctuation and emoji") {
    // "中文。" + "ab" + family emoji + thumbs-up
    const std::string family = "\U0001F468\u200D\U0001F469\u200D\U0001F467";
    const std::string text = "\u4E2D\u6587\u3002ab" + family + "\U0001F44D";
    const int cjk_end = 6;
    const int punct_end = 9;
    const int ab_end = 11;
    const int family_end = ab_end + static_cast<int>(family.size());
    const int size = static_cast<int>(text.size());
    CHECK(te::word_right(text, 0) == cjk_end);  // a CJK run is one word
    CHECK(te::word_right(text, cjk_end) == punct_end);
    CHECK(te::word_right(text, punct_end) == ab_end);
    CHECK(te::word_right(text, ab_end) == family_end);  // one emoji per word, whole cluster
    CHECK(te::word_right(text, family_end) == size);
    CHECK(te::word_left(text, size) == family_end);
    CHECK(te::word_left(text, family_end) == ab_end);
    CHECK(same(te::word_range(text, 3), TextSelection{0, cjk_end}));
  }

  TEST_CASE(
      "word_range prefers the word to the left at its end, and line_range splits on \\n") {
    const std::string text = "one two\nthree";
    CHECK(same(te::word_range(text, 1), TextSelection{0, 3}));
    CHECK(same(te::word_range(text, 3), TextSelection{0, 3}));  // right edge of "one"
    CHECK(same(te::word_range(text, 13), TextSelection{8, 13}));
    CHECK(same(te::word_range("a   b", 2), TextSelection{1, 4}));  // a run of spaces
    CHECK(same(te::line_range(text, 2), TextSelection{0, 7}));
    CHECK(same(te::line_range(text, 7), TextSelection{0, 7}));
    CHECK(same(te::line_range(text, 8), TextSelection{8, 13}));
    CHECK(same(te::line_range("", 0), TextSelection{0, 0}));
  }

  TEST_CASE("move_word extends with shift; delete_word deletes a word and is one undo step") {
    TextEditState state;
    state.text = "alpha beta gamma";
    state.cursor = 0;
    CHECK(te::move_word(state, false, true));
    CHECK(state.selection_anchor == 0);
    CHECK(state.cursor == 5);
    state.selection_anchor.reset();
    state.cursor = static_cast<int>(state.text.size());
    CHECK(te::delete_word(state, true));
    CHECK(state.text == "alpha beta ");
    state.cursor = 0;
    CHECK(te::delete_word(state, false));
    CHECK(state.text == " beta ");
    REQUIRE(te::undo(state));
    REQUIRE(te::undo(state));
    CHECK(state.text == "alpha beta gamma");
  }

  TEST_CASE("max_graphemes truncates typing and paste at a grapheme boundary") {
    TextEditState state;
    state.max_graphemes = 4;
    const std::string family = "\U0001F468\u200D\U0001F469\u200D\U0001F467";
    CHECK(te::insert(state, "ab" + family + "cd"));  // 5 clusters, 4 fit
    CHECK(state.text == "ab" + family + "c");
    CHECK_FALSE(te::insert(state, "z"));  // full
    CHECK(state.text == "ab" + family + "c");
    // Replacing a selection frees its clusters.
    te::select_range(state, 0, 2);
    CHECK(te::insert(state, "\u4F60\u597D\u5417"));  // room for 2
    CHECK(state.text == "\u4F60\u597D" + family + "c");
  }

  TEST_CASE("click counting: time and distance thresholds, cycling after three") {
    dg::ClickCounter counter;
    CHECK(dg::register_click(counter, PixelPoint{10, 10}, AnimTime{0}) == 1);
    CHECK(dg::register_click(counter, PixelPoint{12, 11}, AnimTime{300}) == 2);
    CHECK(dg::register_click(counter, PixelPoint{12, 11}, AnimTime{600}) == 3);
    CHECK(dg::register_click(counter, PixelPoint{12, 11}, AnimTime{700}) == 1);
    CHECK(dg::register_click(counter, PixelPoint{12, 11}, AnimTime{1300}) == 1);  // too slow
    CHECK(dg::register_click(counter, PixelPoint{30, 11}, AnimTime{1400}) == 1);  // too far
  }

  TEST_CASE("TextField: double-click selects a word, triple-click the whole text") {
    Scene scene = build(WidgetKind::kTextField, "hello big world");
    // x = 80 is inside "big" (glyphs 6..8, 12px each, so 72..108).
    scene.widgets.text_field_click(scene.tree, scene.fonts, scene.editor, 80, false, 2);
    CHECK(same(selection_of(scene), TextSelection{6, 9}));
    scene.widgets.text_field_click(scene.tree, scene.fonts, scene.editor, 80, false, 3);
    CHECK(same(selection_of(scene), TextSelection{0, 15}));
  }

  TEST_CASE("TextArea: double-click selects a word, triple-click its logical line") {
    Scene scene = build(WidgetKind::kTextArea, "first line\nsecond one");
    // Line 2 ('s' at x=0); y in the second line.
    const int second_line_y =
        scene.tree.local_bounds(scene.widgets.at(scene.editor).caret).height + 4;
    scene.widgets.text_area_click(scene.tree, scene.fonts, scene.editor,
                                  PixelPoint{30, second_line_y}, false, 2);
    CHECK(same(selection_of(scene), TextSelection{11, 17}));
    scene.widgets.text_area_click(scene.tree, scene.fonts, scene.editor,
                                  PixelPoint{30, second_line_y}, false, 3);
    CHECK(same(selection_of(scene), TextSelection{11, 21}));
  }

  TEST_CASE("WidgetSet: word moves, undo/redo and max length on both editors") {
    for (const WidgetKind kind : {WidgetKind::kTextField, WidgetKind::kTextArea}) {
      Scene scene = build(kind, "");
      scene.widgets.editor_set_max_length(scene.editor, 8);
      scene.widgets.set_edit_time(AnimTime{0});
      CHECK(
          scene.widgets.editor_insert(scene.tree, scene.fonts, scene.editor, "one two three"));
      const TextEditState* state = scene.widgets.editor_state(scene.editor);
      REQUIRE(state != nullptr);
      CHECK(state->text == "one two ");
      if (kind == WidgetKind::kTextField) {
        scene.widgets.text_field_move(scene.tree, scene.fonts, scene.editor,
                                      dg::TextFieldMove::kWordLeft, false);
      } else {
        scene.widgets.text_area_move(scene.tree, scene.fonts, scene.editor,
                                     dg::TextAreaMove::kWordLeft, false);
      }
      CHECK(state->cursor == 4);
      CHECK(scene.widgets.editor_delete_word(scene.tree, scene.fonts, scene.editor, true));
      CHECK(state->text == "two ");
      CHECK(scene.widgets.editor_undo(scene.tree, scene.fonts, scene.editor));
      CHECK(state->text == "one two ");
      CHECK(scene.widgets.editor_redo(scene.tree, scene.fonts, scene.editor));
      CHECK(state->text == "two ");
      CHECK(
          scene.widgets.editor_set_text(scene.tree, scene.fonts, scene.editor, "reset", true));
      CHECK_FALSE(scene.widgets.editor_undo(scene.tree, scene.fonts, scene.editor));
    }
  }
}
