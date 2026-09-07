// NativeWindow - the deliberate hole in the abstraction.
//
// design.md section 5.14.4 calls this the carrier for T3, the tier of
// platform capability that is either platform-specific or too rarely wanted
// to justify a cross-platform interface: printing, screen capture, per-OS
// APIs. Rather than growing IPlatform for each of them, the host language
// gets the native handle and calls the OS itself.
//
// It is also the input side of embedded mode (section 5.14.5): a host that
// already owns a window hands it to IPlatform::attach_native() and drawgui
// renders into it instead of creating one.
//
// USING THIS LEAVES DRAWGUI'S CROSS-PLATFORM GUARANTEE. The handle's lifetime
// belongs to drawgui, and a host must not hold a reference to it across
// frames. That is the documented risk section 5.14.4 requires be stated
// out loud rather than discovered.

#pragma once

#include <cstdint>

namespace dg {

// Tells the host how to interpret NativeWindow::handle. The list is the one
// design.md section 5.14.4 enumerates.
enum class NativeHandleKind : std::uint8_t {
  kNone,
  kWin32Hwnd,
  kCocoaNsWindow,
  kX11Window,
  kWaylandSurface,
  kAndroidNativeWindow,
  kUikitUiView,
};

struct NativeWindow {
  NativeHandleKind kind = NativeHandleKind::kNone;

  // The OS object itself. For kX11Window this carries an XID rather than a
  // pointer, matching the C ABI shape in design.md section 5.14.4; the host
  // converts it back. Null when kind is kNone.
  void* handle = nullptr;

  // X11 Display* or Wayland wl_display*. Null on every other platform,
  // because nothing else needs a second handle to be usable.
  void* display = nullptr;

  friend constexpr bool operator==(const NativeWindow&, const NativeWindow&) = default;
};

}  // namespace dg
