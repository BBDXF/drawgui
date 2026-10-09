// dg::App's loop driven directly (no C ABI): tooltip deadlines waking
// pump(-1), timers, Waker from another thread, run()/quit(), and
// destroy_window().

#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

#include <doctest/doctest.h>

#include "runtime_fixture.h"

using runtime_test::of;
using runtime_test::Rt;
namespace ev = dg::events;

namespace {

long long elapsed_ms(std::chrono::steady_clock::time_point since) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now() - since)
      .count();
}

}  // namespace

TEST_SUITE("runtime loop") {
  TEST_CASE("a tooltip deadline wakes pump(-1)") {
    Rt t;
    REQUIRE(t.app.set_tooltip_delay(60) == dg::Status::kOk);
    CHECK(t.app.set_tooltip_delay(-1) == dg::Status::kValueOutOfRange);
    const dg::NodeId box = t.add(dg::NodeKind::kBox);
    (void)t.add(dg::NodeKind::kLabel, box);
    REQUIRE(t.win->set_tooltip(box, "A helpful tip") == dg::Status::kOk);
    (void)t.pump(1);
    const int baseline = t.count();
    // A safety net so a broken deadline fails instead of hanging.
    const dg::TimerId fallback = t.app.add_timer(3000, false);
    REQUIRE(fallback.value != 0);

    t.wm().warp_pointer(t.win->id(), 20, 10);
    const auto start = std::chrono::steady_clock::now();
    while (t.count() == baseline && elapsed_ms(start) < 3000) {
      (void)t.app.pump(-1);
    }
    CHECK(t.count() > baseline);
    CHECK(elapsed_ms(start) < 1500);
    (void)t.app.cancel_timer(fallback);
  }

  TEST_CASE("timers and Waker::post_user_event from another thread") {
    Rt t;
    const dg::TimerId once = t.app.add_timer(0, false);
    REQUIRE(once.value != 0);
    std::vector<ev::TimerEvent> timers;
    for (int i = 0; i < 20 && timers.empty(); ++i) {
      timers = of<ev::TimerEvent>(t.app.pump(50));
    }
    REQUIRE(timers.size() == 1);
    CHECK(timers[0].timer == once);
    CHECK_FALSE(dg::window_of(dg::Event(timers[0])).has_value());

    const dg::Waker waker = t.app.waker();
    const std::uint64_t payload = 0x123456789ABCDEF0ULL;
    std::thread poster([&] {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      CHECK(waker.post_user_event(payload));
    });
    std::vector<ev::UserEvent> user;
    const auto start = std::chrono::steady_clock::now();
    while (user.empty() && elapsed_ms(start) < 5000) {
      user = of<ev::UserEvent>(t.app.pump(-1));
    }
    poster.join();
    REQUIRE(user.size() == 1);
    CHECK(user[0].payload == payload);
  }

  TEST_CASE("run() delivers events and returns once every window is closed") {
    Rt t;
    const dg::Waker waker = t.app.waker();
    std::thread poster([&] { CHECK(waker.post_user_event(7)); });
    poster.join();
    int users = 0;
    bool closed = false;
    t.app.run([&](const dg::Event& event) {
      std::visit(dg::Overloaded{
                     [&](const ev::UserEvent& e) {
                       ++users;
                       CHECK(e.payload == 7);
                       t.wm().request_close(t.win->id());
                     },
                     [&](const ev::WindowClosedEvent& e) {
                       closed = true;
                       CHECK(e.window == t.win->id());
                     },
                     [](const auto& /*other*/) {},
                 },
                 event);
    });
    CHECK(users == 1);
    CHECK(closed);
    CHECK(t.win->closed());
    t.app.run([](const dg::Event& /*event*/) { FAIL("no window is open"); });

    t.app.destroy_window(t.win);
    CHECK(t.app.windows().empty());
  }

  TEST_CASE("quit() ends run(); the rest of the batch stays queued") {
    Rt t;
    const dg::Waker waker = t.app.waker();
    REQUIRE(waker.post_user_event(1));
    REQUIRE(waker.post_user_event(2));
    std::vector<std::uint64_t> seen;
    t.app.run([&](const dg::Event& event) {
      if (const auto* user = dg::event_as<ev::UserEvent>(event)) {
        seen.push_back(user->payload);
        t.app.quit();
      }
    });
    REQUIRE(seen.size() == 1);
    std::vector<ev::UserEvent> rest = of<ev::UserEvent>(t.app.poll());
    for (int i = 0; i < 5 && rest.empty(); ++i) {
      rest = of<ev::UserEvent>(t.app.pump(5));
    }
    REQUIRE(rest.size() == 1);
    CHECK(rest[0].payload != seen[0]);
  }

  TEST_CASE("destroy_window() frees an open window; its queued events outlive it") {
    Rt t;
    dg::WindowOptions options;
    options.title = "second";
    options.width = 100;
    options.height = 80;
    dg::Window* second = t.app.create_window(options).value();
    const dg::WindowId id = second->id();
    REQUIRE(second->create(dg::NodeKind::kButton).has_value());
    (void)t.pump(1);
    REQUIRE(second->focus(*second->root()) == dg::Status::kOk);
    t.app.destroy_window(second);
    CHECK(t.app.find(id) == nullptr);
    CHECK(t.app.windows().size() == 1);
    const std::vector<ev::FocusChangedEvent> focus = of<ev::FocusChangedEvent>(t.pump(1));
    REQUIRE(focus.size() == 1);
    CHECK(focus[0].window == id);
  }
}
