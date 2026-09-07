// IWindow - one window, or one host-provided surface drawgui renders into.
//
// The method set follows design.md section 5.1. Everything here is T1
// (section 5.14.1): present on every target platform, and a GUI cannot work
// without it. Anything optional is a T2 service reached through
// IPlatform::query_service<T>() instead, which is what keeps this interface
// from growing without bound.
//
// Nothing in this file names a backend, a windowing system or a graphics API.
// That is constraint C1, and this is the file it is about.

#pragma once

#include <cstdint>
#include <string_view>

#include "drawgui/graphics/types.h"
#include "drawgui/platform/display.h"
#include "drawgui/platform/error.h"
#include "drawgui/platform/frame.h"
#include "drawgui/platform/native_window.h"
#include "drawgui/platform/types.h"
#include "drawgui/platform/window_desc.h"

namespace dg {

// Defined by the input layer in P4 (design.md section 5.5). Only named here,
// because embedded mode's shape has to be fixed now (section 5.14.5) and the
// input model must not be designed by the platform layer that carries it.
struct InputEvent;

// design.md section 5.9.6 lists `cursor` among the interaction properties.
// The set is small on purpose: every shape has to exist, and look right, on
// five platforms, and a shape that falls back to an arrow everywhere is worse
// than not offering it.
enum class CursorShape : std::uint8_t {
  kArrow,
  kText,
  kHand,
  kCrosshair,
  kResizeHorizontal,
  kResizeVertical,
  kResizeTopLeftBottomRight,
  kResizeTopRightBottomLeft,
  kNotAllowed,
  kBusy,
  kHidden,
};

class IWindow {
 public:
  IWindow(const IWindow&) = delete;
  IWindow& operator=(const IWindow&) = delete;
  IWindow(IWindow&&) = delete;
  IWindow& operator=(IWindow&&) = delete;

  virtual ~IWindow() = default;

  // --- Identity -------------------------------------------------------------

  [[nodiscard]] virtual WindowId id() const = 0;
  [[nodiscard]] virtual WindowKind kind() const = 0;

  // Invalid for a kNormal window; the window this one belongs to otherwise.
  [[nodiscard]] virtual WindowId owner() const = 0;

  // Which display the window is currently on. design.md section 5.4.9: moving
  // between screens changes dpi_scale() and requires a repaint, but not a
  // relayout, because layout is in logical pixels throughout.
  [[nodiscard]] virtual DisplayId display() const = 0;

  // --- Mutable state --------------------------------------------------------

  virtual PlatformResult<void> set_title(std::string_view title) = 0;

  // Logical pixels. Named for the space it is in: a window has both a logical
  // and a physical size and they differ by dpi_scale(), so an unqualified
  // set_size() is the kind of ambiguity that shows up as a window that is
  // right on one monitor and wrong on the next.
  virtual PlatformResult<void> set_logical_size(Size size) = 0;

  virtual PlatformResult<void> set_position(Point position) = 0;
  virtual PlatformResult<void> set_visible(bool visible) = 0;

  // Whole-window compositing opacity in [0, 1] - what the OS compositor does
  // with the finished window. design.md section 5.11.5 warns explicitly
  // against confusing this with WindowDesc::transparent_framebuffer, which
  // decides whether the framebuffer has an alpha channel at all and cannot be
  // changed after creation. This one can be changed at any time.
  virtual PlatformResult<void> set_opacity(float opacity) = 0;

  // --- Geometry -------------------------------------------------------------

  [[nodiscard]] virtual float dpi_scale() const = 0;
  [[nodiscard]] virtual Size logical_size() const = 0;
  [[nodiscard]] virtual PixelSize physical_size() const = 0;

  // --- Frame ----------------------------------------------------------------

  // Makes this window's rendering context current and returns the target for
  // this frame. See frame.h for why this is a description rather than a Skia
  // surface.
  //
  // Errors a caller is expected to handle rather than log: kFrameNotAvailable
  // when the window is occluded or minimized, and kGraphicsContextLost, which
  // design.md section 5.17.3 treats as a routine desktop event with a defined
  // recovery path rather than as an edge case.
  [[nodiscard]] virtual PlatformResult<FrameTarget> begin_frame() = 0;

  // Presents what was drawn. Must be called exactly once for every successful
  // begin_frame().
  virtual PlatformResult<void> end_frame() = 0;

  // Marks the window as needing to be drawn, and wakes the loop if it is
  // blocked. design.md section 5.15.1: drawing is on demand, and an idle
  // application blocks at 0% CPU rather than spinning on vsync. This is the
  // call that ends that state, so it is the one that must be cheap and safe
  // to call many times per frame.
  virtual void request_redraw() = 0;

  // --- Embedded mode (design.md section 5.14.5) -----------------------------
  //
  // MVP implements the standalone mode only, but the shape is fixed now: who
  // owns the event loop reaches into window creation, render timing and input
  // source, so adding it afterwards is a rewrite rather than a change. Both
  // calls return kWrongLoopMode on a platform drawgui owns the loop of.

  // Draws this window immediately rather than waiting for a frame the host's
  // loop will never schedule.
  virtual PlatformResult<void> render_now() = 0;

  // Feeds the host's input to drawgui. In embedded mode there is no OS event
  // queue of our own to read from - the host already consumed it.
  virtual PlatformResult<void> inject_event(const InputEvent& event) = 0;

  // --- Input ----------------------------------------------------------------

  // The IME hook. design.md section 5.5 fixes this signature at MVP and
  // leaves the backend to P7; the rectangle is where the caret is, in logical
  // pixels, so the platform can place its candidate window without knowing
  // anything about the text.
  virtual PlatformResult<void> start_text_input(Rect caret_rect) = 0;
  virtual PlatformResult<void> stop_text_input() = 0;

  virtual PlatformResult<void> set_cursor(CursorShape shape) = 0;

  // design.md section 5.5: after a press, move and release must keep arriving
  // even once the pointer has left the window. Two calls rather than a bool,
  // because capture is a state the caller must release explicitly and a
  // forgotten `false` reads as an ordinary argument rather than as a stuck
  // window.
  virtual PlatformResult<void> capture_pointer() = 0;
  virtual PlatformResult<void> release_pointer_capture() = 0;

  // --- Escape hatch ---------------------------------------------------------

  // design.md section 5.14.4. Using the result leaves drawgui's cross-platform
  // guarantee, and the handle must not be held across frames.
  [[nodiscard]] virtual PlatformResult<NativeWindow> native_handle() const = 0;

 protected:
  IWindow() = default;
};

}  // namespace dg
