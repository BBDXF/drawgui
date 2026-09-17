#include "scroll_check.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <ostream>
#include <vector>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/shortcuts/action_ids.generated.h"
#include "drawgui/shortcuts/action_scopes.h"
#include "drawgui/shortcuts/keyboard_scroll.h"
#include "drawgui/shortcuts/logical_key.generated.h"
#include "drawgui/shortcuts/router.h"
#include "drawgui/window/window_manager.h"

#include "scroll_scene.h"

namespace scroll_check {
namespace {

using dg::NodeId;
using dg::PixelPoint;
using dg::PixelRect;
using dg::PixelSize;
using dg::RasterSurface;
using dg::RenderTree;

dg::TreeSpec spec_for(PixelSize size) {
  dg::TreeSpec spec;
  spec.viewport = size;
  spec.background.fill = dg::Color::from_argb(0xFF14171C);
  return spec;
}

constexpr PixelSize kSize{900, 760};

std::vector<std::uint8_t> snapshot(const RasterSurface& surface) {
  const dg::PixelView view = surface.peek_pixels();
  const auto* bytes = static_cast<const std::uint8_t*>(view.pixels);
  return std::vector<std::uint8_t>{
      bytes, bytes + (view.row_bytes * static_cast<std::size_t>(view.height))};
}

// --------------------------------------------------------------------------
// Claim 1: composition. Content overflows its viewport on both axes without
// the container's own size following it - the whole point of scroll_axis.
// --------------------------------------------------------------------------

bool check_composition(std::ostream& out) {
  scroll_scene::Scene scene = scroll_scene::build(spec_for(kSize));
  bool ok = true;

  const PixelRect vp = scene.tree.bounds(scene.handles.vertical_viewport);
  const PixelRect vc = scene.tree.bounds(scene.handles.vertical_content);
  if (vp.height != scroll_scene::kVerticalViewportHeight) {
    out << "  FAIL: vertical viewport height " << vp.height << ", expected "
        << scroll_scene::kVerticalViewportHeight
        << " (a scrolling child must not push its "
           "container open)\n";
    ok = false;
  }
  const int expected_content_height =
      (scroll_scene::kVerticalItemHeight * scroll_scene::kVerticalItemCount) +
      (4 * (scroll_scene::kVerticalItemCount - 1));  // gap=4 between items
  if (vc.height != expected_content_height) {
    out << "  FAIL: vertical content height " << vc.height << ", expected "
        << expected_content_height << "\n";
    ok = false;
  }
  if (vc.height <= vp.height) {
    out << "  FAIL: vertical content (" << vc.height << ") does not overflow its viewport ("
        << vp.height << ") - the scene proves nothing about scrolling if it fits\n";
    ok = false;
  }

  const PixelRect hp = scene.tree.bounds(scene.handles.horizontal_viewport);
  const PixelRect hc = scene.tree.bounds(scene.handles.horizontal_content);
  const int expected_content_width =
      (scroll_scene::kHorizontalItemWidth * scroll_scene::kHorizontalItemCount) +
      (4 * (scroll_scene::kHorizontalItemCount - 1));
  if (hc.width != expected_content_width) {
    out << "  FAIL: horizontal content width " << hc.width << ", expected "
        << expected_content_width << "\n";
    ok = false;
  }
  if (hc.width <= hp.width) {
    out << "  FAIL: horizontal content (" << hc.width << ") does not overflow its viewport ("
        << hp.width << ")\n";
    ok = false;
  }
  if (!scene.tree.diagnostics().empty()) {
    // Not actually a failure condition by itself, but worth surfacing if the
    // scene starts producing diagnostics nobody asked for.
    out << "  note: " << scene.tree.diagnostics().front() << "\n";
  }

  if (ok) {
    out << "  OK: both strips' content overflows its viewport, and neither viewport followed "
           "it\n";
  }
  return ok;
}

// --------------------------------------------------------------------------
// Claim 2: hit testing follows the offset - a scrolled-out item does not
// respond, a scrolled-in one does.
// --------------------------------------------------------------------------

bool check_hit_follows_offset(std::ostream& out) {
  scroll_scene::Scene scene = scroll_scene::build(spec_for(kSize));
  bool ok = true;

  const PixelRect vp = scene.tree.bounds(scene.handles.vertical_viewport);
  const PixelPoint inside_top{vp.x + 10, vp.y + 10};

  const std::optional<NodeId> at_rest = scene.tree.render().hit_test(inside_top);
  if (at_rest != scene.handles.vertical_items.front()) {
    out << "  FAIL: at rest, the viewport's top-left does not hit item 0\n";
    ok = false;
  }

  // Scroll down by three items' worth. Item 0 is now well above the
  // viewport - clipped away - and the item that has moved into its place is
  // hittable there instead.
  const int shift = 3 * (scroll_scene::kVerticalItemHeight + 4);
  const PixelRect content_box = scene.tree.content_bounds(scene.handles.vertical_viewport);
  scene.widgets.scroll_by(scene.tree.render(), scene.handles.vertical_viewport, content_box, 0,
                          shift);

  const std::optional<NodeId> after_scroll = scene.tree.render().hit_test(inside_top);
  if (after_scroll == scene.handles.vertical_items.front()) {
    out << "  FAIL: item 0 is still hit after scrolling " << shift
        << " px - it should have scrolled out of view\n";
    ok = false;
  }
  if (after_scroll != scene.handles.vertical_items[3]) {
    out << "  FAIL: expected item 3 to have scrolled into item 0's old screen position\n";
    ok = false;
  }

  if (ok) {
    out << "  OK: item 0 stopped responding once scrolled out, and item 3 answered in its "
           "place\n";
  }
  return ok;
}

// --------------------------------------------------------------------------
// Claim 3: overscroll clamps at both ends (the "hard stop" platform default,
// design.md section 5.16.2).
// --------------------------------------------------------------------------

bool check_overscroll_clamps(std::ostream& out) {
  scroll_scene::Scene scene = scroll_scene::build(spec_for(kSize));
  bool ok = true;

  const PixelRect content_box = scene.tree.content_bounds(scene.handles.vertical_viewport);
  const PixelRect vc = scene.tree.bounds(scene.handles.vertical_content);
  const int max_offset = vc.height - content_box.height;

  // Scrolling up from rest must clamp at exactly 0, not go negative.
  scene.widgets.scroll_by(scene.tree.render(), scene.handles.vertical_viewport, content_box, 0,
                          -500);
  int offset = scene.tree.render().scroll_offset(scene.handles.vertical_viewport).y;
  if (offset != 0) {
    out << "  FAIL: scrolling up from rest produced offset " << offset << ", expected 0\n";
    ok = false;
  }

  // Scrolling down far past the content must clamp at max_offset exactly,
  // not at some arbitrary point past it.
  scene.widgets.scroll_by(scene.tree.render(), scene.handles.vertical_viewport, content_box, 0,
                          1000000);
  offset = scene.tree.render().scroll_offset(scene.handles.vertical_viewport).y;
  if (offset != max_offset) {
    out << "  FAIL: scrolling far past the bottom produced offset " << offset << ", expected "
        << max_offset << " (content " << vc.height << " - viewport " << content_box.height
        << ")\n";
    ok = false;
  }

  // One more notch past the clamp must be a no-op - scroll_by must report
  // "nothing changed" rather than silently moving the offset a pixel past
  // its own clamp through accumulated rounding.
  const bool moved = scene.widgets.scroll_by(
      scene.tree.render(), scene.handles.vertical_viewport, content_box, 0, 100);
  if (moved) {
    out << "  FAIL: scroll_by reported a change at an offset already clamped to its maximum\n";
    ok = false;
  }

  if (ok) {
    out << "  OK: overscroll clamps at 0 and at exactly " << max_offset
        << " (content minus viewport), and a further nudge past the clamp is a no-op\n";
  }
  return ok;
}

// --------------------------------------------------------------------------
// Claim 4: the horizontal axis works the same way as the vertical one - the
// property is not secretly hardcoded to one axis.
// --------------------------------------------------------------------------

bool check_horizontal_axis(std::ostream& out) {
  scroll_scene::Scene scene = scroll_scene::build(spec_for(kSize));
  bool ok = true;

  const PixelRect hp_content = scene.tree.content_bounds(scene.handles.horizontal_viewport);
  const PixelPoint inside_left{scene.tree.bounds(scene.handles.horizontal_viewport).x + 10,
                               scene.tree.bounds(scene.handles.horizontal_viewport).y + 10};

  if (scene.tree.render().hit_test(inside_left) != scene.handles.horizontal_items.front()) {
    out << "  FAIL: at rest, the horizontal strip's left edge does not hit item 0\n";
    ok = false;
  }

  const int shift = 2 * (scroll_scene::kHorizontalItemWidth + 4);
  scene.widgets.scroll_by(scene.tree.render(), scene.handles.horizontal_viewport, hp_content,
                          shift, 0);
  if (scene.tree.render().hit_test(inside_left) != scene.handles.horizontal_items[2]) {
    out << "  FAIL: after scrolling right by " << shift
        << " px, item 2 should answer at the "
           "strip's left edge\n";
    ok = false;
  }
  // A vertical delta sent to a horizontal-only viewport must be ignored.
  const bool moved_by_vertical_delta = scene.widgets.scroll_by(
      scene.tree.render(), scene.handles.horizontal_viewport, hp_content, 0, 40);
  if (moved_by_vertical_delta) {
    out << "  FAIL: a vertical delta moved a horizontal-only viewport\n";
    ok = false;
  }

  if (ok) {
    out << "  OK: the horizontal strip scrolls on its own axis and ignores the other one\n";
  }
  return ok;
}

// --------------------------------------------------------------------------
// Claim 5: no relayout. Scrolling costs a repaint; the layout tree visits
// nothing on a frame where only the offset changed.
// --------------------------------------------------------------------------

bool check_no_relayout(std::ostream& out) {
  scroll_scene::Scene scene = scroll_scene::build(spec_for(kSize));
  scene.tree.layout();  // settle any leftover dirt from build()

  const PixelRect content_box = scene.tree.content_bounds(scene.handles.vertical_viewport);
  scene.widgets.scroll_by(scene.tree.render(), scene.handles.vertical_viewport, content_box, 0,
                          70);

  const dg::LayoutStats stats = scene.tree.layout();
  bool ok = true;
  if (stats.nodes_visited != 0 || stats.nodes_relaid_out != 0) {
    out << "  FAIL: a scroll-only frame visited " << stats.nodes_visited << " and relaid out "
        << stats.nodes_relaid_out << " nodes; expected 0 of each\n";
    ok = false;
  }
  if (ok) {
    out << "  OK: layout() visited 0 nodes after an offset-only change - scrolling costs a "
           "repaint, never a relayout\n";
  }
  return ok;
}

// --------------------------------------------------------------------------
// The standing gate: incremental equals full, across a scroll script that
// crosses both clamps and mixes in an ordinary style change.
// --------------------------------------------------------------------------

bool check_identity(std::ostream& out) {
  std::optional<RasterSurface> damaged = RasterSurface::create(kSize.width, kSize.height);
  std::optional<RasterSurface> whole = RasterSurface::create(kSize.width, kSize.height);
  if (!damaged.has_value() || !whole.has_value()) {
    out << "  FAIL: could not allocate a raster surface\n";
    return false;
  }

  scroll_scene::Scene incremental = scroll_scene::build(spec_for(kSize));
  scroll_scene::Scene full = scroll_scene::build(spec_for(kSize));
  incremental.tree.render().repaint_full(*damaged);
  full.tree.render().repaint_full(*whole);

  const int vertical_offsets[] = {0, 60, 300, 5000, 0, -900, 90};
  const int horizontal_offsets[] = {0, 100, -50, 3000, 0};

  for (std::size_t step = 0; step < std::size(vertical_offsets); ++step) {
    const auto apply = [&](scroll_scene::Scene& scene) {
      const PixelRect vp = scene.tree.content_bounds(scene.handles.vertical_viewport);
      scene.widgets.scroll_by(scene.tree.render(), scene.handles.vertical_viewport, vp, 0,
                              vertical_offsets[step]);
      if (step < std::size(horizontal_offsets)) {
        const PixelRect hp = scene.tree.content_bounds(scene.handles.horizontal_viewport);
        scene.widgets.scroll_by(scene.tree.render(), scene.handles.horizontal_viewport, hp,
                                horizontal_offsets[step], 0);
      }
      // Every other step also recolours an item under a clip, so the scene
      // exercises "damage while scrolled", not only "damage from scrolling".
      if (step % 2 == 0) {
        scene.tree.render().set_fill(
            scene.handles.vertical_items[step % scene.handles.vertical_items.size()],
            dg::Color::from_argb(0xFF00FF88));
      }
    };
    apply(incremental);
    apply(full);

    incremental.tree.render().repaint(*damaged);
    full.tree.render().repaint_full(*whole);
    if (snapshot(*damaged) != snapshot(*whole)) {
      out << "  FAIL: pixels differ at step " << step << "\n";
      return false;
    }
  }

  out << "  OK: incremental scrolling matches a full repaint, byte for byte, across a script "
         "that crosses both overscroll clamps\n";
  return true;
}

// --------------------------------------------------------------------------
// Claim 6: keyboard scrolling (8-3c), through the ACTUAL event pipeline -
// WindowManager::post_key()/post_logical_key() onto the real SDL3 event
// queue, pump()'d back as a real dg::KeyEvent, then dg::route_key_event()
// (the shortcut router's second consumer) and dg::apply_keyboard_scroll() -
// not a KeyEvent built by hand, matching examples/21_focus's own
// check_real_tab_key_event() precedent for proving a path a user actually
// takes rather than one shaped to make the code look good.
// --------------------------------------------------------------------------

bool check_keyboard_scroll_end_to_end(std::ostream& out) {
  dg::Expected<dg::WindowManager, dg::WindowError> made = dg::WindowManager::create();
  if (!made.has_value()) {
    out << "  FAIL: could not start the window system: " << made.error().message << "\n";
    return false;
  }
  dg::WindowManager manager = std::move(made.value());
  dg::WindowSpec spec;
  spec.title = "scrolling (headless, keyboard)";
  spec.width = kSize.width;
  spec.height = kSize.height;
  const dg::Expected<dg::WindowId, dg::WindowError> window = manager.open(spec);
  if (!window.has_value()) {
    out << "  FAIL: could not open a window: " << window.error().message << "\n";
    return false;
  }

  scroll_scene::Scene scene = scroll_scene::build(spec_for(kSize));
  // No dg::Focus here, on purpose: route_key_event()'s `focused` parameter
  // is just the NodeId level 2 bubbles from, the identical thing this
  // file's own check_hit_follows_offset() passes a plain NodeId for above -
  // whether that id came from dg::Focus::current() or, as here, from
  // "whichever item the test wants focused" makes no difference to the
  // router, which never calls into dg::Focus at all.
  const dg::NodeId focused = scene.handles.vertical_items[7];
  const dg::PixelRect viewport_content =
      scene.tree.content_bounds(scene.handles.vertical_viewport);

  manager.post_logical_key(window.value(), /*down=*/true, dg::LogicalKey::kPageDown);
  const dg::PumpResult pumped = manager.pump(200);

  bool saw_page_down = false;
  bool ok = true;
  for (const dg::KeyEvent& event : pumped.key) {
    if (event.logical_key != dg::LogicalKey::kPageDown) {
      continue;
    }
    saw_page_down = true;

    const dg::ActionScopes scopes;  // these four actions are app-scope; none is registered
    const dg::RoutingContext ctx{scene.tree.render(), scene.widgets, scopes,
                                 dg::all_shortcut_bindings()};
    const dg::KeyRouteResult routed = dg::route_key_event(event, window.value(), focused, ctx);
    if (routed.outcome != dg::KeyRouteOutcome::kRouted || !routed.action.has_value() ||
        routed.action->action_id != DG_ACTION_SCROLL_PAGE_DOWN) {
      out << "  FAIL: the real posted PageDown did not route to scroll_page_down\n";
      ok = false;
      continue;
    }
    if (!dg::apply_keyboard_scroll(routed.action->action_id, focused, scene.tree,
                                   scene.widgets)) {
      out << "  FAIL: apply_keyboard_scroll reported no movement for a fresh viewport\n";
      ok = false;
    }
  }
  if (!saw_page_down) {
    out << "  FAIL: a posted SDLK_PAGEDOWN never round-tripped through pump() as a KeyEvent\n";
    ok = false;
  }

  const dg::PixelPoint offset =
      scene.tree.render().scroll_offset(scene.handles.vertical_viewport);
  if (offset.y != viewport_content.height) {
    out << "  FAIL: keyboard PageDown moved the vertical viewport by " << offset.y
        << " px, expected exactly one viewport height (" << viewport_content.height << ")\n";
    ok = false;
  }

  if (ok) {
    out << "  OK: a real SDLK_PAGEDOWN, posted onto the platform's own event queue and pumped "
           "back, routed through route_key_event() to scroll_page_down and scrolled the "
           "focused item's scroll view by exactly one viewport height\n";
  }
  return ok;
}

}  // namespace

int run(std::ostream& out) {
  out << "verify: scrolling and lists, on the scene the demo puts on screen\n";
  bool ok = true;
  ok = check_composition(out) && ok;
  ok = check_hit_follows_offset(out) && ok;
  ok = check_overscroll_clamps(out) && ok;
  ok = check_horizontal_axis(out) && ok;
  ok = check_no_relayout(out) && ok;
  ok = check_identity(out) && ok;
  ok = check_keyboard_scroll_end_to_end(out) && ok;
  out << (ok ? "PASS\n" : "FAIL\n");
  return ok ? 0 : 1;
}

}  // namespace scroll_check
