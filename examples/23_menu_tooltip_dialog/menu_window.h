// Window driver for examples/23_menu_tooltip_dialog: three controls sharing
// one host window, each gated on the prerequisite doc/menus.md section 6
// named for it.
//
//   Context menu   right-click (PointerButton::kSecondary) on menu_target
//                  opens a 3-item menu through PopupHost, anchored at the
//                  POINTER position rather than a widget's own bounds -
//                  the one thing a dropdown's anchor never needed to be.
//   Tooltip        continuous hover over hover_target for kTooltipDelayMs
//                  opens a single-line label through PopupHost with
//                  PopupWindowKind::kTooltip; leaving ends it immediately.
//                  The frame loop's own timeout is -1 (block indefinitely)
//                  whenever nothing is being timed toward a tooltip, and a
//                  short poll only while one is - 6-1's own idle-vs-active
//                  frame-loop shape (examples/17_animation's own
//                  kFramePacerMs), reused for a NEW reason (a hover delay)
//                  rather than re-derived.
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
//                  for why a genuinely separate OS window (like a native
//                  popup) has no meaningful Focus-level "outside" to refuse
//                  in the first place.

#pragma once

#include <iosfwd>
#include <string>

#include "drawgui/base/pixel_geometry.h"

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

}  // namespace menu_window
