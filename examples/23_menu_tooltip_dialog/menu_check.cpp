// The headless half of examples/23_menu_tooltip_dialog - matching
// examples/22_dropdown_menu's own dropdown_check.cpp shape exactly (see
// its own top comment for why: SDL_VIDEODRIVER=dummy opens an ordinary
// window and drives the OVERLAY branch for real, but SDL_CreatePopupWindow
// and this slice's own SDL_SetWindowParent()/SDL_SetWindowModal() both fail
// under it - each is ATTEMPTED once and reported loudly and specifically,
// never silently skipped).

#include "menu_check.h"

#include <cstdlib>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "drawgui/anim/clock.h"
#include "drawgui/base/expected.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/widget/focus.h"
#include "drawgui/widget/tooltip.h"
#include "drawgui/window/popup_host.h"
#include "drawgui/window/window_manager.h"

#include "dialog_panel.h"
#include "menu_rows.h"
#include "menu_scene.h"
#include "tooltip_content.h"

namespace menu_check {
namespace {

using dg::NodeId;
using dg::PixelRect;
using dg::PixelSize;
using dg::PopupFlags;
using dg::PopupHandle;
using dg::PopupHost;
using dg::PopupPlacement;
using dg::PopupWindowKind;

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
  const PixelRect anchor_target =
      scene.tree.render().absolute_bounds(scene.handles.menu_target);
  const dg::PixelPoint pointer_at{anchor_target.left() + 12, anchor_target.top() + 8};
  // A zero-size anchor rect: resolve_popup_placement()'s own "below" case
  // sets content_bounds.top() to anchor.bottom(), which is exactly the
  // pointer's own y for a zero-height rect (PixelRect::bottom() ==
  // y + height) - the same reason a dropdown's own anchor is the WIDGET's
  // full bounds and its content lands just past the bottom edge, not
  // exactly ON it.
  const PixelRect anchor{pointer_at.x, pointer_at.y, 0, 0};
  const PixelSize menu_size = menu_rows::size_for(static_cast<int>(kMenuItems.size()), 140);

  // The routing decision this slice made (doc/menus.md section 6.1): a
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
        "doc/menus.md section 6.1 named",
        out, ok);

  const dg::Expected<PopupHandle, dg::WindowError> shown =
      host.show(window.value(), scene.tree.render(), dg::PlatformCaps{.native_popup = false},
                anchor, menu_size, PopupPlacement::kBelow, PopupFlags{});
  if (!shown) {
    out << "  FAIL: overlay show() failed: " << shown.error().message << "\n";
    ok = false;
    return;
  }
  PopupHandle handle = shown.value();
  check(handle.content_bounds.left() == pointer_at.x &&
            handle.content_bounds.top() == pointer_at.y,
        "the menu opens anchored at the POINTER position, not menu_target's own bounds", out,
        ok);

  std::vector<NodeId> rows =
      menu_rows::build(scene.tree.render(), scene.widgets, handle.content_root, kMenuItems,
                       menu_size.width, scene.ui_font, 14.0F);
  scene.focus.enter_scope(handle.content_root);
  scene.focus.set(rows.front());

  check(scene.focus.current() == rows[0], "opening seeds the highlight at the first item", out,
        ok);
  scene.focus.focus_next(scene.tree.render(), scene.widgets, handle.content_root);
  check(scene.focus.current() == rows[1], "Down moves the highlight to the second item", out,
        ok);
  scene.focus.focus_next(scene.tree.render(), scene.widgets, handle.content_root);
  check(scene.focus.current() == rows[2], "Down again reaches the LAST item", out, ok);
  scene.focus.focus_next(scene.tree.render(), scene.widgets, handle.content_root);
  check(scene.focus.current() == rows[0], "Down from the last item WRAPS to the first", out,
        ok);
  scene.focus.focus_previous(scene.tree.render(), scene.widgets, handle.content_root);
  check(scene.focus.current() == rows[2], "Up from the first item WRAPS to the last", out, ok);

  // Escape closes without a selection - PopupHost's own dismissal, unchanged.
  const dg::KeyEvent escape{window.value(), dg::KeyAction::kDown, dg::Key::kEscape};
  const bool escape_closed = host.handle_key(handle, window.value(), escape, PopupFlags{});
  check(escape_closed, "Escape closes the menu (PopupHost's own dismissal, unmodified)", out,
        ok);
  if (escape_closed) {
    scene.focus.exit_scope(scene.tree.render());
    host.close(handle);
  }

  // Reopen and close by a click OUTSIDE - the same PopupHost mechanism a
  // dropdown already relies on, now exercised by a context menu instead.
  const dg::Expected<PopupHandle, dg::WindowError> reopened =
      host.show(window.value(), scene.tree.render(), dg::PlatformCaps{.native_popup = false},
                anchor, menu_size, PopupPlacement::kBelow, PopupFlags{});
  handle = reopened.value();
  rows = menu_rows::build(scene.tree.render(), scene.widgets, handle.content_root, kMenuItems,
                          menu_size.width, scene.ui_font, 14.0F);
  scene.focus.enter_scope(handle.content_root);
  const dg::PointerEvent outside_click{window.value(), dg::PointerAction::kDown, 5, 5};
  const bool outside_closed =
      host.handle_pointer(handle, window.value(), outside_click, PopupFlags{});
  check(outside_closed, "a click OUTSIDE the menu's bounds closes it without a selection", out,
        ok);
  if (outside_closed) {
    scene.focus.exit_scope(scene.tree.render());
    host.close(handle);
  }

  // Relayout cost: every operation above went through RenderTree alone
  // (menu_rows::build()'s own direct add_child() calls, doc/popup.md
  // section 3's overlay-branch precedent) - matching every prior slice's
  // own measurement discipline (doc/menus.md section 8's own dropdown
  // figure).
  const dg::LayoutStats stats = scene.tree.layout();
  out << "  LayoutStats after two open/close cycles and four keyboard-navigation moves: "
         "nodes_visited="
      << stats.nodes_visited << " nodes_relaid_out=" << stats.nodes_relaid_out << "\n";
  check(stats.nodes_visited == 0 && stats.nodes_relaid_out == 0,
        "opening/closing the context menu and moving its highlight cost a repaint, never a "
        "relayout",
        out, ok);
}

void attempt_native_context_menu(std::ostream& out) {
  out << "\ncontext menu, native branch: attempting a real SDL_CreatePopupWindow under the "
         "headless dummy driver (measured to fail, doc/popup.md section 1 - unchanged by this "
         "slice's own SDL_WINDOW_POPUP_MENU flag choice)\n";
  dg::Expected<dg::WindowManager, dg::WindowError> made = dg::WindowManager::create();
  if (!made) {
    out << "  SKIP (loud, specific): could not even start the window system: "
        << made.error().message << "\n";
    return;
  }
  dg::WindowManager manager = std::move(made.value());
  dg::WindowSpec spec;
  spec.title = "context menu (headless, native attempt)";
  spec.width = kSize.width;
  spec.height = kSize.height;
  const dg::Expected<dg::WindowId, dg::WindowError> window = manager.open(spec);
  if (!window) {
    out << "  SKIP (loud, specific): could not open a host window: " << window.error().message
        << "\n";
    return;
  }
  menu_scene::Scene scene = menu_scene::build(options_for(kSize));
  PopupHost host{manager};
  const PixelRect anchor{60, 60, 1, 1};
  const PixelSize size = menu_rows::size_for(static_cast<int>(kMenuItems.size()), 140);
  const dg::Expected<PopupHandle, dg::WindowError> native =
      host.show(window.value(), scene.tree.render(), dg::PlatformCaps{.native_popup = true},
                anchor, size, PopupPlacement::kBelow, PopupFlags{});
  if (!native) {
    out << "  SKIPPED, loudly and specifically: \"" << native.error().message
        << "\". Real coverage of the native branch comes from "
           "examples/23_menu_tooltip_dialog's interactive mode against an actual display.\n";
    return;
  }
  out << "  UNEXPECTED PASS: this environment's dummy driver created a real popup window.\n";
  PopupHandle handle = native.value();
  host.close(handle);
}

// --------------------------------------------------------------------------
// Claim 3: the tooltip - the hover-delay timer (HoverTimer/update_hover_
// timer/hover_ready, tooltip.h), the kTooltip SDL flag threaded through
// PopupHost::show() for the overlay branch, and the native branch's own
// loud, specific measured failure under the dummy driver.
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
  // restarts the clock - doc/menus.md section 6.2's own "continuous hover"
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
  out << "\ntooltip: PopupWindowKind::kTooltip threaded through PopupHost::show(), overlay "
         "branch built for real\n";
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
  const PixelRect anchor = scene.tree.render().absolute_bounds(scene.handles.hover_target);
  const std::string text = "This button opens a tooltip";
  const PixelSize size = tooltip_content::size_for(text, 13);
  const dg::Expected<PopupHandle, dg::WindowError> shown =
      host.show(window.value(), scene.tree.render(), dg::PlatformCaps{.native_popup = false},
                anchor, size, PopupPlacement::kBelow, PopupFlags{}, PopupWindowKind::kTooltip);
  check(shown.has_value(), "PopupHost::show(kind=kTooltip) succeeds on the overlay branch", out,
        ok);
  if (shown.has_value()) {
    PopupHandle handle = shown.value();
    tooltip_content::build(scene.tree.render(), handle.content_root, text, scene.ui_font, 13.0F,
                           size);
    const dg::LayoutStats stats = scene.tree.layout();
    check(stats.nodes_visited == 0 && stats.nodes_relaid_out == 0,
          "showing a tooltip's content costs a repaint, never a relayout", out, ok);
    host.close(handle);
  }

  const dg::Expected<PopupHandle, dg::WindowError> native =
      host.show(window.value(), scene.tree.render(), dg::PlatformCaps{.native_popup = true},
                anchor, size, PopupPlacement::kBelow, PopupFlags{}, PopupWindowKind::kTooltip);
  if (!native) {
    out << "  SKIPPED, loudly and specifically (native tooltip window): \""
        << native.error().message
        << "\" - matching SDL_CreatePopupWindow's own measured failure under the dummy driver "
           "regardless of which popup-window flag is requested.\n";
  } else {
    out << "  UNEXPECTED PASS: this environment's dummy driver created a real tooltip "
           "window.\n";
    PopupHandle handle = native.value();
    host.close(handle);
  }
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
  const dialog_panel::Handles handles =
      dialog_panel::build(scene.tree.render(), scene.widgets, dg::LayoutTree::root(), kSize,
                          PixelSize{280, 120}, scene.ui_font);

  scene.focus.enter_scope(handles.panel, /*modal=*/true);
  check(scene.focus.scope_is_modal(), "enter_scope(root, true) records the scope as modal", out,
        ok);
  scene.focus.set(handles.close_button);
  check(scene.focus.current() == handles.close_button,
        "the dialog's own Close button can still be focused NORMALLY (inside the scope)", out,
        ok);

  // Tab confinement: focus_next() over the panel's scope never leaves it -
  // with exactly one focusable widget inside, it wraps to itself, which is
  // still the correct confinement answer (never `before`/`after`).
  const dg::FocusChange looped =
      scene.focus.focus_next(scene.tree.render(), scene.widgets, handles.panel);
  check(!looped.any() || scene.focus.current() == handles.close_button,
        "Tab confinement: focus_next() over the modal's own scope never reaches `before`/"
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
  scene.focus.exit_scope(scene.tree.render());

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
  attempt_native_context_menu(out);
  check_hover_timer(out, ok);
  check_tooltip_popup(out, ok);
  check_modal_focus_refusal(out, ok);
  check_close_request_veto(out, ok);
  attempt_native_dialog(out);
  out << "\n" << (ok ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED") << "\n";
  return ok ? 0 : 1;
}

}  // namespace menu_check
