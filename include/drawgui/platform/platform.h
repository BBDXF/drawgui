// IPlatform - everything drawgui needs from the machine it is running on.
//
// This is layer 1 of design.md section 4 and the whole of constraint C1:
// windowing, the event loop, file watching, lifecycle and the clipboard are
// abstracted on day one so that no `#ifdef __linux__` ever reaches the layers
// above. The retrospective this project inherits from records inotify and
// POSIX signals as what locked its predecessor to Linux permanently - not
// because either was a bad choice, but because by the time it mattered they
// were load-bearing.
//
// The interface is defined before any backend exists, deliberately. An
// interface extracted after the fact is shaped by whichever platform's API
// went in first, and every later platform then arrives as a special case.
//
// The method set follows section 5.1. It is held to T1 (section 5.14.1):
// present everywhere, and the GUI does not work without it. Optional
// capability is a T2 service behind query_service<T>(), which section 5.14.2
// requires be answerable at query time rather than by trying and failing.

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "drawgui/platform/caps.h"
#include "drawgui/platform/display.h"
#include "drawgui/platform/error.h"
#include "drawgui/platform/file_watch.h"
#include "drawgui/platform/frame.h"
#include "drawgui/platform/lifecycle.h"
#include "drawgui/platform/native_window.h"
#include "drawgui/platform/service.h"
#include "drawgui/platform/types.h"
#include "drawgui/platform/window.h"
#include "drawgui/platform/window_desc.h"

namespace dg {

// Who owns the event loop. design.md section 5.14.5 makes this a property of
// the platform instance rather than of each call, because it decides three
// separate things - whether drawgui creates windows, when it renders, and
// where input comes from - and a per-call answer would let those three
// disagree.
//
// It is queryable for the same reason services are (section 5.14.2): a caller
// should be able to ask, not try.
enum class LoopMode : std::uint8_t {
  // drawgui owns the loop: run_loop() blocks, wake() interrupts it. The mode
  // the MVP implements.
  kStandalone,

  // The host owns the loop: it calls pump_once(), IWindow::inject_event() and
  // IWindow::render_now(). drawgui creates no windows of its own and renders
  // into surfaces attached with attach_native().
  kEmbedded,
};

class IPlatform : public IServiceProvider {
 public:
  IPlatform(const IPlatform&) = delete;
  IPlatform& operator=(const IPlatform&) = delete;
  IPlatform(IPlatform&&) = delete;
  IPlatform& operator=(IPlatform&&) = delete;

  ~IPlatform() override = default;

  // --- Capability -----------------------------------------------------------

  [[nodiscard]] virtual PlatformCaps capabilities() const = 0;
  [[nodiscard]] virtual LoopMode loop_mode() const = 0;

  // --- Windows --------------------------------------------------------------

  // The platform owns every window it creates. On success the pointer is
  // never null, and it stays valid until destroy_window() is called for that
  // id or the platform itself is destroyed.
  //
  // Returns kUnsupported when a second window is asked for and
  // PlatformCaps::multi_window is false, and kWrongLoopMode in embedded mode,
  // where the host provides surfaces instead.
  [[nodiscard]] virtual PlatformResult<IWindow*> create_window(const WindowDesc& desc) = 0;

  // Embedded mode's counterpart to create_window: drawgui renders into a
  // surface the host already owns (design.md sections 5.14.4 and 5.14.5). The
  // descriptor supplies the parts that are still drawgui's to decide; its
  // size and position fields are ignored, because the host's window has both
  // already.
  [[nodiscard]] virtual PlatformResult<IWindow*> attach_native(const NativeWindow& native,
                                                               const WindowDesc& desc) = 0;

  virtual PlatformResult<void> destroy_window(WindowId id) = 0;

  // Returns nullptr when the id names no live window. Input events carry a
  // WindowId (design.md section 5.2) and an event can outlive the window it
  // names, so this is a lookup rather than an assertion.
  [[nodiscard]] virtual IWindow* find_window(WindowId id) = 0;

  // --- Event loop, standalone mode ------------------------------------------

  // Runs until quit(). Returns kWrongLoopMode in embedded mode.
  //
  // design.md section 5.15.1: with no dirty window and no running animation
  // this blocks rather than spinning, so an idle application costs 0% CPU.
  // That is the line between a GUI toolkit and a game engine, and it is why
  // wake() has to exist at all.
  [[nodiscard]] virtual PlatformResult<void> run_loop() = 0;

  // Interrupts a blocked run_loop(). The one method here that may be called
  // from another thread - it is how a worker (design.md section 5.10.3's
  // decode pool) tells the UI thread there is something to do.
  virtual void wake() = 0;

  // Asks run_loop() to return. Idempotent; safe to call before run_loop()
  // starts, in which case run_loop() returns immediately.
  virtual void quit() = 0;

  // --- Event loop, embedded mode --------------------------------------------

  // Processes whatever input and platform events are pending and returns
  // without blocking. Returns kWrongLoopMode in standalone mode.
  virtual PlatformResult<void> pump_once() = 0;

  // --- Displays -------------------------------------------------------------

  // Every connected display, primary first. Empty only if the platform has no
  // display at all.
  [[nodiscard]] virtual std::vector<DisplayInfo> displays() const = 0;

  // --- Clipboard ------------------------------------------------------------
  //
  // Plain text only: design.md section 5.14.1 puts the text clipboard in T1
  // and rich formats in T2, because text is the part every target platform
  // agrees on.

  [[nodiscard]] virtual PlatformResult<std::string> clipboard_text() const = 0;
  virtual PlatformResult<void> set_clipboard_text(std::string_view text) = 0;

  // --- File watching --------------------------------------------------------

  // Watches each path - a file or a directory - and reports changes beneath
  // it. See file_watch.h for what a backend does and does not promise.
  [[nodiscard]] virtual PlatformResult<WatchId> watch_files(
      std::span<const std::string_view> paths, FileWatchCallback callback) = 0;

  virtual PlatformResult<void> unwatch_files(WatchId watch) = 0;

  // --- Lifecycle ------------------------------------------------------------

  // Replaces any previously installed callback; there is one observer, and
  // fanning it out to several is the core's job rather than every backend's.
  //
  // design.md section 5.14.7 puts pause / resume / low_memory /
  // safe_area_changed in T1: on mobile they are a mandatory contract and
  // ignoring them gets the process terminated. Desktop backends never invoke
  // this, which is exactly why the position has to be fixed before a desktop
  // backend is written.
  virtual void on_lifecycle(LifecycleCallback callback) = 0;

 protected:
  IPlatform() = default;
};

}  // namespace dg
