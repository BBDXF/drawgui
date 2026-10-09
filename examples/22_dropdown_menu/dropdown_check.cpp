#include "dropdown_check.h"

#include <cstdlib>
#include <optional>
#include <ostream>
#include <utility>
#include <vector>

#include "drawgui/anim/animation_engine.h"
#include "drawgui/base/expected.h"
#include "drawgui/render/node_lifecycle.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/shortcuts/action_scopes.h"
#include "drawgui/theme/theme_bindings.h"
#include "drawgui/ui/dropdown.h"
#include "drawgui/widget/focus.h"
#include "drawgui/widget/widget_set.h"
#include "drawgui/window/popup_host.h"
#include "drawgui/window/window_manager.h"

#include "dropdown_scene.h"
#include "dropdown_window.h"

namespace dropdown_check {
namespace {

using dg::NodeId;
using dg::PixelRect;
using dg::PixelSize;
using dg::PopupHost;

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

  // Runner's own first-frame viewport must be the window's real
  // drawable_size() (device pixels, HiDPI-scaled where applicable), never
  // Settings::size (a headless-only "what if there is no window at all"
  // fallback) - the regression this slice fixes. dummy_driver answers
  // drawable_size() with exactly the size passed to open() above, so this
  // assertion is equally true whether initial_viewport() is implemented
  // correctly or simply returns settings.size; what it DOES pin is the
  // real function Runner's constructor calls, not a duplicate - a future
  // change that makes Runner stop calling initial_viewport() (reverting to
  // settings.size directly, inline) changes the code this test observes.
  dropdown_window::Settings runner_settings;
  runner_settings.size = kSize;
  const dg::PixelSize drawable =
      manager.drawable_size(window.value()).value_or(dg::PixelSize{});
  check(dropdown_window::initial_viewport(manager, window.value(), runner_settings) == drawable,
        "Runner's initial viewport is manager.drawable_size(), not Settings::size", out, ok);

  dropdown_scene::Scene scene = dropdown_scene::build(options_for(kSize));
  if (!scene.fonts.has_value()) {
    out << "  FAIL: no system fonts found\n";
    ok = false;
    return;
  }
  const dg::FontCatalog& fonts = *scene.fonts;
  PopupHost host{manager};
  dg::ThemeBindings theme_bindings;
  dg::AnimationEngine animation;
  dg::ActionScopes action_scopes;
  dg::UiContext ctx{
      host,
      window.value(),
      scene.tree.render(),
      {scene.widgets, theme_bindings, scene.focus, animation, action_scopes, scene.interaction},
      &fonts,
      scene.ui_font,
      dg::PopupStyle{}};
  dg::Dropdown dropdown{scene.handles.dropdown};
  const std::size_t live_baseline =
      dg::subtree_of(scene.tree.render(), dg::RenderTree::root()).size();

  auto key = [&](dg::Key k) {
    dg::KeyEvent event;
    event.window = window.value();
    event.key = k;
    return dropdown.handle_key(ctx, event);
  };
  auto row = [&](int index) {
    return dropdown.menu().item_nodes()[static_cast<std::size_t>(index)];
  };
  auto selected = [&]() {
    return scene.widgets.dropdown_selected_index(scene.handles.dropdown);
  };

  // Opening is dg::Dropdown's own trigger: Down while the anchor is focused.
  scene.focus.set(scene.handles.dropdown);

  // --- the FIRST option (index 0, "Apple"): opened with no prior
  // selection, the highlight seeds at 0 with no keyboard movement at all.
  check(key(dg::Key::kDown).event == dg::DropdownEvent::kOpened,
        "Down on the focused anchor opens the option list", out, ok);
  check(scene.focus.current() == row(0),
        "opening with no prior selection seeds the highlight at option 0 (\"Apple\")", out, ok);
  const dg::DropdownResult first = key(dg::Key::kEnter);
  check(first.event == dg::DropdownEvent::kChanged && selected() == 0,
        "Enter at the FIRST option (\"Apple\", index 0) commits it", out, ok);
  check(scene.focus.current() == scene.handles.dropdown, "closing returns focus to the anchor",
        out, ok);

  // --- a MIDDLE option (index 2, "Cherry"): Down x2 from the reseeded
  // highlight (now at the just-committed index 0).
  key(dg::Key::kEnter);
  check(scene.focus.current() == row(0),
        "reopening seeds the highlight at the just-committed selection (index 0)", out, ok);
  key(dg::Key::kDown);
  key(dg::Key::kDown);
  check(scene.focus.current() == row(2), "Down x2 from index 0 highlights index 2 (\"Cherry\")",
        out, ok);
  const dg::DropdownResult middle = key(dg::Key::kEnter);
  check(middle.event == dg::DropdownEvent::kChanged && middle.previous == 0 && selected() == 2,
        "Enter at a MIDDLE option (\"Cherry\", index 2) commits it and reports the previous "
        "index",
        out, ok);

  // --- the LAST option (index 4), plus wraparound in both directions over
  // the identical dg::Focus traversal Tab already uses (7-4).
  key(dg::Key::kDown);
  check(scene.focus.current() == row(2), "reopening seeds the highlight at index 2 again", out,
        ok);
  key(dg::Key::kDown);
  key(dg::Key::kDown);
  check(scene.focus.current() == row(4), "Down x2 from index 2 highlights index 4 (last)", out,
        ok);
  key(dg::Key::kDown);
  check(scene.focus.current() == row(0), "Down from the LAST option wraps FORWARD to index 0",
        out, ok);
  key(dg::Key::kUp);
  check(scene.focus.current() == row(4),
        "Up from the FIRST option wraps BACKWARD to the last option", out, ok);
  key(dg::Key::kEnter);
  check(selected() == 4, "Enter at the LAST option (\"Elderberry\", index 4) commits it", out,
        ok);

  // --- closing WITHOUT committing (Escape) leaves the selection untouched.
  key(dg::Key::kDown);
  key(dg::Key::kDown);
  check(scene.focus.current() == row(0),
        "Down from index 4 wraps to index 0 (highlight only, not yet committed)", out, ok);
  const dg::DropdownResult escaped = key(dg::Key::kEscape);
  check(escaped.event == dg::DropdownEvent::kClosed && selected() == 4,
        "closing WITHOUT Enter (Escape) leaves the prior selection (index 4) untouched", out,
        ok);

  // --- a click outside closes without committing too.
  key(dg::Key::kDown);
  dg::PointerEvent outside;
  outside.window = window.value();
  outside.action = dg::PointerAction::kDown;
  outside.x = kSize.width - 2;
  outside.y = 2;
  const dg::DropdownResult dismissed = dropdown.handle_pointer(ctx, outside);
  check(dismissed.event == dg::DropdownEvent::kClosed && selected() == 4 && !dropdown.is_open(),
        "a click outside closes the list without changing the selection", out, ok);

  // --- a direct click on a specific row commits THAT row, regardless of
  // which one is currently highlighted.
  key(dg::Key::kDown);
  check(scene.focus.current() == row(4), "reopening seeds the highlight at index 4 again", out,
        ok);
  const PixelRect banana = scene.tree.render().absolute_bounds(row(1));
  dg::PointerEvent click = outside;
  click.x = banana.x + (banana.width / 2);
  click.y = banana.y + (banana.height / 2);
  dropdown.handle_pointer(ctx, click);
  check(selected() == 1,
        "a click on row 1 (\"Banana\") commits it even though index 4 was highlighted", out,
        ok);
  check(dg::subtree_of(scene.tree.render(), dg::RenderTree::root()).size() == live_baseline,
        "after every close the live node count is back at its baseline (PopupHost::destroy())",
        out, ok);

  // --- the measured relayout cost: every operation above went through
  // RenderTree alone (dropdown_select()'s tree.set_text(), the popup rows'
  // own direct RenderTree::add_child() calls) -
  // LayoutTree::layout() should find nothing dirty.
  const dg::LayoutStats stats = scene.tree.layout();
  out << "  LayoutStats after opening/closing the dropdown seven times and five selection "
         "changes: "
         "nodes_visited="
      << stats.nodes_visited << " nodes_relaid_out=" << stats.nodes_relaid_out << "\n";
  check(stats.nodes_visited == 0 && stats.nodes_relaid_out == 0,
        "opening/closing the dropdown and changing its selection cost a repaint, never a "
        "relayout",
        out, ok);
}

// --------------------------------------------------------------------------
// Claim 3: the selected option is marked in the open list - dg::Dropdown
// opens its Menu with a check mark on the current selection only, and the
// highlight starts there.
// --------------------------------------------------------------------------

void check_selected_row_marked(std::ostream& out, bool& ok) {
  out << "\nselected row: the open list checks the current selection and no other option\n";
  dg::Expected<dg::WindowManager, dg::WindowError> made = dg::WindowManager::create();
  if (!made) {
    out << "  FAIL: could not start the window system\n";
    ok = false;
    return;
  }
  dg::WindowManager manager = std::move(made.value());
  dg::WindowSpec spec;
  spec.width = kSize.width;
  spec.height = kSize.height;
  const dg::Expected<dg::WindowId, dg::WindowError> window = manager.open(spec);
  dropdown_scene::Scene scene = dropdown_scene::build(options_for(kSize));
  if (!window || !scene.fonts.has_value()) {
    out << "  FAIL: no window or no fonts\n";
    ok = false;
    return;
  }
  constexpr int kSelected = 2;
  scene.widgets.dropdown_select(scene.tree.render(), *scene.fonts, scene.handles.dropdown,
                                kSelected);
  PopupHost host{manager};
  dg::ThemeBindings theme_bindings;
  dg::AnimationEngine animation;
  dg::ActionScopes action_scopes;
  dg::UiContext ctx{
      host,
      window.value(),
      scene.tree.render(),
      {scene.widgets, theme_bindings, scene.focus, animation, action_scopes, scene.interaction},
      &*scene.fonts,
      scene.ui_font,
      dg::PopupStyle{}};
  dg::Dropdown dropdown{scene.handles.dropdown};
  dropdown.open(ctx);
  const std::vector<dg::MenuItem>& items = dropdown.menu().spec().items;
  bool only_selected = items.size() == dropdown_scene::kOptions.size();
  for (std::size_t i = 0; i < items.size(); ++i) {
    only_selected = only_selected && items[i].checked == (static_cast<int>(i) == kSelected);
  }
  check(only_selected, "exactly the selected option (index 2) carries the check mark", out, ok);
  check(dropdown.menu().highlighted(ctx) == kSelected, "and the highlight opens on it", out,
        ok);
  dropdown.close(ctx);
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
  check_selected_row_marked(out, ok);
  out << "\n" << (ok ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED") << "\n";
  return ok ? 0 : 1;
}

}  // namespace dropdown_check
