#include "text_field_check.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/render_tree.h"

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

}  // namespace

int run(std::ostream& out) {
  out << "verify: text input, on the scene the demo puts on screen\n";
  bool ok = true;
  ok = check_click_focuses_and_is_exclusive(out) && ok;
  ok = check_typing_and_editing(out) && ok;
  ok = check_overflow_ellipsis_and_scroll(out) && ok;
  ok = check_editing_costs_no_relayout(out) && ok;
  ok = check_identity(out) && ok;
  out << (ok ? "PASS\n" : "FAIL\n");
  return ok ? 0 : 1;
}

}  // namespace text_field_check
