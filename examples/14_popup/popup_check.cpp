#include "popup_check.h"

#include <cstdlib>
#include <cstring>
#include <optional>
#include <ostream>
#include <utility>

#include "drawgui/base/expected.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/window/popup_host.h"
#include "drawgui/window/window_manager.h"

#include "popup_scene.h"
#include "popup_window.h"

namespace popup_check {
namespace {

using dg::Color;
using dg::PixelRect;
using dg::PixelSize;
using dg::PopupFlags;
using dg::PopupHandle;
using dg::PopupHost;
using dg::PopupPlacement;
using dg::RasterSurface;
using dg::RenderTree;
using dg::TreeSpec;

constexpr PixelSize kHostViewport{400, 300};

bool check(bool condition, const char* what, std::ostream& out, bool& ok) {
  out << "  " << (condition ? "PASS" : "FAIL") << " " << what << "\n";
  ok = ok && condition;
  return condition;
}

void check_placement(std::ostream& out, bool& ok) {
  out << "placement: below/above and left/right flip, plus clamp, against hand-derived "
         "rectangles\n";

  // Fits below: anchor near the top of a 400x300 parent, 160x104 content.
  // Horizontally the anchor's left edge (40) plus content width (160) is
  // 200, comfortably inside 400, so no horizontal flip/clamp triggers here.
  const PixelRect fits_below =
      dg::resolve_popup_placement(PixelRect{40, 40, 100, 24}, popup_scene::kContentSize,
                                  PopupPlacement::kBelow, PixelSize{400, 300});
  check(fits_below == (PixelRect{40, 64, 160, 104}), "fits below stays below", out, ok);

  // Does not fit below (anchor at y=220, content 104 tall, bottom 220+24+104
  // = 348 > 300): flips to above, top = anchor.top() - content.height = 116.
  const PixelRect flips_above =
      dg::resolve_popup_placement(PixelRect{40, 220, 100, 24}, popup_scene::kContentSize,
                                  PopupPlacement::kBelow, PixelSize{400, 300});
  check(flips_above == (PixelRect{40, 116, 160, 104}), "flips to above when below overruns",
        out, ok);

  // Neither vertical side fits (anchor bottom 74, content 104 tall, against
  // a 90px-tall parent: below overruns to 178, above goes negative to -54).
  // Hand-derived: use_below stays true (the caller's stated kBelow
  // preference, since above doesn't fit either so the flip condition never
  // triggers), giving top = anchor.bottom() = 74 before clamping. But
  // content.height (104) >= parent height (90), so the clamp interval
  // [0, 90-104] is empty/inverted and this falls into the "content taller
  // than parent" branch instead: top = 0. Horizontally nothing changes
  // (40 + 160 = 200 <= 400), so left stays 40.
  const PixelRect neither_fits =
      dg::resolve_popup_placement(PixelRect{40, 50, 100, 24}, popup_scene::kContentSize,
                                  PopupPlacement::kBelow, PixelSize{400, 90});
  check(neither_fits == (PixelRect{40, 0, 160, 104}),
        "neither vertical side fits: top-aligns instead of overrunning", out, ok);

  // --- Horizontal: flip, clamp, and the content-wider-than-parent edge case,
  // mirroring the vertical cases above. Parent is 400 wide in every case
  // below; vertical stays trivial (anchor near the top, content well under
  // 300 tall) so only the horizontal axis is exercised.

  // Anchor near the right edge: left-aligned (anchor.left() + content.width
  // = 350 + 160 = 510 > 400) does not fit, but right-aligned to the anchor
  // (anchor.right() - content.width = 370 - 160 = 210 >= 0) does. Flips to
  // left = 210.
  const PixelRect flips_right =
      dg::resolve_popup_placement(PixelRect{350, 40, 20, 24}, popup_scene::kContentSize,
                                  PopupPlacement::kBelow, PixelSize{400, 300});
  check(flips_right == (PixelRect{210, 64, 160, 104}),
        "flips to right-aligned-to-anchor when left-aligned overruns", out, ok);

  // Anchor extremely close to the right edge with content almost as wide as
  // the parent (380 of 400): left-aligned overruns (350+380=730>400) AND
  // right-aligned-to-anchor goes negative (anchor.right()=360, 360-380=-20
  // < 0). Neither flip fits but content (380) is still narrower than the
  // parent (400), so this clamps rather than falling into the
  // wider-than-parent branch: left = parent.width - content.width =
  // 400 - 380 = 20 (flush against the parent's own right edge).
  const PixelSize wide_content{380, 40};
  const PixelRect clamped_right = dg::resolve_popup_placement(
      PixelRect{350, 40, 10, 24}, wide_content, PopupPlacement::kBelow, PixelSize{400, 300});
  check(clamped_right.left() == 20, "neither horizontal side fits: clamps to the right edge",
        out, ok);

  // Anchor near the left edge with content wider than the parent itself
  // (450 of 400): both flips fail for the same reason as the vertical
  // content-taller-than-parent case, and the clamp interval
  // [0, 400-450] is empty/inverted, so this left-aligns at 0 instead of
  // producing a negative left edge.
  const PixelSize overwide_content{450, 40};
  const PixelRect overwide = dg::resolve_popup_placement(
      PixelRect{10, 40, 20, 24}, overwide_content, PopupPlacement::kBelow, PixelSize{400, 300});
  check(overwide.left() == 0, "content wider than the parent: left-aligns at 0", out, ok);
}

// The overlay branch driven through the real API against a real (dummy
// video driver) WindowManager - PopupHost is overlay-only now (doc/popup.md
// B-1/B-2), so there is no second branch left to hold this equal against;
// what this checks is that PopupHost::show()/close()/handle_pointer()/
// handle_key() behave correctly end to end against a real host window and
// a real placement flip, which is the whole of what a caller needs from
// this type.
int check_overlay(std::ostream& out, bool& ok) {
  dg::Expected<dg::WindowManager, dg::WindowError> made = dg::WindowManager::create();
  if (!made) {
    out << "  FAIL: could not start the window system (even under the dummy driver): "
        << made.error().message << "\n";
    return 1;
  }
  dg::WindowManager windows = std::move(made.value());

  dg::WindowSpec spec;
  spec.title = "popup host (headless)";
  spec.width = kHostViewport.width;
  spec.height = kHostViewport.height;
  spec.fill = Color::from_argb(0xFF14171C);
  const dg::Expected<dg::WindowId, dg::WindowError> host_window = windows.open(spec);
  if (!host_window) {
    out << "  FAIL: could not open the host window: " << host_window.error().message << "\n";
    return 1;
  }

  // Runner's own first-frame viewport must be the window's real
  // drawable_size(), never Settings::size - see
  // examples/22_dropdown_menu/dropdown_check.cpp's own assertion of the
  // same shape for the full argument on why this pins the real function
  // Runner's constructor calls rather than a duplicate.
  popup_window::Settings runner_settings;
  runner_settings.size = kHostViewport;
  const dg::PixelSize drawable =
      windows.drawable_size(host_window.value()).value_or(dg::PixelSize{});
  check(popup_window::initial_viewport(windows, host_window.value(), runner_settings) == drawable,
       "Runner's initial viewport is manager.drawable_size(), not Settings::size", out, ok);

  TreeSpec host_spec;
  host_spec.viewport = kHostViewport;
  host_spec.background.fill = spec.fill;
  RenderTree host_tree{host_spec};

  PopupHost host{windows};
  const PixelRect anchor{40, 40, 100, 24};

  const dg::Expected<PopupHandle, dg::WindowError> opened =
      host.show(host_window.value(), host_tree, anchor, popup_scene::kContentSize,
                PopupPlacement::kBelow, PopupFlags{});
  if (!opened) {
    out << "  FAIL: show() failed: " << opened.error().message << "\n";
    return 1;
  }
  PopupHandle handle = opened.value();
  check(handle.open, "show() opened the overlay", out, ok);

  // The real placement math, exercised through the real API rather than
  // only checked standalone above: this anchor is near the top of a
  // 300px-tall host, so it must have stayed below, at the same rectangle
  // resolve_popup_placement() itself would produce.
  const PixelRect expected_bounds = dg::resolve_popup_placement(
      anchor, popup_scene::kContentSize, PopupPlacement::kBelow, kHostViewport);
  check(handle.content_bounds == expected_bounds,
        "show()'s content_bounds matches resolve_popup_placement() for this anchor", out, ok);

  popup_scene::build_menu_content(host_tree, handle.content_root, popup_scene::kContentSize);

  std::optional<RasterSurface> host_surface =
      RasterSurface::create(kHostViewport.width, kHostViewport.height);
  if (!host_surface.has_value()) {
    out << "  FAIL: could not allocate a surface\n";
    return 1;
  }
  host_tree.repaint_full(*host_surface);

  // Dismissal: click outside closes it.
  const dg::PointerEvent outside_click{host_window.value(), dg::PointerAction::kDown, 5, 5};
  const bool dismissed_by_click =
      host.handle_pointer(handle, host_window.value(), outside_click, PopupFlags{});
  check(dismissed_by_click && !handle.open, "click outside dismisses the overlay", out, ok);

  // Once closed, the region it used to occupy is not hittable as the
  // popup's content any more - the clip-to-empty technique this slice uses
  // instead of true node removal (RenderTree is append-only).
  host_tree.repaint_full(*host_surface);
  const std::optional<dg::NodeId> hit_after_close =
      host_tree.hit_test(dg::PixelPoint{anchor.left() + 10, anchor.bottom() + 10});
  check(hit_after_close != handle.content_root, "closed overlay is not hittable", out, ok);

  // Re-open near the BOTTOM of the host window this time, so this exercises
  // the flip-to-above branch through the real API too, not just fits-below.
  const PixelRect low_anchor{40, 220, 100, 24};
  const dg::Expected<PopupHandle, dg::WindowError> reopened =
      host.show(host_window.value(), host_tree, low_anchor, popup_scene::kContentSize,
                PopupPlacement::kBelow, PopupFlags{});
  if (!reopened) {
    out << "  FAIL: re-open failed: " << reopened.error().message << "\n";
    return 1;
  }
  PopupHandle reopened_handle = reopened.value();
  const PixelRect expected_flip = dg::resolve_popup_placement(
      low_anchor, popup_scene::kContentSize, PopupPlacement::kBelow, kHostViewport);
  check(expected_flip.top() < low_anchor.top(), "this anchor genuinely needs the flip", out, ok);
  check(reopened_handle.content_bounds == expected_flip,
        "show() flips to above for an anchor near the bottom, through the real API", out, ok);

  const dg::PointerEvent inside_click{host_window.value(), dg::PointerAction::kDown,
                                      reopened_handle.content_bounds.x + 10,
                                      reopened_handle.content_bounds.y + 10};
  const bool stayed_open =
      !host.handle_pointer(reopened_handle, host_window.value(), inside_click, PopupFlags{});
  check(stayed_open && reopened_handle.open, "click inside does not dismiss the overlay", out,
        ok);

  // Escape closes it.
  const dg::KeyEvent escape{host_window.value(), dg::KeyAction::kDown, dg::Key::kEscape};
  const bool dismissed_by_escape =
      host.handle_key(reopened_handle, host_window.value(), escape, PopupFlags{});
  check(dismissed_by_escape && !reopened_handle.open, "Escape dismisses the overlay", out, ok);

  // Re-open near the RIGHT edge of the host window, exercising the
  // left/right flip through the real API too, not just standalone above.
  const PixelRect right_anchor{350, 40, 20, 24};
  const dg::Expected<PopupHandle, dg::WindowError> reopened_right =
      host.show(host_window.value(), host_tree, right_anchor, popup_scene::kContentSize,
                PopupPlacement::kBelow, PopupFlags{});
  if (!reopened_right) {
    out << "  FAIL: re-open (right anchor) failed: " << reopened_right.error().message << "\n";
    return 1;
  }
  PopupHandle right_handle = reopened_right.value();
  const PixelRect expected_right_flip = dg::resolve_popup_placement(
      right_anchor, popup_scene::kContentSize, PopupPlacement::kBelow, kHostViewport);
  check(expected_right_flip.left() < right_anchor.left(),
        "this anchor genuinely needs the horizontal flip", out, ok);
  check(right_handle.content_bounds == expected_right_flip,
        "show() flips to right-aligned-to-anchor for an anchor near the right edge, through "
        "the real API",
        out, ok);

  return 0;
}

}  // namespace

int run(std::ostream& out) {
  // Guarantees this check runs the same way in CI (no DISPLAY at all) and on
  // a developer machine with a real one: SDL3's dummy video driver opens
  // ordinary ("normal") windows successfully with no display whatsoever -
  // measured directly for this slice - which is what lets the overlay
  // branch and PopupHost's dismissal routing be exercised for real, through
  // a genuine dg::WindowManager, rather than skipped.
  setenv("SDL_VIDEODRIVER", "dummy", 1);

  out << "POPUP: placement math standalone, then the same math exercised through the real "
         "overlay API (PopupHost is overlay-only now - doc/popup.md's B-1/B-2 finding deleted "
         "the native branch this check used to also hold equal).\n\n";

  bool ok = true;
  check_placement(out, ok);
  out << "\noverlay branch through the real API (dummy-driver window, no display needed):\n";
  if (check_overlay(out, ok) != 0) {
    return 1;
  }

  out << (ok ? "\nPASS\n" : "\nFAIL\n");
  return ok ? 0 : 1;
}

}  // namespace popup_check
