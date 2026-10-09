// WindowId and TimerId on their own, so headers that only name windows and
// timers (drawgui/app/events.h) need not pull in the window manager.

#pragma once

#include <cstdint>

namespace dg {

// Identifies one open window.
//
// A struct rather than a bare integer so that it cannot be passed where a
// count, an index or a size was meant. The value is only meaningful to the
// WindowManager that issued it, and only while that window is open.
struct WindowId {
  std::uint32_t value = 0;

  friend bool operator==(WindowId, WindowId) = default;
};

// A timer created by WindowManager::add_timer(). Never 0 for a live timer,
// never reused within one WindowManager.
struct TimerId {
  std::uint64_t value = 0;

  friend bool operator==(TimerId, TimerId) = default;
};

}  // namespace dg
