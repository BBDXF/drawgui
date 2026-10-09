// WidgetKind::kTextArea: the multi-line editor over the shared editing core,
// driven through WidgetSet's text_area_* functions against a real font (the
// generated "DgTest Latin", every glyph the same 0.6em-wide bar, so wrap
// points are predictable) and, at the end, through the C ABI.

#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/abi/drawgui.h"
#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/types.h"
#include "drawgui/render/font_catalog.h"
#include "drawgui/render/prop_ids.generated.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/widget/widget_set.h"

namespace {

using dg::Color;
using dg::FontCatalog;
using dg::NodeId;
using dg::NodeStyle;
using dg::PixelPoint;
using dg::PixelRect;
using dg::RenderTree;
using dg::TextAreaMove;
using dg::Widget;
using dg::WidgetKind;
using dg::WidgetSet;

constexpr float kFontSize = 20.0F;
constexpr int kPool = 16;

struct AreaScene {
  RenderTree tree;
  WidgetSet widgets;
  FontCatalog fonts;
  NodeId area;
};

FontCatalog test_fonts() {
  const dg::Expected<FontCatalog, dg::FontError> scanned = FontCatalog::scan(DG_TEST_FONT_DIR);
  REQUIRE(scanned.has_value());
  return scanned.value();
}

dg::TreeSpec spec() {
  dg::TreeSpec out;
  out.viewport = dg::PixelSize{600, 400};
  out.background.fill = Color::from_argb(0xFF14171C);
  return out;
}

AreaScene build_area(int width, int height, const std::string& text, bool scrollbar = false) {
  AreaScene scene{RenderTree{spec()}, WidgetSet{}, test_fonts(), NodeId{}};
  const dg::Expected<dg::FontId, dg::FontError> font = scene.fonts.add("DgTest Latin", false);
  REQUIRE(font.has_value());
  RenderTree& tree = scene.tree;

  NodeStyle area_style;
  area_style.overflow = dg::Overflow::kClip;
  scene.area = tree.add_child(RenderTree::root(), PixelRect{0, 0, width, height}, area_style);

  Widget widget;
  widget.kind = WidgetKind::kTextArea;
  widget.selection_layer = tree.add_child(scene.area, PixelRect{}, NodeStyle{});
  NodeStyle fill;
  fill.fill = Color::from_argb(0x802E86DE);
  for (int i = 0; i < kPool; ++i) {
    widget.highlight_pool.push_back(tree.add_child(widget.selection_layer, PixelRect{}, fill));
  }
  for (int i = 0; i < 4; ++i) {
    widget.underline_pool.push_back(tree.add_child(scene.area, PixelRect{}, fill));
  }
  NodeStyle content;
  content.text.font = font.value();
  content.text.size = kFontSize;
  content.text.color = Color::from_argb(0xFFE8EDF4);
  content.text.align = dg::TextAlign::kLeft;
  content.text.wrap = true;
  widget.content = tree.add_child(scene.area, PixelRect{}, content);
  widget.caret = tree.add_child(scene.area, PixelRect{}, fill);
  widget.edit.text = text;
  scene.widgets.attach(scene.area, widget);
  if (scrollbar) {
    const NodeId track = tree.add_child(scene.area, PixelRect{}, NodeStyle{});
    const NodeId thumb = tree.add_child(track, PixelRect{}, NodeStyle{});
    scene.widgets.attach_scrollbar(tree, scene.area, track, thumb);
  }
  scene.widgets.text_area_set_focus(tree, scene.fonts, scene.area, true);
  return scene;
}

bool move(AreaScene& s, TextAreaMove where, bool extend = false) {
  return s.widgets.text_area_move(s.tree, s.fonts, s.area, where, extend);
}

PixelRect caret(const AreaScene& s) {
  return s.tree.local_bounds(s.widgets.at(s.area).caret);
}

// The caret's rectangle relative to the viewport (scroll applied) lies
// entirely inside the area.
bool caret_visible(const AreaScene& s) {
  const PixelRect c = caret(s);
  const int y = c.y - s.tree.scroll_offset(s.area).y;
  const int height = s.tree.local_bounds(s.area).height;
  return c.height > 0 && y >= 0 && y + c.height <= height;
}

}  // namespace

TEST_SUITE("text area") {
  TEST_CASE("a 200-line document scrolls so the caret is always visible") {
    std::string doc;
    for (int i = 0; i < 200; ++i) {
      doc += "line " + std::to_string(i) + "\n";
    }
    AreaScene s = build_area(300, 200, doc, /*scrollbar=*/true);
    CHECK(caret_visible(s));
    const auto check_each = [&](TextAreaMove where, int times) {
      for (int i = 0; i < times; ++i) {
        move(s, where);
        REQUIRE(caret_visible(s));
      }
    };
    check_each(TextAreaMove::kDocStart, 1);
    CHECK(s.tree.scroll_offset(s.area).y == 0);
    check_each(TextAreaMove::kLineDown, 230);
    CHECK(s.widgets.text_area_cursor(s.area) == static_cast<int>(doc.size()));
    check_each(TextAreaMove::kPageUp, 6);
    check_each(TextAreaMove::kLineUp, 30);
    check_each(TextAreaMove::kPageDown, 4);
    for (int i = 0; i < 20; ++i) {
      s.widgets.text_area_insert(s.tree, s.fonts, s.area, i % 5 == 4 ? "\n" : "x");
      REQUIRE(caret_visible(s));
      s.widgets.text_area_backspace(s.tree, s.fonts, s.area);
      REQUIRE(caret_visible(s));
    }
    CHECK(s.widgets.text_area_text(s.area) == doc);
    check_each(TextAreaMove::kDocEnd, 1);
    CHECK(s.tree.scroll_offset(s.area).y > 0);

    // Wheel-style scrolling moves the view, not the caret.
    const int cursor = s.widgets.text_area_cursor(s.area);
    CHECK(s.widgets.scroll_by(s.tree, s.area, PixelRect{0, 0, 300, 200}, 0, -400));
    CHECK(s.widgets.text_area_cursor(s.area) == cursor);
  }

  TEST_CASE("Up/Down through a short line returns to the original column") {
    AreaScene s = build_area(400, 200, "abcdefgh\nab\nabcdefgh");
    move(s, TextAreaMove::kDocStart);
    for (int i = 0; i < 6; ++i) {
      move(s, TextAreaMove::kCharRight);
    }
    CHECK(s.widgets.text_area_cursor(s.area) == 6);
    move(s, TextAreaMove::kLineDown);
    CHECK(s.widgets.text_area_cursor(s.area) == 11);  // end of the short "ab"
    move(s, TextAreaMove::kLineDown);
    CHECK(s.widgets.text_area_cursor(s.area) == 18);
    move(s, TextAreaMove::kLineUp);
    move(s, TextAreaMove::kLineUp);
    CHECK(s.widgets.text_area_cursor(s.area) == 6);
    // First line: Up goes to the start; last line: Down goes to the end.
    move(s, TextAreaMove::kLineUp);
    CHECK(s.widgets.text_area_cursor(s.area) == 0);
    move(s, TextAreaMove::kDocEnd);
    move(s, TextAreaMove::kLineDown);
    CHECK(s.widgets.text_area_cursor(s.area) == 20);
    // A horizontal move forgets the column.
    move(s, TextAreaMove::kDocStart);
    move(s, TextAreaMove::kCharRight);
    CHECK_FALSE(s.widgets.at(s.area).edit.preferred_x.has_value());
  }

  TEST_CASE("Home/End follow soft-wrapped visual lines") {
    // 12px glyphs, 120px wrap width: "aaaa bbbb " fits on the first row.
    AreaScene s = build_area(120, 200, "aaaa bbbb cccc dddd");
    move(s, TextAreaMove::kDocStart);
    const PixelRect first = caret(s);
    move(s, TextAreaMove::kLineEnd);
    const int wrap = s.widgets.text_area_cursor(s.area);
    CHECK(wrap > 0);
    CHECK(wrap < 19);
    CHECK(caret(s).y == first.y);  // upstream: drawn at the end of row 0
    CHECK(caret(s).x > first.x);
    move(s, TextAreaMove::kLineStart);
    CHECK(s.widgets.text_area_cursor(s.area) == 0);

    move(s, TextAreaMove::kDocEnd);
    const PixelRect last = caret(s);
    CHECK(last.y > first.y);
    move(s, TextAreaMove::kLineStart);
    CHECK(s.widgets.text_area_cursor(s.area) == wrap);
    CHECK(caret(s).y == last.y);  // downstream: the start of row 1
    CHECK(caret(s).x == first.x);
    move(s, TextAreaMove::kLineEnd);
    CHECK(s.widgets.text_area_cursor(s.area) == 19);
  }

  TEST_CASE("a drag across three lines shows three highlight rects") {
    AreaScene s = build_area(400, 200, "aaaa\nbbbb\ncccc\ndddd");
    const int line = caret(s).height;
    REQUIRE(line > 0);
    s.widgets.text_area_click(s.tree, s.fonts, s.area, PixelPoint{25, line / 2}, false);
    s.widgets.text_area_click(s.tree, s.fonts, s.area, PixelPoint{25, (2 * line) + (line / 2)},
                              true);
    const std::optional<dg::TextSelection> selection = s.widgets.text_area_selection(s.area);
    REQUIRE(selection.has_value());
    CHECK(selection->start == 2);  // NOLINT(bugprone-unchecked-optional-access)
    CHECK(selection->end == 12);   // NOLINT(bugprone-unchecked-optional-access)
    CHECK(s.widgets.text_area_highlight_rects(s.tree, s.area).size() == 3);

    // A selected newline on an empty line gets its own small block.
    AreaScene empty = build_area(400, 200, "a\n\nb");
    empty.widgets.text_area_select_all(empty.tree, empty.fonts, empty.area);
    CHECK(empty.widgets.text_area_highlight_rects(empty.tree, empty.area).size() == 3);
  }

  TEST_CASE("an IME composition across a wrap draws two underline segments") {
    AreaScene s = build_area(120, 200, "");
    s.widgets.text_area_composition_update(s.tree, s.fonts, s.area, "xxxxxxxxxxxxxxx", 15, 0);
    CHECK(s.widgets.at(s.area).edit.composing);
    CHECK(s.widgets.text_area_underline_rects(s.tree, s.area).size() == 2);
    s.widgets.text_area_cancel_composition(s.tree, s.fonts, s.area);
    CHECK(s.widgets.text_area_underline_rects(s.tree, s.area).empty());
    CHECK(s.widgets.text_area_text(s.area).empty());
  }

  TEST_CASE("Enter keeps the newline; the kind-agnostic accessors see it") {
    AreaScene s = build_area(300, 120, "");
    CHECK(s.widgets.editor_insert(s.tree, s.fonts, s.area, "a\nb"));
    CHECK(s.widgets.text_area_text(s.area) == "a\nb");
    const dg::TextEditState* state = s.widgets.editor_state(s.area);
    REQUIRE(state != nullptr);
    CHECK(state->cursor == 3);  // NOLINT(clang-analyzer-core.NullDereference)
    // Caret on the empty line after a trailing newline sits below line 0.
    const PixelRect before = caret(s);
    s.widgets.text_area_insert(s.tree, s.fonts, s.area, "\n");
    CHECK(caret(s).y > before.y);
    CHECK(caret(s).x == s.widgets.at(s.area).text_inset);
  }
}

// --- Through the C ABI ---------------------------------------------------

namespace {

struct AbiArea {
  dg_app_t* app = nullptr;
  dg_window_t* window = nullptr;
  dg_node_t* area = nullptr;

  explicit AbiArea(std::uint16_t type = DG_NODE_TYPE_TEXT_AREA) {
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    app = dg_app_create(nullptr);
    REQUIRE(app != nullptr);
    REQUIRE(dg_app_add_font(app, DG_TEST_FONT_DIR, "DgTest Latin") == DG_ERR_OK);
    dg_window_opts opts{};
    opts.size = sizeof(opts);
    opts.title = "abi text area";
    opts.width = 360;
    opts.height = 200;
    window = dg_window_create(app, &opts);
    REQUIRE(window != nullptr);
    area = dg_node_create(app, type);
    REQUIRE(area != nullptr);
    REQUIRE(dg_window_set_root(window, area) == DG_ERR_OK);
  }
  AbiArea(const AbiArea&) = delete;
  AbiArea& operator=(const AbiArea&) = delete;
  AbiArea(AbiArea&&) = delete;
  AbiArea& operator=(AbiArea&&) = delete;
  ~AbiArea() { dg_app_destroy(app); }

  // Pumps and returns how many DG_EVENT_TEXT_CHANGED arrived.
  int pump() const {
    int changed = 0;
    for (int i = 0; i < 4; ++i) {
      REQUIRE(dg_wait_events(app, 5) >= 0);
      dg_event events[16];
      const std::int32_t n = dg_poll_events(app, events, 16);
      for (std::int32_t k = 0; k < n; ++k) {
        changed += events[k].kind == DG_EVENT_TEXT_CHANGED ? 1 : 0;
      }
    }
    return changed;
  }

  void click(std::int32_t x, std::int32_t y) const {
    (void)pump();
    REQUIRE(dg_debug_warp_pointer(window, x, y) == DG_ERR_OK);
    REQUIRE(dg_debug_post_pointer_button(window, 1, x, y) == DG_ERR_OK);
    REQUIRE(dg_debug_post_pointer_button(window, 0, x, y) == DG_ERR_OK);
    (void)pump();
  }

  [[nodiscard]] std::string text() const {
    std::uint32_t len = 0;
    REQUIRE(dg_node_get_text(area, nullptr, 0, &len) == DG_ERR_OK);
    std::string out(len, '\0');
    REQUIRE(dg_node_get_text(area, out.data(), len, nullptr) == DG_ERR_OK);
    return out;
  }
};

}  // namespace

TEST_SUITE("abi text area") {
  TEST_CASE("create, type, Enter -> TEXT_CHANGED and \"a\\nb\"") {
    AbiArea f;
    CHECK(f.text().empty());
    f.click(40, 20);
    REQUIRE(dg_debug_post_text_input(f.window, "a") == DG_ERR_OK);
    CHECK(f.pump() == 1);
    REQUIRE(dg_debug_post_text_field_key(f.window, 1, "Enter", 0) == DG_ERR_OK);
    CHECK(f.pump() == 1);
    REQUIRE(dg_debug_post_text_input(f.window, "b") == DG_ERR_OK);
    CHECK(f.pump() == 1);
    CHECK(f.text() == "a\nb");

    // Up/Left/Down/End are accepted and change nothing.
    for (const char* key : {"Up", "Down", "PageUp", "PageDown", "Left", "End"}) {
      REQUIRE(dg_debug_post_text_field_key(f.window, 1, key, 0) == DG_ERR_OK);
      CHECK(f.pump() == 0);
    }
    REQUIRE(dg_debug_post_text_field_key(f.window, 1, "Backspace", 0) == DG_ERR_OK);
    CHECK(f.pump() == 1);
    CHECK(f.text() == "a\n");
    CHECK(dg_debug_post_text_field_key(f.window, 1, "Bogus", 0) == DG_ERR_INVALID_ARGUMENT);

    // dg_node_set_text replaces the editor's model.
    const std::string replaced = "one\ntwo";
    REQUIRE(dg_node_set_text(f.area, DG_PROP_TEXT, replaced.data(),
                             static_cast<std::uint32_t>(replaced.size())) == DG_ERR_OK);
    CHECK(f.text() == replaced);
  }

  TEST_CASE("Mod+Z / Mod+Shift+Z / Mod+Y undo and redo through dg_debug_post_key") {
    AbiArea f;
    f.click(40, 20);
    REQUIRE(dg_debug_post_text_input(f.window, "x") == DG_ERR_OK);
    CHECK(f.pump() == 1);
    REQUIRE(dg_debug_post_key(f.window, 1, "Mod+Z") == DG_ERR_OK);
    CHECK(f.pump() == 1);
    CHECK(f.text().empty());
    REQUIRE(dg_debug_post_key(f.window, 1, "Mod+Shift+Z") == DG_ERR_OK);
    CHECK(f.pump() == 1);
    CHECK(f.text() == "x");
    REQUIRE(dg_debug_post_key(f.window, 1, "Mod+Z") == DG_ERR_OK);
    CHECK(f.pump() == 1);
    REQUIRE(dg_debug_post_key(f.window, 1, "Mod+Y") == DG_ERR_OK);
    CHECK(f.pump() == 1);
    CHECK(f.text() == "x");
    REQUIRE(dg_debug_post_key(f.window, 1, "Mod+Y") == DG_ERR_OK);  // nothing left to redo
    CHECK(f.pump() == 0);
  }

  TEST_CASE("Mod+A/C/X/V copy, cut and paste through dg_debug_post_key") {
    AbiArea f;
    f.click(40, 20);
    const std::string init = "a\nb";
    REQUIRE(dg_node_set_text(f.area, DG_PROP_TEXT, init.data(),
                             static_cast<std::uint32_t>(init.size())) == DG_ERR_OK);
    f.click(40, 20);
    REQUIRE(dg_debug_post_key(f.window, 1, "Mod+A") == DG_ERR_OK);
    CHECK(f.pump() == 0);
    REQUIRE(dg_debug_post_key(f.window, 1, "Mod+C") == DG_ERR_OK);
    CHECK(f.pump() == 0);
    CHECK(f.text() == init);
    REQUIRE(dg_debug_post_key(f.window, 1, "Mod+X") == DG_ERR_OK);
    CHECK(f.pump() == 1);
    CHECK(f.text().empty());
    REQUIRE(dg_debug_post_key(f.window, 1, "Mod+V") == DG_ERR_OK);
    CHECK(f.pump() == 1);
    CHECK(f.text() == init);  // newline kept in a text area
  }

  TEST_CASE("Mod+A/C/X/V in a text field copy, cut and paste through dg_debug_post_key") {
    AbiArea f(DG_NODE_TYPE_TEXT_FIELD);
    f.click(40, 20);
    const std::string init = "ab";
    REQUIRE(dg_node_set_text(f.area, DG_PROP_TEXT, init.data(),
                             static_cast<std::uint32_t>(init.size())) == DG_ERR_OK);
    f.click(40, 20);
    REQUIRE(dg_debug_post_key(f.window, 1, "Mod+A") == DG_ERR_OK);
    CHECK(f.pump() == 0);
    REQUIRE(dg_debug_post_key(f.window, 1, "Mod+C") == DG_ERR_OK);
    CHECK(f.pump() == 0);
    CHECK(f.text() == init);
    REQUIRE(dg_debug_post_key(f.window, 1, "Mod+X") == DG_ERR_OK);
    CHECK(f.pump() == 1);
    CHECK(f.text().empty());
    REQUIRE(dg_debug_post_key(f.window, 1, "Mod+V") == DG_ERR_OK);
    CHECK(f.pump() == 1);
    CHECK(f.text() == init);
  }

  TEST_CASE("a disabled text area takes no edits") {
    AbiArea f;
    f.click(40, 20);
    REQUIRE(dg_debug_post_text_input(f.window, "a") == DG_ERR_OK);
    CHECK(f.pump() == 1);
    dg_value disabled{};
    disabled.size = sizeof(disabled);
    disabled.type = DG_VALUE_BOOL;
    disabled.bits = 1;
    REQUIRE(dg_node_set_prop(f.area, DG_PROP_DISABLED, &disabled) == DG_ERR_OK);
    REQUIRE(dg_debug_post_text_input(f.window, "b") == DG_ERR_OK);
    REQUIRE(dg_debug_post_text_field_key(f.window, 1, "Enter", 0) == DG_ERR_OK);
    CHECK(f.pump() == 0);
    f.click(40, 20);
    REQUIRE(dg_debug_post_text_input(f.window, "c") == DG_ERR_OK);
    CHECK(f.pump() == 0);
    CHECK(f.text() == "a");
  }

  TEST_CASE("the I-beam cursor shows over a text area") {
    AbiArea f;
    f.click(40, 20);
    CHECK(dg_debug_get_cursor(f.window) == static_cast<std::int32_t>(DG_CURSOR_TEXT));
  }
}
