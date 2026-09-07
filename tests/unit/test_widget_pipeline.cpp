// Hit testing and the state machine, composed - over the scene that is
// actually on screen.
//
// test_hit_test.cpp proves the traversal at every pixel of a scene built to be
// awkward. test_interaction.cpp proves the state machine at its edges with no
// tree at all. Neither can catch a defect in the JOIN: the climb from the node
// the pointer landed on to the widget that owns it, and the routing that turns
// a platform event into a state transition.
//
// So this file drives examples/05_widgets through its own dispatch() - the
// same function the window loop calls - and asks the questions a user would.

#include <cstdint>
#include <optional>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/widget/interaction.h"
#include "drawgui/window/window_manager.h"

#include "widget_scene.h"

namespace {

using dg::NodeId;
using dg::PixelPoint;
using dg::PixelRect;
using dg::PointerAction;
using dg::PointerEvent;

widget_scene::Scene make_scene(int width = 960, int height = 720) {
  widget_scene::Options options;
  options.spec.viewport = dg::PixelSize{width, height};
  options.spec.background.fill = dg::Color::from_argb(0xFF11161D);
  return widget_scene::build(options);
}

PointerEvent at(PointerAction action, PixelPoint point) {
  return PointerEvent{dg::WindowId{}, action, point.x, point.y};
}

PixelPoint centre_of(const widget_scene::Scene& scene, NodeId id) {
  const PixelRect bounds = scene.tree.bounds(id);
  return PixelPoint{bounds.x + (bounds.width / 2), bounds.y + (bounds.height / 2)};
}

// Pixels of `region` that resolve to something other than `expected`.
// Extracted so the case that sweeps an overlap reads as one assertion rather
// than as a nested loop wrapped around an assertion macro.
std::size_t not_resolving_to(const widget_scene::Scene& scene, const PixelRect& region,
                             NodeId expected) {
  std::size_t wrong = 0;
  for (int y = region.top(); y < region.bottom(); ++y) {
    for (int x = region.left(); x < region.right(); ++x) {
      const std::optional<NodeId> found =
          scene.widgets.widget_at(scene.tree.render(), PixelPoint{x, y});
      wrong += found == std::optional<NodeId>{expected} ? std::size_t{0} : std::size_t{1};
    }
  }
  return wrong;
}

// The first pair of interactive widgets that share an exact edge and a top.
// Searched for rather than named, so that moving the scene around cannot leave
// this case silently testing two widgets that are no longer adjacent.
std::vector<NodeId> adjacent_pair(const widget_scene::Scene& scene) {
  for (const NodeId id : scene.handles.interactive) {
    const PixelRect bounds = scene.tree.bounds(id);
    for (const NodeId other : scene.handles.interactive) {
      const PixelRect neighbour = scene.tree.bounds(other);
      const bool touches =
          other != id && bounds.right() == neighbour.left() && bounds.top() == neighbour.top();
      if (touches) {
        return {id, other};
      }
    }
  }
  return {};
}

}  // namespace

TEST_SUITE("widget pipeline") {
  TEST_CASE("the demo scene resolves a font and puts text on screen") {
    const widget_scene::Scene scene = make_scene();

    // Not a style check. If the font manager silently resolved nothing, every
    // label in the demo would be blank and every other case here would still
    // pass - so the one thing that cannot be inferred from behaviour is
    // asserted directly.
    CHECK(scene.handles.ui_font.is_valid());

    std::size_t with_text = 0;
    for (std::uint32_t index = 0; index < scene.tree.node_count(); ++index) {
      with_text += scene.tree.render().style(NodeId{index}).text.text.empty() ? std::size_t{0}
                                                                              : std::size_t{1};
    }
    CHECK(with_text > 5);
  }

  TEST_CASE("every label in the scene has a box big enough to paint into") {
    const widget_scene::Scene scene = make_scene();

    // This guard exists because two labels in this very scene were built with
    // a grow weight and no height. `grow` is a MAIN-axis share; in a row
    // aligned kCenter a leaf is loosely constrained on the cross axis and
    // shrinks to fit, and sub-step 2 deliberately has no intrinsic sizing, so
    // a string contributes NOTHING and the box collapses to zero height. The
    // label then paints nothing, damages an empty rectangle, and no pixel test
    // can see it - a defect injected into set_text() was caught by absolutely
    // nothing until this was found.
    //
    // A text node with no area is always a mistake, so it is checked directly
    // rather than hoped for.
    for (std::uint32_t index = 0; index < scene.tree.node_count(); ++index) {
      const NodeId id{index};
      if (scene.tree.render().style(id).text.text.empty()) {
        continue;
      }
      const PixelRect bounds = scene.tree.bounds(id);
      CHECK_MESSAGE(!bounds.is_empty(), "text node ", index, " ('",
                    scene.tree.render().style(id).text.text, "') has a ", bounds.width, "x",
                    bounds.height, " box and paints nothing");
    }
  }

  TEST_CASE("clicking a button's label activates the button, not the label") {
    widget_scene::Scene scene = make_scene();
    dg::Interaction interaction;

    const NodeId label = scene.handles.nested_label;
    const NodeId button = scene.handles.nested_owner;
    const PixelPoint point = centre_of(scene, label);

    // The pointer really is over the label: the climb is doing the work, not
    // a lucky miss.
    REQUIRE(scene.tree.render().hit_test(point) == std::optional<NodeId>{label});

    CHECK(widget_scene::dispatch(scene, interaction, at(PointerAction::kMove, point)).entered ==
          std::optional<NodeId>{button});
    widget_scene::dispatch(scene, interaction, at(PointerAction::kDown, point));
    CHECK(widget_scene::dispatch(scene, interaction, at(PointerAction::kUp, point)).clicked ==
          std::optional<NodeId>{button});
  }

  TEST_CASE("a panel takes no clicks") {
    widget_scene::Scene scene = make_scene();
    dg::Interaction interaction;

    const PixelPoint point = centre_of(scene, scene.handles.lab);
    REQUIRE(scene.tree.render().hit_test(point).has_value());
    CHECK_FALSE(scene.widgets.widget_at(scene.tree.render(), point).has_value());

    widget_scene::dispatch(scene, interaction, at(PointerAction::kMove, point));
    widget_scene::dispatch(scene, interaction, at(PointerAction::kDown, point));
    const dg::InteractionChange up =
        widget_scene::dispatch(scene, interaction, at(PointerAction::kUp, point));
    CHECK_FALSE(up.clicked.has_value());
    CHECK(scene.clicks == 0);
  }

  TEST_CASE("only the topmost of two overlapping widgets is hovered") {
    widget_scene::Scene scene = make_scene();
    dg::Interaction interaction;

    const PixelRect under = scene.tree.bounds(scene.handles.lab_under);
    const PixelRect over = scene.tree.bounds(scene.handles.lab_over);
    const PixelRect shared = dg::intersect(under, over);
    REQUIRE_FALSE(shared.is_empty());

    // Every pixel of the overlap, not one sample: an off-by-one in the
    // traversal shows at an edge, and a single centre probe would miss it.
    CHECK(not_resolving_to(scene, shared, scene.handles.lab_over) == 0);

    const PixelPoint inside{shared.x + 1, shared.y + 1};
    widget_scene::dispatch(scene, interaction, at(PointerAction::kMove, inside));
    CHECK(interaction.state_of(scene.handles.lab_over).hovered);
    CHECK_FALSE(interaction.state_of(scene.handles.lab_under).hovered);
  }

  TEST_CASE("a widget spilling out of its parent is clickable where it spills") {
    widget_scene::Scene scene = make_scene();
    dg::Interaction interaction;

    const PixelRect spill = scene.tree.bounds(scene.handles.lab_overflow);
    const PixelRect host = scene.tree.bounds(scene.handles.lab_host);

    // The scene is meant to contain this shape. If a layout change ever stops
    // producing it, this fails loudly instead of quietly testing nothing.
    REQUIRE(spill.right() > host.right());

    const PixelPoint outside{spill.right() - 2, spill.y + (spill.height / 2)};
    REQUIRE_FALSE(dg::contains(host, outside));

    CHECK(scene.widgets.widget_at(scene.tree.render(), outside) ==
          std::optional<NodeId>{scene.handles.lab_overflow});
    widget_scene::dispatch(scene, interaction, at(PointerAction::kMove, outside));
    widget_scene::dispatch(scene, interaction, at(PointerAction::kDown, outside));
    CHECK(widget_scene::dispatch(scene, interaction, at(PointerAction::kUp, outside)).clicked ==
          std::optional<NodeId>{scene.handles.lab_overflow});
  }

  TEST_CASE("crossing between adjacent widgets is one leave and one enter") {
    widget_scene::Scene scene = make_scene();
    dg::Interaction interaction;

    const std::vector<NodeId> adjacent = adjacent_pair(scene);
    REQUIRE(adjacent.size() == 2);

    const PixelRect left = scene.tree.bounds(adjacent[0]);
    const int y = left.top() + (left.height / 2);
    const PixelPoint last_of_left{left.right() - 1, y};
    const PixelPoint first_of_right{left.right(), y};

    widget_scene::dispatch(scene, interaction, at(PointerAction::kMove, last_of_left));
    REQUIRE(interaction.hovered() == std::optional<NodeId>{adjacent[0]});

    const dg::InteractionChange crossing =
        widget_scene::dispatch(scene, interaction, at(PointerAction::kMove, first_of_right));
    CHECK(crossing.left == std::optional<NodeId>{adjacent[0]});
    CHECK(crossing.entered == std::optional<NodeId>{adjacent[1]});
  }

  TEST_CASE("a jump across the window reports one leave and one enter") {
    widget_scene::Scene scene = make_scene();
    dg::Interaction interaction;

    const NodeId first = scene.handles.interactive.front();
    const NodeId last = scene.handles.lab_over;
    widget_scene::dispatch(scene, interaction,
                           at(PointerAction::kMove, centre_of(scene, first)));

    // One event, from one side of the window to the other, with several
    // widgets in between that are never named.
    const dg::InteractionChange jump = widget_scene::dispatch(
        scene, interaction, at(PointerAction::kMove, centre_of(scene, last)));
    CHECK(jump.left == std::optional<NodeId>{first});
    CHECK(jump.entered == std::optional<NodeId>{last});
  }

  TEST_CASE("the checkbox keeps its state across clicks and shows it") {
    widget_scene::Scene scene = make_scene();
    dg::Interaction interaction;

    const NodeId checkbox = scene.handles.checkbox;
    const NodeId indicator = scene.widgets.at(checkbox).indicator;
    const PixelPoint point = centre_of(scene, checkbox);
    const bool before = scene.widgets.is_checked(checkbox);
    const dg::Color mark_before = scene.tree.render().style(indicator).fill;

    widget_scene::dispatch(scene, interaction, at(PointerAction::kMove, point));
    widget_scene::dispatch(scene, interaction, at(PointerAction::kDown, point));
    widget_scene::dispatch(scene, interaction, at(PointerAction::kUp, point));

    CHECK(scene.widgets.is_checked(checkbox) != before);
    CHECK(scene.tree.render().style(indicator).fill != mark_before);

    widget_scene::dispatch(scene, interaction, at(PointerAction::kDown, point));
    widget_scene::dispatch(scene, interaction, at(PointerAction::kUp, point));
    CHECK(scene.widgets.is_checked(checkbox) == before);
    CHECK(scene.tree.render().style(indicator).fill == mark_before);
  }

  TEST_CASE("a press dragged out of the window and released elsewhere clicks nothing") {
    widget_scene::Scene scene = make_scene();
    dg::Interaction interaction;

    const NodeId button = scene.handles.interactive.front();
    const PixelPoint point = centre_of(scene, button);
    const dg::Color resting = scene.tree.render().style(button).fill;

    widget_scene::dispatch(scene, interaction, at(PointerAction::kMove, point));
    widget_scene::dispatch(scene, interaction, at(PointerAction::kDown, point));
    CHECK(scene.tree.render().style(button).fill != resting);

    widget_scene::dispatch(scene, interaction, at(PointerAction::kLeave, PixelPoint{}));

    // The widget must LOOK unpressed the moment the pointer is gone, even
    // though the press is still held.
    CHECK(scene.tree.render().style(button).fill == resting);

    const PixelPoint elsewhere = centre_of(scene, scene.handles.lab);
    widget_scene::dispatch(scene, interaction, at(PointerAction::kMove, elsewhere));
    const dg::InteractionChange up =
        widget_scene::dispatch(scene, interaction, at(PointerAction::kUp, elsewhere));
    CHECK_FALSE(up.clicked.has_value());
    CHECK(scene.clicks == 0);
    CHECK(scene.tree.render().style(button).fill == resting);
  }

  TEST_CASE("a widget that reflows is hit where it moved to, not where it was") {
    widget_scene::Scene scene = make_scene(1120, 800);
    const NodeId pinned = scene.handles.lab_pinned;
    const PixelRect before = scene.tree.bounds(pinned);
    const PixelPoint was{before.x + (before.width / 2), before.y + (before.height / 2)};
    REQUIRE(scene.widgets.widget_at(scene.tree.render(), was) == std::optional<NodeId>{pinned});

    scene.tree.resize(dg::PixelSize{820, 620});
    scene.tree.layout();

    const PixelRect after = scene.tree.bounds(pinned);

    // The scene is meant to contain a widget that MOVES. Everything else in it
    // is a fixed size at a fixed offset and sits still through a reflow, which
    // would make this case pass without testing anything.
    REQUIRE(after != before);

    const PixelPoint now{after.x + (after.width / 2), after.y + (after.height / 2)};
    CHECK(scene.widgets.widget_at(scene.tree.render(), now) == std::optional<NodeId>{pinned});

    // And the rectangle it vacated does not still answer for it. There is no
    // stored hit geometry to go stale - hit testing reads the live tree - and
    // this is what says so.
    CHECK(scene.widgets.widget_at(scene.tree.render(), was) != std::optional<NodeId>{pinned});
  }

  TEST_CASE("hover follows the widget when a resize moves it under a still pointer") {
    widget_scene::Scene scene = make_scene(960, 720);
    dg::Interaction interaction;

    const PixelPoint point = centre_of(scene, scene.handles.lab_pinned);
    widget_scene::dispatch(scene, interaction, at(PointerAction::kMove, point));
    REQUIRE(interaction.hovered() == std::optional<NodeId>{scene.handles.lab_pinned});

    // A resize the pointer did not follow. No platform sends a motion event
    // for this, so without resync() the hover would still name whatever used
    // to be here.
    scene.tree.resize(dg::PixelSize{640, 480});
    scene.tree.layout();
    widget_scene::resync(scene, interaction, point);

    const std::optional<NodeId> now = scene.widgets.widget_at(scene.tree.render(), point);
    CHECK(interaction.hovered() == now);

    // And it agrees with a fresh hit test, which is the whole claim: there is
    // no cached hit rectangle that could have gone stale.
    for (const NodeId id : scene.handles.interactive) {
      CHECK(interaction.state_of(id).hovered == (now == std::optional<NodeId>{id}));
    }
  }
}
