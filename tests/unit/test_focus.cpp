// dg::Focus's own new job: Tab order, scopes, the popup-close/list-recycle
// hazards, and the ring - all over a scene deliberately shaped so a wrong
// answer is VISIBLE rather than a uniform list of identical widgets (the
// "missing scene shape" failure mode this project's own catalogue names as
// the one that bites hardest here): mixed WidgetKinds,
// a container nesting two levels deep, a non-focusable label sandwiched
// between two focusable siblings, a zero-area widget, a negative-tab_index
// widget, and one sibling whose Tab position is the REVERSE of its tree
// position.
//
// No window, no surface: dg::Focus itself is tested exactly the way
// dg::Interaction already is (test_interaction.cpp's own header comment),
// against a plain RenderTree + WidgetSet built in-process.

#include <cstddef>
#include <optional>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
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

  TEST_CASE("update_focus_ring() outlines the target with four inward, non-overlapping strips") {
    Scene scene = build_scene();
    FocusRing ring;
    dg::update_focus_ring(scene.tree, RenderTree::root(), ring, scene.button_a,
                          Color::from_argb(0xFFFF8800), /*thickness=*/2, /*gap=*/2);

    CHECK(ring.created);
    // button_a's absolute bounds are (10, 5, 80, 30). Inward geometry ("why
    // inward"): the
    // ring's outer edge is inset gap(2) from that box, its inner edge a
    // further thickness(2) in - hand-derived, not re-derived from the
    // function under test.
    //   outer = bounds.inflated_by(-2)  = {12, 7, 76, 26} -> edges (12,7,88,33)
    //   inner = bounds.inflated_by(-4)  = {14, 9, 72, 22} -> edges (14,9,86,31)
    CHECK(scene.tree.local_bounds(ring.top) == (PixelRect{12, 7, 76, 2}));
    CHECK(scene.tree.local_bounds(ring.bottom) == (PixelRect{12, 31, 76, 2}));
    CHECK(scene.tree.local_bounds(ring.left) == (PixelRect{12, 9, 2, 22}));
    CHECK(scene.tree.local_bounds(ring.right) == (PixelRect{86, 9, 2, 22}));

    // CONTRACT CHANGE, recorded rather than silently rewritten (the same
    // documentation discipline FocusRing's own header comment now names):
    // this used to CHECK_FALSE these four - the old OUTWARD geometry kept
    // every strip clear of button_a's own bounds by construction. The new
    // INWARD geometry puts every strip INSIDE button_a's own bounds on
    // purpose (that is the entire fix: a ring inside the target cannot run
    // off the edge of a flush-edge container the way an outward one did),
    // so all four now intersect. Painted-pixel safety (a click on the
    // button still resolves to the button) is verified separately below
    // by "update_focus_ring() actually paints pixels..." - a ring strip
    // carries no attached Widget, so WidgetSet::owner_of() climbs past it
    // regardless of this geometric overlap; see FocusRing's own header
    // comment for the full argument.
    const PixelRect button_bounds = scene.tree.absolute_bounds(scene.button_a);
    CHECK(dg::intersects(scene.tree.local_bounds(ring.top), button_bounds));
    CHECK(dg::intersects(scene.tree.local_bounds(ring.bottom), button_bounds));
    CHECK(dg::intersects(scene.tree.local_bounds(ring.left), button_bounds));
    CHECK(dg::intersects(scene.tree.local_bounds(ring.right), button_bounds));
    // ...but still fully CONTAINED by it - the ring never spills outside
    // the target it outlines.
    CHECK(dg::contains(button_bounds, scene.tree.local_bounds(ring.top)));
    CHECK(dg::contains(button_bounds, scene.tree.local_bounds(ring.bottom)));
    CHECK(dg::contains(button_bounds, scene.tree.local_bounds(ring.left)));
    CHECK(dg::contains(button_bounds, scene.tree.local_bounds(ring.right)));

    // Blurring (target == nullopt) collapses every strip to empty - invisible
    // and, being empty, unhittable too.
    dg::update_focus_ring(scene.tree, RenderTree::root(), ring, std::nullopt,
                          Color::from_argb(0xFFFF8800));
    CHECK(scene.tree.local_bounds(ring.top).is_empty());
    CHECK(scene.tree.local_bounds(ring.bottom).is_empty());
    CHECK(scene.tree.local_bounds(ring.left).is_empty());
    CHECK(scene.tree.local_bounds(ring.right).is_empty());
  }

  TEST_CASE(
      "update_focus_ring() converts absolute target bounds into a NON-ORIGIN parent's own "
      "local space") {
    // Structural blind spot this closes: every OTHER test in this file uses
    // RenderTree::root() as `parent`, whose absolute origin and
    // scroll_offset are both (0, 0) - so a coordinate-space bug (writing
    // absolute coordinates through set_local_bounds(), which expects
    // local) would be invisible there. This scene deliberately puts a
    // non-origin, non-zero-scroll container between root and the target so
    // that bug is forced to show up as a WRONG absolute_bounds() reading
    // back, not merely a wrong-looking local_bounds().
    Scene scene = build_scene();
    const NodeId container =
        scene.tree.add_child(RenderTree::root(), PixelRect{40, 60, 200, 100}, NodeStyle{});
    scene.tree.set_scroll_offset(container, dg::PixelPoint{5, 3});
    // container's absolute origin is (40, 60); with scroll_offset (5, 3), a
    // child's absolute = child.local.offset_by(40 - 5, 60 - 3) = (+35, +57).
    const NodeId target =
        scene.tree.add_child(container, PixelRect{10, 10, 80, 30}, NodeStyle{});
    REQUIRE(scene.tree.absolute_bounds(target) == (PixelRect{45, 67, 80, 30}));

    FocusRing ring;
    dg::update_focus_ring(scene.tree, container, ring, target, Color::from_argb(0xFFFF8800),
                          /*thickness=*/2, /*gap=*/2);

    // The real invariant: the ring's ABSOLUTE bounds outline the target's
    // absolute bounds, regardless of what coordinate space `parent` sits
    // in. absolute_bounds() re-derives this from whatever local_bounds()
    // was actually written, through the tree's own repositioning - so this
    // assertion is the one that fails if update_focus_ring() ever again
    // writes an absolute rectangle through set_local_bounds() without
    // converting it first.
    //
    // target absolute = (45, 67, 80, 30); outer = inflated_by(-2) =
    // (47, 69, 76, 26) -> edges (47,69,123,95); inner = inflated_by(-4) =
    // (49, 71, 72, 22) -> edges (49,71,121,93).
    CHECK(scene.tree.absolute_bounds(ring.top) == (PixelRect{47, 69, 76, 2}));
    CHECK(scene.tree.absolute_bounds(ring.bottom) == (PixelRect{47, 93, 76, 2}));
    CHECK(scene.tree.absolute_bounds(ring.left) == (PixelRect{47, 71, 2, 22}));
    CHECK(scene.tree.absolute_bounds(ring.right) == (PixelRect{121, 71, 2, 22}));

    // The local_bounds() this implies - container's local-to-absolute
    // offset is (40 - 5, 60 - 3) = (35, 57); undo it to get local from the
    // absolute values just checked.
    CHECK(scene.tree.local_bounds(ring.top) == (PixelRect{12, 12, 76, 2}));
    CHECK(scene.tree.local_bounds(ring.bottom) == (PixelRect{12, 36, 76, 2}));
    CHECK(scene.tree.local_bounds(ring.left) == (PixelRect{12, 14, 2, 22}));
    CHECK(scene.tree.local_bounds(ring.right) == (PixelRect{86, 14, 2, 22}));
  }

  TEST_CASE(
      "update_focus_ring() keeps all four strips visible for a flush-edge, full-width target "
      "(the dropdown-row regression this slice fixes)") {
    // Replicates examples/22_dropdown_menu/dropdown_options.cpp's own row
    // shape exactly: a target that is flush with its container's left and
    // right edges, and flush top-to-bottom against its neighbours, with no
    // margin anywhere around it for an outward ring to occupy.
    TreeSpec spec;
    spec.viewport = dg::PixelSize{200, 140};
    RenderTree tree{spec};
    const NodeId root = RenderTree::root();
    const NodeId row = tree.add_child(root, PixelRect{0, 0, 200, 28}, NodeStyle{});

    FocusRing ring;
    dg::update_focus_ring(tree, root, ring, row, Color::from_argb(0xFFFF8800),
                          /*thickness=*/2, /*gap=*/2);

    const PixelRect viewport_rect{0, 0, 200, 140};
    CHECK(dg::intersects(tree.local_bounds(ring.top), viewport_rect));
    CHECK(dg::intersects(tree.local_bounds(ring.bottom), viewport_rect));
    CHECK(dg::intersects(tree.local_bounds(ring.left), viewport_rect));
    CHECK(dg::intersects(tree.local_bounds(ring.right), viewport_rect));
    // Not merely non-empty - every strip actually has pixels ON the
    // viewport, which the OLD outward geometry could not promise here.
    CHECK_FALSE(tree.local_bounds(ring.top).is_empty());
    CHECK_FALSE(tree.local_bounds(ring.bottom).is_empty());
    CHECK_FALSE(tree.local_bounds(ring.left).is_empty());
    CHECK_FALSE(tree.local_bounds(ring.right).is_empty());

    // Why the OLD geometry failed here, worked by hand as the archival
    // record this slice's own task asked for: outward geometry was
    // `outer = bounds.inflated_by(gap+thickness)`,
    // `inner = bounds.inflated_by(gap)`, with bounds = {0, 0, 200, 28}.
    // The left strip would have been
    // from_edges(outer.left=-4, inner.top=-2, inner.left=-2, inner.
    // bottom=30) = PixelRect{-4, -2, 2, 32} - entirely at x in [-4, -2),
    // off the left edge of a 200-wide surface. Symmetrically the right
    // strip lands at x in [200+2, 200+4). Neither ever painted a pixel,
    // for ANY row, on ANY dropdown - the bug this whole slice is for.
  }

  TEST_CASE(
      "update_focus_ring() actually paints ring-colored pixels on all four sides, and does "
      "not paint over the target's own center") {
    // The layer no existing ring test exercised at all (this file's own
    // header comment: "No window, no surface... against a plain RenderTree
    // + WidgetSet"). Geometry-only assertions cannot see a strip that
    // computes a plausible-looking rectangle sitting entirely off-surface;
    // only a raster readback can.
    TreeSpec spec;
    spec.viewport = dg::PixelSize{100, 60};
    RenderTree tree{spec};
    const NodeId root = RenderTree::root();
    NodeStyle target_style;
    const Color kTargetColor = Color::rgba(0x20, 0x20, 0x20);
    target_style.fill = kTargetColor;
    const NodeId target = tree.add_child(root, PixelRect{10, 10, 60, 30}, target_style);

    FocusRing ring;
    const Color kRingColor = Color::rgba(0xFF, 0x88, 0x00);
    dg::update_focus_ring(tree, root, ring, target, kRingColor, /*thickness=*/2, /*gap=*/2);

    std::optional<dg::RasterSurface> surface =
        dg::RasterSurface::create(tree.viewport().width, tree.viewport().height);
    REQUIRE(surface.has_value());
    tree.repaint_full(*surface);
    const dg::PixelView view = surface->peek_pixels();
    REQUIRE(view.pixels != nullptr);

    auto pixel_at = [&](int x, int y) {
      const std::size_t offset =
          (static_cast<std::size_t>(y) * view.row_bytes) + (static_cast<std::size_t>(x) * 4);
      return Color::rgba(view.pixels[offset + 2], view.pixels[offset + 1], view.pixels[offset]);
    };

    // target absolute bounds (10, 10, 60, 30); outer = inflated_by(-2) =
    // edges (12, 12, 68, 38); inner = inflated_by(-4) = edges (14, 14, 66,
    // 36). One midpoint sample per strip.
    CHECK(pixel_at(/*x=*/40, /*y=*/13) == kRingColor);  // top strip, y in [12,14)
    CHECK(pixel_at(/*x=*/40, /*y=*/37) == kRingColor);  // bottom strip, y in [36,38)
    CHECK(pixel_at(/*x=*/13, /*y=*/25) == kRingColor);  // left strip, x in [12,14)
    CHECK(pixel_at(/*x=*/67, /*y=*/25) == kRingColor);  // right strip, x in [66,68)

    // The ring must not paint over the target's own center - the whole
    // point of a ring rather than a solid overlay.
    CHECK(pixel_at(/*x=*/40, /*y=*/25) == kTargetColor);
    CHECK_FALSE(pixel_at(/*x=*/40, /*y=*/25) == kRingColor);
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

  TEST_CASE(
      "a ring reused across a later-added, non-origin clipping container is painted UNDER "
      "the container (the examples/22_dropdown_menu overlay-ring regression), while a fresh "
      "FocusRing parented at the container itself paints correctly on top") {
    // The real shape: a host ring is created FIRST (examples/22_dropdown_menu's
    // scene_.ring, examples/23_menu_tooltip_dialog's scene_.ring, ...), and
    // only LATER does an overlay - PopupHost::show()'s own Overflow::kClip
    // content_root - get add_child()'d as a new sibling under root, at a
    // non-zero absolute position. RenderTree::Impl::paint_node_and_children
    // walks a node's children in add order (tree_paint.cpp's own doc
    // comment; render/hit_test.cpp's header comment states the same rule
    // from the reading-backwards side) - so the container, added after the
    // ring, paints AFTER it and its own fill covers the ring's strips
    // wherever they overlap. This is exactly the bug commit 48c9cd3 left
    // behind: reusing the host's own FocusRing with the host root as
    // `parent` while the actual target lives inside a popup's content_root.
    TreeSpec spec;
    spec.viewport = dg::PixelSize{200, 160};
    RenderTree tree{spec};
    const NodeId root = RenderTree::root();
    const Color kRingColor = Color::rgba(0xFF, 0x88, 0x00);

    // 1. The host ring, created first, targeting an ordinary host node -
    // exactly like scene_.ring's first-ever update_focus_ring() call at
    // scene build time, long before any popup exists.
    const NodeId host_target =
        tree.add_child(root, PixelRect{10, 10, 60, 24}, NodeStyle{});
    FocusRing host_ring;
    dg::update_focus_ring(tree, root, host_ring, host_target, kRingColor,
                          /*thickness=*/2, /*gap=*/2);

    // 2. The overlay container, added AFTER the ring - PopupHost::show()'s
    // own container_style: Overflow::kClip, non-zero absolute position
    // (content_bounds sits at the popup's own anchor, not the origin).
    NodeStyle container_style;
    container_style.overflow = dg::Overflow::kClip;
    container_style.fill = Color::rgba(0x11, 0x11, 0x11);
    const PixelRect content_bounds{40, 60, 100, 50};
    const NodeId container = tree.add_child(root, content_bounds, container_style);

    // 3. A target row inside the container, matching a dropdown row's own
    // full-width-of-its-container shape.
    NodeStyle row_style;
    row_style.fill = Color::rgba(0x22, 0x22, 0x22);
    const NodeId row = tree.add_child(container, PixelRect{0, 0, 100, 28}, row_style);
    const PixelRect row_absolute = tree.absolute_bounds(row);

    // --- The regression: reusing host_ring, parented at root, retargeted
    // at `row`. host_ring.created is already true, so update_focus_ring()
    // repositions the SAME four nodes created in step 1 - they remain
    // children of root, created before `container`, and therefore still
    // painted before it no matter where they are repositioned to. ---
    dg::update_focus_ring(tree, root, host_ring, row, kRingColor, /*thickness=*/2, /*gap=*/2);

    // Geometry alone looks right - the ring's absolute bounds do wrap the
    // row - which is exactly why this bug survived on pure-geometry
    // assertions until a raster readback was added.
    const PixelRect outer = row_absolute.inflated_by(-2);
    CHECK(tree.absolute_bounds(host_ring.top).top() == outer.top());

    std::optional<dg::RasterSurface> broken_surface =
        dg::RasterSurface::create(tree.viewport().width, tree.viewport().height);
    REQUIRE(broken_surface.has_value());
    tree.repaint_full(*broken_surface);
    const dg::PixelView broken_view = broken_surface->peek_pixels();
    REQUIRE(broken_view.pixels != nullptr);
    auto pixel_at = [](const dg::PixelView& view, int x, int y) {
      const std::size_t offset =
          (static_cast<std::size_t>(y) * view.row_bytes) + (static_cast<std::size_t>(x) * 4);
      return Color::rgba(view.pixels[offset + 2], view.pixels[offset + 1], view.pixels[offset]);
    };

    // The four points a correctly-visible ring would paint ring-colored -
    // one per strip, computed the same way the raster test above does.
    const int top_y = outer.top() + 1;
    const int bottom_y = outer.bottom() - 1;
    const int left_x = outer.left() + 1;
    const int right_x = outer.right() - 1;
    const int mid_x = row_absolute.x + (row_absolute.width / 2);
    const int mid_y = row_absolute.y + (row_absolute.height / 2);

    // THE BUG, CAUGHT: none of the four points is ring-colored. The
    // container's own fill (painted after the stale ring) covers every one
    // of them - this is the "one-frame flash then gone" symptom exactly:
    // geometry says the ring is there, the raster says otherwise.
    CHECK(pixel_at(broken_view, mid_x, top_y) != kRingColor);
    CHECK(pixel_at(broken_view, mid_x, bottom_y) != kRingColor);
    CHECK(pixel_at(broken_view, left_x, mid_y) != kRingColor);
    CHECK(pixel_at(broken_view, right_x, mid_y) != kRingColor);

    // --- The fix: a NEW FocusRing, parented at the container itself. Its
    // four nodes are created (add_child) as children of `container` right
    // now - after `row`, so painted after it (on top, within the
    // container's own clip), and as descendants of `container` they are
    // painted wherever `container` is painted, never before it. ---
    FocusRing popup_ring;
    dg::update_focus_ring(tree, container, popup_ring, row, kRingColor, /*thickness=*/2,
                          /*gap=*/2);

    // Coordinate transform check: `container` sits at a non-origin
    // absolute position (content_bounds above), so update_focus_ring()'s
    // absolute -> parent-local conversion is genuinely exercised here (the
    // .cpp's own comment: "every existing call site today passes a parent
    // whose absolute origin ... [is] (0, 0), making this a no-op - but it
    // is not a no-op in general"). The ring's ABSOLUTE bounds must still
    // wrap the row's absolute bounds correctly despite the non-zero parent
    // origin.
    CHECK(tree.absolute_bounds(popup_ring.top).top() == outer.top());
    CHECK(tree.absolute_bounds(popup_ring.bottom).bottom() == outer.bottom());
    CHECK(tree.absolute_bounds(popup_ring.left).left() == outer.left());
    CHECK(tree.absolute_bounds(popup_ring.right).right() == outer.right());

    std::optional<dg::RasterSurface> fixed_surface =
        dg::RasterSurface::create(tree.viewport().width, tree.viewport().height);
    REQUIRE(fixed_surface.has_value());
    tree.repaint_full(*fixed_surface);
    const dg::PixelView fixed_view = fixed_surface->peek_pixels();
    REQUIRE(fixed_view.pixels != nullptr);

    // THE FIX, CONFIRMED: all four points are now ring-colored - the ring
    // is not merely geometrically correct but actually VISIBLE, unhidden
    // by the container it lives inside.
    CHECK(pixel_at(fixed_view, mid_x, top_y) == kRingColor);
    CHECK(pixel_at(fixed_view, mid_x, bottom_y) == kRingColor);
    CHECK(pixel_at(fixed_view, left_x, mid_y) == kRingColor);
    CHECK(pixel_at(fixed_view, right_x, mid_y) == kRingColor);
  }
}
