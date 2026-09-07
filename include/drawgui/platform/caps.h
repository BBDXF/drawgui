// PlatformCaps - what this platform actually has.
//
// design.md section 5.1 names this the key to a cross-platform multi-window
// model: mobile has no OS-level popup window, desktop does, and the core must
// not assume desktop semantics. The consumer that makes this concrete is
// PopupHost (section 5.2), which resolves a dropdown, context menu or tooltip
// to a real OS window when native_popup is true and to an in-window overlay
// layer when it is not. Widgets never create windows themselves, so this one
// struct is where that decision is taken for all of them.
//
// The field list is exactly the one section 5.1 spells out. Nothing is added
// speculatively: a capability flag with no consumer is a promise every future
// backend has to keep for nobody.

#pragma once

namespace dg {

struct PlatformCaps {
  // Every field defaults to false, so a backend opts in to each capability it
  // can actually deliver. The opposite default would mean a half-written
  // backend silently claims everything, and the first symptom would be a
  // menu opening into a window that platform cannot create.

  // False on mobile, where the application owns one surface.
  bool multi_window = false;

  // Whether a popup can be a real OS window that escapes its parent's bounds.
  // Measured true on Linux desktop under both x11 and Wayland - the popup is
  // a genuine OS window with correct parent linkage and it overhangs the
  // parent - which resolves design.md section 12's first open question and
  // lets PopupHost take the native path there.
  bool native_popup = false;

  bool native_menubar = false;
  bool system_tray = false;

  // Whether a window can be created with an alpha channel in its framebuffer.
  //
  // This gates WindowDesc::transparent_framebuffer, not a settable property.
  // design.md section 5.11.5: the alpha configuration has to be requested
  // when the GL/Metal context is created and cannot be switched afterwards.
  // It is a different thing from IWindow::set_opacity(), which is whole-window
  // compositing opacity and is mutable at any time; that section warns
  // explicitly against confusing the two.
  bool window_transparency = false;

  // Whether the platform has a native file chooser at all. The chooser itself
  // is a T2 service reached through query_service<T>() (design.md section
  // 5.14.1 demotes open_file_dialog() out of T1); this flag is what lets a
  // widget decide whether to offer the button before asking.
  bool file_dialog = false;

  friend constexpr bool operator==(const PlatformCaps&, const PlatformCaps&) = default;
};

}  // namespace dg
