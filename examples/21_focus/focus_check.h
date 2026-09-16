// The headless half of examples/21_focus: Tab/Shift-Tab order over the
// mixed-kind scene, wrapping, tab_index override, focus scopes across both
// PopupHost branches, the mid-composition Tab-away hazard (7-3's own
// crash-hazard finding, re-checked here rather than assumed fixed), and the
// real SDLK_TAB -> Key::kTab wiring - driven through a real (dummy-driver)
// WindowManager + PopupHost, matching examples/14_popup's own precedent for
// why a display is not required.

#pragma once

#include <iosfwd>

namespace focus_check {

int run(std::ostream& out);

}  // namespace focus_check
