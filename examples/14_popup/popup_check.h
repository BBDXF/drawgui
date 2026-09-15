// The headless half of examples/14_popup: PopupHost's placement math, the
// overlay branch driven through the real API against a real (dummy-driver)
// window, and the equivalence this slice's report calls its single most
// valuable artifact - checked without a display, so CTest can run it.
//
// The native branch's real OS window is deliberately NOT attempted here.
// SDL3's dummy video driver (SDL_setenv("SDL_VIDEODRIVER", "dummy", 1),
// below) is enough to open ordinary windows headlessly - measured, and used
// for the overlay branch and for a real WindowManager to hand PopupHost -
// but SDL_CreatePopupWindow fails under it with "That operation is not
// supported", measured directly for this slice rather than assumed. This
// mirrors examples/01_sdl3_multi_window's own precedent (no CTest entry,
// because it needs a display) rather than inventing a new one: the native
// branch is real and is exercised for real by examples/14_popup's
// interactive mode against an actual display, which is where
// doc/popup.md's measurements of it come from. This check still ATTEMPTS
// it, once, so the failure is asserted and reported rather than silently
// never run - see run()'s own comment at the attempt.

#pragma once

#include <iosfwd>

namespace popup_check {

int run(std::ostream& out);

}  // namespace popup_check
