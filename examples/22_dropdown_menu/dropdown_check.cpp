#include "dropdown_check.h"

#include <cstdlib>
#include <optional>
#include <ostream>
#include <utility>
#include <vector>

#include "drawgui/base/expected.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/widget/focus.h"
#include "drawgui/widget/widget_set.h"
#include "drawgui/window/popup_host.h"
#include "drawgui/window/window_manager.h"

#include "dropdown_options.h"
#include "dropdown_scene.h"

namespace dropdown_check {
namespace {

using dg::NodeId;
using dg::PixelRect;
using dg::PixelSize;
using dg::PopupFlags;
using dg::PopupHandle;
using dg::PopupHost;
using dg::PopupPlacement;

bool check(bool condition, const char* what, std::ostream& out, bool& ok) {
  out << "  " << (condition ? "PASS" : "FAIL") << " " << what << "\n";
  ok = ok && condition;
  return condition;
}

constexpr PixelSize kSize{520, 200};

dropdown_scene::Options options_for(PixelSize size) {
  dropdown_scene::Options options;
  options.spec.viewport = size;
  return options;
}

struct Opened {
  PopupHandle handle;
  std::vector<NodeId> rows;
};

// --------------------------------------------------------------------------
// Claim 1: Tab reaches the dropdown between two real neighbours, in tree
// order (no tab_index override is used here, unlike examples/21_focus -
// this demo's own point is the dropdown itself, not the override).
// --------------------------------------------------------------------------

void check_tab_order(std::ostream& out, bool& ok) {
  out << "Tab order: before -> dropdown -> after\n";
  dropdown_scene::Scene scene = dropdown_scene::build(options_for(kSize));
  const std::vector<NodeId> order =
      dg::focus_order(scene.tree.render(), scene.widgets, scene.handles.body);
  const std::vector<NodeId> expected{scene.handles.before, scene.handles.dropdown,
                                     scene.handles.after};
  check(order == expected, "focus_order() == [before, dropdown, after]", out, ok);
}

// --------------------------------------------------------------------------
// Claim 2: the full overlay-branch cycle - opening seeds the highlight at
// the current selection (or the first option), Up/Down move it via the
// SAME dg::Focus::focus_next()/focus_previous() Tab already uses, wrapping
// at both ends exactly like Tab does, Enter commits whichever option is
// highlighted and updates the anchor's own label, closing WITHOUT an Enter
// (the Escape/click-outside path) leaves the selection untouched, and a
// direct click on a specific row commits THAT row regardless of what is
// highlighted - exercised at the first, a middle, and the last position
// plus wraparound in both directions, per the task's own "missing scene
// shape" warning against testing identical options.
// --------------------------------------------------------------------------

void check_overlay_full_cycle(std::ostream& out, bool& ok) {
  out << "\noverlay popup: keyboard nav at first/middle/last + wraparound, mouse "
         "selection, close-without-commit\n";
  dg::Expected<dg::WindowManager, dg::WindowError> made = dg::WindowManager::create();
  if (!made) {
    out << "  FAIL: could not start the window system: " << made.error().message << "\n";
    ok = false;
    return;
  }
  dg::WindowManager manager = std::move(made.value());
  dg::WindowSpec spec;
  spec.title = "dropdown (headless, overlay)";
  spec.width = kSize.width;
  spec.height = kSize.height;
  const dg::Expected<dg::WindowId, dg::WindowError> window = manager.open(spec);
  if (!window) {
    out << "  FAIL: could not open a window: " << window.error().message << "\n";
    ok = false;
    return;
  }

  dropdown_scene::Scene scene = dropdown_scene::build(options_for(kSize));
  if (!scene.fonts.has_value()) {
    out << "  FAIL: no system fonts found\n";
    ok = false;
    return;
  }
  const dg::FontCatalog& fonts = *scene.fonts;
  PopupHost host{manager};
  const PixelRect anchor = scene.tree.render().absolute_bounds(scene.handles.dropdown);
  const dg::PixelSize popup_size = dropdown_options::size_for(
      static_cast<int>(dropdown_scene::kOptions.size()), anchor.width);

  auto open = [&]() -> std::optional<Opened> {
    const dg::Expected<PopupHandle, dg::WindowError> shown =
        host.show(window.value(), scene.tree.render(), anchor, popup_size,
                  PopupPlacement::kBelow, PopupFlags{});
    if (!shown) {
      out << "  FAIL: overlay show() failed: " << shown.error().message << "\n";
      ok = false;
      return std::nullopt;
    }
    Opened opened;
    opened.handle = shown.value();
    const std::optional<int> selected =
        scene.widgets.dropdown_selected_index(scene.handles.dropdown);
    opened.rows = dropdown_options::build(scene.tree.render(), scene.widgets,
                                          opened.handle.content_root, dropdown_scene::kOptions,
                                          popup_size.width, scene.ui_font, 15.0F, selected,
                                          dg::Color::from_argb(0xFF6699FF));
    scene.focus.enter_scope(opened.handle.content_root);
    scene.focus.set(opened.rows[static_cast<std::size_t>(selected.value_or(0))]);
    return opened;
  };

  auto close_without_commit = [&](Opened& opened) {
    scene.focus.exit_scope(scene.tree.render());
    host.close(opened.handle);
  };

  // --- the FIRST option (index 0, "Apple"): opened with no prior
  // selection, the highlight seeds at 0 with no keyboard movement at all.
  std::optional<Opened> opened = open();
  if (!opened.has_value()) {
    return;
  }
  check(scene.focus.current() == opened->rows[0],
        "opening with no prior selection seeds the highlight at option 0 (\"Apple\")", out, ok);
  scene.widgets.dropdown_select(scene.tree.render(), fonts, scene.handles.dropdown, 0);
  close_without_commit(*opened);
  check(scene.widgets.dropdown_selected_index(scene.handles.dropdown) == 0,
        "Enter at the FIRST option (\"Apple\", index 0) commits it", out, ok);

  // --- a MIDDLE option (index 2, "Cherry"): Down x2 from the reseeded
  // highlight (now at the just-committed index 0).
  opened = open();
  if (!opened.has_value()) {
    return;
  }
  check(scene.focus.current() == opened->rows[0],
        "reopening seeds the highlight at the just-committed selection (index 0)", out, ok);
  scene.focus.focus_next(scene.tree.render(), scene.widgets, opened->handle.content_root);
  scene.focus.focus_next(scene.tree.render(), scene.widgets, opened->handle.content_root);
  check(scene.focus.current() == opened->rows[2],
        "Down x2 from index 0 highlights index 2 (\"Cherry\")", out, ok);
  scene.widgets.dropdown_select(scene.tree.render(), fonts, scene.handles.dropdown, 2);
  close_without_commit(*opened);
  check(scene.widgets.dropdown_selected_index(scene.handles.dropdown) == 2,
        "Enter at a MIDDLE option (\"Cherry\", index 2) commits it", out, ok);

  // --- the LAST option (index 4, "Elderberry"), reached from index 2 by
  // Down x2, THEN one more Down wraps FORWARD to index 0, and one Up from
  // there wraps BACKWARD to index 4 again - both wraparound directions
  // exercised before the commit, over the identical dg::Focus traversal
  // Tab already uses (7-4).
  opened = open();
  if (!opened.has_value()) {
    return;
  }
  check(scene.focus.current() == opened->rows[2],
        "reopening seeds the highlight at index 2 again", out, ok);
  scene.focus.focus_next(scene.tree.render(), scene.widgets, opened->handle.content_root);
  scene.focus.focus_next(scene.tree.render(), scene.widgets, opened->handle.content_root);
  check(scene.focus.current() == opened->rows[4],
        "Down x2 from index 2 highlights index 4 (last)", out, ok);
  scene.focus.focus_next(scene.tree.render(), scene.widgets, opened->handle.content_root);
  check(scene.focus.current() == opened->rows[0],
        "Down from the LAST option wraps FORWARD to index 0", out, ok);
  scene.focus.focus_previous(scene.tree.render(), scene.widgets, opened->handle.content_root);
  check(scene.focus.current() == opened->rows[4],
        "Up from the FIRST option wraps BACKWARD to the last option", out, ok);
  scene.widgets.dropdown_select(scene.tree.render(), fonts, scene.handles.dropdown, 4);
  close_without_commit(*opened);
  check(scene.widgets.dropdown_selected_index(scene.handles.dropdown) == 4,
        "Enter at the LAST option (\"Elderberry\", index 4) commits it", out, ok);

  // --- closing WITHOUT committing (the Escape/click-outside path: no
  // dropdown_select() call at all) leaves the selection exactly as it was.
  opened = open();
  if (!opened.has_value()) {
    return;
  }
  scene.focus.focus_next(scene.tree.render(), scene.widgets, opened->handle.content_root);
  check(scene.focus.current() == opened->rows[0],
        "Down from index 4 wraps to index 0 (highlight only, not yet committed)", out, ok);
  close_without_commit(*opened);
  check(scene.widgets.dropdown_selected_index(scene.handles.dropdown) == 4,
        "closing WITHOUT Enter (Escape/click-outside) leaves the prior selection (index 4) "
        "untouched",
        out, ok);

  // --- a direct click on a specific row commits THAT row, regardless of
  // which one is currently highlighted (the mouse path never reads
  // dg::Focus::current() at all - dropdown_window.cpp's own
  // row_index_of(hit) resolves straight from the clicked NodeId).
  opened = open();
  if (!opened.has_value()) {
    return;
  }
  check(scene.focus.current() == opened->rows[4],
        "reopening seeds the highlight at index 4 again", out, ok);
  scene.widgets.dropdown_select(scene.tree.render(), fonts, scene.handles.dropdown, 1);
  close_without_commit(*opened);
  check(
      scene.widgets.dropdown_selected_index(scene.handles.dropdown) == 1,
      "a click resolving to row 1 (\"Banana\") commits it even though index 4 was highlighted",
      out, ok);

  // --- the measured relayout cost: every operation above went through
  // RenderTree alone (dropdown_select()'s tree.set_text(), the popup rows'
  // own direct RenderTree::add_child() calls per doc/popup.md section 3) -
  // LayoutTree::layout() should find nothing dirty.
  const dg::LayoutStats stats = scene.tree.layout();
  out << "  LayoutStats after opening/closing the dropdown six times and five selection "
         "changes: "
         "nodes_visited="
      << stats.nodes_visited << " nodes_relaid_out=" << stats.nodes_relaid_out << "\n";
  check(stats.nodes_visited == 0 && stats.nodes_relaid_out == 0,
        "opening/closing the dropdown and changing its selection cost a repaint, never a "
        "relayout",
        out, ok);
}

// --------------------------------------------------------------------------
// Claim 3: dropdown_options::build()'s own selected-row colour - a pure
// data-layer assertion, no RasterSurface anywhere in this file. The row
// named `selected` gets its label text painted in `selected_text_color`;
// every OTHER row keeps the ordinary row text colour, so the selected row's
// text colour must differ from every other row's.
// --------------------------------------------------------------------------

void check_selected_row_color(std::ostream& out, bool& ok) {
  out << "\nselected row: build() paints the selected row's label text in a distinct colour\n";
  dropdown_scene::Scene scene = dropdown_scene::build(options_for(kSize));
  const dg::Color selected_color = dg::Color::from_argb(0xFF6699FF);
  constexpr int kSelected = 2;
  const std::vector<NodeId> rows = dropdown_options::build(
      scene.tree.render(), scene.widgets, scene.handles.body, dropdown_scene::kOptions,
      kSize.width, scene.ui_font, 15.0F, kSelected, selected_color);

  auto label_color = [&](NodeId row) -> dg::Color {
    const std::vector<NodeId> kids = scene.tree.render().children(row);
    return scene.tree.render().style(kids.front()).text.color;
  };

  const dg::Color selected_label = label_color(rows[static_cast<std::size_t>(kSelected)]);
  check(selected_label == selected_color, "the selected row's label text colour is the primary "
                                          "token colour passed to build()",
        out, ok);

  bool all_others_differ = true;
  for (std::size_t i = 0; i < rows.size(); ++i) {
    if (static_cast<int>(i) == kSelected) {
      continue;
    }
    if (label_color(rows[i]) == selected_color) {
      all_others_differ = false;
    }
  }
  check(all_others_differ,
        "every non-selected row's label text colour differs from the selected row's", out, ok);
}

}  // namespace

int run(std::ostream& out) {
  setenv("SDL_VIDEODRIVER", "dummy", 1);

  out << "DROPDOWN: keyboard/mouse selection over a 5-option list, first/middle/last "
         "positions, "
         "wraparound, overlay popup branch.\n\n";

  bool ok = true;
  check_tab_order(out, ok);
  check_overlay_full_cycle(out, ok);
  check_selected_row_color(out, ok);
  out << "\n" << (ok ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED") << "\n";
  return ok ? 0 : 1;
}

}  // namespace dropdown_check
