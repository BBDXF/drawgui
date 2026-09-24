// The headless half of examples/14_popup: PopupHost's placement math -
// flipping and clamping on both axes - driven through the real API against a
// real (dummy-driver) window, checked without a display so CTest can run it.
//
// PopupHost is overlay-only (48c9cd3 deleted the native-OS-popup branch this
// check used to also exercise and cross-check for equivalence against;
// that branch's one exclusive capability was never
// actually used by the placement math built on top of it). This check's job
// is now placement/flip verification for the one remaining, overlay,
// implementation - not an equivalence oracle between two branches that no
// longer both exist.
//
// SDL3's dummy video driver (SDL_setenv("SDL_VIDEODRIVER", "dummy", 1),
// below) is enough to open ordinary windows headlessly - used here for a
// real WindowManager to hand PopupHost - matching examples/01_sdl3_multi_
// window's own precedent (no CTest entry, because THAT example needs a
// display) rather than inventing a new one.

#pragma once

#include <iosfwd>

namespace popup_check {

int run(std::ostream& out);

}  // namespace popup_check
