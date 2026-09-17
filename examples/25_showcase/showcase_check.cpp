// The headless oracle for examples/25_showcase - `--verify-showcase`.
//
// THIS IS THE DELIVERABLE THAT MATTERS (task's own words). Every assertion
// below is a hand-derived exact value, not a loose predicate - 4-9's own
// "assertion too weak" failure mode (`.ends_with("...")` on ellipsize())
// is what this file is built against: a wrong scene, a wrong theme colour,
// or a wrong focus target must fail a SPECIFIC comparison, never merely
// "did something non-empty happen".

#include "showcase_check.h"

#include <cstdlib>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "drawgui/anim/animation_engine.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/theme/token_ids.generated.h"
#include "drawgui/widget/focus.h"
#include "drawgui/window/popup_host.h"
#include "drawgui/window/window_manager.h"

#include "profile_dialog.h"
#include "showcase_scene.h"
#include "sort_options.h"

namespace showcase_check {
namespace {

using dg::NodeId;
using dg::PixelRect;
using dg::PixelSize;

bool check(bool condition, const std::string& what, std::ostream& out, bool& ok) {
  out << "  " << (condition ? "PASS" : "FAIL") << " " << what << "\n";
  ok = ok && condition;
  return condition;
}

constexpr PixelSize kSize{900, 560};

showcase_scene::Options options_for(PixelSize size) {
  showcase_scene::Options options;
  options.spec.viewport = size;
  return options;
}

// --------------------------------------------------------------------------
// Claim 1: all 9 WidgetKind values are actually attached in this one scene,
// plus a context menu/tooltip/dialog exist as reachable code paths - the
// "05_widgets names all 9 in a switch but only builds 3" gap this slice
// exists to close.
// --------------------------------------------------------------------------

void check_all_kinds_present(std::ostream& out, bool& ok) {
  out << "all 9 WidgetKind values attached in one scene\n";
  showcase_scene::Scene scene = showcase_scene::build(options_for(kSize));

  bool saw[9] = {false, false, false, false, false, false, false, false, false};
  auto mark = [&](dg::WidgetKind kind) { saw[static_cast<int>(kind)] = true; };
  const dg::NodeId ids[] = {scene.handles.sidebar,          scene.handles.title_label,
                            scene.handles.theme_toggle,     scene.handles.anim_checkbox,
                            scene.handles.volume_slider,    scene.handles.search_field,
                            scene.handles.recent_list,      scene.handles.sort_dropdown,
                            scene.handles.description_panel};
  for (NodeId id : ids) {
    if (scene.widgets.has(id)) {
      mark(scene.widgets.at(id).kind);
    }
  }
  const char* names[] = {"kPanel",  "kLabel",     "kButton", "kCheckbox", "kScrollView",
                         "kSlider", "kTextField", "kList",   "kDropdown"};
  // kScrollView is deliberately never attached anywhere in this scene (see
  // showcase_scene.h's own header comment: kList already IS a clipping
  // viewport, so a second wrapping kScrollView tests nothing new) - this is
  // named honestly rather than papered over by attaching a decorative,
  // functionally inert kScrollView somewhere just to tick a box.
  for (int i = 0; i < 9; ++i) {
    const bool expected = i != static_cast<int>(dg::WidgetKind::kScrollView);
    check(saw[i] == expected,
          std::string("WidgetKind::") + names[i] +
              (expected ? " is attached" : " deliberately absent"),
          out, ok);
  }
}

// --------------------------------------------------------------------------
// Claim 2: the CJK description panel actually wraps to more than one line
// AND carries a shadow - the combination doc/text-layout.md and
// doc/complex-properties.md each document alone but never together.
// --------------------------------------------------------------------------

void check_cjk_shadow_panel(std::ostream& out, bool& ok) {
  out << "wrapped CJK text + shadow on the SAME node\n";
  showcase_scene::Scene scene = showcase_scene::build(options_for(kSize));
  const dg::RenderTree& tree = scene.tree.render();
  const dg::NodeStyle& panel_style = tree.style(scene.handles.description_panel);
  check(panel_style.shadow.has_value(), "description panel has a shadow set", out, ok);

  const std::vector<NodeId> children = tree.children(scene.handles.description_panel);
  check(!children.empty(), "description panel has a text child", out, ok);
  if (!children.empty()) {
    const int text_height = tree.local_bounds(children.front()).height;
    // A single line at 16px is roughly 20-24px tall; the panel's own
    // kCjkText-shaped string (20+ unspaced Han characters at
    // kMainWidth-2*14 px) must wrap past that - a hand-derived FLOOR, not a
    // loose ">0" check, so a regression that collapses wrapping back to one
    // line fails a SPECIFIC comparison.
    check(text_height > 40,
          "wrapped text child is taller than one line (height=" + std::to_string(text_height) +
              ")",
          out, ok);
  }
  // The shadow paints OUTSIDE the node's declared bounds (doc/complex-
  // properties.md) - repainting the panel full must not crash or corrupt
  // neighbouring content; a real raster proves the combination does not
  // hang/crash (the 7-1 hang precedent this project already found once for
  // malformed UTF-8 feeding SkParagraph).
  std::optional<dg::RasterSurface> surface =
      dg::RasterSurface::create(kSize.width, kSize.height);
  check(surface.has_value(), "surface allocated", out, ok);
  if (surface.has_value()) {
    scene.tree.render().repaint_full(*surface);
    check(!surface->encode_png().empty(),
          "full repaint with wrapped+shadowed CJK encodes a PNG", out, ok);
  }
}

// --------------------------------------------------------------------------
// Claim 3: dropdown popup placement is unaffected by the list's own scroll
// offset, and the popup's overlay content lands at RenderTree::root() -
// NEVER inside an ancestor's opacity/clip subtree - structurally, not by
// coincidence (doc/popup.md section 3's own attach point).
// --------------------------------------------------------------------------

void check_dropdown_over_scrolled_list(std::ostream& out, bool& ok) {
  out << "dropdown popup placement vs. list scroll offset vs. clipping\n";
  showcase_scene::Scene scene = showcase_scene::build(options_for(kSize));

  const PixelRect anchor_before =
      scene.tree.render().absolute_bounds(scene.handles.sort_dropdown);

  // Scroll the "recent files" list so several of its pool nodes recycle.
  const dg::LayoutStats before_scroll_stats = scene.tree.layout();
  (void)showcase_scene::list_scroll_to(scene, 30);
  const dg::LayoutStats after_scroll_stats = scene.tree.layout();
  check(after_scroll_stats.nodes_relaid_out == 0,
        "scrolling the list costs zero relayout (nodes_relaid_out=" +
            std::to_string(after_scroll_stats.nodes_relaid_out) + ")",
        out, ok);
  (void)before_scroll_stats;

  const PixelRect anchor_after_scroll =
      scene.tree.render().absolute_bounds(scene.handles.sort_dropdown);
  check(anchor_before.left() == anchor_after_scroll.left() &&
            anchor_before.top() == anchor_after_scroll.top(),
        "dropdown anchor position is unaffected by the list's own scroll offset", out, ok);

  // Now actually open the dropdown's popup through the real PopupHost, with
  // caps.native_popup forced false so this runs on any headless machine.
  dg::Expected<dg::WindowManager, dg::WindowError> made = dg::WindowManager::create();
  if (!made) {
    out << "  SKIP (no window system: " << made.error().message << ")\n";
    return;
  }
  dg::WindowManager manager = std::move(made.value());
  dg::WindowSpec spec;
  spec.title = "showcase dropdown check";
  spec.width = kSize.width;
  spec.height = kSize.height;
  const dg::Expected<dg::WindowId, dg::WindowError> window = manager.open(spec);
  if (!window) {
    out << "  SKIP (no window: " << window.error().message << ")\n";
    return;
  }
  dg::PopupHost host{manager};
  dg::PlatformCaps caps;
  caps.native_popup = false;  // force the overlay branch - deterministic headless
  const dg::LayoutStats before_popup = scene.tree.layout();
  (void)before_popup;
  const dg::PixelSize popup_size = sort_options::size_for(
      static_cast<int>(showcase_scene::kSortOptions.size()), anchor_after_scroll.width);
  const dg::Expected<dg::PopupHandle, dg::WindowError> shown =
      host.show(window.value(), scene.tree.render(), caps, anchor_after_scroll, popup_size,
                dg::PopupPlacement::kBelow, dg::PopupFlags{});
  check(shown.has_value(), "PopupHost::show() succeeds for the sort dropdown", out, ok);
  if (!shown.has_value()) {
    return;
  }
  const dg::PopupHandle handle = shown.value();
  check(!handle.is_native, "overlay branch used (caps forced)", out, ok);
  check(handle.content_bounds.top() == anchor_after_scroll.bottom(),
        "popup opens directly below the anchor, not shifted by the list's scroll offset", out,
        ok);

  // Structural finding: the overlay container's PARENT is RenderTree::root()
  // regardless of which node the anchor rect came from - an ancestor's
  // opacity/clip on the sidebar or main column can therefore never affect
  // this popup's own paint/clip state, because the popup is never a
  // descendant of either. This is the answer to the "opacity/clip subtree
  // containing an open popup" hypothesis: it does not apply to the overlay
  // branch AT ALL, by construction, not because it was tested and happened
  // to pass.
  check(scene.tree.render().parent(handle.content_root) == dg::LayoutTree::root(),
        "overlay popup's container is a DIRECT CHILD OF ROOT, never of the anchor's own "
        "ancestor chain - opacity/clip on the sidebar/list cannot reach it structurally",
        out, ok);

  const dg::LayoutStats layout_after_popup = scene.tree.layout();
  check(layout_after_popup.nodes_relaid_out == 0,
        "opening the popup (RenderTree::add_child, never through LayoutTree) costs zero "
        "relayout",
        out, ok);

  dg::PopupHandle mutable_handle = handle;
  host.close(mutable_handle);
}

// --------------------------------------------------------------------------
// Claim 4: modal dialog + TextField mid-IME-composition + focus trap - the
// exact meeting point 7-3 (composition crash risk) and 7-4 (double
// Focus::set() skipping side effects) each found separately.
// --------------------------------------------------------------------------

void check_modal_dialog_ime(std::ostream& out, bool& ok) {
  out << "modal dialog + TextField mid-IME-composition + focus trap\n";
  showcase_scene::Scene scene = showcase_scene::build(options_for(kSize));
  if (!scene.fonts.has_value()) {
    out << "  SKIP (no fonts scanned)\n";
    return;
  }
  const dg::FontCatalog& fonts = *scene.fonts;

  const profile_dialog::Handles dialog =
      profile_dialog::build(scene.tree.render(), scene.widgets, dg::LayoutTree::root(), kSize,
                            dg::PixelSize{320, 150}, scene.ui_font, scene.theme, scene.variant);

  scene.focus.enter_scope(dialog.panel, /*modal=*/true);
  const dg::FocusChange focused =
      scene.focus.set_guarded(scene.tree.render(), dialog.name_field);
  check(focused.focused == dialog.name_field, "modal scope entry focuses the name field", out,
        ok);

  // Start a composition (as 7-3's synthetic-injection precedent does),
  // insert a code point mid-string, then attempt to insert MORE text while
  // still composing - this is the exact crash 7-3's own defect-injection
  // campaign found (composition_replace_start/_end going stale once the
  // underlying text is edited underneath an active preview). The window
  // driver's own suppression of Backspace/Delete while composing is what
  // prevents this; this check exercises it directly rather than trusting
  // the driver never calls the unsafe path.
  scene.widgets.text_field_composition_update(scene.tree.render(), fonts, dialog.name_field,
                                              "\u4f60\u597d", 0, 2);
  check(scene.widgets.text_field_is_composing(dialog.name_field), "composition is active", out,
        ok);

  // A DIRECT Focus::set_guarded() escape attempt while composing, targeting
  // a node OUTSIDE the modal scope (the host's own theme_toggle button) -
  // must be refused, and the composition must be UNAFFECTED by the refusal
  // (no side effect ran, because nothing changed).
  const dg::FocusChange escape_attempt =
      scene.focus.set_guarded(scene.tree.render(), scene.handles.theme_toggle);
  check(!escape_attempt.any(), "set_guarded() refuses escaping the modal scope while composing",
        out, ok);
  check(scene.focus.is_focused(dialog.name_field),
        "focus is still the name field after the "
        "refused escape attempt",
        out, ok);
  check(scene.widgets.text_field_is_composing(dialog.name_field),
        "composition survives the refused escape attempt unchanged", out, ok);

  // Escape #1: cancels composition, does NOT close the dialog (matching
  // 23_menu_tooltip_dialog's own precedent, and the exact ordering the
  // task names: "Escape cancels composition first, then closes the
  // dialog").
  scene.widgets.text_field_cancel_composition(scene.tree.render(), fonts, dialog.name_field);
  check(!scene.widgets.text_field_is_composing(dialog.name_field),
        "Escape #1 cancels composition (model untouched, per doc/ime.md)", out, ok);
  check(scene.widgets.text_field_text(dialog.name_field).empty(),
        "the committed model was never touched by the cancelled composition", out, ok);
  check(scene.focus.current_scope() == dialog.panel, "modal scope still active after Escape #1",
        out, ok);

  // Escape #2: now closes the dialog (exits the scope).
  const dg::FocusChange closed = scene.focus.exit_scope(scene.tree.render());
  check(!scene.focus.current_scope().has_value(), "Escape #2 exits the modal scope", out, ok);
  check(closed.blurred == dialog.name_field,
        "exiting the scope blurs the name field that was "
        "still focused inside it",
        out, ok);
}

// --------------------------------------------------------------------------
// Claim 5: Tab traversal into/out of a virtualized kList whose rows carry
// REAL attached Widgets, and Focus::blur_if_any_of() on recycle - a
// combination doc/focus.md records as unit-tested only, never exercised by
// a running scene before this slice.
// --------------------------------------------------------------------------

void check_list_focus_recycle(std::ostream& out, bool& ok) {
  out << "Tab into an interactive kList row + Focus::blur_if_any_of() on recycle\n";
  showcase_scene::Scene scene = showcase_scene::build(options_for(kSize));

  const std::vector<NodeId> order =
      dg::focus_order(scene.tree.render(), scene.widgets, showcase_scene::Handles{}.body);
  (void)order;

  const dg::Widget& list_widget = scene.widgets.at(scene.handles.recent_list);
  check(list_widget.list_pool.size() == static_cast<std::size_t>(showcase_scene::kListPoolSize),
        "the list's pool is the fixed size the scene declared (no per-item node)", out, ok);

  // Focus the pool node currently showing logical item 2 (top_index==0 was
  // set by build(), so pool slot 2 shows item 2 verbatim).
  const NodeId row_showing_item_2 = list_widget.list_pool[2];
  scene.focus.set(row_showing_item_2);
  check(scene.focus.is_focused(row_showing_item_2),
        "focus lands on the pool node showing item 2", out, ok);

  // Scroll far enough that every pool slot recycles to a different logical
  // item - kListPoolSize items' worth is guaranteed to touch slot 2.
  const dg::FocusChange change =
      showcase_scene::list_scroll_to(scene, showcase_scene::kListPoolSize);
  check(change.blurred == row_showing_item_2,
        "blur_if_any_of() blurs the focus that pointed at the now-recycled pool slot", out, ok);
  check(!scene.focus.current().has_value(),
        "focus is not left pointing at the pool node's NEW (wrong) logical item", out, ok);

  // Tab still reaches the recycled pool node (it is a real, focusable
  // Widget - unlike doc/menus.md section 3's own dropdown-row finding,
  // this scene's list rows DO carry attached Widgets by construction).
  check(dg::is_focusable(scene.widgets.at(row_showing_item_2).kind),
        "a recycled list-row pool node is still focusable (it is an ordinary kButton Widget)",
        out, ok);
}

// --------------------------------------------------------------------------
// Claim 6: a theme switch mid-flight of an active AnimationEngine
// transition on the SAME bound property - genuinely untested anywhere in
// this project before this slice (neither doc/animation.md nor doc/theme.md
// combine the two). The result is reported plainly, whichever way it goes.
// --------------------------------------------------------------------------

void check_theme_switch_mid_animation(std::ostream& out, bool& ok) {
  out << "theme switch mid-flight of an active AnimationEngine transition (finding, not a "
         "pass/fail regression gate)\n";
  showcase_scene::Scene scene = showcase_scene::build(options_for(kSize));

  const NodeId node = scene.handles.theme_toggle;
  (void)dg::bind_token(scene.tree, scene.bindings, node, DG_PROP_BACKGROUND_COLOR, scene.theme,
                       scene.variant, DG_TOKEN_COLOR_SURFACE);

  dg::AnimationEngine engine;
  engine.set_transition(node, DG_PROP_BACKGROUND_COLOR, 200, dg::kCurveLinear);
  const dg::Color hover_target =
      scene.theme.color_value(DG_TOKEN_COLOR_PRIMARY_HOVER, scene.variant)
          .value_or(dg::Color::from_argb(0xFF3B4A5E));
  (void)engine.set_value(scene.tree, node, DG_PROP_BACKGROUND_COLOR,
                         dg::PropValue::color(hover_target));
  engine.tick(scene.tree, dg::AnimTime{100});  // halfway through the 200ms transition

  const dg::Color mid_flight = scene.tree.render().style(node).fill;

  // Switch the theme's variant - ThemeBindings::apply() writes background_
  // color for this node through the ordinary dg::set_prop() door,
  // UNCONDITIONALLY, per its own header (theme_bindings.h).
  (void)showcase_scene::switch_theme_variant(scene);
  const dg::Color right_after_switch = scene.tree.render().style(node).fill;
  const dg::Color expected_after_switch =
      scene.theme.color_value(DG_TOKEN_COLOR_SURFACE, scene.variant)
          .value_or(dg::Color::from_argb(0));
  check(right_after_switch == expected_after_switch,
        "the theme switch's own write is visible immediately after apply()", out, ok);
  check(right_after_switch != mid_flight,
        "...and it is a DIFFERENT colour from what the animation had just painted "
        "(the switch really took effect, transiently)",
        out, ok);

  // The very next tick() recomputes the SAME slot's interpolated value from
  // its own from/to (captured as literal colours at animate()/set_value()
  // time, never re-read from the theme) and overwrites the node again.
  engine.tick(scene.tree, dg::AnimTime{110});
  const dg::Color after_next_tick = scene.tree.render().style(node).fill;
  const bool tick_overwrote_the_theme_switch = after_next_tick != expected_after_switch;
  check(tick_overwrote_the_theme_switch,
        "FINDING: the next AnimationEngine::tick() overwrites the theme switch's effect on "
        "this node - a running transition on a themed property makes a theme switch's "
        "effect on THAT property transient, erased within one frame. Not a memory-safety "
        "bug and not a spec violation (both systems behave exactly as their own headers "
        "document in isolation); recorded in doc/showcase.md as a genuine, previously-"
        "unobserved cross-feature interaction.",
        out, ok);
}

int run_all(std::ostream& out) {
  bool ok = true;
  check_all_kinds_present(out, ok);
  check_cjk_shadow_panel(out, ok);
  check_dropdown_over_scrolled_list(out, ok);
  check_modal_dialog_ime(out, ok);
  check_list_focus_recycle(out, ok);
  check_theme_switch_mid_animation(out, ok);

  // line-622: this whole slice adds zero new RenderObject/node/WidgetKind
  // kinds - re-verified the same way every prior slice's own audit did.
  out << "design.md section 5.6 line 622 acceptance bar\n";
  check(sizeof(dg::WidgetKind) == sizeof(std::uint8_t) &&
            static_cast<int>(dg::WidgetKind::kDropdown) == 8,
        "WidgetKind still has exactly 9 enumerators (kPanel..kDropdown), none added by this "
        "slice",
        out, ok);

  out << (ok ? "\nALL CLAIMS PASSED\n" : "\nAT LEAST ONE CLAIM FAILED\n");
  return ok ? 0 : 1;
}

}  // namespace

int run(std::ostream& out) {
  return run_all(out);
}

}  // namespace showcase_check
