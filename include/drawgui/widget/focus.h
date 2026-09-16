// Which widget, if any, currently receives keyboard events, in what order Tab
// visits them, and how a change of focus is shown - design.md section 5.2's
// per-window `FocusManager`, and P4's own acceptance line ("Tab 序正确").
//
// 4-9 built the smallest thing ITS demo needed: one optional NodeId, no
// order, no containment. This slice builds the rest, and the header comment
// that used to say "No tab order, no focus TREE" is corrected here rather
// than left stale - doc/focus.md records the decision in full; the short
// form:
//
// THERE IS NO THIRD TREE. design.md section 5.2 says "焦点树", but asking
// what that noun actually has to DO (Tab order needs a traversal sequence;
// a popup needs a boundary Tab does not cross) finds that `RenderTree`
// already IS the tree Tab order needs - it already exposes `children()` and
// `parent()`, added for 6-3's `dg_dump_layout_tree`, and a pre-order walk of
// it *is* DOM-shaped tab order. The one thing RenderTree does not carry is
// WHICH node bounds a scope, which is exactly one optional NodeId - the same
// "smallest structure that does the job" argument WidgetSet's own header
// makes against a widget tree, and ThemeBindings' own header makes against a
// generation-counter handle. A `Focus` instance stays what 4-9 built - a
// side table over NodeId, not a tree - it is only handed `RenderTree`/
// `WidgetSet` PER CALL, the same way every WidgetSet method already is,
// never storing a reference to either.
//
// STILL ONE FOCUSED WIDGET AT A TIME, still per-window (one `Focus` instance
// per `RenderTree`/window, matching design.md's own per-window
// `FocusManager` literally): a native popup (doc/popup.md) is a genuinely
// separate `RenderTree` in a genuinely separate OS window, so it gets its
// own, separate `Focus` instance - which is also why cross-window focus
// needed NO NodeId-plus-window-id struct here (doc/popup.md section 5's own
// named gap): two `Focus` instances never share a NodeId space, so there is
// nothing to disambiguate. An OVERLAY popup, by contrast, shares the host's
// own `RenderTree` and therefore the host's own `Focus` instance; Tab must
// not let it leak into the parent window's widgets while the popup is open,
// which is what `enter_scope()`/`exit_scope()` below are for.
//
// 7-5b (doc/menus.md section 6.3) grows exactly one more bit onto the SAME
// scope: `enter_scope(root, modal)`'s `modal` flag, and `set_guarded()`
// alongside `set()`, is `Dialog`'s modal focus trap - closing the gap 7-4
// named and deliberately left open ("the smaller mechanism a modal trap
// could be built ON TOP of, not the trap itself"). No second scope concept
// was built: a modal scope IS a Tab scope, with refusal switched on.

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "drawgui/graphics/types.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/widget/widget_set.h"

namespace dg {

// What one focus change touched - the same atomic-transition shape
// InteractionChange already has, for the identical reason: a caller must
// never observe an instant where two widgets are focused, or where the one
// that lost focus has not yet been told to repaint itself unfocused.
struct FocusChange {
  std::optional<NodeId> blurred;
  std::optional<NodeId> focused;

  [[nodiscard]] bool any() const { return blurred.has_value() || focused.has_value(); }
};

// Whether `kind` can receive keyboard focus at all - a property of the
// WidgetKind alone, never of an instance, so this is the exhaustive switch
// every closed-enum kind in this project already gets rather than a runtime
// flag.
//
// kButton/kCheckbox/kSlider/kTextField: yes - each is the kind of thing a
// desktop toolkit puts a tab stop on, and each already has a visible,
// steady-state APPEARANCE that does not depend on the pointer being over it
// (doc/focus.md section 2). kDropdown (7-5, doc/menus.md) joins this list
// for the identical reason kButton is on it - it is an ordinary interactive
// anchor with a steady-state appearance (the currently selected label).
//
// kPanel/kLabel: no - a container and static text carry no interaction at
// all today (WidgetSet's own `interactive()` already excludes both from
// pointer input for the identical reason).
//
// kScrollView/kList: no, DECLINED rather than overlooked. A desktop toolkit
// conventionally makes a scrollable region a tab stop so arrow/Page keys can
// scroll it without a pointer - but this project has no keyboard-scrolling
// code AT ALL yet (doc/scrolling.md section 1 named the precondition,
// design.md section 5.5.1's intent mechanism, as still absent), and this
// project's own standing rule is that an interface is extracted from a
// working implementation, never written ahead of one. Making these
// focusable now would draw a visible ring around a widget that Tab can
// reach but no key can then act on - worse than not reaching it at all.
// doc/focus.md section 3 names this as unblocked-but-not-built.
[[nodiscard]] constexpr bool is_focusable(WidgetKind kind) {
  switch (kind) {
    case WidgetKind::kButton:
    case WidgetKind::kCheckbox:
    case WidgetKind::kSlider:
    case WidgetKind::kTextField:
    case WidgetKind::kDropdown:
      return true;
    case WidgetKind::kPanel:
    case WidgetKind::kLabel:
    case WidgetKind::kScrollView:
    case WidgetKind::kList:
      break;
  }
  return false;
}

// Whether `id` is `scope_root` itself or a descendant of it, walked through
// RenderTree::parent() - the identical ancestor climb WidgetSet::owner_of()
// already uses, one direction. This is the whole of what a "focus scope"
// needs to answer, and the reason a scope is one NodeId rather than a
// second tree: containment is a question RenderTree's own parent links
// already answer.
[[nodiscard]] bool is_within(const RenderTree& tree, NodeId scope_root, NodeId id);

// Every focusable descendant-or-self of `scope_root`, in TAB ORDER -
// DOM-shaped tree order (a pre-order walk of RenderTree::children()) by
// default, reordered by an explicit `Widget::tab_index` override exactly
// the way HTML's own tabindex does:
//
//   - widgets with a POSITIVE tab_index come first, ascending by that
//     value, ties broken by tree order (a caller reordering exactly one
//     widget sets exactly one tab_index rather than renumbering a whole
//     scene);
//   - every other widget (tab_index unset, or exactly 0) follows, in plain
//     tree order;
//   - a NEGATIVE tab_index removes a widget from THIS list - it is still
//     focusable by a direct click (dg::Focus::set() takes any NodeId), it
//     is simply not a Tab/Shift-Tab stop, HTML's own tabindex="-1"
//     convention.
//
// Skipped entirely, at any tab_index: a node with no attached Widget, a
// Widget whose `kind` is_focusable() rejects, and a ZERO-AREA node
// (`RenderTree::absolute_bounds().is_empty()`) - the mechanism that makes a
// popup closed by clipping its container to empty (doc/popup.md section 3)
// or a not-yet-assigned kList pool slot fall out of Tab order for free,
// with no special-case code naming either one.
//
// NOT SKIPPED: `NodeStyle::opacity == 0`. RenderTree::hit_test()'s own
// header records, by name, that hit testing ignores opacity entirely - a
// click still reaches a fully faded widget - and Tab reaching the identical
// set of widgets a click can reach is the CONSISTENT reading of that rule,
// not a divergence from it (doc/focus.md section 3 restates 4-5's own
// argument for why: opacity is a continuous animated quantity with no
// natural threshold to place an interaction cutoff at).
[[nodiscard]] std::vector<NodeId> focus_order(const RenderTree& tree, const WidgetSet& widgets,
                                              NodeId scope_root);

class Focus {
 public:
  // Focuses `target`. Passing std::nullopt blurs whatever is focused without
  // focusing anything new - clicking empty space or a non-focusable widget.
  // Focusing the widget that is already focused reports no change, matching
  // Interaction's identical no-op shape for a redundant hover.
  //
  // Takes NO RenderTree, on purpose - unconditional, exactly the shape 4-9
  // built and every one of this class's existing five call sites (as of
  // 7-4) already assume. `Dialog`'s modal trap (doc/menus.md section 6.3)
  // needed a version that CAN refuse a target outside an active modal
  // scope, and that check needs a tree (is_within() takes one) - rather
  // than thread one through every existing call site to reason about a flag
  // most of them will never set, set_guarded() below is a second entry
  // point that only a modal-aware caller (a Dialog's own dispatch loop)
  // calls; every caller here before 7-5b keeps calling this one, unchanged.
  FocusChange set(std::optional<NodeId> target);

  // set()'s modal-aware sibling (doc/menus.md section 6.3): identical to
  // set(target) UNLESS a modal scope is active (enter_scope(root, true))
  // AND `target` has a value that is NOT is_within() that scope's root, in
  // which case this REFUSES - returns FocusChange{} (no change at all,
  // exactly what set() already returns for a redundant no-op) rather than
  // moving focus outside the trap. Blurring to nothing
  // (target == std::nullopt) is NOT refused - a click on empty space inside
  // the dialog's own window still clears focus the ordinary way; only a
  // target that names something OUTSIDE the modal scope is what this method
  // exists to stop. Every caller that never enters a MODAL scope (a plain
  // popup's enter_scope(root) with modal defaulted to false) sees this
  // behave identically to set() - there is nothing to refuse when
  // scope_modal_ is false.
  FocusChange set_guarded(const RenderTree& tree, std::optional<NodeId> target);

  [[nodiscard]] std::optional<NodeId> current() const { return focused_; }
  [[nodiscard]] bool is_focused(NodeId id) const {
    return focused_.has_value() && *focused_ == id;
  }

  // --- Tab / Shift-Tab, over focus_order() ---

  // Moves focus to the next widget in focus_order(tree, widgets,
  // scope_root(default_root)) after whichever is currently focused, or to
  // the FIRST one when nothing is (a plain Tab into an unfocused window).
  // WRAPS past the last entry back to the first - design.md names no
  // "wrap or stop" choice, and every desktop toolkit this project's own
  // MVP-8 widget set is modelled on wraps, so this does too, named rather
  // than defaulted into silently. A caller wanting Tab to also move the OS
  // window (multi-window Tab-cycling) is out of this slice's scope, matching
  // the "shortcut/intent four-level routing" decline.
  FocusChange focus_next(const RenderTree& tree, const WidgetSet& widgets, NodeId default_root);

  // The identical walk, backwards - Shift-Tab. Wraps past the first entry to
  // the last.
  FocusChange focus_previous(const RenderTree& tree, const WidgetSet& widgets,
                             NodeId default_root);

  // --- Focus scopes: the popup boundary, and (7-5b) the modal trap ---

  // Confines focus_next()/focus_previous() to `root`'s own subtree until the
  // matching exit_scope() - the popup case: Tab inside an open dropdown/menu
  // must not walk back out into the window that opened it.
  //
  // `modal` (default false) is 7-5b's own addition (doc/menus.md section
  // 6.3): false is 7-4's original, unchanged meaning - nothing refuses a
  // direct click or a plain set() from escaping, only Tab/Shift-Tab's own
  // traversal is bounded, so every existing caller (a plain popup/dropdown
  // scope) is unaffected by this parameter existing. true additionally
  // makes set_guarded() (above) refuse a target outside `root` - a
  // `Dialog`'s modal focus trap, composed onto the SAME scope mechanism
  // rather than a second, parallel concept: a modal scope is a Tab scope
  // with one more bit set, not a different kind of thing, because a modal
  // dialog needs Tab confinement too and there is no reason to track the
  // boundary twice.
  //
  // A SINGLE ACTIVE SCOPE, not a stack: PopupHost itself never nests one
  // popup inside another (doc/popup.md section 6, "no second popup ever
  // opens from within a first one" - `PopupHost` "holds no parent-popup
  // relationship"), so a stack of depth greater than one has no working
  // caller to justify it, matching this project's own "extracted from a
  // working implementation" rule. Calling this while a scope is already
  // active replaces it; there is no nested restore.
  void enter_scope(NodeId root, bool modal = false);

  // Leaves the active scope (a no-op if none is active), clearing the modal
  // flag too. If the currently focused widget is `is_within()` the scope's
  // own root, it is BLURRED first - the popup-close hazard the task names by
  // name: an overlay popup's own container is not removed on close, only
  // clipped to an empty rectangle (doc/popup.md section 3), so a widget
  // focus still names is not a dangling NodeId (the node object still
  // exists) but IS one nobody can see, click, or usefully route a keystroke
  // to any more. Leaving Focus pointed at it would keep painting a ring
  // around nothing.
  FocusChange exit_scope(const RenderTree& tree);

  [[nodiscard]] std::optional<NodeId> current_scope() const { return scope_root_; }

  // Whether the active scope (if any) is a modal one - set_guarded()'s own
  // condition, exposed so a caller/test can assert on it directly.
  [[nodiscard]] bool scope_is_modal() const { return scope_modal_; }

  // The root focus_next()/focus_previous() should walk from right now:
  // current_scope() if a scope is active, `default_root` otherwise. Exposed
  // so a caller building focus_order() directly (a test, a diagnostic) uses
  // the identical root Tab itself would.
  [[nodiscard]] NodeId scope_root(NodeId default_root) const {
    return scope_root_.value_or(default_root);
  }

  // --- The list-recycling hazard (doc/list.md section 1's pool nodes are
  // never freed, only reassigned to a different logical item) ---

  // Blurs the current focus if it names any id in `recycled` - the ids
  // WidgetSet::list_sync()/list_scroll_by() just returned as reassigned.
  // 5-3 already made a recycled NodeId safe to HOLD (the pool node is never
  // destroyed); this is what makes it safe to KEEP POINTING FOCUS AT -
  // without this call, Tab-ing to item 3, scrolling, and pressing Space
  // would toggle whatever item now occupies that pool slot, not the one the
  // user was looking at. A caller passes the plain node ids of whichever
  // ListSlot entries changed; this file has no dependency on WidgetSet's
  // kList fields to stay decoupled the way its own header already argues
  // Focus should be.
  FocusChange blur_if_any_of(std::span<const NodeId> recycled);

 private:
  std::optional<NodeId> focused_;
  std::optional<NodeId> scope_root_;
  bool scope_modal_ = false;
};

// --- Focus ring: visible keyboard-focus indication (design.md section 11
// declines a11y IMPLEMENTATION, but a focus ring is not a11y plumbing - it
// is the plain, sighted-keyboard-user affordance that makes Tab itself
// usable, so it belongs to this slice rather than to a future a11y one). ---

// Four thin filled strips - top, bottom, left, right - in the margin OUTSIDE
// a widget's own bounds, never overlapping it.
//
// NOT ONE RECTANGLE DRAWN OVER THE WIDGET. RenderTree::hit_test() has no
// "ignore me" flag (its own header: `Overflow::kClip` and `opacity` are the
// only two things that change what a point resolves to) - a solid ring
// rectangle sized to the widget's own bounds would sit on top of it in
// paint order and would therefore WIN every future hit test against it,
// silently making a focused button unclickable. Four strips confined to the
// margin around the widget - never inside it - cannot have this effect: the
// widget's own bounds are never covered by the ring's bounds.
//
// PERMANENT, LAZILY CREATED, NEVER REMOVED, matching 5-3's list pool and
// PopupHost's own overlay-close precedent (doc/popup.md section 3): a ring
// unused after a scope exits (e.g. a popup's own ring after the popup
// closes) is collapsed to an empty rect rather than deleted, because
// RenderTree has no removal primitive at all.
struct FocusRing {
  NodeId top;
  NodeId bottom;
  NodeId left;
  NodeId right;
  bool created = false;
};

// Creates `ring`'s four strip nodes under `parent` the first time this is
// called (paint order: added AFTER whatever `parent` already holds, so the
// ring always outdraws older siblings - see doc/focus.md section 5 for why
// a NEW FocusRing, not the same one, is created for a popup's own content
// rather than reusing the host window's), then repositions them:
//
//   target.has_value()   outlines `tree.absolute_bounds(*target)`, outset by
//                        `gap` and `thickness` pixels - a visible ring
//                        `thickness` px wide, `gap` px clear of the widget.
//   target == nullopt    collapses all four strips to an empty PixelRect -
//                        invisible and, being empty, unhittable too (the
//                        same "empty rect removes a subtree from painting
//                        and hit testing" rule doc/clipping.md already
//                        established for a closed overlay popup).
//
// `ring_color` is read by the caller from Theme::color_value(
// DG_TOKEN_COLOR_FOCUS_RING, variant) rather than hardcoded - passed in as a
// plain dg::Color rather than resolved in here, because this file has no
// dependency on drawgui/theme (a ring node is exactly the kind of
// paint/positioning-only state TextField's caret and selection_highlight
// already are, and neither of those goes through ThemeBindings/
// dg::set_prop() either - doc/focus.md section 5).
void update_focus_ring(RenderTree& tree, NodeId parent, FocusRing& ring,
                       std::optional<NodeId> target, Color ring_color, int thickness = 2,
                       int gap = 2);

}  // namespace dg
