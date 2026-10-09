// The render-free editing core (include/drawgui/widget/text_edit.h), driven
// directly on a TextEditState - no tree, no fonts. The TextField tests
// (test_text_input.cpp) cover the same rules through WidgetSet's wrappers;
// these pin the core on its own so a second editor can rely on it.

#include <string>

#include <doctest/doctest.h>

#include "drawgui/widget/text_edit.h"

namespace {

namespace te = dg::text_edit;
using dg::TextEditState;

TextEditState state_with(std::string text, int cursor) {
  TextEditState state;
  state.text = std::move(text);
  state.cursor = cursor;
  return state;
}

const std::string kFamily = "\U0001F468\u200D\U0001F469\u200D\U0001F467\u200D\U0001F466";

}  // namespace

TEST_CASE("text_edit: insert at the cursor, then over a selection") {
  TextEditState state = state_with("ad", 1);
  CHECK(te::insert(state, "bc"));
  CHECK(state.text == "abcd");
  CHECK(state.cursor == 3);

  state.selection_anchor = 0;  // select "abc"
  CHECK(te::insert(state, "X"));
  CHECK(state.text == "Xd");
  CHECK(state.cursor == 1);
  CHECK_FALSE(state.selection_anchor.has_value());

  // Empty input with no selection changes nothing; control bytes are
  // dropped and malformed UTF-8 repaired.
  CHECK_FALSE(te::insert(state, ""));
  CHECK(te::insert(state, std::string{"a\tb\x80"}));
  CHECK(state.text == "Xab\xEF\xBF\xBD"
                      "d");
}

TEST_CASE("text_edit: replace_range clamps and orders its range") {
  TextEditState state = state_with("hello", 0);
  CHECK(te::replace_range(state, 4, 1, "EY"));
  CHECK(state.text == "hEYo");
  CHECK(state.cursor == 3);
  CHECK_FALSE(te::replace_range(state, 2, 2, ""));
  CHECK(te::replace_range(state, -5, 99, ""));
  CHECK(state.text.empty());
}

TEST_CASE("text_edit: sanitize_insertable keeps '\\n' only when asked") {
  CHECK(te::sanitize_insertable("a\nb") == "ab");
  CHECK(te::sanitize_insertable("a\nb", true) == "a\nb");
  CHECK(te::sanitize_insertable("a\r\tb", true) == "ab");
}

TEST_CASE("text_edit: backspace and delete remove one whole ZWJ / emoji cluster") {
  TextEditState state = state_with("A" + kFamily + "\U0001F600B", 0);
  state.cursor = static_cast<int>(state.text.size()) - 1;  // before "B"
  CHECK(te::backspace(state));                              // the 4-byte U+1F600
  CHECK(state.text == "A" + kFamily + "B");
  CHECK(te::backspace(state));  // the whole 25-byte family in one step
  CHECK(state.text == "AB");
  CHECK(state.cursor == 1);

  state = state_with("A" + kFamily + "B", 1);
  CHECK(te::delete_forward(state));
  CHECK(state.text == "AB");
  CHECK(state.cursor == 1);

  state = state_with("AB", 0);
  CHECK_FALSE(te::backspace(state));
  state.cursor = 2;
  CHECK_FALSE(te::delete_forward(state));
}

TEST_CASE("text_edit: backspace with a selection deletes exactly the selection") {
  TextEditState state = state_with("abcdef", 4);
  state.selection_anchor = 1;
  CHECK(te::backspace(state));
  CHECK(state.text == "aef");
  CHECK(state.cursor == 1);
}

TEST_CASE("text_edit: horizontal moves step graphemes, extend, and collapse") {
  TextEditState state = state_with("A" + kFamily + "B", 0);
  CHECK(te::move_horizontal(state, false, false));
  CHECK(state.cursor == 1);
  CHECK(te::move_horizontal(state, false, true));
  CHECK(state.cursor == 1 + static_cast<int>(kFamily.size()));
  REQUIRE(te::selection(state).has_value());
  CHECK(te::selection(state)->start == 1);

  // Plain Left collapses to the selection's near edge.
  CHECK(te::move_horizontal(state, true, false));
  CHECK(state.cursor == 1);
  CHECK_FALSE(state.selection_anchor.has_value());

  CHECK(te::set_cursor(state, 0, false));
  CHECK_FALSE(te::move_horizontal(state, true, false));
}

TEST_CASE("text_edit: select_all") {
  TextEditState state = state_with("abc", 1);
  CHECK(te::select_all(state));
  CHECK(state.cursor == 3);
  CHECK(state.selection_anchor == 0);
  CHECK_FALSE(te::select_all(state));
  TextEditState empty;
  CHECK_FALSE(te::select_all(empty));
}

TEST_CASE("text_edit: composition previews without touching the model, then commits") {
  TextEditState state = state_with("ab", 1);
  CHECK(te::composition_update(state, "\u4e2d\u6d77", 1, 1));
  CHECK(state.composing);
  CHECK(state.text == "ab");
  CHECK(state.cursor == 1);
  CHECK(state.composition_focus_start == 3);
  CHECK(state.composition_focus_length == 3);

  const te::Projection shown = te::display_projection(state);
  CHECK(shown.text == "a\u4e2d\u6d77b");
  CHECK(shown.cursor == 4);
  REQUIRE(shown.underline.has_value());
  CHECK(shown.underline->start == 1);
  CHECK(shown.underline->end == 7);
  REQUIRE(shown.highlight.has_value());
  CHECK(shown.highlight->start == 4);

  // Editing keys belong to the IME while composing.
  CHECK_FALSE(te::backspace(state));
  CHECK_FALSE(te::move_horizontal(state, true, false));

  // A real commit replaces the composition.
  CHECK(te::insert(state, "\u4e2d"));
  CHECK_FALSE(state.composing);
  CHECK(state.text == "a\u4e2d"
                      "b");
  CHECK(state.cursor == 4);
}

TEST_CASE("text_edit: cancelling a composition restores the committed view") {
  TextEditState state = state_with("abcd", 3);
  state.selection_anchor = 1;
  CHECK(te::composition_update(state, "xy", 2, 0));
  CHECK(state.composition_replace_start == 1);
  CHECK(state.composition_replace_end == 3);
  CHECK(te::display_projection(state).text == "axyd");

  CHECK(te::cancel_composition(state));
  CHECK_FALSE(te::cancel_composition(state));
  CHECK(state.text == "abcd");
  CHECK(te::display_projection(state).text == "abcd");
  REQUIRE(te::selection(state).has_value());

  // An empty preedit is a cancel, too.
  CHECK(te::composition_update(state, "z", 0, 0));
  CHECK(te::composition_update(state, "", 0, 0));
  CHECK_FALSE(state.composing);
}
