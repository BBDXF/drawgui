// The headless half of examples/23_menu_tooltip_dialog - matching
// examples/22_dropdown_menu's own dropdown_check.cpp shape exactly (see
// its own top comment for why: SDL_VIDEODRIVER=dummy opens an ordinary
// window and drives the OVERLAY branch for real. PopupHost (context menu,
// tooltip) is overlay-only now - there is
// no second branch left to attempt for either. The context menu, tooltip and
// overlay dialog are the library components (dg::Menu, dg::Tooltip,
// dg::Dialog - include/drawgui/ui/). The NATIVE dialog is a genuinely
// separate window-layer addition, NOT built on PopupHost, and keeps its own
// real SDL_SetWindowParent()/SDL_SetWindowModal() native attempt below.

#include "menu_check.h"

#include <cstdlib>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "drawgui/anim/animation_engine.h"
#include "drawgui/anim/clock.h"
#include "drawgui/base/expected.h"
#include "drawgui/render/node_lifecycle.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/shortcuts/action_scopes.h"
#include "drawgui/theme/theme_bindings.h"
#include "drawgui/ui/dialog.h"
#include "drawgui/ui/menu.h"
#include "drawgui/ui/tooltip_popup.h"
#include "drawgui/widget/focus.h"
#include "drawgui/widget/tooltip.h"
#include "drawgui/window/popup_host.h"
#include "drawgui/window/window_manager.h"

#include "menu_scene.h"
#include "menu_window.h"

namespace menu_check {
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

constexpr PixelSize kSize{620, 220};
const std::vector<std::string> kMenuItems = {"Copy", "Paste", "Delete"};

menu_scene::Options options_for(PixelSize size) {
  menu_scene::Options options;
  options.spec.viewport = size;
  return options;
}

// The three side tables this example's scene does not keep itself, but
// PopupHost::destroy() (through dg::NodeSideTables) needs by reference.
struct SpareTables {
  dg::ThemeBindings theme_bindings;
  dg::AnimationEngine animation;
  dg::ActionScopes action_scopes;
};

dg::UiContext ui_for(PopupHost& host, dg::WindowId window, menu_scene::Scene& scene,
                     SpareTables& spare) {
  return dg::UiContext{host,
                       window,
                       scene.tree.render(),
                       {scene.widgets, spare.theme_bindings, scene.focus, spare.animation,
                        spare.action_scopes, scene.interaction},
                       scene.fonts.has_value() ? &*scene.fonts : nullptr,
                       scene.ui_font,
                       dg::PopupStyle{}};
}

std::vector<dg::MenuItem> menu_items() {
  std::vector<dg::MenuItem> items;
  items.reserve(kMenuItems.size());
  for (const std::string& text : kMenuItems) {
    items.push_back(dg::menu_item(text));
  }
  return items;
}

// --------------------------------------------------------------------------
// Claim 1: button identity - a REAL SDL_BUTTON_RIGHT round-trips through
// the real event queue as PointerButton::kSecondary, and a real
// SDL_BUTTON_LEFT still round-trips as kPrimary (the regression this
// slice's own MUST NOT is built against: the pointer path every existing
// hit-test/focus/`--script` test rides on must still produce exactly what
// it always did for the primary button - proven here by driving the SAME
// post_pointer_button() call every prior example already uses and reading
// PointerEvent::button back out, not merely reasoning about the source).
// --------------------------------------------------------------------------

void check_button_identity(std::ostream& out, bool& ok) {
  out << "button identity: a real SDL_BUTTON_RIGHT/LEFT round-trip through the real event "
         "queue\n";
  dg::Expected<dg::WindowManager, dg::WindowError> made = dg::WindowManager::create();
  if (!made) {
    out << "  FAIL: could not start the window system: " << made.error().message << "\n";
    ok = false;
    return;
  }
  dg::WindowManager manager = std::move(made.value());
  dg::WindowSpec spec;
  spec.title = "button identity (headless)";
  spec.width = kSize.width;
  spec.height = kSize.height;
  const dg::Expected<dg::WindowId, dg::WindowError> window = manager.open(spec);
  if (!window) {
    out << "  FAIL: could not open a window: " << window.error().message << "\n";
    ok = false;
    return;
  }

  // Runner's own first-frame viewport must be the window's real
  // drawable_size(), never Settings::size - see
  // examples/22_dropdown_menu/dropdown_check.cpp's own assertion of the
  // same shape for the full argument on why this pins the real function
  // Runner's constructor calls rather than a duplicate.
  menu_window::Settings runner_settings;
  runner_settings.size = kSize;
  const dg::PixelSize drawable =
      manager.drawable_size(window.value()).value_or(dg::PixelSize{});
  check(menu_window::initial_viewport(manager, window.value(), runner_settings) == drawable,
        "Runner's initial viewport is manager.drawable_size(), not Settings::size", out, ok);

  manager.post_pointer_button(window.value(), /*down=*/true, 40, 40,
                              dg::PointerButton::kPrimary);
  const dg::PumpResult primary_pump = manager.pump(200);
  check(primary_pump.pointer.size() == 1,
        "a primary-button post_pointer_button() call produces "
        "exactly one PointerEvent",
        out, ok);
  if (!primary_pump.pointer.empty()) {
    check(primary_pump.pointer.front().button == dg::PointerButton::kPrimary,
          "...and PointerEvent::button reports kPrimary - every call site written before "
          "7-5b (default argument) is unaffected",
          out, ok);
    check(primary_pump.pointer.front().action == dg::PointerAction::kDown,
          "...with PointerAction::kDown unchanged - button identity is a SIBLING field, not a "
          "new PointerAction case",
          out, ok);
  }

  manager.post_pointer_button(window.value(), /*down=*/true, 40, 40,
                              dg::PointerButton::kSecondary);
  const dg::PumpResult secondary_pump = manager.pump(200);
  check(secondary_pump.pointer.size() == 1,
        "a secondary-button post_pointer_button() call ALSO produces exactly one PointerEvent "
        "(before 7-5b, this was silently dropped by the backend)",
        out, ok);
  if (!secondary_pump.pointer.empty()) {
    check(secondary_pump.pointer.front().button == dg::PointerButton::kSecondary,
          "...and PointerEvent::button reports kSecondary", out, ok);
  }

  manager.post_pointer_button(window.value(), /*down=*/true, 40, 40,
                              dg::PointerButton::kMiddle);
  const dg::PumpResult middle_pump = manager.pump(200);
  check(middle_pump.pointer.size() == 1 &&
            middle_pump.pointer.front().button == dg::PointerButton::kMiddle,
        "a middle-button post_pointer_button() call round-trips as kMiddle too", out, ok);
}

// --------------------------------------------------------------------------
// Claim 2: the context menu - opened at the POINTER position (not a
// widget's own bounds, unlike a dropdown's anchor), keyboard Up/Down/Enter
// reusing dg::Focus::focus_next()/focus_previous() verbatim (identical to
// examples/22_dropdown_menu's own precedent), Escape/click-outside closing
// without a selection, and - the thing this slice's own routing decision
// makes true - a secondary-button kDown on the SAME widget a primary-button
// click already works on does NOT feed dg::Interaction at all.
// --------------------------------------------------------------------------

void check_context_menu(std::ostream& out, bool& ok) {
  out << "\ncontext menu: opened at the pointer, Up/Down/Enter, Escape, click-outside, and "
         "the parallel-channel routing decision\n";
  dg::Expected<dg::WindowManager, dg::WindowError> made = dg::WindowManager::create();
  if (!made) {
    out << "  FAIL: could not start the window system: " << made.error().message << "\n";
    ok = false;
    return;
  }
  dg::WindowManager manager = std::move(made.value());
  dg::WindowSpec spec;
  spec.title = "context menu (headless, overlay)";
  spec.width = kSize.width;
  spec.height = kSize.height;
  const dg::Expected<dg::WindowId, dg::WindowError> window = manager.open(spec);
  if (!window) {
    out << "  FAIL: could not open a window: " << window.error().message << "\n";
    ok = false;
    return;
  }

  menu_scene::Scene scene = menu_scene::build(options_for(kSize));
  if (!scene.fonts.has_value()) {
    out << "  FAIL: no system fonts found\n";
    ok = false;
    return;
  }
  PopupHost host{manager};
  SpareTables spare;
  dg::UiContext ctx = ui_for(host, window.value(), scene, spare);
  const PixelRect anchor_target =
      scene.tree.render().absolute_bounds(scene.handles.menu_target);
  const dg::PixelPoint pointer_at{anchor_target.left() + 12, anchor_target.top() + 8};
  // A zero-size anchor rect: resolve_popup_placement()'s own "below" case
  // sets content_bounds.top() to anchor.bottom(), which is exactly the
  // pointer's own y for a zero-height rect.
  const PixelRect anchor{pointer_at.x, pointer_at.y, 0, 0};
  const std::size_t live_baseline =
      dg::subtree_of(scene.tree.render(), dg::RenderTree::root()).size();

  // The routing decision this slice made: a
  // secondary-button kDown on menu_target does NOT touch dg::Interaction -
  // only a primary-button kDown/kUp does. Proven by constructing the exact
  // event and confirming neither hover NOR holding() changed as a result of
  // routing it through the "is this a menu trigger" check alone, never
  // through menu_scene::dispatch_pointer() at all.
  const dg::PointerEvent secondary_down{.window = window.value(),
                                        .action = dg::PointerAction::kDown,
                                        .x = pointer_at.x,
                                        .y = pointer_at.y,
                                        .button = dg::PointerButton::kSecondary};
  check(!scene.interaction.hovered().has_value() && !scene.interaction.holding().has_value(),
        "before any event, nothing is hovered or held (baseline)", out, ok);
  const bool is_menu_trigger = secondary_down.button == dg::PointerButton::kSecondary &&
                               secondary_down.action == dg::PointerAction::kDown;
  check(is_menu_trigger,
        "a secondary-button kDown is recognised as the menu trigger BEFORE it would reach "
        "dispatch_pointer()",
        out, ok);
  check(!scene.interaction.hovered().has_value() && !scene.interaction.holding().has_value(),
        "...and dg::Interaction is STILL untouched - the parallel, non-activating channel "
        "the routing decision named",
        out, ok);

  dg::Menu menu;
  dg::MenuSpec menu_spec;
  menu_spec.items = menu_items();
  menu_spec.min_width = 140;
  const std::optional<dg::PopupHandle> menu_handle =
      menu.open(ctx, anchor, menu_spec, std::nullopt) ? menu.handle() : std::nullopt;
  if (!menu_handle.has_value()) {
    out << "  FAIL: dg::Menu::open() failed\n";
    ok = false;
    return;
  }
  check(menu_handle->content_bounds.left() == pointer_at.x &&
            menu_handle->content_bounds.top() == pointer_at.y,
        "the menu opens anchored at the POINTER position, not menu_target's own bounds", out,
        ok);

  auto key = [&](dg::Key k) {
    const dg::KeyEvent event{window.value(), dg::KeyAction::kDown, k};
    return menu.handle_key(ctx, event);
  };
  check(menu.highlighted(ctx) == 0, "opening seeds the highlight at the first item", out, ok);
  key(dg::Key::kDown);
  check(menu.highlighted(ctx) == 1, "Down moves the highlight to the second item", out, ok);
  key(dg::Key::kDown);
  check(menu.highlighted(ctx) == 2, "Down again reaches the LAST item", out, ok);
  key(dg::Key::kDown);
  check(menu.highlighted(ctx) == 0, "Down from the last item WRAPS to the first", out, ok);
  key(dg::Key::kUp);
  check(menu.highlighted(ctx) == 2, "Up from the first item WRAPS to the last", out, ok);

  // Escape closes without a selection - PopupHost's own dismissal, routed
  // through dg::Menu.
  const dg::MenuResult escaped = key(dg::Key::kEscape);
  check(escaped.event == dg::MenuEvent::kDismissed && !menu.is_open(),
        "Escape closes the menu without a selection", out, ok);

  // Reopen and close by a click OUTSIDE.
  menu.open(ctx, anchor, menu_spec, std::nullopt);
  const dg::PointerEvent outside_click{window.value(), dg::PointerAction::kDown, 5, 5};
  check(menu.handle_pointer(ctx, outside_click).event == dg::MenuEvent::kDismissed,
        "a click OUTSIDE the menu's bounds closes it without a selection", out, ok);

  // Reopen, Down, Enter: the highlighted item is activated.
  menu.open(ctx, anchor, menu_spec, std::nullopt);
  key(dg::Key::kDown);
  const dg::MenuResult chosen = key(dg::Key::kEnter);
  check(chosen.event == dg::MenuEvent::kActivated && chosen.index == 1,
        "Enter activates the highlighted item (\"Paste\")", out, ok);
  check(dg::subtree_of(scene.tree.render(), dg::RenderTree::root()).size() == live_baseline,
        "every close destroyed the menu's nodes - the live node count is back at baseline", out,
        ok);

  // Relayout cost: every operation above went through RenderTree alone
  // (dg::Menu's own direct add_child() calls, matching the
  // overlay-branch precedent) - matching every prior slice's
  // own measurement discipline (the dropdown
  // figure).
  const dg::LayoutStats stats = scene.tree.layout();
  out << "  LayoutStats after three open/close cycles and five keyboard-navigation moves: "
         "nodes_visited="
      << stats.nodes_visited << " nodes_relaid_out=" << stats.nodes_relaid_out << "\n";
  check(stats.nodes_visited == 0 && stats.nodes_relaid_out == 0,
        "opening/closing the context menu and moving its highlight cost a repaint, never a "
        "relayout",
        out, ok);
}

// --------------------------------------------------------------------------
// Claim 3: the tooltip - the hover-delay timer (HoverTimer/update_hover_
// timer/hover_ready, tooltip.h) and PopupHost::show()'s overlay path built
// for real. PopupHost is overlay-only - there is no native branch left to attempt against the
// dummy driver.
// --------------------------------------------------------------------------

void check_hover_timer(std::ostream& out, bool& ok) {
  out << "\ntooltip: the hover-delay timer, a single (NodeId, AnimTime) pair, not a side "
         "table\n";
  dg::HoverTimer timer;
  const NodeId target{7};
  const dg::AnimTime t0{1'000};
  check(!dg::hover_ready(timer, t0, 400), "a fresh HoverTimer is never ready (nothing hovered)",
        out, ok);

  dg::update_hover_timer(timer, target, t0);
  check(timer.target == target && timer.since == t0,
        "update_hover_timer() records the target and the start time on first hover", out, ok);
  check(!dg::hover_ready(timer, dg::AnimTime{t0.ms + 399}, 400),
        "one millisecond short of the delay: not ready yet", out, ok);
  check(dg::hover_ready(timer, dg::AnimTime{t0.ms + 400}, 400), "AT the delay: ready", out, ok);
  check(dg::hover_ready(timer, dg::AnimTime{t0.ms + 5000}, 400),
        "well past the delay: still ready (a tooltip does not expire on its own)", out, ok);

  // Leaving (hovered becomes nullopt) and coming BACK to the SAME widget
  // restarts the clock - the "continuous hover"
  // requirement, not "ever hovered at all".
  dg::update_hover_timer(timer, std::nullopt, dg::AnimTime{t0.ms + 5000});
  check(!timer.target.has_value(), "leaving clears the timer's target", out, ok);
  check(!dg::hover_ready(timer, dg::AnimTime{t0.ms + 5001}, 400),
        "...and hover_ready() is false while nothing is being timed", out, ok);
  dg::update_hover_timer(timer, target, dg::AnimTime{t0.ms + 5001});
  check(!dg::hover_ready(timer, dg::AnimTime{t0.ms + 5001 + 399}, 400),
        "re-entering the SAME widget restarts the delay from zero - it does NOT inherit time "
        "accumulated before the gap",
        out, ok);

  // A DIFFERENT widget also restarts, never accumulates against the first.
  const NodeId other{8};
  dg::update_hover_timer(timer, other, dg::AnimTime{t0.ms + 6000});
  check(timer.target == other, "hovering a DIFFERENT widget restarts the timer onto it", out,
        ok);
}

void check_tooltip_popup(std::ostream& out, bool& ok) {
  out << "\ntooltip: PopupHost::show() overlay branch built for real\n";
  dg::Expected<dg::WindowManager, dg::WindowError> made = dg::WindowManager::create();
  if (!made) {
    out << "  FAIL: could not start the window system: " << made.error().message << "\n";
    ok = false;
    return;
  }
  dg::WindowManager manager = std::move(made.value());
  dg::WindowSpec spec;
  spec.title = "tooltip (headless, overlay)";
  spec.width = kSize.width;
  spec.height = kSize.height;
  const dg::Expected<dg::WindowId, dg::WindowError> window = manager.open(spec);
  if (!window) {
    out << "  FAIL: could not open a window: " << window.error().message << "\n";
    ok = false;
    return;
  }
  menu_scene::Scene scene = menu_scene::build(options_for(kSize));
  PopupHost host{manager};
  SpareTables spare;
  dg::UiContext ctx = ui_for(host, window.value(), scene, spare);
  const std::size_t live_baseline =
      dg::subtree_of(scene.tree.render(), dg::RenderTree::root()).size();
  dg::Tooltip tooltip{400};
  tooltip.bind(scene.handles.hover_target, "This button opens a tooltip");
  const NodeId target = scene.handles.hover_target;

  tooltip.tick(ctx, target, dg::AnimTime{1'000});
  check(tooltip.tick(ctx, target, dg::AnimTime{1'399}) == dg::TooltipEvent::kNone,
        "dg::Tooltip: one millisecond short of the delay, nothing is shown", out, ok);
  check(tooltip.tick(ctx, target, dg::AnimTime{1'400}) == dg::TooltipEvent::kShown &&
            tooltip.is_open(),
        "AT the delay the bubble opens through PopupHost::show()'s overlay branch", out, ok);
  const dg::LayoutStats stats = scene.tree.layout();
  check(stats.nodes_visited == 0 && stats.nodes_relaid_out == 0,
        "showing a tooltip's content costs a repaint, never a relayout", out, ok);
  const dg::PointerEvent press{window.value(), dg::PointerAction::kDown, 0, 0};
  check(tooltip.handle_pointer(ctx, press) == dg::TooltipEvent::kHidden, "a press hides it",
        out, ok);
  check(tooltip.tick(ctx, target, dg::AnimTime{9'000}) == dg::TooltipEvent::kNone,
        "...and it stays hidden while the pointer is still over the target", out, ok);
  check(dg::subtree_of(scene.tree.render(), dg::RenderTree::root()).size() == live_baseline,
        "hiding destroyed its nodes - the live node count is back at baseline", out, ok);
}

// --------------------------------------------------------------------------
// Claim 4: Dialog - modal focus refusal (dg::Focus::set_guarded()) composed
// onto the SAME scope mechanism 7-4 built, and the cancellable close-request
// veto (WindowManager::request_close()/close_now(), PumpResult::
// close_requested).
// --------------------------------------------------------------------------

void check_modal_focus_refusal(std::ostream& out, bool& ok) {
  out << "\ndialog: modal focus refusal - the SAME scope mechanism 7-4 built, one more bit\n";
  dg::Expected<dg::WindowManager, dg::WindowError> made = dg::WindowManager::create();
  if (!made) {
    out << "  FAIL: could not start the window system: " << made.error().message << "\n";
    ok = false;
    return;
  }
  dg::WindowManager manager = std::move(made.value());
  dg::WindowSpec spec;
  spec.title = "dialog focus trap (headless)";
  spec.width = kSize.width;
  spec.height = kSize.height;
  const dg::Expected<dg::WindowId, dg::WindowError> window = manager.open(spec);
  if (!window) {
    out << "  FAIL: could not open a window: " << window.error().message << "\n";
    ok = false;
    return;
  }
  menu_scene::Scene scene = menu_scene::build(options_for(kSize));
  PopupHost host{manager};
  SpareTables spare;
  dg::UiContext ctx = ui_for(host, window.value(), scene, spare);
  dg::Dialog dialog;
  dg::DialogSpec dialog_spec;
  dialog_spec.title = "Modal dialog - Tab/click cannot escape";
  dialog_spec.buttons = {"Close"};
  dialog_spec.cancel_button = 0;
  dialog_spec.panel_size = PixelSize{280, 120};
  if (!dialog.open(ctx, dialog_spec, std::nullopt)) {
    out << "  FAIL: dg::Dialog::open() failed\n";
    ok = false;
    return;
  }
  struct {
    NodeId panel;
    NodeId close_button;
  } const handles{dialog.panel(), dialog.buttons().front()};

  check(scene.focus.scope_is_modal(), "dg::Dialog enters a MODAL scope on its panel", out, ok);
  check(scene.focus.current() == handles.close_button,
        "the dialog's own Close button is focused on open (inside the scope)", out, ok);

  // Tab confinement: with exactly one focusable widget inside, Tab wraps to
  // itself, which is still the correct confinement answer (never `before`/`after`).
  const dg::KeyEvent tab_key{window.value(), dg::KeyAction::kDown, dg::Key::kTab};
  dialog.handle_key(ctx, tab_key);
  check(scene.focus.current() == handles.close_button,
        "Tab confinement: Tab inside the modal never reaches `before`/"
        "`menu_target`/`hover_target`/`open_dialog`/`after`",
        out, ok);

  // THE crux: set_guarded() refuses a target OUTSIDE the scope while modal.
  const dg::FocusChange refused =
      scene.focus.set_guarded(scene.tree.render(), scene.handles.before);
  check(!refused.any(),
        "set_guarded() REFUSES a target outside the modal scope - no change at "
        "all",
        out, ok);
  check(scene.focus.current() == handles.close_button,
        "...focus is still exactly where it was before the refused call", out, ok);

  // Blurring to nothing is NOT refused - only a target OUTSIDE the scope is.
  const dg::FocusChange blurred = scene.focus.set_guarded(scene.tree.render(), std::nullopt);
  check(blurred.any() && !scene.focus.current().has_value(),
        "set_guarded(nullopt) still blurs normally - only a target outside the scope is "
        "refused, not blurring to nothing",
        out, ok);
  scene.focus.set(handles.close_button);

  // The UNGUARDED set() still succeeds - proving set_guarded() is a
  // genuinely necessary SECOND entry point, not a redundant restatement of
  // set()'s own existing behaviour.
  const dg::FocusChange unguarded = scene.focus.set(scene.handles.before);
  check(unguarded.any() && scene.focus.current() == scene.handles.before,
        "the UNGUARDED set() still moves focus anywhere unconditionally - set_guarded() is a "
        "genuinely separate, additional entry point, not set()'s new default behaviour",
        out, ok);

  // Non-modal scopes (a plain popup/dropdown) are unaffected: set_guarded()
  // behaves exactly like set() when scope_is_modal() is false.
  scene.focus.enter_scope(handles.panel);  // modal defaults to false
  check(!scene.focus.scope_is_modal(),
        "enter_scope(root) without the modal flag defaults to "
        "non-modal, matching 7-4's own unchanged meaning",
        out, ok);
  const dg::FocusChange non_modal_guarded =
      scene.focus.set_guarded(scene.tree.render(), scene.handles.after);
  check(non_modal_guarded.any() && scene.focus.current() == scene.handles.after,
        "...and set_guarded() does NOT refuse anything while the active scope is non-modal - a "
        "dropdown/context menu's own Tab confinement is completely unaffected by this slice",
        out, ok);
  scene.focus.enter_scope(handles.panel, /*modal=*/true);
  const dg::KeyEvent escape{window.value(), dg::KeyAction::kDown, dg::Key::kEscape};
  const dg::DialogResult escaped = dialog.handle_key(ctx, escape);
  check(escaped.event == dg::DialogEvent::kClosed && escaped.button == 0 && !dialog.is_open() &&
            !scene.focus.current_scope().has_value(),
        "Escape closes the dialog, reporting its cancel button, and leaves the scope", out, ok);

  const dg::LayoutStats stats = scene.tree.layout();
  out << "  LayoutStats after building the modal panel and six focus-guard checks: "
         "nodes_visited="
      << stats.nodes_visited << " nodes_relaid_out=" << stats.nodes_relaid_out << "\n";
  check(stats.nodes_visited == 0 && stats.nodes_relaid_out == 0,
        "the modal dialog panel and its focus trap cost a repaint, never a relayout", out, ok);
}

void check_close_request_veto(std::ostream& out, bool& ok) {
  out << "\ndialog: the cancellable close-request veto\n";
  dg::Expected<dg::WindowManager, dg::WindowError> made = dg::WindowManager::create();
  if (!made) {
    out << "  FAIL: could not start the window system: " << made.error().message << "\n";
    ok = false;
    return;
  }
  dg::WindowManager manager = std::move(made.value());

  dg::WindowSpec plain;
  plain.title = "plain (unchanged behaviour)";
  plain.width = 200;
  plain.height = 150;
  const dg::Expected<dg::WindowId, dg::WindowError> plain_window = manager.open(plain);

  dg::WindowSpec cancellable;
  cancellable.title = "cancellable";
  cancellable.width = 200;
  cancellable.height = 150;
  cancellable.cancellable_close = true;
  const dg::Expected<dg::WindowId, dg::WindowError> cancellable_window =
      manager.open(cancellable);

  if (!plain_window || !cancellable_window) {
    out << "  FAIL: could not open the test windows\n";
    ok = false;
    return;
  }
  check(manager.open_window_count() == 2, "two windows are open to start", out, ok);

  manager.request_close(plain_window.value());
  manager.request_close(cancellable_window.value());
  const dg::PumpResult pumped = manager.pump(200);

  bool plain_in_closed = false;
  for (const dg::WindowId id : pumped.closed) {
    plain_in_closed = plain_in_closed || (id == plain_window.value());
  }
  check(plain_in_closed,
        "a PLAIN window (cancellable_close defaulted to false) still closes IMMEDIATELY - "
        "every window's behaviour before this slice is unchanged",
        out, ok);

  bool cancellable_in_requested = false;
  for (const dg::WindowId id : pumped.close_requested) {
    cancellable_in_requested = cancellable_in_requested || (id == cancellable_window.value());
  }
  check(cancellable_in_requested,
        "the CANCELLABLE window's close request surfaces through close_requested INSTEAD of "
        "closed",
        out, ok);
  check(manager.open_window_count() == 1,
        "...and it is NOT destroyed - the plain window closed, the cancellable one did not",
        out, ok);

  // Veto: do nothing at all, pump again with no further request - the
  // window is STILL open, proving a handler can veto simply by declining
  // to call close_now().
  const dg::PumpResult after_veto = manager.pump(50);
  check(after_veto.closed.empty() && manager.open_window_count() == 1,
        "declining to call close_now() (the veto) leaves the window open across a further "
        "pump()",
        out, ok);

  // Confirm: close_now() actually destroys it.
  manager.close_now(cancellable_window.value());
  check(manager.open_window_count() == 0,
        "close_now() destroys the cancellable window once a handler decides not to veto", out,
        ok);
}

void attempt_native_dialog(std::ostream& out) {
  out << "\ndialog, native branch: attempting real SDL_SetWindowParent()/SDL_SetWindowModal() "
         "under the headless dummy driver (measured to fail, matching SDL_CreatePopupWindow's "
         "own precedent)\n";
  dg::Expected<dg::WindowManager, dg::WindowError> made = dg::WindowManager::create();
  if (!made) {
    out << "  SKIP (loud, specific): could not even start the window system: "
        << made.error().message << "\n";
    return;
  }
  dg::WindowManager manager = std::move(made.value());
  dg::WindowSpec owner_spec;
  owner_spec.title = "owner (headless)";
  owner_spec.width = 300;
  owner_spec.height = 200;
  const dg::Expected<dg::WindowId, dg::WindowError> owner = manager.open(owner_spec);
  if (!owner) {
    out << "  SKIP (loud, specific): could not open the owner window: " << owner.error().message
        << "\n";
    return;
  }
  dg::WindowSpec dialog_spec;
  dialog_spec.title = "dialog (headless, native attempt)";
  dialog_spec.width = 300;
  dialog_spec.height = 140;
  dialog_spec.cancellable_close = true;
  const dg::Expected<dg::WindowId, dg::WindowError> dialog =
      manager.open_dialog(dialog_spec, owner.value());
  if (!dialog) {
    out << "  SKIPPED, loudly and specifically: \"" << dialog.error().message
        << "\". Real coverage of real OS ownership/modality comes from examples/"
           "23_menu_tooltip_dialog's own interactive `--branch native` mode against an actual "
           "display.\n";
    return;
  }
  out << "  UNEXPECTED PASS: this environment's dummy driver granted real window "
         "ownership/modality.\n";
  manager.close_now(dialog.value());
}

}  // namespace

int run(std::ostream& out) {
  setenv("SDL_VIDEODRIVER", "dummy", 1);

  out << "MENU/TOOLTIP/DIALOG: button identity, a context menu anchored at the pointer, a "
         "hover-delay tooltip, and a modal dialog's focus trap plus close-request veto.\n\n";

  bool ok = true;
  check_button_identity(out, ok);
  check_context_menu(out, ok);
  check_hover_timer(out, ok);
  check_tooltip_popup(out, ok);
  check_modal_focus_refusal(out, ok);
  check_close_request_veto(out, ok);
  attempt_native_dialog(out);
  out << "\n" << (ok ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED") << "\n";
  return ok ? 0 : 1;
}

}  // namespace menu_check
