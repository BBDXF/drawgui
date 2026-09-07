// WindowManager - several OS windows at once, and the routing that keeps them
// independent.
//
// This header was extracted from src/platform/sdl3/window_manager.cpp after
// that implementation was running, and it declares only what
// examples/multi_window.cpp actually calls. Nothing here is a seam for a
// second backend: the project's previous attempt wrote twelve abstract
// platform headers before any backend existed, and an interface no
// implementation has ever contradicted is a guess with a build rule. When a
// second platform is measured, whatever the two genuinely share can be
// extracted then - from two working implementations rather than from a
// document.
//
// Nothing from the underlying windowing library appears below, and nothing
// from it ever should. A caller includes this header and links drawgui; that
// is the entire contract.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "drawgui/base/expected.h"
#include "drawgui/graphics/types.h"

namespace dg {

// Why an operation failed, in a form fit for a diagnostic.
//
// A message rather than an error code, because nothing branches on the
// reason - every caller so far prints it - and the platform's own text
// ("Couldn't find matching GL context", "No available video device") is the
// part that makes a failure debuggable. A code would discard exactly that and
// buy a switch statement nobody writes. It can become a code the day some
// caller needs to act differently on one.
struct WindowError {
  std::string message;
};

// Identifies one open window.
//
// A struct rather than a bare integer so that it cannot be passed where a
// count, an index or a size was meant. The value is only meaningful to the
// WindowManager that issued it, and only while that window is open.
struct WindowId {
  std::uint32_t value = 0;

  friend bool operator==(WindowId, WindowId) = default;
};

// What a window should be when it appears.
//
// Grouped rather than passed as four arguments: title, width and height and
// fill describe one thing, and a call site reading open(title, 480, 320, c)
// invites the two ints to be swapped.
struct WindowSpec {
  std::string title;
  int width = 0;
  int height = 0;

  // Filled flat, so that "which window is this" is answerable by looking at
  // it. The alpha channel is ignored; a window is opaque.
  Color fill;
};

class WindowManager {
 public:
  // Initializes the platform's video subsystem. One at a time per process.
  [[nodiscard]] static Expected<WindowManager, WindowError> create();

  WindowManager(WindowManager&&) noexcept;
  WindowManager& operator=(WindowManager&&) noexcept;
  WindowManager(const WindowManager&) = delete;
  WindowManager& operator=(const WindowManager&) = delete;

  // Closes every window still open and shuts the video subsystem down.
  ~WindowManager();

  // Opens a window immediately. Windows already open are unaffected, and the
  // new one is filled with spec.fill before this returns.
  [[nodiscard]] Expected<WindowId, WindowError> open(const WindowSpec& spec);

  [[nodiscard]] std::size_t open_window_count() const;

  // Asks for one window to close, by the same route the window manager's own
  // close button takes. It has not closed when this returns - the close is
  // observed from pump(), exactly like a user-initiated one. An id that names
  // no open window is ignored, which is what makes asking twice harmless.
  void request_close(WindowId id);

  // Waits up to timeout_ms for something to happen, then handles everything
  // queued. Returns the windows that closed during this call, in the order
  // they closed; empty on a timeout.
  [[nodiscard]] std::vector<WindowId> pump(int timeout_ms);

 private:
  struct Impl;

  explicit WindowManager(std::unique_ptr<Impl> impl);

  std::unique_ptr<Impl> impl_;
};

}  // namespace dg
