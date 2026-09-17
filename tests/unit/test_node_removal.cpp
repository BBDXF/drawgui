// Node removal (8-5): {index, generation} NodeId, RenderTree/LayoutTree
// remove_child(), and node_lifecycle.h's on_node_removed() reaching every
// one of the six NodeId-keyed side tables (WidgetSet, ThemeBindings, Focus,
// AnimationEngine, ActionScopes, Interaction) - see that file's own header
// for why the signature makes every table a required parameter rather than
// an optional one.
//
// No window, no surface - the same in-process RenderTree/LayoutTree
// precedent test_focus.cpp/test_shortcut_routing.cpp already set.

#include <cstdint>
#include <optional>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/anim/animation_engine.h"
#include "drawgui/anim/clock.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/layout/box.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/props/node_props.h"
#include "drawgui/render/prop_ids.generated.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/shortcuts/action_ids.generated.h"
#include "drawgui/shortcuts/action_scopes.h"
#include "drawgui/shortcuts/router.h"
#include "drawgui/theme/theme_bindings.h"
#include "drawgui/theme/token_ids.generated.h"
#include "drawgui/widget/focus.h"
#include "drawgui/widget/interaction.h"
#include "drawgui/widget/widget_set.h"

#include "render/node_lifecycle.h"

namespace {

using dg::ActionScopes;
using dg::AnimationEngine;
using dg::AnimHandle;
using dg::AnimTime;
using dg::BoxStyle;
using dg::Color;
using dg::Focus;
using dg::Interaction;
using dg::LayoutKind;
using dg::LayoutTree;
using dg::NodeId;
using dg::NodeStyle;
using dg::on_node_removed;
using dg::PixelRect;
using dg::PixelSize;
using dg::PropValue;
using dg::RenderTree;
using dg::ThemeBindings;
using dg::TreeSpec;
using dg::Widget;
using dg::WidgetKind;
using dg::WidgetSet;

dg::TreeSpec spec_of() {
  dg::TreeSpec spec;
  spec.viewport = dg::PixelSize{200, 200};
  return spec;
}

// root -> parent -> {child, sibling}, child -> grandchild - deep enough
// that "remove a leaf" and "remove a subtree" are genuinely different
// shapes, and that the former parent (`parent`) keeps one live child
// (`sibling`) after `child`'s subtree is removed, which is what makes
// "exactly one relayout of the former parent" checkable against a
// nonzero-but-small nodes_visited rather than a degenerate empty tree.
struct Scene {
  LayoutTree tree{spec_of()};
  WidgetSet widgets;
  ThemeBindings theme_bindings;
  Focus focus;
  AnimationEngine animation;
  ActionScopes action_scopes;
  Interaction interaction;

  NodeId parent;
  NodeId child;
  NodeId grandchild;
  NodeId sibling;

  Scene() {
    BoxStyle box;
    box.width = 40;
    box.height = 20;
    NodeStyle style;
    style.fill = Color::rgba(0x10, 0x20, 0x30);

    parent = tree.add_child(LayoutTree::root(), box, style);
    child = tree.add_child(parent, box, style);
    grandchild = tree.add_child(child, box, style);
    sibling = tree.add_child(parent, box, style);
    tree.layout_full();
  }
};

}  // namespace

TEST_SUITE("node_removal") {
  TEST_CASE("removing a leaf drops it from its parent's children") {
    Scene scene;
    REQUIRE(scene.tree.render().children(scene.parent).size() == 2);

    CHECK(on_node_removed(scene.tree, scene.grandchild, scene.widgets, scene.theme_bindings,
                          scene.focus, scene.animation, scene.action_scopes,
                          scene.interaction));

    const std::vector<NodeId> child_children = scene.tree.render().children(scene.child);
    CHECK(child_children.empty());
    CHECK_FALSE(scene.tree.render().is_valid(scene.grandchild));

    // Gone from paint and hit testing: RenderTree keeps no live node at
    // that former position for anything to hit any more (whatever
    // absolute_bounds() the tombstoned index reported no longer belongs
    // to any live node) - the leaf's sibling structure is untouched.
    CHECK(scene.tree.render().children(scene.parent).size() == 2);
  }

  TEST_CASE("removing a subtree tombstones every descendant, not just its root") {
    Scene scene;
    const std::vector<NodeId> subtree = dg::subtree_of(scene.tree.render(), scene.child);
    REQUIRE(subtree.size() == 2);  // child, grandchild

    CHECK(on_node_removed(scene.tree, scene.child, scene.widgets, scene.theme_bindings,
                          scene.focus, scene.animation, scene.action_scopes,
                          scene.interaction));

    for (const NodeId id : subtree) {
      CHECK_FALSE(scene.tree.render().is_valid(id));
    }
    REQUIRE(scene.tree.render().children(scene.parent).size() == 1);
    CHECK(scene.tree.render().children(scene.parent).front() == scene.sibling);
  }

  TEST_CASE("a stale NodeId is rejected once its slot is reused") {
    Scene scene;
    const NodeId stale = scene.grandchild;
    REQUIRE(on_node_removed(scene.tree, stale, scene.widgets, scene.theme_bindings, scene.focus,
                            scene.animation, scene.action_scopes, scene.interaction));
    CHECK_FALSE(scene.tree.render().is_valid(stale));

    BoxStyle box;
    box.width = 10;
    box.height = 10;
    const NodeId reused = scene.tree.add_child(scene.child, box, NodeStyle{});

    // The free-list hands the tombstoned slot straight back out - same
    // index, a bumped generation - which is the whole point of the
    // generation counter: the OLD id must not be mistaken for the NEW
    // occupant even though they share a numeric index.
    REQUIRE(reused.index == stale.index);
    CHECK(reused.generation != stale.generation);
    CHECK(scene.tree.render().is_valid(reused));
    CHECK_FALSE(scene.tree.render().is_valid(stale));
    CHECK_FALSE(reused == stale);
  }

  TEST_CASE("a tombstoned slot does not masquerade as the root in an ancestor walk") {
    Scene scene;
    const NodeId stale = scene.grandchild;
    REQUIRE(on_node_removed(scene.tree, stale, scene.widgets, scene.theme_bindings, scene.focus,
                            scene.animation, scene.action_scopes, scene.interaction));

    // The trap: a naively-tombstoned Node{} defaults `parent` to 0 (the
    // root's own index), which would make parent(stale) look exactly like
    // the root rather than like "nothing to climb through at all".
    // RenderTree::parent() must refuse to read that field for an invalid
    // id and hand back `stale` itself instead - the same sentinel the
    // root uses to stop every climb in this codebase.
    CHECK(scene.tree.render().parent(stale) == stale);
    CHECK_FALSE(scene.tree.render().parent(stale) == RenderTree::root());

    // The router's own climb (WidgetSet::owner_of(), the shape
    // resolve_action() bubbles focus through) must terminate immediately
    // rather than silently reaching the root through the stale id's
    // leftover ancestry.
    CHECK_FALSE(scene.widgets.owner_of(scene.tree.render(), stale).has_value());
  }

  TEST_CASE("every one of the six side tables is actually cleaned up") {
    Scene scene;
    const NodeId victim = scene.grandchild;

    Widget widget;
    widget.kind = WidgetKind::kButton;
    scene.widgets.attach(victim, widget);

    scene.focus.set(victim);

    const AnimHandle handle =
        scene.animation.animate(scene.tree, victim, DG_PROP_OPACITY, PropValue::number(1.0F),
                                PropValue::number(0.0F), 1000, dg::kCurveLinear);
    REQUIRE(scene.animation.is_active(handle));

    scene.action_scopes.scope(victim, DG_ACTION_SELECT_ALL);

    ThemeBindings theme_bindings;
    theme_bindings.bind(victim, DG_PROP_BACKGROUND_COLOR, DG_TOKEN_COLOR_SURFACE);

    scene.interaction.moved_over(victim);
    scene.interaction.pressed_on(victim);
    REQUIRE(scene.interaction.hovered() == victim);
    REQUIRE(scene.interaction.holding() == victim);

    REQUIRE(on_node_removed(scene.tree, victim, scene.widgets, theme_bindings, scene.focus,
                            scene.animation, scene.action_scopes, scene.interaction));

    CHECK_FALSE(scene.widgets.has(victim));
    CHECK_FALSE(scene.focus.current() == victim);
    CHECK_FALSE(scene.animation.is_active(handle));
    CHECK_FALSE(scene.action_scopes.consumes(victim, DG_ACTION_SELECT_ALL));
    CHECK(theme_bindings.bindings_for(victim).empty());
    CHECK_FALSE(scene.interaction.hovered() == victim);
    CHECK_FALSE(scene.interaction.holding() == victim);
  }

  TEST_CASE("damage equals the removed subtree's last absolute bounds") {
    Scene scene;
    dg::RasterSurface surface = dg::RasterSurface::create(200, 200).value();
    scene.tree.render().repaint_full(surface);
    REQUIRE(scene.tree.render().damage().is_empty());

    const PixelRect child_bounds = scene.tree.render().absolute_bounds(scene.child);
    const PixelRect grandchild_bounds = scene.tree.render().absolute_bounds(scene.grandchild);

    REQUIRE(on_node_removed(scene.tree, scene.child, scene.widgets, scene.theme_bindings,
                            scene.focus, scene.animation, scene.action_scopes,
                            scene.interaction));

    const PixelRect damaged = scene.tree.render().damage().bounds();
    CHECK(damaged.left() <= child_bounds.left());
    CHECK(damaged.top() <= child_bounds.top());
    CHECK(damaged.right() >= child_bounds.right());
    CHECK(damaged.bottom() >= child_bounds.bottom());
    CHECK(damaged.right() >= grandchild_bounds.right());
    CHECK(damaged.bottom() >= grandchild_bounds.bottom());
  }

  TEST_CASE("exactly one relayout of the former parent") {
    Scene scene;
    // A fresh baseline pass so the incremental cache is fully warm before
    // the removal - otherwise the very first layout() after construction
    // would visit everything regardless of what removal did.
    scene.tree.layout();

    REQUIRE(on_node_removed(scene.tree, scene.child, scene.widgets, scene.theme_bindings,
                            scene.focus, scene.animation, scene.action_scopes,
                            scene.interaction));

    const dg::LayoutStats stats = scene.tree.layout();
    // Exactly one dirty root (the former parent), and it - plus whatever
    // is still under it (only `sibling` now that `child` is gone) - is
    // everything the pass touches: parent and sibling, nothing from the
    // removed subtree and nothing above the former parent.
    CHECK(stats.dirty_roots == 1);
    CHECK(stats.nodes_visited == 2);
    CHECK(stats.nodes_relaid_out == 1);  // parent's own box did not change size
  }

  TEST_CASE("removing the root is refused, not a silent no-op with a surprising result") {
    Scene scene;
    const std::size_t before = scene.tree.render().node_count();
    CHECK_FALSE(on_node_removed(scene.tree, LayoutTree::root(), scene.widgets,
                                scene.theme_bindings, scene.focus, scene.animation,
                                scene.action_scopes, scene.interaction));
    CHECK(scene.tree.render().node_count() == before);
    CHECK(scene.tree.render().is_valid(RenderTree::root()));
    CHECK(scene.tree.render().children(RenderTree::root()).size() == 1);
  }

  TEST_CASE("removing an already-invalid id is refused") {
    Scene scene;
    REQUIRE(on_node_removed(scene.tree, scene.grandchild, scene.widgets, scene.theme_bindings,
                            scene.focus, scene.animation, scene.action_scopes,
                            scene.interaction));
    CHECK_FALSE(on_node_removed(scene.tree, scene.grandchild, scene.widgets,
                                scene.theme_bindings, scene.focus, scene.animation,
                                scene.action_scopes, scene.interaction));
  }
}
