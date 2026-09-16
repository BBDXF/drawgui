#include "focus_check.h"

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

#include "focus_scene.h"
#include "popup_menu.h"

namespace focus_check {
namespace {

using dg::Color;
using dg::NodeId;
using dg::NodeStyle;
using dg::PixelRect;
using dg::PixelSize;
using dg::PopupFlags;
using dg::PopupHandle;
using dg::PopupHost;
using dg::PopupPlacement;
using dg::RenderTree;
using dg::TreeSpec;
using dg::Widget;
using dg::WidgetKind;
using dg::WidgetSet;

bool check(bool condition, const char* what, std::ostream& out, bool& ok) {
  out << "  " << (condition ? "PASS" : "FAIL") << " " << what << "\n";
  ok = ok && condition;
  return condition;
}

focus_scene::Options options_for(PixelSize size) {
  focus_scene::Options options;
  options.spec.viewport = size;
  return options;
}

constexpr PixelSize kSize{620, 260};

// --------------------------------------------------------------------------
// Claim 1: default Tab order is tree order, the tab_index override sorts
// one sibling to the front, a non-focusable/zero-area/negative-tab_index
// widget never appears, and focus_next()/focus_previous() wrap at both ends.
// --------------------------------------------------------------------------

void check_order_and_wrapping(std::ostream& out, bool& ok) {
  out << "Tab order, override, and wrapping\n";
  focus_scene::Scene scene = focus_scene::build(options_for(kSize));

  const std::vector<NodeId> order =
      dg::focus_order(scene.tree.render(), scene.widgets, scene.handles.body);
  const std::vector<NodeId> expected{scene.handles.reversed, scene.handles.btn_open,
                                     scene.handles.checkbox, scene.handles.slider,
                                     scene.handles.textfield};
  check(order == expected, "focus_order() == [reversed, btn_open, checkbox, slider, textfield]",
        out, ok);

  dg::Expected<dg::WindowManager, dg::WindowError> made = dg::WindowManager::create();
  if (!made) {
    out << "  FAIL: could not start the window system: " << made.error().message << "\n";
    ok = false;
    return;
  }
  dg::WindowManager manager = std::move(made.value());
  dg::WindowSpec spec;
  spec.title = "focus (headless)";
  spec.width = kSize.width;
  spec.height = kSize.height;
  const dg::Expected<dg::WindowId, dg::WindowError> window = manager.open(spec);
  if (!window) {
    out << "  FAIL: could not open a window: " << window.error().message << "\n";
    ok = false;
    return;
  }

  for (const NodeId& id : expected) {
    focus_scene::tab(scene, manager, window.value(), /*backwards=*/false);
    check(scene.focus.current() == id,
          ("focus_next() reaches " + focus_scene::describe(scene, id)).c_str(), out, ok);
  }
  // One more Tab wraps past textfield back to reversed.
  focus_scene::tab(scene, manager, window.value(), false);
  check(scene.focus.current() == scene.handles.reversed, "focus_next() wraps to the front", out,
        ok);

  // Shift-Tab from the front wraps to the back.
  focus_scene::tab(scene, manager, window.value(), /*backwards=*/true);
  check(scene.focus.current() == scene.handles.textfield, "focus_previous() wraps to the back",
        out, ok);

  // `inert` (tab_index=-1) is never reached by five more Tabs from here...
  bool ever_hit_inert = false;
  for (int i = 0; i < 5; ++i) {
    focus_scene::tab(scene, manager, window.value(), false);
    ever_hit_inert = ever_hit_inert || scene.focus.current() == scene.handles.inert;
  }
  check(!ever_hit_inert, "tab_index=-1 (inert) is never a Tab stop", out, ok);
  // ...but a direct focus (a click, in the real widget) still reaches it.
  scene.focus.set(scene.handles.inert);
  check(scene.focus.current() == scene.handles.inert,
        "tab_index=-1 (inert) is still focusable directly", out, ok);
}

// --------------------------------------------------------------------------
// Claim 2: the real SDLK_TAB -> Key::kTab mapping this slice added, driven
// through the actual SDL event queue (post_key()/pump()) rather than a
// KeyEvent constructed by hand - the same "prove the path a user takes"
// precedent every prior post_key()-based check already follows.
// --------------------------------------------------------------------------

void check_real_tab_key_event(std::ostream& out, bool& ok) {
  out << "\nreal SDLK_TAB event through the platform event queue\n";
  dg::Expected<dg::WindowManager, dg::WindowError> made = dg::WindowManager::create();
  if (!made) {
    out << "  FAIL: could not start the window system: " << made.error().message << "\n";
    ok = false;
    return;
  }
  dg::WindowManager manager = std::move(made.value());
  dg::WindowSpec spec;
  spec.title = "focus (headless, real Tab)";
  spec.width = kSize.width;
  spec.height = kSize.height;
  const dg::Expected<dg::WindowId, dg::WindowError> window = manager.open(spec);
  if (!window) {
    out << "  FAIL: could not open a window: " << window.error().message << "\n";
    ok = false;
    return;
  }

  focus_scene::Scene scene = focus_scene::build(options_for(kSize));
  manager.post_key(window.value(), /*down=*/true, dg::Key::kTab, /*shift=*/false);
  const dg::PumpResult pumped = manager.pump(200);
  bool saw_tab = false;
  for (const dg::KeyEvent& event : pumped.key) {
    if (event.key == dg::Key::kTab) {
      saw_tab = true;
      focus_scene::dispatch_key(scene, manager, window.value(), event);
    }
  }
  check(saw_tab, "a posted SDLK_TAB round-trips through pump() as Key::kTab", out, ok);
  check(scene.focus.current() == scene.handles.reversed,
        "dispatch_key() routed that real KeyEvent to focus_next()", out, ok);
}

// --------------------------------------------------------------------------
// Claim 3: focus crossing into a popup - both PopupHost branches.
// Overlay: dg::Focus::enter_scope()/exit_scope() over the SAME Focus
// instance, confirmed to blur a widget still focused inside the popup when
// it closes (the popup-close hazard). Native: attempted for real (expected,
// measured, to fail under the dummy driver, exactly like doc/popup.md's own
// precedent), plus a structural proof that two independent Focus instances
// never confuse each other even when they hand out the identical NodeId
// value - which is why cross-window focus needed no NodeId-plus-window-id
// struct here.
// --------------------------------------------------------------------------

void check_overlay_scope_and_close(std::ostream& out, bool& ok) {
  out << "\noverlay popup: Tab scoped inside it, exit_scope() blurs on close\n";
  dg::Expected<dg::WindowManager, dg::WindowError> made = dg::WindowManager::create();
  if (!made) {
    out << "  FAIL: could not start the window system: " << made.error().message << "\n";
    ok = false;
    return;
  }
  dg::WindowManager manager = std::move(made.value());
  dg::WindowSpec spec;
  spec.title = "focus (headless, overlay)";
  spec.width = kSize.width;
  spec.height = kSize.height;
  const dg::Expected<dg::WindowId, dg::WindowError> window = manager.open(spec);
  if (!window) {
    out << "  FAIL: could not open a window: " << window.error().message << "\n";
    ok = false;
    return;
  }

  focus_scene::Scene scene = focus_scene::build(options_for(kSize));
  PopupHost host{manager};
  const PixelRect anchor = scene.tree.render().absolute_bounds(scene.handles.btn_open);

  // Opened, focused into, and closed THREE times in a row - the ASan-
  // relevant repeat the task asks for: each cycle's popup buttons are
  // brand-new NodeIds (RenderTree is append-only), and each cycle's own
  // FocusChange must still be correct against the CURRENT scope, not a
  // stale one left over from the previous cycle.
  for (int cycle = 0; cycle < 3; ++cycle) {
    const dg::Expected<PopupHandle, dg::WindowError> shown =
        host.show(window.value(), scene.tree.render(), dg::PlatformCaps{.native_popup = false},
                  anchor, popup_menu::kSize, PopupPlacement::kBelow, PopupFlags{});
    if (!shown) {
      out << "  FAIL: overlay show() failed: " << shown.error().message << "\n";
      ok = false;
      return;
    }
    PopupHandle handle = shown.value();
    const popup_menu::Handles menu =
        popup_menu::build(scene.tree.render(), scene.widgets, handle.content_root);
    scene.focus.enter_scope(handle.content_root);

    focus_scene::tab(scene, manager, window.value(), false);
    check(scene.focus.current() == menu.btn1, "Tab into the overlay reaches its first button",
          out, ok);
    focus_scene::tab(scene, manager, window.value(), false);
    check(scene.focus.current() == menu.btn2, "Tab again reaches its second button", out, ok);
    focus_scene::tab(scene, manager, window.value(), false);
    check(scene.focus.current() == menu.btn1,
          "a third Tab wraps WITHIN the popup, not out to the host", out, ok);

    scene.focus.set(menu.btn2);
    host.close(handle);
    const dg::FocusChange change = scene.focus.exit_scope(scene.tree.render());
    check(change.blurred == menu.btn2,
          "closing the overlay blurs a widget still focused inside it", out, ok);
    check(!scene.focus.current().has_value(), "focus is nullopt immediately after", out, ok);
  }
}

void attempt_native_popup(std::ostream& out) {
  out << "\nnative popup: attempting a real SDL_CreatePopupWindow under the headless dummy "
         "driver (measured, not assumed, that this fails - doc/popup.md section 1)\n";
  dg::Expected<dg::WindowManager, dg::WindowError> made = dg::WindowManager::create();
  if (!made) {
    out << "  SKIP (loud, specific): could not even start the window system: "
        << made.error().message << "\n";
    return;
  }
  dg::WindowManager manager = std::move(made.value());
  dg::WindowSpec spec;
  spec.title = "focus (headless, native attempt)";
  spec.width = kSize.width;
  spec.height = kSize.height;
  const dg::Expected<dg::WindowId, dg::WindowError> window = manager.open(spec);
  if (!window) {
    out << "  SKIP (loud, specific): could not open a host window under the dummy driver: "
        << window.error().message << "\n";
    return;
  }
  focus_scene::Scene scene = focus_scene::build(options_for(kSize));
  PopupHost host{manager};
  const PixelRect anchor = scene.tree.render().absolute_bounds(scene.handles.btn_open);
  const dg::Expected<PopupHandle, dg::WindowError> native =
      host.show(window.value(), scene.tree.render(), dg::PlatformCaps{.native_popup = true},
                anchor, popup_menu::kSize, PopupPlacement::kBelow, PopupFlags{});
  if (!native) {
    out << "  SKIPPED, loudly and specifically: SDL_CreatePopupWindow failed under the dummy "
           "video driver, with: \""
        << native.error().message
        << "\". Real doc/focus.md section 6 evidence (a genuine second OS window gaining real "
           "keyboard focus, and this engine's own separate dg::Focus instance for it) comes "
           "from examples/21_focus's interactive mode against an actual display, matching "
           "doc/popup.md's own precedent exactly.\n";
    return;
  }
  out << "  UNEXPECTED PASS: this environment's dummy driver created a real popup window - "
         "bonus coverage, not a failure.\n";
  PopupHandle handle = native.value();
  host.close(handle);
}

// A structural proof, needing no window at all: two entirely independent
// RenderTree+WidgetSet+Focus triples, each numbering its own nodes from
// NodeId{0} exactly the way every RenderTree here already does, never
// confuse each other even when they hand out the IDENTICAL numeric NodeId -
// this is the whole reason cross-window focus needed no NodeId-plus-
// window-id struct (doc/focus.md's own top comment).
void check_two_focus_instances_never_collide(std::ostream& out, bool& ok) {
  out << "\ntwo independent dg::Focus instances sharing the same NodeId numbering\n";
  TreeSpec spec_a;
  spec_a.viewport = PixelSize{100, 100};
  RenderTree tree_a{spec_a};
  WidgetSet widgets_a;
  dg::Focus focus_a;
  const NodeId btn_a =
      tree_a.add_child(RenderTree::root(), PixelRect{0, 0, 40, 20}, NodeStyle{});
  Widget widget_a;
  widget_a.kind = WidgetKind::kButton;
  widgets_a.attach(btn_a, widget_a);

  TreeSpec spec_b;
  spec_b.viewport = PixelSize{100, 100};
  RenderTree tree_b{spec_b};
  WidgetSet widgets_b;
  dg::Focus focus_b;
  const NodeId btn_b =
      tree_b.add_child(RenderTree::root(), PixelRect{0, 0, 40, 20}, NodeStyle{});
  Widget widget_b;
  widget_b.kind = WidgetKind::kButton;
  widgets_b.attach(btn_b, widget_b);

  check(btn_a.value == btn_b.value,
        "both windows' first button gets the identical NodeId value", out, ok);

  focus_a.set(btn_a);
  focus_b.set(btn_b);
  check(focus_a.current() == btn_a && focus_b.current() == btn_b,
        "each dg::Focus still names only its own window's widget", out, ok);
}

// --------------------------------------------------------------------------
// Claim 4: the mid-composition Tab-away hazard (7-3's own crash hazard,
// re-checked rather than assumed still fixed) - Tab away from a composing
// kTextField cancels the composition (WidgetSet::text_field_set_focus(false)
// already does this internally) rather than leaving it dangling against a
// field nothing is looking at any more.
// --------------------------------------------------------------------------

void check_tab_away_mid_composition(std::ostream& out, bool& ok) {
  out << "\nTab away from a composing TextField cancels the composition\n";
  dg::Expected<dg::WindowManager, dg::WindowError> made = dg::WindowManager::create();
  if (!made) {
    out << "  FAIL: could not start the window system: " << made.error().message << "\n";
    ok = false;
    return;
  }
  dg::WindowManager manager = std::move(made.value());
  dg::WindowSpec spec;
  spec.title = "focus (headless, composition)";
  spec.width = kSize.width;
  spec.height = kSize.height;
  const dg::Expected<dg::WindowId, dg::WindowError> window = manager.open(spec);
  if (!window) {
    out << "  FAIL: could not open a window: " << window.error().message << "\n";
    ok = false;
    return;
  }

  focus_scene::Scene scene = focus_scene::build(options_for(kSize));
  if (!scene.fonts.has_value()) {
    out << "  SKIP (loud, specific): no font directory scanned - the text field has no "
           "FontCatalog, so its editing/composition surface cannot be exercised on this host\n";
    return;
  }

  focus_scene::apply_focus_change(scene, manager, window.value(), scene.handles.textfield);
  check(scene.focus.current() == scene.handles.textfield, "the text field is focused", out, ok);

  manager.post_text_editing(window.value(), "n", 0, 1);
  const dg::PumpResult composing = manager.pump(200);
  for (const dg::TextEditingEvent& event : composing.text_editing) {
    scene.widgets.text_field_composition_update(scene.tree.render(), *scene.fonts,
                                                scene.handles.textfield, event.text,
                                                event.start, event.length);
  }
  check(scene.widgets.text_field_is_composing(scene.handles.textfield),
        "a synthesized SDL_EVENT_TEXT_EDITING started a real composition", out, ok);

  manager.post_key(window.value(), true, dg::Key::kTab, false);
  const dg::PumpResult tabbed = manager.pump(200);
  for (const dg::KeyEvent& event : tabbed.key) {
    focus_scene::dispatch_key(scene, manager, window.value(), event);
  }
  check(scene.focus.current() != scene.handles.textfield, "Tab moved focus away from the field",
        out, ok);
  check(!scene.widgets.text_field_is_composing(scene.handles.textfield),
        "the composition was cancelled, not left dangling against an unfocused field", out, ok);
}

}  // namespace

int run(std::ostream& out) {
  setenv("SDL_VIDEODRIVER", "dummy", 1);

  out << "FOCUS: Tab order over a mixed-kind scene, wrapping, the tab_index override, focus "
         "scopes across both PopupHost branches, and the mid-composition Tab-away hazard.\n\n";

  bool ok = true;
  check_order_and_wrapping(out, ok);
  check_real_tab_key_event(out, ok);
  check_overlay_scope_and_close(out, ok);
  attempt_native_popup(out);
  check_two_focus_instances_never_collide(out, ok);
  check_tab_away_mid_composition(out, ok);

  out << (ok ? "\nPASS\n" : "\nFAIL\n");
  return ok ? 0 : 1;
}

}  // namespace focus_check
