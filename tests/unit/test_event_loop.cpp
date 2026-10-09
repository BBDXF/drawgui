// Event-loop additions on dg::WindowManager: click counts, pointer capture,
// timers and the thread-safe wake. A real manager under SDL's dummy driver;
// time is a hand-advanced fake clock (set_clock()), so nothing here sleeps on
// a deadline.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <thread>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/anim/clock.h"
#include "drawgui/graphics/types.h"
#include "drawgui/widget/interaction.h"
#include "drawgui/window/window_manager.h"

namespace {

using dg::AnimTime;
using dg::PointerAction;
using dg::PointerButton;
using dg::PumpResult;
using dg::TimerId;
using dg::WindowId;
using dg::WindowManager;

std::int64_t g_now_ms = 0;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

AnimTime fake_now() {
  return AnimTime{g_now_ms};
}

WindowManager make_manager() {
  setenv("SDL_VIDEODRIVER", "dummy", 1);
  auto made = WindowManager::create();
  REQUIRE(made.has_value());
  WindowManager manager = std::move(made.value());
  g_now_ms = 1000;
  manager.set_clock(&fake_now);
  return manager;
}

WindowId open_window(WindowManager& manager) {
  dg::WindowSpec spec;
  spec.title = "event loop test";
  spec.width = 64;
  spec.height = 48;
  spec.fill = dg::Color::from_argb(0xFF000000);
  auto opened = manager.open(spec);
  REQUIRE(opened.has_value());
  return opened.value();
}

std::vector<dg::PointerEvent> downs(const PumpResult& result) {
  std::vector<dg::PointerEvent> out;
  for (const dg::PointerEvent& event : result.pointer) {
    if (event.action == PointerAction::kDown) {
      out.push_back(event);
    }
  }
  return out;
}

void click(WindowManager& manager, WindowId id, int x, int y) {
  manager.post_pointer_button(id, true, x, y);
  manager.post_pointer_button(id, false, x, y);
}

}  // namespace

TEST_CASE("pointer: derived click counts follow ClickCounter thresholds") {
  WindowManager manager = make_manager();
  const WindowId id = open_window(manager);
  (void)manager.pump(0);

  click(manager, id, 10, 10);
  PumpResult first = manager.pump(0);
  REQUIRE(first.pointer.size() == 2);
  CHECK(first.pointer[0].clicks == 1);
  CHECK(first.pointer[1].action == PointerAction::kUp);
  CHECK(first.pointer[1].clicks == 1);

  g_now_ms += 200;
  click(manager, id, 11, 11);
  PumpResult second = manager.pump(0);
  REQUIRE(downs(second).size() == 1);
  CHECK(downs(second)[0].clicks == 2);
  CHECK(second.pointer.back().clicks == 2);  // the kUp repeats its press

  g_now_ms += 200;
  click(manager, id, 11, 11);
  CHECK(downs(manager.pump(0))[0].clicks == 3);

  g_now_ms += 2000;  // too slow: restarts
  click(manager, id, 11, 11);
  CHECK(downs(manager.pump(0))[0].clicks == 1);

  g_now_ms += 100;  // too far: restarts
  click(manager, id, 40, 11);
  CHECK(downs(manager.pump(0))[0].clicks == 1);
}

TEST_CASE("pointer: a platform click count is passed through unchanged") {
  WindowManager manager = make_manager();
  const WindowId id = open_window(manager);
  manager.post_pointer_button(id, true, 5, 5, PointerButton::kPrimary, 4);
  manager.post_pointer_button(id, false, 5, 5, PointerButton::kPrimary, 4);
  PumpResult result = manager.pump(0);
  REQUIRE(downs(result).size() == 1);
  CHECK(downs(result)[0].clicks == 4);
  CHECK(result.pointer.back().clicks == 4);
}

TEST_CASE("pointer: counts are per button") {
  WindowManager manager = make_manager();
  const WindowId id = open_window(manager);
  click(manager, id, 5, 5);
  manager.post_pointer_button(id, true, 5, 5, PointerButton::kSecondary);
  manager.post_pointer_button(id, false, 5, 5, PointerButton::kSecondary);
  const auto pressed = downs(manager.pump(0));
  REQUIRE(pressed.size() == 2);
  CHECK(pressed[1].button == PointerButton::kSecondary);
  CHECK(pressed[1].clicks == 1);
}

TEST_CASE("capture: explicit, observable, ends on all-buttons-up, release or close") {
  WindowManager manager = make_manager();
  const WindowId id = open_window(manager);
  const WindowId other = open_window(manager);

  CHECK_FALSE(manager.pointer_capture().has_value());
  CHECK_FALSE(manager.capture_pointer(WindowId{999999}));

  manager.post_pointer_button(id, true, 5, 5);
  (void)manager.pump(0);
  REQUIRE(manager.capture_pointer(id));
  REQUIRE(manager.pointer_capture().has_value());
  CHECK(*manager.pointer_capture() == id);

  // A second button pressed and released keeps the capture: one is still held.
  manager.post_pointer_button(id, true, 5, 5, PointerButton::kSecondary);
  manager.post_pointer_button(id, false, 5, 5, PointerButton::kSecondary);
  (void)manager.pump(0);
  CHECK(manager.pointer_capture().has_value());

  // A release reported outside the window is still delivered, then capture ends.
  manager.post_pointer_button(id, false, -20, 300);
  PumpResult up = manager.pump(0);
  REQUIRE(up.pointer.size() == 1);
  CHECK(up.pointer[0].action == PointerAction::kUp);
  CHECK(up.pointer[0].x == -20);
  CHECK_FALSE(manager.pointer_capture().has_value());

  REQUIRE(manager.capture_pointer(other));
  manager.release_pointer_capture();
  CHECK_FALSE(manager.pointer_capture().has_value());

  REQUIRE(manager.capture_pointer(other));
  manager.close_now(other);
  CHECK_FALSE(manager.pointer_capture().has_value());
}

TEST_CASE("capture: Interaction keeps routing to the held widget outside it") {
  dg::Interaction interaction;
  const dg::NodeId held{3};
  CHECK(interaction.capture_target(std::nullopt) == std::nullopt);
  (void)interaction.pressed_on(held);
  CHECK(interaction.capture_target(std::nullopt) == held);
  CHECK(interaction.capture_target(dg::NodeId{7}) == held);
  const dg::InteractionChange outside = interaction.released_on(std::nullopt);
  CHECK(outside.released == held);
  CHECK_FALSE(outside.clicked.has_value());
  CHECK(interaction.capture_target(dg::NodeId{7}) == dg::NodeId{7});
}

TEST_CASE("timers: ordering, one-shot, repeat and cancel") {
  WindowManager manager = make_manager();

  const TimerId late = manager.add_timer(300);
  const TimerId early = manager.add_timer(100);
  const TimerId tie = manager.add_timer(100);
  const TimerId repeating = manager.add_timer(50, true);
  const TimerId cancelled = manager.add_timer(10);
  CHECK(manager.cancel_timer(cancelled));
  CHECK_FALSE(manager.cancel_timer(cancelled));
  CHECK(early.value != 0);

  CHECK(manager.pump(0).timers.empty());

  g_now_ms += 100;
  PumpResult at100 = manager.pump(0);
  REQUIRE(at100.timers.size() == 3);
  CHECK(at100.timers[0] == repeating);  // due at 50
  CHECK(at100.timers[1] == early);
  CHECK(at100.timers[2] == tie);  // same deadline, created later

  // Rescheduled from when it was found due (100), not replayed for 100.
  g_now_ms += 49;
  CHECK(manager.pump(0).timers.empty());
  g_now_ms += 1;
  PumpResult at150 = manager.pump(0);
  REQUIRE(at150.timers.size() == 1);
  CHECK(at150.timers[0] == repeating);

  g_now_ms += 500;  // several periods late: still once
  PumpResult at650 = manager.pump(0);
  REQUIRE(at650.timers.size() == 2);
  CHECK(at650.timers[0] == repeating);      // due at 1200
  CHECK(at650.timers[1] == late);           // due at 1300
  CHECK_FALSE(manager.cancel_timer(late));  // one-shot already gone

  CHECK(manager.cancel_timer(repeating));
  g_now_ms += 1000;
  CHECK(manager.pump(0).timers.empty());
}

TEST_CASE("timers: pump's wait is cut to the nearest deadline") {
  WindowManager manager = make_manager();
  CHECK(manager.effective_wait_ms(-1) == -1);
  CHECK(manager.effective_wait_ms(500) == 500);

  const TimerId timer = manager.add_timer(200);
  (void)manager.add_timer(900);
  CHECK(manager.effective_wait_ms(-1) == 200);
  CHECK(manager.effective_wait_ms(500) == 200);
  CHECK(manager.effective_wait_ms(50) == 50);
  g_now_ms += 150;
  CHECK(manager.effective_wait_ms(-1) == 50);
  g_now_ms += 100;
  CHECK(manager.effective_wait_ms(-1) == 0);  // overdue: do not sleep
  CHECK(manager.cancel_timer(timer));
  CHECK(manager.effective_wait_ms(-1) == 650);
}

TEST_CASE("timers: a real pump with no limit returns for a due timer") {
  WindowManager manager = make_manager();
  manager.set_clock(nullptr);  // real clock: the wait must actually end
  const TimerId timer = manager.add_timer(20);
  PumpResult result;
  for (int i = 0; i < 50 && result.timers.empty(); ++i) {
    result = manager.pump(-1);
  }
  REQUIRE(result.timers.size() == 1);
  CHECK(result.timers[0] == timer);
}

TEST_CASE("wake: post_user_event and wake from another thread end a blocking pump") {
  WindowManager manager = make_manager();
  (void)manager.pump(0);

  std::atomic<bool> posted{false};
  std::thread poster([&manager, &posted] {
    posted = manager.post_user_event(0x1234'5678'9ABC'DEF0ULL);
    (void)manager.post_user_event(7);
    (void)manager.wake();
    (void)manager.wake();
  });
  poster.join();
  CHECK(posted.load());

  // No timeout: only the posted events can end this wait.
  PumpResult result = manager.pump(-1);
  REQUIRE(result.user_events.size() == 2);
  CHECK(result.user_events[0] == 0x1234'5678'9ABC'DEF0ULL);
  CHECK(result.user_events[1] == 7);
  CHECK(result.woken);

  // Concurrently with a pump that is already blocked.
  std::thread waker([&manager] {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    (void)manager.wake();
  });
  PumpResult woken;
  for (int i = 0; i < 20 && !woken.woken; ++i) {
    woken = manager.pump(-1);
  }
  waker.join();
  CHECK(woken.woken);
  CHECK(woken.user_events.empty());
}

// Moved from examples/23_menu_tooltip_dialog's --verify-menus when that
// example moved to dg::App: the WindowManager-level close-request veto.
TEST_CASE("close request: a cancellable window can veto, a plain one closes at once") {
  WindowManager manager = make_manager();
  dg::WindowSpec plain;
  plain.width = 200;
  plain.height = 150;
  const auto plain_window = manager.open(plain);
  dg::WindowSpec cancellable = plain;
  cancellable.cancellable_close = true;
  const auto cancellable_window = manager.open(cancellable);
  REQUIRE(plain_window.has_value());
  REQUIRE(cancellable_window.has_value());
  CHECK(manager.open_window_count() == 2);

  manager.request_close(plain_window.value());
  manager.request_close(cancellable_window.value());
  const PumpResult pumped = manager.pump(200);
  bool plain_closed = false;
  for (const WindowId id : pumped.closed) {
    plain_closed = plain_closed || id == plain_window.value();
  }
  CHECK(plain_closed);
  bool cancellable_requested = false;
  for (const WindowId id : pumped.close_requested) {
    cancellable_requested = cancellable_requested || id == cancellable_window.value();
  }
  CHECK(cancellable_requested);
  CHECK(manager.open_window_count() == 1);

  // The veto: not calling close_now() leaves it open across another pump.
  const PumpResult after_veto = manager.pump(50);
  CHECK(after_veto.closed.empty());
  CHECK(manager.open_window_count() == 1);

  manager.close_now(cancellable_window.value());
  CHECK(manager.open_window_count() == 0);
}
