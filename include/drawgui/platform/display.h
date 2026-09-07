// DisplayInfo - one monitor, as the platform reports it.
//
// design.md section 5.1 lists displays() as T1 for a reason that section
// 5.4.9 then makes precise: DPI is per-display, not per-application, and a
// window dragged between screens changes scale without changing layout. The
// scale therefore has to be attributable to a screen rather than to a global.

#pragma once

#include <string>

#include "drawgui/graphics/types.h"
#include "drawgui/platform/types.h"

namespace dg {

namespace detail {
struct DisplayIdTag;
}  // namespace detail

using DisplayId = Handle<detail::DisplayIdTag>;

struct DisplayInfo {
  DisplayId id;

  // Whatever the platform calls this monitor. For display to a human only -
  // it is not stable across reconnects and must never be used as a key.
  std::string name;

  // Logical pixels, in the virtual-desktop coordinate space that
  // WindowDesc::position and IWindow::set_position also use. Multiple
  // displays tile this space; the primary display is not necessarily at the
  // origin.
  Rect bounds;

  // bounds minus whatever the desktop environment has reserved - taskbars,
  // docks, panels. This is what a window should be placed and maximized
  // against, not bounds.
  Rect work_area;

  // Logical pixels multiplied by this give physical pixels on this display.
  float dpi_scale = 1.0F;

  // Zero when the platform does not report it. Used to pace the frame loop
  // (design.md section 5.15.1), which falls back to a fixed interval rather
  // than guessing.
  float refresh_rate_hz = 0.0F;

  bool is_primary = false;

  friend bool operator==(const DisplayInfo&, const DisplayInfo&) = default;
};

}  // namespace dg
