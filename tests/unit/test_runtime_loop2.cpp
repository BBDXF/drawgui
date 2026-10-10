// The runtime loop, part 2: window close confirmation, and animations the
// pump advances (Window::animate()/set_transition()), timed by a fake clock
// through App::set_clock().

#include <chrono>
#include <cstdint>
#include <vector>

#include <doctest/doctest.h>

#include "runtime_fixture.h"

using runtime_test::of;
using runtime_test::Rt;
namespace ev = dg::events;

namespace {

// Starts at the real clock: the pumps before use_clock() ticked on it, and
// an engine's time must not run backwards.
struct FakeClock {
  std::int64_t now = dg::steady_anim_time().ms;
};

void use_clock(Rt& t, FakeClock& clock) {
  t.app.set_clock([&clock] { return dg::AnimTime{clock.now}; });
}

float opacity(Rt& t, dg::NodeId node) {
  return t.win->layout().render().style(node).opacity;
}

dg::NodeId box(Rt& t) {
  const dg::NodeId node = t.add(dg::NodeKind::kBox);
  t.set(node, DG_PROP_WIDTH, dg::PropValue::length(40.0F));
  t.set(node, DG_PROP_HEIGHT, dg::PropValue::length(40.0F));
  return node;
}

}  // namespace

TEST_CASE("close: the title-bar close closes a window by default") {
  Rt t;
  CHECK_FALSE(t.win->close_confirmation());
  t.wm().request_close(t.win->id());
  const std::vector<dg::Event> events = t.pump();
  CHECK(of<ev::CloseRequestedEvent>(events).empty());
  REQUIRE(of<ev::WindowClosedEvent>(events).size() == 1);
  CHECK(t.win->closed());
}

TEST_CASE("close: with confirmation on, a close request is an event and the app decides") {
  Rt t;
  t.win->set_close_confirmation(true);
  t.wm().request_close(t.win->id());
  std::vector<dg::Event> events = t.pump();
  const std::vector<ev::CloseRequestedEvent> requested = of<ev::CloseRequestedEvent>(events);
  REQUIRE(requested.size() == 1);
  CHECK(requested[0].window == t.win->id());
  CHECK(of<ev::WindowClosedEvent>(events).empty());

  // Doing nothing refuses: still open across later pumps, and it paints.
  events = t.pump();
  CHECK_FALSE(t.win->closed());
  CHECK(t.app.find(t.win->id()) == t.win);

  // Accepting: close() closes it and queues WindowClosedEvent.
  t.win->close();
  CHECK(t.win->closed());
  events = t.app.poll();
  REQUIRE(of<ev::WindowClosedEvent>(events).size() == 1);
  t.win->close();
  CHECK(of<ev::WindowClosedEvent>(t.app.poll()).empty());
}

TEST_CASE("close: WindowOptions::close_confirmation turns it on from the start") {
  Rt t;
  dg::WindowOptions options;
  options.title = "second";
  options.width = 120;
  options.height = 90;
  options.close_confirmation = true;
  const auto opened = t.app.create_window(options);
  REQUIRE(opened.has_value());
  dg::Window* second = opened.value();
  CHECK(second->close_confirmation());
  t.wm().request_close(second->id());
  t.wm().request_close(t.win->id());
  const std::vector<dg::Event> events = t.pump();
  REQUIRE(of<ev::CloseRequestedEvent>(events).size() == 1);
  CHECK(of<ev::CloseRequestedEvent>(events)[0].window == second->id());
  CHECK(t.win->closed());
  CHECK_FALSE(second->closed());
  t.app.destroy_window(second);
}

TEST_CASE("animate: the pump advances it on the app clock and reports the end") {
  Rt t;
  FakeClock clock;
  use_clock(t, clock);
  const dg::NodeId node = box(t);
  (void)t.pump(1);

  const auto started = t.win->animate(node, DG_PROP_OPACITY, dg::PropValue::number(0.0F),
                                      dg::PropValue::number(1.0F), 100, dg::kCurveLinear);
  REQUIRE(started.has_value());
  const dg::AnimationId id = started.value();
  CHECK(id > 0);
  CHECK(t.win->animating());

  (void)t.pump(1);
  CHECK(opacity(t, node) == doctest::Approx(0.0F));

  clock.now += 50;
  CHECK(of<ev::AnimationFinishedEvent>(t.pump(1)).empty());
  CHECK(opacity(t, node) == doctest::Approx(0.5F));

  // While animating, even an unbounded wait comes back within a frame.
  const auto before = std::chrono::steady_clock::now();
  (void)t.app.pump(-1);
  CHECK(std::chrono::steady_clock::now() - before < std::chrono::milliseconds(1000));

  clock.now += 60;
  const std::vector<ev::AnimationFinishedEvent> done = of<ev::AnimationFinishedEvent>(t.pump(1));
  REQUIRE(done.size() == 1);
  CHECK(done[0].animation == id);
  CHECK(done[0].node == node);
  CHECK(done[0].window == t.win->id());
  CHECK(opacity(t, node) == doctest::Approx(1.0F));
  CHECK_FALSE(t.win->animating());
  CHECK(t.win->cancel_animation(id) == dg::Status::kUnknownId);
}

TEST_CASE("animate: idle time before an animation starts does not count") {
  Rt t;
  FakeClock clock;
  use_clock(t, clock);
  const dg::NodeId node = box(t);
  (void)t.pump(1);
  clock.now += 10000;  // a long idle wait
  const auto started = t.win->animate(node, DG_PROP_OPACITY, dg::PropValue::number(1.0F),
                                      dg::PropValue::number(0.0F), 200, dg::kCurveLinear);
  REQUIRE(started.has_value());
  clock.now += 100;
  (void)t.pump(1);
  CHECK(opacity(t, node) == doctest::Approx(0.5F));
}

TEST_CASE("animate: `from` defaults to the current value") {
  Rt t;
  FakeClock clock;
  use_clock(t, clock);
  const dg::NodeId node = box(t);
  t.set(node, DG_PROP_OPACITY, dg::PropValue::number(0.2F));
  (void)t.pump(1);
  REQUIRE(t.win->animate(node, DG_PROP_OPACITY, std::nullopt, dg::PropValue::number(1.0F), 100,
                         dg::kCurveLinear)
              .has_value());
  clock.now += 50;
  (void)t.pump(1);
  CHECK(opacity(t, node) == doctest::Approx(0.6F));
}

TEST_CASE("animate: a cancelled animation stays where it is and reports nothing") {
  Rt t;
  FakeClock clock;
  use_clock(t, clock);
  const dg::NodeId node = box(t);
  const auto started = t.win->animate(node, DG_PROP_OPACITY, dg::PropValue::number(0.0F),
                                      dg::PropValue::number(1.0F), 100, dg::kCurveLinear);
  REQUIRE(started.has_value());
  clock.now += 25;
  (void)t.pump(1);
  CHECK(t.win->cancel_animation(started.value()) == dg::Status::kOk);
  CHECK(t.win->cancel_animation(started.value()) == dg::Status::kUnknownId);
  clock.now += 200;
  CHECK(of<ev::AnimationFinishedEvent>(t.pump(2)).empty());
  CHECK(opacity(t, node) == doctest::Approx(0.25F));
  CHECK_FALSE(t.win->animating());
}

TEST_CASE("animate: removing the node cancels its animations") {
  Rt t;
  FakeClock clock;
  use_clock(t, clock);
  const dg::NodeId node = box(t);
  const auto started = t.win->animate(node, DG_PROP_OPACITY, dg::PropValue::number(0.0F),
                                      dg::PropValue::number(1.0F), 100, dg::kCurveLinear);
  REQUIRE(started.has_value());
  REQUIRE(t.win->remove(node) == dg::Status::kOk);
  clock.now += 200;
  CHECK(of<ev::AnimationFinishedEvent>(t.pump(2)).empty());
  CHECK_FALSE(t.win->animating());
  CHECK(t.win->cancel_animation(started.value()) == dg::Status::kUnknownId);
}

TEST_CASE("animate: reduced motion lands at the next pump") {
  Rt t;
  FakeClock clock;
  use_clock(t, clock);
  t.app.set_reduced_motion(true);
  CHECK(t.app.reduced_motion());
  const dg::NodeId node = box(t);
  const auto started = t.win->animate(node, DG_PROP_OPACITY, dg::PropValue::number(0.0F),
                                      dg::PropValue::number(1.0F), 5000, dg::kCurveEaseInOut);
  REQUIRE(started.has_value());
  CHECK(of<ev::AnimationFinishedEvent>(t.pump(1)).size() == 1);
  CHECK(opacity(t, node) == doctest::Approx(1.0F));
}

TEST_CASE("animate: refusals") {
  Rt t;
  const dg::NodeId node = box(t);
  const auto number = dg::PropValue::number(1.0F);
  CHECK(t.win->animate(dg::NodeId{}, DG_PROP_OPACITY, std::nullopt, number, 10).error() ==
        dg::Status::kInvalidNode);
  CHECK(t.win->animate(node, 0xFFFF, std::nullopt, number, 10).error() == dg::Status::kUnknownId);
  CHECK(t.win->animate(node, DG_PROP_OPACITY, std::nullopt,
                       dg::PropValue::color(dg::Color::from_argb(0xFF000000U)), 10)
            .error() == dg::Status::kTypeMismatch);
  CHECK(t.win->animate(node, DG_PROP_OPACITY, std::nullopt, number, -1).error() ==
        dg::Status::kValueOutOfRange);
  CHECK(t.win->animate(node, DG_PROP_OPACITY, std::nullopt, number, 10, 99).error() ==
        dg::Status::kValueOutOfRange);
  CHECK(t.win->set_transition(node, DG_PROP_OPACITY, 10, 0) == dg::Status::kValueOutOfRange);
  CHECK(t.win->cancel_animation(12345) == dg::Status::kUnknownId);
  CHECK_FALSE(t.win->animating());
}

TEST_CASE("set_transition: later writes interpolate, retarget mid-flight, and 0 removes it") {
  Rt t;
  FakeClock clock;
  use_clock(t, clock);
  const dg::NodeId node = box(t);
  t.set(node, DG_PROP_OPACITY, dg::PropValue::number(0.0F));
  REQUIRE(t.win->set_transition(node, DG_PROP_OPACITY, 100, dg::kCurveLinear) == dg::Status::kOk);
  (void)t.pump(1);

  t.set(node, DG_PROP_OPACITY, dg::PropValue::number(1.0F));
  CHECK(t.win->animating());
  clock.now += 50;
  // An implicit transition queues no AnimationFinishedEvent.
  CHECK(of<ev::AnimationFinishedEvent>(t.pump(1)).empty());
  CHECK(opacity(t, node) == doctest::Approx(0.5F));

  // Retarget from the in-flight value, not from the original start.
  t.set(node, DG_PROP_OPACITY, dg::PropValue::number(0.0F));
  clock.now += 50;
  (void)t.pump(1);
  CHECK(opacity(t, node) == doctest::Approx(0.25F));
  clock.now += 60;
  CHECK(of<ev::AnimationFinishedEvent>(t.pump(1)).empty());
  CHECK(opacity(t, node) == doctest::Approx(0.0F));
  CHECK_FALSE(t.win->animating());

  REQUIRE(t.win->set_transition(node, DG_PROP_OPACITY, 0) == dg::Status::kOk);
  t.set(node, DG_PROP_OPACITY, dg::PropValue::number(0.7F));
  CHECK_FALSE(t.win->animating());
  CHECK(opacity(t, node) == doctest::Approx(0.7F));
}

TEST_CASE("set_transition: a colour transition interpolates the fill") {
  Rt t;
  FakeClock clock;
  use_clock(t, clock);
  const dg::NodeId node = box(t);
  t.set(node, DG_PROP_BACKGROUND_COLOR, dg::PropValue::color(dg::Color::from_argb(0xFF000000U)));
  REQUIRE(t.win->set_transition(node, DG_PROP_BACKGROUND_COLOR, 100, dg::kCurveLinear) ==
          dg::Status::kOk);
  t.set(node, DG_PROP_BACKGROUND_COLOR, dg::PropValue::color(dg::Color::from_argb(0xFFFFFFFFU)));
  clock.now += 50;
  (void)t.pump(1);
  const int red = t.win->layout().render().style(node).fill.red();
  CHECK(red > 0x60);
  CHECK(red < 0xA0);
}
