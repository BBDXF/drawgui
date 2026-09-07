// Application lifecycle - the T1 contract mobile platforms impose.
//
// design.md section 5.14.7 places pause / resume / low_memory /
// safe_area_changed in T1 rather than among the optional services, and says
// why: on a mobile platform these are not features, they are a mandatory
// contract, and ignoring them gets the process terminated by the OS.
// Desktop backends implement them as no-ops. The interface position is fixed
// now, in the same change that defines IPlatform, because retrofitting a
// lifecycle into a running application is where the retrospective this
// project inherits from says the cost is.
//
// low_memory in particular has a concrete consumer already named: design.md
// section 5.10.3 requires it to shrink the GPU texture cache.

#pragma once

#include <cstdint>
#include <functional>

#include "drawgui/platform/window_desc.h"

namespace dg {

enum class LifecycleEventKind : std::uint8_t {
  // The application is no longer in the foreground. Rendering must stop and
  // the graphics context may be torn down by the OS before kResume arrives.
  kPause,

  kResume,

  // Release what can be rebuilt. Section 5.10.3: this shrinks the texture
  // cache; the render tree and layout results are unaffected, because they
  // hold no GPU resources.
  kLowMemory,

  // The usable region of the window changed - notch, home indicator,
  // on-screen keyboard, rotation.
  kSafeAreaChanged,
};

// Logical pixels of the window's edges that are covered by system chrome and
// must not carry interactive content. Zero on every desktop platform.
struct SafeAreaInsets {
  float left = 0.0F;
  float top = 0.0F;
  float right = 0.0F;
  float bottom = 0.0F;

  friend constexpr bool operator==(const SafeAreaInsets&, const SafeAreaInsets&) = default;
};

struct LifecycleEvent {
  LifecycleEventKind kind = LifecycleEventKind::kPause;

  // The window the event concerns, or an invalid id for an application-wide
  // event. kSafeAreaChanged always names a window; kLowMemory never does.
  WindowId window;

  // Meaningful only for kSafeAreaChanged.
  SafeAreaInsets safe_area;

  friend bool operator==(const LifecycleEvent&, const LifecycleEvent&) = default;
};

// Called on the thread that owns the event loop, never concurrently with
// rendering.
using LifecycleCallback = std::function<void(const LifecycleEvent&)>;

}  // namespace dg
