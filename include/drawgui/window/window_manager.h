// WindowManager - several OS windows at once, and the routing that keeps them
// independent.
//
// This header was extracted from src/platform/sdl3/window_manager.cpp after
// that implementation was running, and it declares only what
// examples/01_sdl3_multi_window/main.cpp actually calls. Nothing here is a seam for a
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
#include <span>
#include <string>
#include <vector>

#include "drawgui/base/expected.h"
#include "drawgui/base/pixel_geometry.h"
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

// How the bytes of one pixel are arranged in memory.
//
// One enumerator, because one is what has been measured. A window surface on
// this platform is a 32-bit XRGB word, which on a little-endian machine is
// the bytes blue, green, red, unused - the same order a CPU-rasterized
// drawgui frame comes out in, so a frame reaches the screen by copying rather
// than converting. surface_format() reports a failure rather than a second
// enumerator when a window disagrees, because nothing can convert yet and
// quietly presenting the wrong channels is worse than saying so.
enum class PixelFormat : std::uint8_t {
  kBgra8888,
};

// Somebody else's pixels, borrowed for the duration of one call.
//
// This is the whole of what the window layer knows about rendering: an
// address, a shape, a row stride and a channel order. It names no surface
// type, no renderer and no graphics library, so the window layer stays
// ignorant of how the image was produced - which is the property that let
// this header survive the previous attempt's deletion.
struct ImageView {
  const std::uint8_t* pixels = nullptr;
  int width = 0;
  int height = 0;

  // Bytes between the starts of consecutive rows. Passed rather than assumed
  // to be width * 4, because a rasterizer is free to pad rows.
  std::size_t row_bytes = 0;

  PixelFormat format = PixelFormat::kBgra8888;
};

// What the pointer did.
//
// Four actions, and no button identity. Only the PRIMARY button produces an
// event at all - the backend drops the others, because nothing routes them and
// an enumerator naming a button no widget can receive would be a promise. The
// consequence is worth stating: a right-click cannot activate a widget here,
// not because the state machine checks, but because the event does not exist.
enum class PointerAction : std::uint8_t {
  kMove,
  kDown,
  kUp,

  // The pointer left the window. It carries NO position - `x` and `y` are
  // zero and mean nothing - because the platform does not report where the
  // pointer went, only that it is no longer here. A caller reading them would
  // be hit-testing the top-left corner and clearing hover for the wrong
  // reason, which is a bug that looks exactly like correct behaviour.
  kLeave,
};

// One thing the pointer did to one window.
//
// The position is in the window's PHYSICAL pixels - the same coordinates
// drawable_size() reports and present() copies into - not the logical ones the
// platform delivers. The conversion happens once, in the backend, because a
// pointer coordinate and a framebuffer coordinate that differ by a display
// scale is how clicks land near a widget instead of on it, and every consumer
// would otherwise have to remember to apply it.
struct PointerEvent {
  WindowId window;
  PointerAction action = PointerAction::kMove;
  int x = 0;
  int y = 0;
};

// Everything one pump() turned up, split by what the caller has to do about
// it.
struct PumpResult {
  // Windows that closed during this call, in the order they closed.
  std::vector<WindowId> closed;

  // Windows whose contents are now stale - newly exposed, or resized. A
  // resize is not reported separately because the only correct response to
  // either is the same: ask for the drawable size again and draw a frame at
  // it. Assuming the old size is still valid is how a resize turns into a
  // torn frame or a heap overflow.
  std::vector<WindowId> needs_repaint;

  // In the order they happened, which is the whole of what makes them usable:
  // a press and the release that ends it are only a click if nothing was
  // reordered between them.
  //
  // A caller must handle `needs_repaint` BEFORE these. A resize changes where
  // every widget is, and a pointer event that arrived in the same pump would
  // otherwise be hit-tested against the layout the window no longer has.
  std::vector<PointerEvent> pointer;
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
  // queued. Empty on a timeout.
  [[nodiscard]] PumpResult pump(int timeout_ms);

  // Moves the actual pointer, in the window's physical pixels.
  //
  // This is the real thing: the display server moves the cursor and delivers
  // the motion event it would have delivered had a hand done it. Together with
  // post_pointer_button() it lets a demo drive itself along a scripted path
  // THROUGH the ordinary event queue, hit testing and state machine - never
  // around them. A scripted mode that called the state machine directly would
  // prove nothing about the path a user takes, which is the only path that can
  // be wrong.
  void warp_pointer(WindowId id, int x, int y);

  // Puts a primary-button press or release on the platform's own event queue,
  // by the same route request_close() uses for a close.
  //
  // Pushed rather than synthesized at the device, because no windowing system
  // offers a "press the button" call - warping the pointer is as far as the
  // real hardware path goes. The event is indistinguishable from a physical
  // one once queued, so everything downstream of pump() is exercised exactly
  // as it is for a user.
  void post_pointer_button(WindowId id, bool down, int x, int y);

  // The size a frame for this window must be rasterized at, right now.
  //
  // Asked for per frame rather than remembered from open(): the window
  // manager, the user and the compositor all resize windows, and a cached
  // size is stale from the moment one of them does.
  [[nodiscard]] Expected<PixelSize, WindowError> drawable_size(WindowId id) const;

  // The channel order this window's surface expects. An error means the
  // window is in a format drawgui has never seen, not that the window is
  // broken - see PixelFormat.
  [[nodiscard]] Expected<PixelFormat, WindowError> surface_format(WindowId id) const;

  // Copies `dirty` out of `image` onto the window and puts it on screen.
  //
  // `image` must cover the whole window - `dirty` selects the part of it that
  // has actually changed, in the window's own pixel coordinates, and is
  // clipped to the window before anything is copied. A caller repainting
  // everything passes the full bounds; a caller repainting a hover highlight
  // or a blinking caret passes just that, and pays for just that.
  //
  // Presenting is what stops the window showing its WindowSpec::fill: that
  // flat colour is what a window displays until its owner draws something,
  // and never again afterwards.
  [[nodiscard]] Expected<void, WindowError> present(WindowId id, const ImageView& image,
                                                    const PixelRect& dirty);

  // The same, for a caller that has several disjoint damage rectangles.
  //
  // Each call to the single-rectangle form is its own round trip to the
  // display server, and at 1080p that round trip costs far more than the
  // pixels do. Measured over six runs each, presenting a frame's two to four
  // damage rectangles one call at a time took 0.57 ms; one call carrying all
  // of them took 0.42 ms.
  //
  // Overlapping rectangles would be copied twice, so the set is expected to
  // be disjoint. dg::DamageRegion produces one.
  [[nodiscard]] Expected<void, WindowError> present(WindowId id, const ImageView& image,
                                                    std::span<const PixelRect> dirty);

 private:
  struct Impl;

  explicit WindowManager(std::unique_ptr<Impl> impl);

  std::unique_ptr<Impl> impl_;
};

}  // namespace dg
