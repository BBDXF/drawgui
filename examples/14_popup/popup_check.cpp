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

// Every pixel of `crop_from` at `crop_at` against every pixel of
// `reference`, which must be exactly `crop_at`'s size. Two different
// RasterSurfaces of two different overall sizes, so row_bytes differs -
// this compares row by row rather than as one flat memcmp.
bool crop_matches(const dg::PixelView& crop_from, const PixelRect& crop_at,
                  const dg::PixelView& reference) {
  if (!crop_from.is_bgra8888 || !reference.is_bgra8888) {
    return false;
  }
  if (reference.width != crop_at.width || reference.height != crop_at.height) {
    return false;
  }
  const auto row_span = static_cast<std::size_t>(crop_at.width) * 4;
  for (int row = 0; row < crop_at.height; ++row) {
    const std::uint8_t* from_row =
        crop_from.pixels + (static_cast<std::size_t>(crop_at.y + row) * crop_from.row_bytes) +
        (static_cast<std::size_t>(crop_at.x) * 4);
    const std::uint8_t* reference_row =
        reference.pixels + (static_cast<std::size_t>(row) * reference.row_bytes);
    if (std::memcmp(from_row, reference_row, row_span) != 0) {
      return false;
    }
  }
  return true;
}

void check_placement(std::ostream& out, bool& ok) {
  out << "placement: below/above flip against hand-derived rectangles\n";

  // Fits below: anchor near the top of a 300px-tall parent, 104px of content.
  const PixelRect fits_below = dg::resolve_popup_placement(
      PixelRect{40, 40, 100, 24}, popup_scene::kContentSize, PopupPlacement::kBelow, 300);
  check(fits_below == (PixelRect{40, 64, 160, 104}), "fits below stays below", out, ok);

  // Does not fit below (anchor at y=220, content 104 tall, bottom 220+24+104
  // = 348 > 300): flips to above, top = anchor.top() - content.height = 116.
  const PixelRect flips_above = dg::resolve_popup_placement(
      PixelRect{40, 220, 100, 24}, popup_scene::kContentSize, PopupPlacement::kBelow, 300);
  check(flips_above == (PixelRect{40, 116, 160, 104}), "flips to above when below overruns",
        out, ok);

  // Neither fits (anchor square in the middle of a parent shorter than the
  // content on both sides): stays with the caller's stated preference rather
  // than oscillating - declined by name in doc/popup.md as the case this
  // slice does not resolve further (horizontal clamping is the same
  // decline).
  const PixelRect neither_fits = dg::resolve_popup_placement(
      PixelRect{40, 50, 100, 24}, popup_scene::kContentSize, PopupPlacement::kBelow, 90);
  check(neither_fits.left() == 40 && neither_fits.width == 160, "neither-fits keeps horizontal",
        out, ok);
}

int check_overlay_and_equivalence(std::ostream& out, bool& ok) {
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

  TreeSpec host_spec;
  host_spec.viewport = kHostViewport;
  host_spec.background.fill = spec.fill;
  RenderTree host_tree{host_spec};

  PopupHost host{windows};
  const PixelRect anchor{40, 40, 100, 24};

  const dg::Expected<PopupHandle, dg::WindowError> overlay =
      host.show(host_window.value(), host_tree, dg::PlatformCaps{.native_popup = false}, anchor,
                popup_scene::kContentSize, PopupPlacement::kBelow, PopupFlags{});
  if (!overlay) {
    out << "  FAIL: overlay show() failed: " << overlay.error().message << "\n";
    return 1;
  }
  PopupHandle overlay_handle = overlay.value();
  check(overlay_handle.open && !overlay_handle.is_native, "overlay show() opened, non-native",
        out, ok);
  popup_scene::build_menu_content(*overlay_handle.tree, overlay_handle.content_root,
                                  popup_scene::kContentSize);

  std::optional<RasterSurface> host_surface =
      RasterSurface::create(kHostViewport.width, kHostViewport.height);
  std::optional<RasterSurface> native_surface =
      RasterSurface::create(popup_scene::kContentSize.width, popup_scene::kContentSize.height);
  if (!host_surface.has_value() || !native_surface.has_value()) {
    out << "  FAIL: could not allocate a surface\n";
    return 1;
  }
  host_tree.repaint_full(*host_surface);

  // The native branch's content, built through the identical
  // build_menu_content() call against a tree/surface pair shaped exactly the
  // way PopupHost::show()'s own native branch builds one (TreeSpec sized to
  // content_size, root() as the parent) - this is the real graphics
  // pipeline (RenderTree + Skia CPU raster), not a stand-in, run without a
  // real second OS window because SDL_CreatePopupWindow itself is not
  // available under the headless dummy driver (see run()'s own attempt
  // below). What this DOES prove: the same content nodes, at the same local
  // geometry, rasterize to the same bytes whether their tree is a fresh
  // instance (as the native branch's would be) or a subtree of the host's
  // own tree cropped out (the overlay branch, exercised above for real).
  // What it does NOT prove: that a genuine second OS window's own
  // presentation pipeline (a second SDL surface, SDL_UpdateWindowSurfaceRects)
  // reproduces this - that claim is what the interactive mode's manual
  // measurement is for.
  TreeSpec native_spec;
  native_spec.viewport = popup_scene::kContentSize;
  RenderTree native_tree{native_spec};
  popup_scene::build_menu_content(native_tree, RenderTree::root(), popup_scene::kContentSize);
  native_tree.repaint_full(*native_surface);

  const bool equivalent =
      crop_matches(host_surface->peek_pixels(), overlay_handle.content_bounds,
                   native_surface->peek_pixels());
  check(equivalent, "overlay content == native-equivalent content, byte for byte", out, ok);

  // Dismissal: click outside closes it.
  const dg::PointerEvent outside_click{host_window.value(), dg::PointerAction::kDown, 5, 5};
  const bool dismissed_by_click =
      host.handle_pointer(overlay_handle, host_window.value(), outside_click, PopupFlags{});
  check(dismissed_by_click && !overlay_handle.open, "click outside dismisses the overlay", out,
        ok);

  // Once closed, the region it used to occupy is not hittable as the
  // popup's content any more - the clip-to-empty technique this slice uses
  // instead of true node removal (RenderTree is append-only).
  host_tree.repaint_full(*host_surface);
  const std::optional<dg::NodeId> hit_after_close =
      host_tree.hit_test(dg::PixelPoint{anchor.left() + 10, anchor.bottom() + 10});
  check(hit_after_close != overlay_handle.content_root, "closed overlay is not hittable", out,
        ok);

  // Re-open, click inside: stays open.
  const dg::Expected<PopupHandle, dg::WindowError> reopened =
      host.show(host_window.value(), host_tree, dg::PlatformCaps{.native_popup = false}, anchor,
                popup_scene::kContentSize, PopupPlacement::kBelow, PopupFlags{});
  if (!reopened) {
    out << "  FAIL: re-open failed: " << reopened.error().message << "\n";
    return 1;
  }
  PopupHandle reopened_handle = reopened.value();
  const dg::PointerEvent inside_click{host_window.value(), dg::PointerAction::kDown,
                                      reopened_handle.content_bounds.x + 10,
                                      reopened_handle.content_bounds.y + 10};
  const bool stayed_open =
      !host.handle_pointer(reopened_handle, host_window.value(), inside_click, PopupFlags{});
  check(stayed_open && reopened_handle.open, "click inside does not dismiss the overlay", out,
        ok);

  // Escape closes it.
  const dg::KeyEvent escape{host_window.value(), dg::KeyAction::kDown, dg::Key::kEscape, false};
  const bool dismissed_by_escape =
      host.handle_key(reopened_handle, host_window.value(), escape, PopupFlags{});
  check(dismissed_by_escape && !reopened_handle.open, "Escape dismisses the overlay", out, ok);

  return 0;
}

int attempt_native(std::ostream& out) {
  out << "native branch: attempting a real SDL_CreatePopupWindow under the headless dummy "
         "driver (measured, not assumed, that this fails)\n";
  dg::Expected<dg::WindowManager, dg::WindowError> made = dg::WindowManager::create();
  if (!made) {
    out << "  SKIP (loud, specific): could not even start the window system: "
        << made.error().message << "\n";
    return 0;
  }
  dg::WindowManager windows = std::move(made.value());
  dg::WindowSpec spec;
  spec.title = "popup host (native attempt)";
  spec.width = kHostViewport.width;
  spec.height = kHostViewport.height;
  const dg::Expected<dg::WindowId, dg::WindowError> host_window = windows.open(spec);
  if (!host_window) {
    out << "  SKIP (loud, specific): could not open a host window under the dummy driver: "
        << host_window.error().message << "\n";
    return 0;
  }
  TreeSpec host_spec;
  host_spec.viewport = kHostViewport;
  RenderTree host_tree{host_spec};
  PopupHost host{windows};
  const dg::Expected<PopupHandle, dg::WindowError> native =
      host.show(host_window.value(), host_tree, dg::PlatformCaps{.native_popup = true},
                PixelRect{40, 40, 100, 24}, popup_scene::kContentSize, PopupPlacement::kBelow,
                PopupFlags{});
  if (!native) {
    out << "  SKIPPED, loudly and specifically: SDL_CreatePopupWindow failed under the SDL "
           "dummy video driver used for headless CI, with: \""
        << native.error().message
        << "\". This is the expected, measured behaviour (doc/popup.md records it) - the "
           "dummy driver can open ordinary windows but not real popup windows, so this "
           "branch's real-OS-window claim is verified instead by examples/14_popup's "
           "interactive mode against an actual display. Not a silent pass: the attempt above "
           "ran for real and its failure is printed.\n";
    return 0;
  }
  out << "  UNEXPECTED PASS: this environment's dummy driver created a real popup window. "
         "Treating this as a bonus success, not a failure.\n";
  PopupHandle handle = native.value();
  host.close(handle);
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

  out << "POPUP: placement math, the overlay branch through the real API, and the "
         "same-content equivalence oracle - PopupHost's single most valuable artifact.\n\n";

  bool ok = true;
  check_placement(out, ok);
  out << "\noverlay branch + equivalence (dummy-driver window, no display needed):\n";
  if (check_overlay_and_equivalence(out, ok) != 0) {
    return 1;
  }
  out << "\n";
  attempt_native(out);

  out << (ok ? "\nPASS\n" : "\nFAIL\n");
  return ok ? 0 : 1;
}

}  // namespace popup_check
