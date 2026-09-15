// The window half of examples/17_animation - design.md section 5.15.1's
// on-demand frame loop, driving three real dg::AnimationEngine clients.
//
// THE LOOP ITSELF is the point of this file, more than any one panel:
// WindowManager::pump(timeout_ms) is handed -1 (block indefinitely, SDL's
// own "wait forever" convention for SDL_WaitEventTimeout) whenever
// `engine_.has_active()` is false, and a short frame-pacer interval whenever
// it is true. There is no GPU here and therefore no real vsync signal to
// wait on - see doc/animation.md for why the frame-pacer interval is
// candidly a CPU-side approximation of vsync rather than the real thing.
//
// `--idle-probe-ms N` is the measurement the task asks for by name: how much
// process CPU time is actually consumed while blocked for N milliseconds
// with nothing animating. It is a separate mode from the interactive window
// because the claim being measured is specifically about the BLOCKED state,
// and mixing it into the ordinary run loop would make the number a function
// of whatever else that loop was doing.

#pragma once

#include <iosfwd>
#include <string>

#include "drawgui/base/pixel_geometry.h"

namespace anim_window {

struct Settings {
  dg::PixelSize size{760, 220};

  int run_ms = 0;

  // design.md section 5.16.1's reduced-motion clause, applied to this demo's
  // own two clients: the slide's duration collapses to zero (engine policy,
  // unconditional) and the caret's blink LOOP stops re-triggering itself and
  // freezes solid instead (a CLIENT-level decision layered on top - see
  // anim_window.cpp's own comment on why the engine's policy alone is not
  // enough for a re-triggering client).
  bool reduced_motion = false;
};

int run(const Settings& settings, std::ostream& out);
int dump_png(const Settings& settings, const std::string& path, std::ostream& out);

// Blocks in WindowManager::pump() for exactly `idle_probe_ms` with nothing
// animating and nothing else posting an event, and reports the process CPU
// time (user+sys, via getrusage) consumed across that block - the measured
// answer to design.md section 5.15.1's "wait_events() 阻塞，CPU 占用 0%".
int idle_probe(const Settings& settings, int idle_probe_ms, std::ostream& out);

}  // namespace anim_window
