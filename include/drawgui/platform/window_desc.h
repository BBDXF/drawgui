// WindowDesc - everything fixed at the moment a window is created.
//
// design.md section 5.2 (constraint C3) requires window semantics - multiple
// windows, dialogs, popups - to be designed on day one rather than faked with
// an in-application modal layer. The four kinds below are that design, and
// the owner link is what makes a dialog modal to something and a popup
// anchored to something.
//
// A field is here rather than on IWindow when it cannot be changed afterwards
// (transparent_framebuffer, kind, owner) or when it is the window's initial
// state (title, size, position, visibility).

#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "drawgui/graphics/types.h"
#include "drawgui/platform/types.h"

namespace dg {

namespace detail {
struct WindowIdTag;
}  // namespace detail

// Identifies a window for the lifetime of that window. Every input event
// carries one (design.md section 5.2), which is how a keystroke is routed to
// the right focus tree.
using WindowId = Handle<detail::WindowIdTag>;

// design.md section 5.2. The backend derives the concrete platform flags from
// the kind rather than taking them field by field: a Popup is by definition
// undecorated, does not take focus and closes when a click lands outside it,
// and a Tooltip additionally receives no input at all. Exposing those as
// separate booleans would let a caller build a decorated tooltip that no
// platform can honour.
enum class WindowKind : std::uint8_t {
  kNormal,
  kDialog,
  kPopup,
  kTooltip,
};

struct WindowDesc {
  std::string title;

  // Logical pixels (dp). design.md section 5.4.9: layout never sees physical
  // pixels; the backend multiplies by the target display's scale.
  Size logical_size{800.0F, 600.0F};

  // Logical pixels, in the virtual-desktop coordinate space DisplayInfo uses.
  // Absent means "let the platform place it", which is the right default for
  // a normal window and wrong for a popup - a popup is positioned against the
  // anchor rectangle its PopupHost computed.
  std::optional<Point> position;

  WindowKind kind = WindowKind::kNormal;

  // Required for kDialog, kPopup and kTooltip; must be invalid for kNormal.
  // Creating an owned window without an owner is kInvalidArgument rather than
  // a silently top-level window, because the difference only shows up later
  // as a dialog that outlives the thing it was modal to.
  WindowId owner;

  bool resizable = true;
  bool decorated = true;
  bool initially_visible = true;

  // Requests a framebuffer with an alpha channel. Gated by
  // PlatformCaps::window_transparency, and creation-time only: design.md
  // section 5.11.5 records that the alpha configuration must be requested
  // when the graphics context is created and cannot be switched later. This
  // is NOT IWindow::set_opacity(), which is whole-window compositing opacity
  // and stays mutable.
  bool transparent_framebuffer = false;

  friend bool operator==(const WindowDesc&, const WindowDesc&) = default;
};

}  // namespace dg
