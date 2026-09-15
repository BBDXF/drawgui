// The window half of examples/15_list: pump(), wheel/drag scrolling routed
// through WidgetSet::list_scroll_by(), and the offscreen modes shared with
// --dump-png, --bench and --verify-list.

#pragma once

#include <iosfwd>
#include <string>

#include "drawgui/base/pixel_geometry.h"

#include "list_scene.h"

namespace list_window {

struct Settings {
  dg::PixelSize size{460, 600};
  int run_ms = 0;

  // Pre-scrolled by this many ITEMS (not pixels) before the first frame -
  // the "jump" the task asks for exercised outside a live drag: a preset of
  // 500 recycles most of the pool in one call, the same code path a fast
  // scroll wheel or a scrollbar drag-to-position would take.
  int preset_jump_items = 0;
};

int run(const Settings& settings, std::ostream& out);
int dump_png(const Settings& settings, const std::string& path, std::ostream& out);

// Measures, and reports honestly: node counts, per-scroll-step time and
// LayoutStats for the virtualized scene against the pre-virtualization
// baseline (list_scene::build_baseline) at the same item count - design.md's
// "1000 项列表 60fps" answered head-on rather than tuned for.
int bench(int item_count, std::ostream& out);

}  // namespace list_window
