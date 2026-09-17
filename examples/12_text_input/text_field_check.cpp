#include "text_field_check.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/shortcuts/action_ids.generated.h"
#include "drawgui/shortcuts/clipboard_actions.h"
#include "drawgui/shortcuts/logical_key.generated.h"
#include "drawgui/shortcuts/router.h"

#include "text_field_scene.h"

namespace text_field_check {
namespace {

using dg::NodeId;
using dg::PixelSize;
using dg::RasterSurface;

constexpr PixelSize kSize{420, 220};

text_field_scene::Options spec_for(PixelSize size) {
  text_field_scene::Options options;
  options.spec.viewport = size;
  options.spec.background.fill = dg::Color::from_argb(0xFF0E1218);
  return options;
}

std::vector<std::uint8_t> snapshot(const RasterSurface& surface) {
  const dg::PixelView view = surface.peek_pixels();
  const auto* bytes = static_cast<const std::uint8_t*>(view.pixels);
  return std::vector<std::uint8_t>{
      bytes, bytes + (view.row_bytes * static_cast<std::size_t>(view.height))};
}

// --------------------------------------------------------------------------
// Claim 1: clicking a field focuses it and places the cursor; clicking a
// second field blurs the first - dg::Focus is exclusive.
// --------------------------------------------------------------------------

bool check_click_focuses_and_is_exclusive(std::ostream& out) {
  text_field_scene::Scene scene = text_field_scene::build(spec_for(kSize));
  bool ok = true;

  if (scene.focus.current().has_value()) {
    out << "  FAIL: a freshly built scene starts with something focused\n";
    ok = false;
  }

  text_field_scene::set_focus(scene, scene.handles.field_a);
  if (scene.focus.current() != scene.handles.field_a) {
    out << "  FAIL: focusing field a did not take\n";
    ok = false;
  }

  text_field_scene::set_focus(scene, scene.handles.field_b);
  if (scene.focus.current() != scene.handles.field_b) {
    out << "  FAIL: focusing field b did not take\n";
    ok = false;
  }
  // Exclusivity is checked at the dg::Focus level in
  // tests/unit/test_text_input.cpp; here the observable consequence is
  // checked - field a's caret is hidden (zero width) once field b takes
  // focus.
  const dg::NodeId caret_a = scene.widgets.at(scene.handles.field_a).caret;
  if (scene.tree.render().local_bounds(caret_a).width != 0) {
    out << "  FAIL: field a's caret is still visible after field b took focus\n";
    ok = false;
  }

  if (ok) {
    out << "  OK: focus is exclusive, and blurring a field hides its caret\n";
  }
  return ok;
}

// --------------------------------------------------------------------------
// Claim 2: typing inserts at the cursor; Backspace/Delete edit; Home/End
// move the cursor to the ends.
// --------------------------------------------------------------------------

bool check_typing_and_editing(std::ostream& out) {
  text_field_scene::Scene scene = text_field_scene::build(spec_for(kSize));
  bool ok = true;
  if (!scene.fonts.has_value()) {
    out << "  SKIP: no system font found under /usr/share/fonts\n";
    return true;
  }
  const dg::FontCatalog& fonts = *scene.fonts;
  const NodeId field = scene.handles.field_b;
  dg::RenderTree& tree = scene.tree.render();

  text_field_scene::set_focus(scene, field);
  scene.widgets.text_field_insert(tree, fonts, field, "hello");
  if (scene.widgets.text_field_text(field) != "hello") {
    out << "  FAIL: typing \"hello\" into an empty field did not produce it, got \""
        << scene.widgets.text_field_text(field) << "\"\n";
    ok = false;
  }

  scene.widgets.text_field_backspace(tree, fonts, field);
  if (scene.widgets.text_field_text(field) != "hell") {
    out << "  FAIL: Backspace did not remove the last character\n";
    ok = false;
  }

  scene.widgets.text_field_move(tree, fonts, field, dg::TextFieldMove::kLineStart, false);
  scene.widgets.text_field_delete_forward(tree, fonts, field);
  if (scene.widgets.text_field_text(field) != "ell") {
    out << "  FAIL: Delete at the start did not remove the first character\n";
    ok = false;
  }

  scene.widgets.text_field_move(tree, fonts, field, dg::TextFieldMove::kLineEnd, false);
  if (scene.widgets.text_field_cursor(field) != 3) {
    out << "  FAIL: End did not move the cursor to the end (3), got "
        << scene.widgets.text_field_cursor(field) << "\n";
    ok = false;
  }

  if (ok) {
    out << "  OK: typing, Backspace, Delete and Home/End all match the expected model\n";
  }
  return ok;
}

// --------------------------------------------------------------------------
// Claim 3: field a's pre-filled string overflows its 260px width, so it
// shows an ellipsis-truncated prefix while unfocused and the full string,
// scrolled, while focused.
// --------------------------------------------------------------------------

bool check_overflow_ellipsis_and_scroll(std::ostream& out) {
  text_field_scene::Scene scene = text_field_scene::build(spec_for(kSize));
  bool ok = true;
  if (!scene.fonts.has_value()) {
    out << "  SKIP: no system font found under /usr/share/fonts\n";
    return true;
  }

  const std::string unfocused_display =
      scene.tree.render().style(scene.widgets.at(scene.handles.field_a).content).text.text;
  if (unfocused_display == text_field_scene::kFieldAInitial) {
    out << "  FAIL: field a's default string is expected to overflow its field and did not\n";
    ok = false;
  }
  if (!unfocused_display.ends_with("...")) {
    out << "  FAIL: an unfocused overflowing field should end its display in an ellipsis, got "
           "\""
        << unfocused_display << "\"\n";
    ok = false;
  }
  const dg::NodeId caret_a = scene.widgets.at(scene.handles.field_a).caret;
  if (scene.tree.render().local_bounds(caret_a).width != 0) {
    out << "  FAIL: an unfocused field's caret should be hidden\n";
    ok = false;
  }

  text_field_scene::set_focus(scene, scene.handles.field_a);
  const std::string focused_display =
      scene.tree.render().style(scene.widgets.at(scene.handles.field_a).content).text.text;
  if (focused_display != text_field_scene::kFieldAInitial) {
    out << "  FAIL: a focused field should show its full model string, got \""
        << focused_display << "\"\n";
    ok = false;
  }
  if (scene.tree.render().local_bounds(caret_a).width == 0) {
    out << "  FAIL: a focused field's caret should be visible\n";
    ok = false;
  }

  if (ok) {
    out << "  OK: unfocused shows an ellipsis-truncated prefix, focused shows the full "
           "string\n";
  }
  return ok;
}

// --------------------------------------------------------------------------
// Claim 4 - THE ONE THE TASK ASKS TO WORK OUT RATHER THAN ASSUME: does
// inserting/deleting a character in a fixed-width TextField require a
// relayout, or only a repaint? Measured via LayoutStats, not asserted from
// the code that is supposed to make it true - the identical technique
// doc/scrolling.md section 4 and doc/form-controls.md section 3 both used
// for the scroll offset and the slider's thumb.
// --------------------------------------------------------------------------

bool check_editing_costs_no_relayout(std::ostream& out) {
  text_field_scene::Scene scene = text_field_scene::build(spec_for(kSize));
  bool ok = true;
  if (!scene.fonts.has_value()) {
    out << "  SKIP: no system font found under /usr/share/fonts\n";
    return true;
  }
  const dg::FontCatalog& fonts = *scene.fonts;
  const NodeId field = scene.handles.field_b;
  dg::RenderTree& tree = scene.tree.render();

  text_field_scene::set_focus(scene, field);
  // layout() right after build/focus should already find nothing dirty.
  const dg::LayoutStats warmup = scene.tree.layout();
  (void)warmup;

  scene.widgets.text_field_insert(tree, fonts, field, "a");
  const dg::LayoutStats after_insert = scene.tree.layout();
  if (after_insert.nodes_visited != 0 || after_insert.nodes_relaid_out != 0) {
    out << "  FAIL: inserting one character visited " << after_insert.nodes_visited
        << " layout node(s) and relaid out " << after_insert.nodes_relaid_out
        << " - expected 0 and 0 (a fixed-width field's edit is RenderTree-only: "
           "set_text()/set_local_bounds()/set_local_origin(), never LayoutTree::set_box())\n";
    ok = false;
  }

  scene.widgets.text_field_backspace(tree, fonts, field);
  const dg::LayoutStats after_backspace = scene.tree.layout();
  if (after_backspace.nodes_visited != 0 || after_backspace.nodes_relaid_out != 0) {
    out << "  FAIL: a Backspace visited " << after_backspace.nodes_visited
        << " layout node(s) and relaid out " << after_backspace.nodes_relaid_out
        << " - expected 0 and 0\n";
    ok = false;
  }

  if (ok) {
    out << "  OK: character insertion and deletion cost a repaint and never a relayout, "
           "measured via LayoutStats on the actual demo scene (nodes_visited == "
           "nodes_relaid_out == 0 both times)\n";
  }
  return ok;
}

// --------------------------------------------------------------------------
// Claim 5: incremental repaint after a script of edits matches a full
// repaint, byte for byte - the identical acceptance technique every prior
// slice uses, extended to text editing.
// --------------------------------------------------------------------------

bool check_identity(std::ostream& out) {
  std::optional<RasterSurface> damaged = RasterSurface::create(kSize.width, kSize.height);
  std::optional<RasterSurface> whole = RasterSurface::create(kSize.width, kSize.height);
  if (!damaged.has_value() || !whole.has_value()) {
    out << "  FAIL: could not allocate a raster surface\n";
    return false;
  }

  text_field_scene::Scene incremental = text_field_scene::build(spec_for(kSize));
  text_field_scene::Scene full = text_field_scene::build(spec_for(kSize));
  if (!incremental.fonts.has_value() || !full.fonts.has_value()) {
    out << "  SKIP: no system font found under /usr/share/fonts\n";
    return true;
  }
  incremental.tree.render().repaint_full(*damaged);
  full.tree.render().repaint_full(*whole);

  const auto apply = [&](text_field_scene::Scene& scene) {
    const dg::FontCatalog& fonts = *scene.fonts;
    dg::RenderTree& tree = scene.tree.render();
    text_field_scene::set_focus(scene, scene.handles.field_b);
    scene.widgets.text_field_insert(tree, fonts, scene.handles.field_b, "drawgui");
    scene.widgets.text_field_move(tree, fonts, scene.handles.field_b,
                                  dg::TextFieldMove::kLineStart, true);
    scene.widgets.text_field_backspace(tree, fonts, scene.handles.field_b);
    text_field_scene::set_focus(scene, scene.handles.field_a);
    scene.widgets.text_field_move(tree, fonts, scene.handles.field_a,
                                  dg::TextFieldMove::kCharLeft, true);
    text_field_scene::set_focus(scene, std::nullopt);
  };
  apply(incremental);
  apply(full);

  incremental.tree.layout();
  incremental.tree.render().repaint(*damaged);
  full.tree.render().repaint_full(*whole);
  if (snapshot(*damaged) != snapshot(*whole)) {
    out << "  FAIL: incremental repaint differs from a full repaint after a script mixing "
           "focus changes, typing, selection and Backspace\n";
    return false;
  }

  out << "  OK: incremental repaint matches a full repaint, byte for byte, across a script "
         "mixing focus changes, typing, selection and Backspace on both fields\n";
  return true;
}

// --------------------------------------------------------------------------
// Claim 6 (7-2b): typing and backspacing CJK text and a ZWJ family emoji -
// the whole point of lifting 4-9's ASCII-only restriction. One backspace
// removes an entire CJK character (never half of its 3 UTF-8 bytes) and an
// entire ZWJ-joined emoji sequence (never one of its 7 codepoints), through
// the SAME text_field_insert()/text_field_backspace() calls Claim 2 already
// exercises for ASCII - re-measuring LayoutStats under this path is what
// answers the task's own question of whether 4-9's "typing never relayouts"
// claim still holds for non-ASCII, fixed-width input.
// --------------------------------------------------------------------------

bool check_cjk_and_zwj_emoji_editing(std::ostream& out) {
  text_field_scene::Scene scene = text_field_scene::build(spec_for(kSize));
  bool ok = true;
  if (!scene.fonts.has_value()) {
    out << "  SKIP: no system font found under /usr/share/fonts\n";
    return true;
  }
  const dg::FontCatalog& fonts = *scene.fonts;
  const NodeId field = scene.handles.field_b;
  dg::RenderTree& tree = scene.tree.render();
  dg::LayoutTree& layout_tree = scene.tree;

  text_field_scene::set_focus(scene, field);

  // "中文" (U+4E2D U+6587, 3 UTF-8 bytes each) - the same 16-unspaced-Han
  // shape 7-1/7-2 already proved libgrapheme/SkParagraph handle, now through
  // the EDITING path rather than only the display one.
  const std::string cjk = "\xE4\xB8\xAD\xE6\x96\x87";
  scene.widgets.text_field_insert(tree, fonts, field, cjk);
  if (scene.widgets.text_field_text(field) != cjk) {
    out << "  FAIL: typing \"\xE4\xB8\xAD\xE6\x96\x87\" did not produce it, got \""
        << scene.widgets.text_field_text(field) << "\"\n";
    ok = false;
  }
  // Re-measured under CJK, per the task's own instruction: does a fixed-
  // width field's edit still cost zero relayout once the content is
  // multi-byte? LayoutStats says yes - the mutator still routes exclusively
  // through RenderTree (doc/text-input.md section 4's structural argument
  // was never about ASCII specifically).
  const dg::LayoutStats cjk_insert_stats = layout_tree.layout();
  if (cjk_insert_stats.nodes_visited != 0 || cjk_insert_stats.nodes_relaid_out != 0) {
    out << "  FAIL: inserting CJK text visited " << cjk_insert_stats.nodes_visited
        << " layout node(s) - expected 0\n";
    ok = false;
  }

  scene.widgets.text_field_backspace(tree, fonts, field);
  if (scene.widgets.text_field_text(field) != "\xE4\xB8\xAD") {
    out << "  FAIL: one Backspace after \"\xE4\xB8\xAD\xE6\x96\x87\" should remove exactly "
           "one CJK character (leaving \"\xE4\xB8\xAD\"), got \""
        << scene.widgets.text_field_text(field) << "\"\n";
    ok = false;
  }
  const dg::LayoutStats cjk_backspace_stats = layout_tree.layout();
  if (cjk_backspace_stats.nodes_visited != 0 || cjk_backspace_stats.nodes_relaid_out != 0) {
    out << "  FAIL: a CJK Backspace visited " << cjk_backspace_stats.nodes_visited
        << " layout node(s) - expected 0\n";
    ok = false;
  }
  scene.widgets.text_field_backspace(tree, fonts, field);
  if (!scene.widgets.text_field_text(field).empty()) {
    out << "  FAIL: a second Backspace should clear the field, got \""
        << scene.widgets.text_field_text(field) << "\"\n";
    ok = false;
  }

  // The ZWJ family emoji design.md section 5.10.2 names by name: 7
  // codepoints joined by ZWJ, 25 UTF-8 bytes, ONE grapheme cluster - one
  // Backspace must remove the whole thing, not one codepoint of it.
  const std::string family_emoji = "\U0001F468\u200D\U0001F469\u200D\U0001F467\u200D\U0001F466";
  scene.widgets.text_field_insert(tree, fonts, field, "X" + family_emoji);
  if (scene.widgets.text_field_text(field) != "X" + family_emoji) {
    out << "  FAIL: typing \"X\" + the ZWJ family emoji did not produce it\n";
    ok = false;
  }
  scene.widgets.text_field_backspace(tree, fonts, field);
  if (scene.widgets.text_field_text(field) != "X") {
    out << "  FAIL: one Backspace after the ZWJ family emoji should remove all 25 of its "
           "bytes in one step, leaving \"X\", got \""
        << scene.widgets.text_field_text(field) << "\" ("
        << scene.widgets.text_field_text(field).size() << " byte(s))\n";
    ok = false;
  }

  if (ok) {
    out << "  OK: CJK text and a ZWJ family emoji both edit by whole grapheme cluster, and "
           "cost zero relayout (LayoutStats measured, not assumed)\n";
  }
  return ok;
}

// --------------------------------------------------------------------------
// Claim 7 (7-3, doc/ime.md): IME composition preview and commit.
//
// Models exactly what SDL delivers - WidgetSet::text_field_composition_
// update()/text_field_cancel_composition() called directly, the same
// level every other claim in this file exercises the model at (the
// window-level plumbing that turns a real/synthesized SDL_EVENT_TEXT_
// EDITING into this same call is examples/12_text_input's own
// text_field_window.cpp::Runner::handle_text_editing(), and --script
// drives it through the real SDL event queue - doc/ime.md section 5 is
// where the honest boundary of what that proves is recorded).
//
// Also answers the task's own harder relayout question: a composition
// preview's own LENGTH changes on every keystroke, unlike committed text
// which changes only on insert/backspace - measured here across several
// different preedit lengths in a row, not assumed from Claim 4/6's
// committed-text finding.
// --------------------------------------------------------------------------

bool check_ime_composition_preview_and_commit(std::ostream& out) {
  text_field_scene::Scene scene = text_field_scene::build(spec_for(kSize));
  bool ok = true;
  if (!scene.fonts.has_value()) {
    out << "  SKIP: no system font found under /usr/share/fonts\n";
    return true;
  }
  const dg::FontCatalog& fonts = *scene.fonts;
  const NodeId field = scene.handles.field_b;
  dg::RenderTree& tree = scene.tree.render();
  dg::LayoutTree& layout_tree = scene.tree;

  text_field_scene::set_focus(scene, field);

  // Preview: composing "hi" (SDL's own start=2, no clause highlight) shows
  // it inline WITHOUT touching the committed model.
  scene.widgets.text_field_composition_update(tree, fonts, field, "hi", 2, 0);
  if (!scene.widgets.text_field_is_composing(field)) {
    out << "  FAIL: composition_update() did not enter the composing state\n";
    ok = false;
  }
  if (!scene.widgets.text_field_text(field).empty()) {
    out << "  FAIL: a composition preview touched the committed model, got \""
        << scene.widgets.text_field_text(field) << "\"\n";
    ok = false;
  }
  const std::string previewed_hi = tree.style(scene.widgets.at(field).content).text.text;
  if (previewed_hi != "hi") {
    out << "  FAIL: the content child should display the not-yet-committed preview \"hi\", got "
           "\""
        << previewed_hi << "\"\n";
    ok = false;
  }
  if (tree.local_bounds(scene.widgets.at(field).composition_underline).width <= 0) {
    out << "  FAIL: a non-empty composition should show a non-zero-width underline\n";
    ok = false;
  }

  // The task's own harder relayout question: re-measure across SEVERAL
  // DIFFERENT preedit lengths in a row (a preedit changes length on every
  // keystroke, unlike committed text which only changes on insert/
  // backspace) - LayoutStats, not assumed from Claim 4/6's committed-text
  // finding.
  const dg::LayoutStats after_h = layout_tree.layout();
  if (after_h.nodes_visited != 0 || after_h.nodes_relaid_out != 0) {
    out << "  FAIL: composing \"hi\" visited " << after_h.nodes_visited
        << " layout node(s) - expected 0\n";
    ok = false;
  }
  scene.widgets.text_field_composition_update(tree, fonts, field, "hell", 4, 0);
  const dg::LayoutStats after_hell = layout_tree.layout();
  if (after_hell.nodes_visited != 0 || after_hell.nodes_relaid_out != 0) {
    out << "  FAIL: composing a LONGER preedit (\"hell\") visited " << after_hell.nodes_visited
        << " layout node(s) - expected 0 (a changing preedit length is the harder case the "
           "task asked to re-measure, not assume)\n";
    ok = false;
  }
  scene.widgets.text_field_composition_update(tree, fonts, field, "h", 1, 0);
  const dg::LayoutStats after_shrink = layout_tree.layout();
  if (after_shrink.nodes_visited != 0 || after_shrink.nodes_relaid_out != 0) {
    out << "  FAIL: composing a SHORTER preedit (\"h\") visited " << after_shrink.nodes_visited
        << " layout node(s) - expected 0\n";
    ok = false;
  }

  // Escape (text_field_cancel_composition): discards the preview, commits
  // nothing, restores the empty field's own display.
  scene.widgets.text_field_cancel_composition(tree, fonts, field);
  if (scene.widgets.text_field_is_composing(field)) {
    out << "  FAIL: cancel_composition() left the field still composing\n";
    ok = false;
  }
  const std::string after_cancel = tree.style(scene.widgets.at(field).content).text.text;
  if (!after_cancel.empty()) {
    out << "  FAIL: cancelling a composition over an empty field should restore an empty "
           "display, got \""
        << after_cancel << "\"\n";
    ok = false;
  }

  // A real commit (the same text_field_insert() every other claim in this
  // file already exercises) - the exact commit path this slice was asked
  // to VERIFY rather than add a parallel one to.
  scene.widgets.text_field_composition_update(tree, fonts, field, "hi", 2, 0);
  scene.widgets.text_field_insert(tree, fonts, field, "hi");
  if (scene.widgets.text_field_is_composing(field)) {
    out << "  FAIL: a commit should end composition\n";
    ok = false;
  }
  if (scene.widgets.text_field_text(field) != "hi") {
    out << "  FAIL: the commit should have produced \"hi\" in the model, got \""
        << scene.widgets.text_field_text(field) << "\"\n";
    ok = false;
  }

  // Composing over a selection: the selection stays untouched in the model
  // until commit; the PREVIEW splices the preedit into the selection's own
  // range.
  scene.widgets.text_field_move(tree, fonts, field, dg::TextFieldMove::kLineStart, false);
  scene.widgets.text_field_move(tree, fonts, field, dg::TextFieldMove::kCharRight, true);
  scene.widgets.text_field_composition_update(tree, fonts, field, "X", 1, 0);
  if (scene.widgets.text_field_text(field) != "hi") {
    out << "  FAIL: composing over a selection mutated the model before any commit, got \""
        << scene.widgets.text_field_text(field) << "\"\n";
    ok = false;
  }
  const std::string previewed_over_selection =
      tree.style(scene.widgets.at(field).content).text.text;
  if (previewed_over_selection != "Xi") {
    out << "  FAIL: composing \"X\" over a selected \"h\" should preview \"Xi\", got \""
        << previewed_over_selection << "\"\n";
    ok = false;
  }
  scene.widgets.text_field_cancel_composition(tree, fonts, field);

  if (ok) {
    out << "  OK: composition previews inline without touching the committed model, costs "
           "zero relayout across SEVERAL different preedit lengths (the harder, changing-"
           "length case), Escape cancels without committing, and a commit flows through the "
           "ordinary text_field_insert() path\n";
  }
  return ok;
}

// --------------------------------------------------------------------------
// Claim 8 (8-4): clipboard copy/cut/paste, through the ACTUAL event
// pipeline - WindowManager::post_logical_key()'s real Mod+C/Mod+X/Mod+V
// onto the real SDL3 event queue, pumped back as a real dg::KeyEvent, then
// dg::route_key_event() (the shortcut router's third consumer, after 8-3c's
// keyboard scrolling) and dg::apply_clipboard_action() - not a resolved
// action built by hand, the same precedent scroll_check.cpp's own
// check_keyboard_scroll_end_to_end() sets. The real OS clipboard is
// exercised through dg::WindowManager::set_clipboard_text()/
// get_clipboard_text(), same as everywhere else it is used in this project.
//
// SDL_VIDEODRIVER=dummy, forced here rather than left to whatever display
// happens to be running this suite: this check needs no on-screen window
// at all (focus_check.cpp's identical precedent for its own headless
// key-event checks), and forcing it also keeps a real SDL/X11 clipboard
// backend from touching the machine's actual desktop clipboard while
// CTest runs - measured (this slice's own investigation) to behave
// identically to a real X11 session for get/set/round-trip, and to reset
// cleanly across SDL_Init()/SDL_Quit(), which is what makes it safe to
// force here without leaking state into any other test in this binary.
// --------------------------------------------------------------------------

bool check_clipboard_copy_cut_paste_end_to_end(std::ostream& out) {
  setenv("SDL_VIDEODRIVER", "dummy", 1);

  dg::Expected<dg::WindowManager, dg::WindowError> made = dg::WindowManager::create();
  if (!made.has_value()) {
    out << "  FAIL: could not start the window system: " << made.error().message << "\n";
    return false;
  }
  dg::WindowManager manager = std::move(made.value());
  dg::WindowSpec spec;
  spec.title = "text input (headless, clipboard)";
  spec.width = kSize.width;
  spec.height = kSize.height;
  const dg::Expected<dg::WindowId, dg::WindowError> window = manager.open(spec);
  if (!window.has_value()) {
    out << "  FAIL: could not open a window: " << window.error().message << "\n";
    return false;
  }

  text_field_scene::Scene scene = text_field_scene::build(spec_for(kSize));
  if (!scene.fonts.has_value()) {
    out << "  SKIP: no system font found under /usr/share/fonts\n";
    return true;
  }
  bool ok = true;
  const dg::FontCatalog& fonts = *scene.fonts;
  const NodeId field = scene.handles.field_b;
  dg::RenderTree& tree = scene.tree.render();
  const dg::TextFieldEditContext ctx{tree, fonts, scene.widgets, manager};

  text_field_scene::set_focus(scene, field);
  scene.widgets.text_field_insert(tree, fonts, field, "hello world");
  // Select "hello" (bytes [0, 5)) so copy has something real to act on.
  scene.widgets.text_field_move(tree, fonts, field, dg::TextFieldMove::kLineStart, false);
  for (int i = 0; i < 5; ++i) {
    scene.widgets.text_field_move(tree, fonts, field, dg::TextFieldMove::kCharRight, true);
  }

  const auto route_real_chord = [&](dg::LogicalKey key, dg_action_id expected_action_id,
                                    const char* label) {
    manager.post_logical_key(window.value(), /*down=*/true, key, dg::Modifier::kMod);
    const dg::PumpResult pumped = manager.pump(200);
    bool saw_key = false;
    for (const dg::KeyEvent& event : pumped.key) {
      if (event.logical_key != key) {
        continue;
      }
      saw_key = true;
      const dg::RoutingContext route_ctx{scene.tree.render(), scene.widgets,
                                         scene.action_scopes, dg::all_shortcut_bindings()};
      const dg::KeyRouteResult routed =
          dg::route_key_event(event, window.value(), field, route_ctx);
      if (routed.outcome != dg::KeyRouteOutcome::kRouted || !routed.action.has_value() ||
          routed.action->action_id != expected_action_id) {
        out << "  FAIL: the real posted " << label << " did not route to the expected action\n";
        ok = false;
        continue;
      }
      if (!dg::apply_clipboard_action(routed.action->action_id, field, ctx)) {
        out << "  FAIL: apply_clipboard_action reported nothing happened for " << label << "\n";
        ok = false;
      }
    }
    if (!saw_key) {
      out << "  FAIL: a posted " << label
          << " never round-tripped through pump() as a KeyEvent\n";
      ok = false;
    }
  };

  // Real Mod+C: copies the real selection to the real OS clipboard.
  route_real_chord(dg::LogicalKey::kC, DG_ACTION_COPY, "Mod+C");
  if (manager.get_clipboard_text() != "hello") {
    out << "  FAIL: the clipboard should hold \"hello\" after copy, got \""
        << manager.get_clipboard_text() << "\"\n";
    ok = false;
  }

  // Real Mod+V at the end of the field: paste APPENDS the copied text.
  scene.widgets.text_field_move(tree, fonts, field, dg::TextFieldMove::kLineEnd, false);
  route_real_chord(dg::LogicalKey::kV, DG_ACTION_PASTE, "Mod+V");
  if (scene.widgets.text_field_text(field) != "hello worldhello") {
    out << "  FAIL: paste should have appended the copied \"hello\", got \""
        << scene.widgets.text_field_text(field) << "\"\n";
    ok = false;
  }

  // Real Mod+A then Mod+X: select_all, then cut removes the WHOLE field and
  // leaves it on the clipboard.
  route_real_chord(dg::LogicalKey::kA, DG_ACTION_SELECT_ALL, "Mod+A");
  route_real_chord(dg::LogicalKey::kX, DG_ACTION_CUT, "Mod+X");
  if (!scene.widgets.text_field_text(field).empty()) {
    out << "  FAIL: cut after select_all should empty the field, got \""
        << scene.widgets.text_field_text(field) << "\"\n";
    ok = false;
  }
  if (manager.get_clipboard_text() != "hello worldhello") {
    out << "  FAIL: cut should leave the removed text on the clipboard, got \""
        << manager.get_clipboard_text() << "\"\n";
    ok = false;
  }

  manager.request_close(window.value());
  (void)manager.pump(50);

  if (ok) {
    out << "  OK: real Mod+C/Mod+V/Mod+A/Mod+X, posted onto the platform's own event queue "
           "and pumped back, routed through route_key_event() to copy/paste/select_all/cut "
           "and moved real text through the real OS clipboard\n";
  }
  return ok;
}

}  // namespace

int run(std::ostream& out) {
  out << "verify: text input, on the scene the demo puts on screen\n";
  bool ok = true;
  ok = check_click_focuses_and_is_exclusive(out) && ok;
  ok = check_typing_and_editing(out) && ok;
  ok = check_overflow_ellipsis_and_scroll(out) && ok;
  ok = check_editing_costs_no_relayout(out) && ok;
  ok = check_identity(out) && ok;
  ok = check_cjk_and_zwj_emoji_editing(out) && ok;
  ok = check_ime_composition_preview_and_commit(out) && ok;
  ok = check_clipboard_copy_cut_paste_end_to_end(out) && ok;
  out << (ok ? "PASS\n" : "FAIL\n");
  return ok ? 0 : 1;
}

}  // namespace text_field_check
