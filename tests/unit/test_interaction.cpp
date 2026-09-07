// The hover / press / click state machine, at its edges.
//
// This is where interaction bugs actually live. The happy path - move on,
// press, release, click - is one line and has never been the problem; what
// breaks is the pointer leaving mid-gesture, two widgets sharing an edge, and
// a press that ends somewhere other than where it began.
//
// No tree, no surface and no window appears below. Interaction takes the
// widget the pointer is over rather than a coordinate, so these cases are
// written directly instead of being staged as scenes and aimed at. Hit testing
// is what turns a coordinate into that widget, and test_hit_test.cpp checks it
// at every pixel; test_widget_pipeline.cpp checks the two composed.

#include <optional>

#include <doctest/doctest.h>

#include "drawgui/render/render_tree.h"
#include "drawgui/widget/interaction.h"

namespace {

using dg::Interaction;
using dg::InteractionChange;
using dg::NodeId;
using dg::PointerState;

constexpr NodeId kA{11};
constexpr NodeId kB{22};
constexpr NodeId kC{33};

std::optional<NodeId> on(NodeId id) {
  return std::optional<NodeId>{id};
}

constexpr std::optional<NodeId> kNothing{};

PointerState idle() {
  return PointerState{false, false};
}
PointerState hovered() {
  return PointerState{true, false};
}
PointerState held() {
  return PointerState{true, true};
}

}  // namespace

TEST_SUITE("interaction") {
  TEST_CASE("a press and a release on the same widget is a click") {
    Interaction interaction;
    CHECK(interaction.moved_over(on(kA)).entered == on(kA));

    const InteractionChange down = interaction.pressed_on(on(kA));
    CHECK(down.pressed == on(kA));
    CHECK_FALSE(down.clicked.has_value());
    CHECK(interaction.state_of(kA) == held());

    const InteractionChange up = interaction.released_on(on(kA));
    CHECK(up.clicked == on(kA));
    CHECK(up.released == on(kA));
    CHECK(interaction.state_of(kA) == hovered());
  }

  TEST_CASE("a release inside the pressed widget does not report a spurious re-entry") {
    Interaction interaction;
    interaction.moved_over(on(kA));
    interaction.pressed_on(on(kA));

    const InteractionChange up = interaction.released_on(on(kA));
    CHECK_FALSE(up.entered.has_value());
    CHECK_FALSE(up.left.has_value());
  }

  TEST_CASE("press inside, drag outside, release does not click and does not stick") {
    Interaction interaction;
    interaction.moved_over(on(kA));
    interaction.pressed_on(on(kA));

    const InteractionChange out = interaction.moved_over(kNothing);
    CHECK(out.left == on(kA));

    // The press is still held - dragging back would still activate it - but
    // the widget must no longer be DRAWN pressed, because the pointer is not
    // on it.
    CHECK(interaction.holding() == on(kA));
    CHECK(interaction.state_of(kA) == idle());

    const InteractionChange up = interaction.released_on(kNothing);
    CHECK_FALSE(up.clicked.has_value());
    CHECK(up.released == on(kA));

    // Nothing stuck: no press held, and no state left saying otherwise.
    CHECK_FALSE(interaction.holding().has_value());
    CHECK(interaction.state_of(kA) == idle());
  }

  TEST_CASE("press inside, drag outside, drag back in, release does click") {
    Interaction interaction;
    interaction.moved_over(on(kA));
    interaction.pressed_on(on(kA));
    interaction.moved_over(kNothing);

    const InteractionChange back = interaction.moved_over(on(kA));
    CHECK(back.entered == on(kA));
    CHECK(interaction.state_of(kA) == held());

    CHECK(interaction.released_on(on(kA)).clicked == on(kA));
  }

  TEST_CASE("releasing over a different widget clicks neither") {
    Interaction interaction;
    interaction.moved_over(on(kA));
    interaction.pressed_on(on(kA));

    const InteractionChange up = interaction.released_on(on(kB));
    CHECK_FALSE(up.clicked.has_value());
    CHECK(up.released == on(kA));

    // B was not hovered during the drag, so releasing over it is B's enter.
    CHECK(up.entered == on(kB));
    CHECK(interaction.state_of(kA) == idle());
    CHECK(interaction.state_of(kB) == hovered());
  }

  TEST_CASE("a press in flight does not hover whatever it is dragged across") {
    Interaction interaction;
    interaction.moved_over(on(kA));
    interaction.pressed_on(on(kA));

    const InteractionChange across = interaction.moved_over(on(kB));
    CHECK(across.left == on(kA));
    CHECK_FALSE(across.entered.has_value());
    CHECK(interaction.state_of(kB) == idle());
    CHECK_FALSE(interaction.hovered().has_value());
  }

  TEST_CASE("the pointer leaving the window clears hover") {
    Interaction interaction;
    interaction.moved_over(on(kA));
    REQUIRE(interaction.state_of(kA) == hovered());

    const InteractionChange gone = interaction.left_window();
    CHECK(gone.left == on(kA));
    CHECK_FALSE(gone.entered.has_value());
    CHECK_FALSE(interaction.hovered().has_value());
    CHECK(interaction.state_of(kA) == idle());
  }

  TEST_CASE("leaving the window mid-press keeps the press but drops the pressed look") {
    Interaction interaction;
    interaction.moved_over(on(kA));
    interaction.pressed_on(on(kA));

    interaction.left_window();
    CHECK(interaction.holding() == on(kA));
    CHECK(interaction.state_of(kA) == idle());

    // Coming back and releasing still activates, which is what every desktop
    // toolkit does and what a user dragging off the edge of the window and
    // back expects.
    interaction.moved_over(on(kA));
    CHECK(interaction.released_on(on(kA)).clicked == on(kA));
  }

  TEST_CASE("leaving the window twice reports the leave once") {
    Interaction interaction;
    interaction.moved_over(on(kA));
    REQUIRE(interaction.left_window().left == on(kA));
    CHECK_FALSE(interaction.left_window().any());
  }

  TEST_CASE("moving between adjacent widgets is exactly one leave and one enter") {
    Interaction interaction;
    interaction.moved_over(on(kA));

    const InteractionChange crossing = interaction.moved_over(on(kB));
    CHECK(crossing.left == on(kA));
    CHECK(crossing.entered == on(kB));

    // Atomic: both sides are in ONE change, so no caller can repaint in a
    // state where neither or both are hovered.
    CHECK(interaction.state_of(kA) == idle());
    CHECK(interaction.state_of(kB) == hovered());
  }

  TEST_CASE("a jump that skips everything in between still reports one leave and one enter") {
    Interaction interaction;
    interaction.moved_over(on(kA));

    // kB is between kA and kC on screen and is never named. A machine that
    // interpolated positions would invent an enter and a leave for it.
    const InteractionChange jump = interaction.moved_over(on(kC));
    CHECK(jump.left == on(kA));
    CHECK(jump.entered == on(kC));
    CHECK(interaction.state_of(kB) == idle());
  }

  TEST_CASE("re-reporting the same widget changes nothing") {
    Interaction interaction;
    interaction.moved_over(on(kA));
    CHECK_FALSE(interaction.moved_over(on(kA)).any());
    CHECK_FALSE(interaction.moved_over(on(kA)).any());
  }

  TEST_CASE("a press with no preceding move still enters the widget first") {
    Interaction interaction;
    const InteractionChange down = interaction.pressed_on(on(kA));
    CHECK(down.entered == on(kA));
    CHECK(down.pressed == on(kA));
    CHECK(interaction.state_of(kA) == held());
  }

  TEST_CASE("pressing on nothing holds nothing and clicks nothing") {
    Interaction interaction;
    const InteractionChange down = interaction.pressed_on(kNothing);
    CHECK_FALSE(down.pressed.has_value());
    CHECK_FALSE(interaction.holding().has_value());

    const InteractionChange up = interaction.released_on(kNothing);
    CHECK_FALSE(up.clicked.has_value());
    CHECK_FALSE(up.released.has_value());
  }

  TEST_CASE("a release with no press outstanding clicks nothing") {
    Interaction interaction;
    interaction.moved_over(on(kA));
    const InteractionChange up = interaction.released_on(on(kA));
    CHECK_FALSE(up.clicked.has_value());
    CHECK_FALSE(up.released.has_value());
  }

  TEST_CASE("two presses without an intervening release do not stack") {
    Interaction interaction;
    interaction.pressed_on(on(kA));
    interaction.pressed_on(on(kB));
    CHECK(interaction.holding() == on(kB));

    // The second press took the grab, so releasing over B activates B and A is
    // left in no state at all.
    CHECK(interaction.released_on(on(kB)).clicked == on(kB));
    CHECK(interaction.state_of(kA) == idle());
  }
}
