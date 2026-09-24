// Window driver for examples/23_menu_tooltip_dialog: three controls sharing
// one host window, each gated on the prerequisite doc/menus.md section 6
// named for it.
//
//   Context menu   right-click (PointerButton::kSecondary) on menu_target
//                  opens a 3-item menu through PopupHost, anchored at the
//                  POINTER position rather than a widget's own bounds -
//                  the one thing a dropdown's anchor never needed to be.
//                  PopupHost is overlay-only (doc/popup.md's B-1/B-2
//                  finding deleted the native-OS-popup branch this file
//                  used to also drive) - the menu is always built into the
//                  host's own scene_.tree.render(), never a second tree.
//   Tooltip        continuous hover over hover_target for kTooltipDelayMs
//                  opens a single-line label through PopupHost; leaving
//                  ends it immediately. The frame loop's own timeout is -1
//                  (block indefinitely) whenever nothing is being timed
//                  toward a tooltip, and a short poll only while one is -
//                  6-1's own idle-vs-active frame-loop shape (examples/
//                  17_animation's own kFramePacerMs), reused for a NEW
//                  reason (a hover delay) rather than re-derived. Like the
//                  context menu, the tooltip is always overlay content now
//                  - built into scene_.tree.render(), never a second tree
//                  or window.
//   Dialog         `--branch overlay` (the default in this demo's headless
//                  check) opens a same-window modal panel - backdrop +
//                  panel, dg::Focus::enter_scope(panel_root, /*modal=*/true)
//                  - and demonstrates the FOCUS-trap half: Tab confined,
//                  and a direct click/set_guarded() targeting `before`/
//                  `after` (outside the panel) is refused. `--branch
//                  native` opens a REAL second OS window
//                  (WindowManager::open_dialog(), real SDL_SetWindowParent/
//                  SDL_SetWindowModal) with its OWN cancellable_close
//                  WindowSpec and demonstrates the CLOSE-REQUEST-veto half:
//                  a "Veto" toggle inside the dialog decides whether its
//                  own Close button's request_close() actually destroys
//                  the window (close_now()) or is silently ignored, proving
//                  a handler can veto a close. The two are not the same
//                  demonstration on purpose - see doc/menus.md section 6.3
//                  for why a genuinely separate OS window has no meaningful
//                  Focus-level "outside" to refuse in the first place.
//                  Dialog is a genuinely separate window-layer addition
//                  (doc/popup.md section 10, WindowManager::open_dialog()),
//                  deliberately NOT built on PopupHost - it is unaffected
//                  by PopupHost becoming overlay-only and both its branches
//                  remain exactly as they were.
//                  `--branch` therefore now only selects the Dialog's own
//                  native/overlay branch; the context menu and tooltip are
//                  overlay regardless of this flag.

#pragma once

#include <iosfwd>
#include <string>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/window/window_manager.h"

namespace menu_window {

struct Settings {
  dg::PixelSize size{620, 220};
  bool force_overlay = false;
  int run_ms = 0;
  std::string font_dir = "/usr/share/fonts";
  int tooltip_delay_ms = 400;
};

int run(const Settings& settings, std::ostream& out);
int dump_png(const Settings& settings, const std::string& path, std::ostream& out);
int idle_probe(const Settings& settings, int idle_probe_ms, std::ostream& out);

// Test-only seam for --verify-menus: the pixel size Runner's constructor
// actually uses for its first scene/tree/surface build - see the .cpp's
// own definition comment for why this is exported rather than file-local.
dg::PixelSize initial_viewport(dg::WindowManager& manager, dg::WindowId window,
                               const Settings& settings);

}  // namespace menu_window
