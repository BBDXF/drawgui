// dg::Focus's own new job: Tab order, scopes, the popup-close/list-recycle
// hazards, and the ring - all over a scene deliberately shaped so a wrong
// answer is VISIBLE rather than a uniform list of identical widgets (the
// "missing scene shape" failure mode .omo/plans/drawgui-kernel.md's own
// catalogue names as the one that bites hardest here): mixed WidgetKinds,
// a container nesting two levels deep, a non-focusable label sandwiched
// between two focusable siblings, a zero-area widget, a negative-tab_index
// widget, and one sibling whose Tab position is the REVERSE of its tree
// position.
//
// No window, no surface: dg::Focus itself is tested exactly the way
// dg::Interaction already is (test_interaction.cpp's own header comment),
// against a plain RenderTree + WidgetSet built in-process.

#include <optional>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/render/render_tree.h"
#include "drawgui/widget/focus.h"
#include "drawgui/widget/widget_set.h"

namespace {

using dg::Color;
using dg::Focus;
using dg::FocusRing;
using dg::NodeId;
using dg::NodeStyle;
using dg::PixelRect;
using dg::RenderTree;
using dg::TreeSpec;
using dg::Widget;
using dg::WidgetKind;
using dg::WidgetSet;

// The scene every test case below shares:
//
//   root
//    |- container1 (kPanel, non-focusable)
//    |    |- button_a     (kButton)     local (10,  5, 80, 30)
//    |    |- label        (kLabel, non-focusable)  local (100, 5, 80, 30)
//    |    |- checkbox_b   (kCheckbox)   local (200, 5, 30, 30)
//    |- container2 (kPanel, offset (0, 50))
//    |    |- slider_c     (kSlider)     local (10,   5, 100, 20) -> abs (10, 55, ...)
//    |    |- textfield_d  (kTextField)  local (150,  5, 120, 30) -> abs (150, 55, ...)
//    |- zero_size_e (kButton), zero area - must never appear in focus_order()
//    |- reversed_f  (kButton, tab_index=1) - last in TREE order, first in TAB order
//    |- negative_g  (kButton, tab_index=-1) - focusable by click, excluded from Tab
//
// Default tab order (container1's children, then container2's, tree order,
// tab_index-unset group) is [button_a, checkbox_b, slider_c, textfield_d];
// reversed_f's positive tab_index sorts it to the FRONT of the whole
// sequence, and negative_g never appears at all - a wrong answer here is
// visibly wrong, not merely a permutation of interchangeable widgets.
struct Scene {
  RenderTree tree;
  WidgetSet widgets;
  NodeId container1;
  NodeId button_a;
  NodeId label;
  NodeId checkbox_b;
  NodeId container2;
  NodeId slider_c;
  NodeId textfield_d;
  NodeId zero_size_e;
  NodeId reversed_f;
  NodeId negative_g;
};

Widget widget(WidgetKind kind, std::optional<int> tab_index = std::nullopt) {
  Widget w;
  w.kind = kind;
  w.tab_index = tab_index;
  return w;
}

Scene build_scene() {
  TreeSpec spec;
  spec.viewport = dg::PixelSize{400, 400};
  Scene scene{RenderTree{spec}, WidgetSet{}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}};

  const NodeId root = RenderTree::root();

  scene.container1 = scene.tree.add_child(root, PixelRect{0, 0, 400, 40}, NodeStyle{});
  scene.button_a =
      scene.tree.add_child(scene.container1, PixelRect{10, 5, 80, 30}, NodeStyle{});
  scene.label = scene.tree.add_child(scene.container1, PixelRect{100, 5, 80, 30}, NodeStyle{});
  scene.checkbox_b =
      scene.tree.add_child(scene.container1, PixelRect{200, 5, 30, 30}, NodeStyle{});

  scene.container2 = scene.tree.add_child(root, PixelRect{0, 50, 400, 40}, NodeStyle{});
  scene.slider_c =
      scene.tree.add_child(scene.container2, PixelRect{10, 5, 100, 20}, NodeStyle{});
  scene.textfield_d =
      scene.tree.add_child(scene.container2, PixelRect{150, 5, 120, 30}, NodeStyle{});

  scene.zero_size_e = scene.tree.add_child(root, PixelRect{0, 0, 0, 0}, NodeStyle{});
  scene.reversed_f = scene.tree.add_child(root, PixelRect{300, 100, 60, 30}, NodeStyle{});
  scene.negative_g = scene.tree.add_child(root, PixelRect{300, 150, 60, 30}, NodeStyle{});

  scene.widgets.attach(scene.container1, widget(WidgetKind::kPanel));
  scene.widgets.attach(scene.button_a, widget(WidgetKind::kButton));
  scene.widgets.attach(scene.label, widget(WidgetKind::kLabel));
  scene.widgets.attach(scene.checkbox_b, widget(WidgetKind::kCheckbox));
  scene.widgets.attach(scene.container2, widget(WidgetKind::kPanel));
  scene.widgets.attach(scene.slider_c, widget(WidgetKind::kSlider));
  scene.widgets.attach(scene.textfield_d, widget(WidgetKind::kTextField));
  scene.widgets.attach(scene.zero_size_e, widget(WidgetKind::kButton));
  scene.widgets.attach(scene.reversed_f, widget(WidgetKind::kButton, 1));
  scene.widgets.attach(scene.negative_g, widget(WidgetKind::kButton, -1));

  return scene;
}

}  // namespace

TEST_SUITE("focus") {
  TEST_CASE("is_focusable() is exhaustive and matches design.md's interactive kinds") {
    CHECK(dg::is_focusable(WidgetKind::kButton));
    CHECK(dg::is_focusable(WidgetKind::kCheckbox));
    CHECK(dg::is_focusable(WidgetKind::kSlider));
    CHECK(dg::is_focusable(WidgetKind::kTextField));
    CHECK_FALSE(dg::is_focusable(WidgetKind::kPanel));
    CHECK_FALSE(dg::is_focusable(WidgetKind::kLabel));
    CHECK_FALSE(dg::is_focusable(WidgetKind::kScrollView));
    CHECK_FALSE(dg::is_focusable(WidgetKind::kList));
  }

  TEST_CASE(
      "focus_order() is tree order by default, skips the non-focusable/zero-area/negative-"
      "tab_index widgets, and puts the positive-tab_index sibling first") {
    Scene scene = build_scene();
    const std::vector<NodeId> order =
        dg::focus_order(scene.tree, scene.widgets, RenderTree::root());

    // reversed_f (tab_index=1) sorts first; the rest are the tab_index-unset
    // group in plain tree order. label (non-focusable), zero_size_e
    // (zero-area) and negative_g (tab_index<0) never appear.
    const std::vector<NodeId> expected{scene.reversed_f, scene.button_a, scene.checkbox_b,
                                       scene.slider_c, scene.textfield_d};
    CHECK(order == expected);
  }

  TEST_CASE("a negative tab_index is still focusable by a direct set(), just not a Tab stop") {
    Scene scene = build_scene();
    Focus focus;
    const dg::FocusChange change = focus.set(scene.negative_g);
    CHECK(change.focused == scene.negative_g);
    CHECK(focus.current() == scene.negative_g);
  }

  TEST_CASE("focus_next()/focus_previous() walk the same order and wrap at both ends") {
    Scene scene = build_scene();
    Focus focus;

    // Nothing focused: Tab goes to the front, Shift-Tab to the back.
    CHECK(focus.focus_next(scene.tree, scene.widgets, RenderTree::root()).focused ==
          scene.reversed_f);
    focus.set(std::nullopt);
    CHECK(focus.focus_previous(scene.tree, scene.widgets, RenderTree::root()).focused ==
          scene.textfield_d);

    focus.set(scene.reversed_f);
    CHECK(focus.focus_next(scene.tree, scene.widgets, RenderTree::root()).focused ==
          scene.button_a);
    CHECK(focus.focus_next(scene.tree, scene.widgets, RenderTree::root()).focused ==
          scene.checkbox_b);
    CHECK(focus.focus_next(scene.tree, scene.widgets, RenderTree::root()).focused ==
          scene.slider_c);
    CHECK(focus.focus_next(scene.tree, scene.widgets, RenderTree::root()).focused ==
          scene.textfield_d);
    // Wraps past the last entry back to the first.
    const dg::FocusChange wrapped =
        focus.focus_next(scene.tree, scene.widgets, RenderTree::root());
    CHECK(wrapped.blurred == scene.textfield_d);
    CHECK(wrapped.focused == scene.reversed_f);

    // Shift-Tab from the front wraps to the back.
    const dg::FocusChange back =
        focus.focus_previous(scene.tree, scene.widgets, RenderTree::root());
    CHECK(back.focused == scene.textfield_d);
  }

  TEST_CASE("is_within() is the ancestor-or-self climb a scope needs, nothing more") {
    Scene scene = build_scene();
    CHECK(dg::is_within(scene.tree, scene.container2, scene.container2));
    CHECK(dg::is_within(scene.tree, scene.container2, scene.slider_c));
    CHECK(dg::is_within(scene.tree, scene.container2, scene.textfield_d));
    CHECK_FALSE(dg::is_within(scene.tree, scene.container2, scene.button_a));
    CHECK_FALSE(dg::is_within(scene.tree, scene.container2, RenderTree::root()));
  }

  TEST_CASE("enter_scope() confines focus_next() to the scope's own subtree") {
    Scene scene = build_scene();
    Focus focus;
    focus.enter_scope(scene.container2);
    CHECK(focus.current_scope() == scene.container2);

    // Tab inside the scope only ever finds slider_c/textfield_d - button_a,
    // checkbox_b and reversed_f (all outside container2) must never appear.
    CHECK(focus.focus_next(scene.tree, scene.widgets, RenderTree::root()).focused ==
          scene.slider_c);
    CHECK(focus.focus_next(scene.tree, scene.widgets, RenderTree::root()).focused ==
          scene.textfield_d);
    // Wraps WITHIN the scope, not out of it.
    CHECK(focus.focus_next(scene.tree, scene.widgets, RenderTree::root()).focused ==
          scene.slider_c);
  }

  TEST_CASE(
      "enter_scope(root, /*modal=*/true) + set_guarded() refuses a target outside the scope "
      "(7-5b's modal focus trap)") {
    Scene scene = build_scene();
    Focus focus;
    focus.enter_scope(scene.container2, /*modal=*/true);
    CHECK(focus.scope_is_modal());
    focus.set(scene.slider_c);

    const dg::FocusChange refused = focus.set_guarded(scene.tree, scene.button_a);
    CHECK_FALSE(refused.any());
    CHECK(focus.current() == scene.slider_c);

    // A target INSIDE the scope is unaffected - set_guarded() behaves
    // exactly like set() there.
    const dg::FocusChange allowed = focus.set_guarded(scene.tree, scene.textfield_d);
    CHECK(allowed.focused == scene.textfield_d);
    CHECK(focus.current() == scene.textfield_d);

    // Blurring to nothing is NOT refused - only a target naming something
    // OUTSIDE the scope is.
    const dg::FocusChange blurred = focus.set_guarded(scene.tree, std::nullopt);
    CHECK(blurred.blurred == scene.textfield_d);
    CHECK_FALSE(focus.current().has_value());
  }

  TEST_CASE(
      "a non-modal scope leaves set_guarded() identical to set() - 7-4's own meaning "
      "unchanged") {
    Scene scene = build_scene();
    Focus focus;
    focus.enter_scope(scene.container2);  // modal defaults to false
    CHECK_FALSE(focus.scope_is_modal());

    const dg::FocusChange change = focus.set_guarded(scene.tree, scene.button_a);
    CHECK(change.focused == scene.button_a);
    CHECK(focus.current() == scene.button_a);
  }

  TEST_CASE(
      "the UNGUARDED set() still moves focus anywhere unconditionally, even under an "
      "active modal scope - set_guarded() is a genuinely separate entry point") {
    Scene scene = build_scene();
    Focus focus;
    focus.enter_scope(scene.container2, /*modal=*/true);
    focus.set(scene.slider_c);

    const dg::FocusChange change = focus.set(scene.button_a);
    CHECK(change.focused == scene.button_a);
    CHECK(focus.current() == scene.button_a);
  }

  TEST_CASE("exit_scope() clears the modal flag along with the scope root") {
    Scene scene = build_scene();
    Focus focus;
    focus.enter_scope(scene.container2, /*modal=*/true);
    focus.exit_scope(scene.tree);
    CHECK_FALSE(focus.scope_is_modal());

    focus.enter_scope(scene.container2);
    CHECK_FALSE(focus.scope_is_modal());
  }
  TEST_CASE(
      "exit_scope() blurs a focused widget still inside the closing scope - the popup-close "
      "hazard") {
    Scene scene = build_scene();
    Focus focus;
    focus.enter_scope(scene.container2);
    focus.set(scene.textfield_d);

    const dg::FocusChange change = focus.exit_scope(scene.tree);
    CHECK(change.blurred == scene.textfield_d);
    CHECK_FALSE(focus.current().has_value());
    CHECK_FALSE(focus.current_scope().has_value());
  }

  TEST_CASE("exit_scope() leaves focus alone when it already points outside the scope") {
    Scene scene = build_scene();
    Focus focus;
    focus.set(scene.button_a);
    focus.enter_scope(scene.container2);

    const dg::FocusChange change = focus.exit_scope(scene.tree);
    CHECK_FALSE(change.any());
    CHECK(focus.current() == scene.button_a);
  }

  TEST_CASE("blur_if_any_of() is the list-recycling hazard: only blurs a name it is given") {
    Scene scene = build_scene();
    Focus focus;
    focus.set(scene.slider_c);

    // A recycle that reassigned OTHER pool slots must not touch focus.
    const dg::FocusChange untouched =
        focus.blur_if_any_of(std::vector<NodeId>{scene.button_a, scene.checkbox_b});
    CHECK_FALSE(untouched.any());
    CHECK(focus.current() == scene.slider_c);

    // A recycle that reassigned THIS slot blurs it.
    const dg::FocusChange blurred =
        focus.blur_if_any_of(std::vector<NodeId>{scene.button_a, scene.slider_c});
    CHECK(blurred.blurred == scene.slider_c);
    CHECK_FALSE(focus.current().has_value());
  }

  TEST_CASE("update_focus_ring() outlines the target with four non-overlapping strips") {
    Scene scene = build_scene();
    FocusRing ring;
    dg::update_focus_ring(scene.tree, RenderTree::root(), ring, scene.button_a,
                          Color::from_argb(0xFFFF8800), /*thickness=*/2, /*gap=*/2);

    CHECK(ring.created);
    // button_a's absolute bounds are (10, 5, 80, 30); outset by gap(2)+
    // thickness(2)=4 for the outer edge, gap(2) alone for the inner edge -
    // hand-derived, not re-derived from the function under test.
    CHECK(scene.tree.local_bounds(ring.top) == (PixelRect{6, 1, 88, 2}));
    CHECK(scene.tree.local_bounds(ring.bottom) == (PixelRect{6, 37, 88, 2}));
    CHECK(scene.tree.local_bounds(ring.left) == (PixelRect{6, 3, 2, 34}));
    CHECK(scene.tree.local_bounds(ring.right) == (PixelRect{92, 3, 2, 34}));

    // None of the four strips overlaps button_a's own bounds - a click on
    // the button must still resolve to the button, never to the ring.
    const PixelRect button_bounds = scene.tree.absolute_bounds(scene.button_a);
    CHECK_FALSE(dg::intersects(scene.tree.local_bounds(ring.top), button_bounds));
    CHECK_FALSE(dg::intersects(scene.tree.local_bounds(ring.bottom), button_bounds));
    CHECK_FALSE(dg::intersects(scene.tree.local_bounds(ring.left), button_bounds));
    CHECK_FALSE(dg::intersects(scene.tree.local_bounds(ring.right), button_bounds));

    // Blurring (target == nullopt) collapses every strip to empty - invisible
    // and, being zero-area, unhittable.
    dg::update_focus_ring(scene.tree, RenderTree::root(), ring, std::nullopt,
                          Color::from_argb(0xFFFF8800));
    CHECK(scene.tree.local_bounds(ring.top).is_empty());
    CHECK(scene.tree.local_bounds(ring.bottom).is_empty());
    CHECK(scene.tree.local_bounds(ring.left).is_empty());
    CHECK(scene.tree.local_bounds(ring.right).is_empty());
  }

  TEST_CASE("two FocusRing instances under the same parent never share a node") {
    Scene scene = build_scene();
    FocusRing base_ring;
    FocusRing popup_ring;
    dg::update_focus_ring(scene.tree, RenderTree::root(), base_ring, scene.button_a,
                          Color::from_argb(0xFFFF8800));
    dg::update_focus_ring(scene.tree, RenderTree::root(), popup_ring, scene.checkbox_b,
                          Color::from_argb(0xFFFF8800));
    CHECK(base_ring.top != popup_ring.top);
    CHECK(base_ring.bottom != popup_ring.bottom);
    CHECK(base_ring.left != popup_ring.left);
    CHECK(base_ring.right != popup_ring.right);
  }
}
