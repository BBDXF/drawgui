// The headless half of examples/22_dropdown_menu: Tab reaches the dropdown
// among real neighbours, opening/closing it through both PopupHost branches,
// keyboard highlight movement (Up/Down reusing dg::Focus::focus_next()/
// focus_previous() verbatim) at the first/a middle/the last option plus
// wraparound in both directions, mouse selection of a specific row, Escape
// leaving the selection untouched, and the measured relayout cost of
// opening/closing and of a selection change - matching examples/21_focus's
// own precedent for why a display is not required (SDL_VIDEODRIVER=dummy
// opens ordinary windows; the native branch is attempted for real and
// reports a loud, specific reason rather than skipping silently, exactly
// as doc/popup.md section 4 and doc/focus.md's own headless check do).

#pragma once

#include <iosfwd>

namespace dropdown_check {

int run(std::ostream& out);

}  // namespace dropdown_check
