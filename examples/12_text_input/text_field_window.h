// The window driver for examples/12_text_input: pump(), pointer routing
// (click-to-focus, click-to-position-cursor, drag-to-select), keyboard
// routing (arrow/Home/End/Backspace/Delete, all gated on FOCUS - design.md
// section 5.5.2's tier-2 "the focused TextField's own editing intents beat
// everything else"), and committed-text routing, plus the offscreen modes
// shared with --dump-png and --probe.

#pragma once

#include <optional>
#include <ostream>
#include <string>

#include "drawgui/base/pixel_geometry.h"

namespace text_field_window {

struct Settings {
  dg::PixelSize size{420, 220};
  int run_ms = 0;

  // Applied once, before the first frame - lets --dump-png/--probe
  // demonstrate a particular state without a live window to type into.
  std::string preset_field_b_text;
  bool preset_focus_field_a = false;
  // Selects field a's first 19 bytes ("The quick brown fox") after focusing
  // it - lets --dump-png/--probe show a selection highlight deterministically.
  bool preset_select_field_a = false;
};

int run(const Settings& settings, std::ostream& out);
int dump_png(const Settings& settings, const std::string& path, std::ostream& out);
int probe(const Settings& settings, dg::PixelPoint point, std::ostream& out);

// Opens a REAL window and drives it through warp_pointer()/
// post_pointer_button()/post_key()/post_text_input() - the same route every
// prior example's --script uses to prove a scripted run exercises the
// actual event queue rather than calling the widget machinery directly:
// clicks field b, types a real string through a real SDL_EVENT_TEXT_INPUT,
// presses Backspace through a real SDL_EVENT_KEY_DOWN. Not a CTest entry,
// matching every other real-window mode in this project.
int script(const Settings& settings, std::ostream& out);

}  // namespace text_field_window
