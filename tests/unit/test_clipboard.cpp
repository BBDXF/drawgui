// Clipboard copy/cut/paste, plus TextField's own select_all (8-4) -
// dg::apply_clipboard_action() (clipboard_actions.h) and
// WindowManager::set_clipboard_text()/get_clipboard_text(), exercised
// directly rather than only through the real event pipeline - the
// end-to-end proof through a real posted Mod+C/Mod+X/Mod+V/Mod+A and
// route_key_event() lives in examples/12_text_input's own
// --verify-text-input (text_field_check.cpp), matching this project's own
// "a unit test proves the mechanism, an example's own verify proves the
// pipeline" split every other slice already uses.
//
// A REAL dg::WindowManager, NOT A FAKE: the clipboard seam this file tests
// is METHODS ON WindowManager (window_manager.h's own comment on why),
// precisely because a free function would let a test construct nothing at
// all and still compile - a fake here would prove only that the fake
// agrees with itself. SDL_VIDEODRIVER=dummy is forced before every
// WindowManager::create() so this file never touches the machine's own
// real desktop clipboard, and (this slice's own empirical finding,
// verified against the installed SDL3 before writing this file) the
// dummy driver's own clipboard resets cleanly across an SDL_Quit()/
// SDL_Init() cycle - each TEST_CASE below constructs its OWN
// WindowManager, so "nothing was ever set" and "a stale value from the
// previous test case" cannot be confused with one another the way sharing
// one process-wide clipboard across tests would risk.

#include <cstdlib>
#include <optional>
#include <string>
#include <utility>

#include <doctest/doctest.h>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/types.h"
#include "drawgui/render/font_catalog.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/shortcuts/action_ids.generated.h"
#include "drawgui/shortcuts/clipboard_actions.h"
#include "drawgui/widget/widget_set.h"
#include "drawgui/window/window_manager.h"

namespace {

using dg::Color;
using dg::Expected;
using dg::FontCatalog;
using dg::FontId;
using dg::NodeId;
using dg::NodeStyle;
using dg::PixelRect;
using dg::PixelSize;
using dg::RenderTree;
using dg::TextAlign;
using dg::TextFieldEditContext;
using dg::TextFieldMove;
using dg::TextSelection;
using dg::TreeSpec;
using dg::Widget;
using dg::WidgetKind;
using dg::WidgetSet;
using dg::WindowError;
using dg::WindowManager;

constexpr float kFontSize = 20.0F;

FontCatalog test_fonts() {
  const Expected<FontCatalog, dg::FontError> scanned = FontCatalog::scan(DG_TEST_FONT_DIR);
  REQUIRE(scanned.has_value());
  return scanned.value();
}

// test_shortcut_routing.cpp's own build_field() fixture, reused rather
// than a second hand-rolled kTextField - a real widget with a real font
// is what lets text_field_move()/text_field_insert() actually move a
// caret and edit text rather than only being trusted to.
struct FieldScene {
  RenderTree tree;
  WidgetSet widgets;
  FontCatalog fonts;
  NodeId field;
};

FieldScene build_field(const std::string& initial) {
  TreeSpec spec;
  spec.viewport = PixelSize{400, 300};
  RenderTree tree{spec};
  WidgetSet widgets;
  FontCatalog fonts = test_fonts();
  const Expected<FontId, dg::FontError> font_id = fonts.add("DgTest Latin", false);
  REQUIRE(font_id.has_value());

  NodeStyle field_style;
  field_style.overflow = dg::Overflow::kClip;
  const NodeId field =
      tree.add_child(RenderTree::root(), PixelRect{0, 0, 200, 30}, field_style);
  const NodeId highlight = tree.add_child(field, PixelRect{0, 0, 0, 30}, NodeStyle{});
  const NodeId underline = tree.add_child(field, PixelRect{0, 0, 0, 30}, NodeStyle{});

  NodeStyle content_style;
  content_style.text.font = font_id.value();
  content_style.text.size = kFontSize;
  content_style.text.color = Color::from_argb(0xFFE8EDF4);
  content_style.text.align = TextAlign::kLeft;
  content_style.text.text = initial;
  const NodeId content = tree.add_child(field, PixelRect{0, 0, 1, 30}, content_style);

  NodeStyle caret_style;
  caret_style.fill = Color::from_argb(0xFFFFFFFF);
  const NodeId caret = tree.add_child(field, PixelRect{0, 0, 2, 30}, caret_style);

  Widget widget;
  widget.kind = WidgetKind::kTextField;
  widget.content = content;
  widget.caret = caret;
  widget.selection_highlight = highlight;
  widget.composition_underline = underline;
  widget.text = initial;
  widget.cursor = static_cast<int>(initial.size());
  widgets.attach(field, widget);

  return FieldScene{std::move(tree), std::move(widgets), std::move(fonts), field};
}

// Selects `text`'s first `count` bytes as [0, count) - the shared "select
// a known prefix" step every test below that needs an active selection
// starts from, rather than repeating the move-to-start-then-extend dance
// inline in each TEST_CASE.
void select_prefix(FieldScene& scene, int count) {
  scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field, TextFieldMove::kLineStart,
                                false);
  for (int i = 0; i < count; ++i) {
    scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field,
                                  TextFieldMove::kCharRight, true);
  }
}

WindowManager make_headless_manager() {
  setenv("SDL_VIDEODRIVER", "dummy", 1);
  Expected<WindowManager, WindowError> made = WindowManager::create();
  REQUIRE(made.has_value());
  return std::move(made.value());
}

}  // namespace

TEST_SUITE("clipboard (8-4)") {
  TEST_CASE("get_clipboard_text with nothing ever set returns empty, never hangs") {
    WindowManager manager = make_headless_manager();
    CHECK(manager.get_clipboard_text().empty());
  }

  TEST_CASE("set then get round-trips exactly, including multi-byte UTF-8 (CJK)") {
    WindowManager manager = make_headless_manager();
    const std::string cjk = "\xE4\xB8\xAD\xE6\x96\x87";  // "中文"
    CHECK(manager.set_clipboard_text(cjk));
    CHECK(manager.get_clipboard_text() == cjk);
  }

  TEST_CASE("copy with a selection puts exactly the selected substring on the clipboard") {
    WindowManager manager = make_headless_manager();
    FieldScene scene = build_field("hello world");
    select_prefix(scene, 5);

    const TextFieldEditContext ctx{scene.tree, scene.fonts, scene.widgets, manager};
    CHECK(dg::apply_clipboard_action(DG_ACTION_COPY, scene.field, ctx));
    CHECK(manager.get_clipboard_text() == "hello");
    // Copy never touches the model - the selection and the text are both
    // exactly as they were.
    CHECK(scene.widgets.text_field_text(scene.field) == "hello world");
  }

  TEST_CASE(
      "copy with NO selection is a deliberate no-op (this slice's own named decision): the "
      "clipboard is left untouched, not filled with the whole field") {
    WindowManager manager = make_headless_manager();
    FieldScene scene = build_field("hello world");
    REQUIRE(manager.set_clipboard_text("sentinel"));

    const TextFieldEditContext ctx{scene.tree, scene.fonts, scene.widgets, manager};
    CHECK_FALSE(dg::apply_clipboard_action(DG_ACTION_COPY, scene.field, ctx));
    CHECK(manager.get_clipboard_text() == "sentinel");
  }

  TEST_CASE("cut removes the selection from the field AND leaves it on the clipboard") {
    WindowManager manager = make_headless_manager();
    FieldScene scene = build_field("hello world");
    select_prefix(scene, 5);

    const TextFieldEditContext ctx{scene.tree, scene.fonts, scene.widgets, manager};
    CHECK(dg::apply_clipboard_action(DG_ACTION_CUT, scene.field, ctx));
    CHECK(manager.get_clipboard_text() == "hello");
    CHECK(scene.widgets.text_field_text(scene.field) == " world");
  }

  TEST_CASE("cut with NO selection is a deliberate no-op, the identical decision copy makes") {
    WindowManager manager = make_headless_manager();
    FieldScene scene = build_field("hello world");
    REQUIRE(manager.set_clipboard_text("sentinel"));

    const TextFieldEditContext ctx{scene.tree, scene.fonts, scene.widgets, manager};
    CHECK_FALSE(dg::apply_clipboard_action(DG_ACTION_CUT, scene.field, ctx));
    CHECK(manager.get_clipboard_text() == "sentinel");
    CHECK(scene.widgets.text_field_text(scene.field) == "hello world");
  }

  TEST_CASE("paste inserts the clipboard's text at the cursor") {
    WindowManager manager = make_headless_manager();
    FieldScene scene = build_field("hello");  // cursor starts at the end
    REQUIRE(manager.set_clipboard_text(" world"));

    const TextFieldEditContext ctx{scene.tree, scene.fonts, scene.widgets, manager};
    CHECK(dg::apply_clipboard_action(DG_ACTION_PASTE, scene.field, ctx));
    CHECK(scene.widgets.text_field_text(scene.field) == "hello world");
  }

  TEST_CASE("paste with an active selection REPLACES it, rather than inserting alongside it") {
    WindowManager manager = make_headless_manager();
    FieldScene scene = build_field("hello world");
    REQUIRE(manager.set_clipboard_text("XYZ"));
    select_prefix(scene, 5);

    const TextFieldEditContext ctx{scene.tree, scene.fonts, scene.widgets, manager};
    CHECK(dg::apply_clipboard_action(DG_ACTION_PASTE, scene.field, ctx));
    CHECK(scene.widgets.text_field_text(scene.field) == "XYZ world");
  }

  TEST_CASE(
      "paste of text containing '\\n' drops the control character - PRE-EXISTING behaviour "
      "(text_field_insert()'s own ASCII control-byte filter), NOT something 8-4 introduces: "
      "a single-line field has nowhere to put a second line. This is the first slice a user "
      "can trigger it trivially (copy two lines from elsewhere, paste), so it is asserted "
      "here by name rather than left to be found later as a defect. Fixing it needs a "
      "multi-line text field, which this project does not have.") {
    WindowManager manager = make_headless_manager();
    FieldScene scene = build_field("");
    REQUIRE(manager.set_clipboard_text("line one\nline two"));

    const TextFieldEditContext ctx{scene.tree, scene.fonts, scene.widgets, manager};
    CHECK(dg::apply_clipboard_action(DG_ACTION_PASTE, scene.field, ctx));
    CHECK(scene.widgets.text_field_text(scene.field) == "line oneline two");
  }

  TEST_CASE("select_all selects the whole field, REPLACING any prior partial selection") {
    WindowManager manager = make_headless_manager();
    FieldScene scene = build_field("hello world");
    select_prefix(scene, 1);  // a partial selection select_all must override, not extend

    const TextFieldEditContext ctx{scene.tree, scene.fonts, scene.widgets, manager};
    CHECK(dg::apply_clipboard_action(DG_ACTION_SELECT_ALL, scene.field, ctx));
    const std::optional<TextSelection> selection =
        scene.widgets.text_field_selection(scene.field);
    REQUIRE(selection.has_value());
    CHECK(selection->start == 0);
    CHECK(selection->end ==
          static_cast<int>(scene.widgets.text_field_text(scene.field).size()));
  }

  TEST_CASE("select_all on an empty field is a no-op: there is nothing to select") {
    WindowManager manager = make_headless_manager();
    FieldScene scene = build_field("");

    const TextFieldEditContext ctx{scene.tree, scene.fonts, scene.widgets, manager};
    CHECK_FALSE(dg::apply_clipboard_action(DG_ACTION_SELECT_ALL, scene.field, ctx));
    CHECK_FALSE(scene.widgets.text_field_selection(scene.field).has_value());
  }
}
