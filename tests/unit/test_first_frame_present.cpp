// Regression test for the black-window-until-mouse-moves bug: an example's
// first frame calling RenderTree::repaint_full() and then presenting only if
// tree.damage() is non-empty NEVER reaches the window, because repaint_full()
// itself retires damage (RenderTree::repaint_full()'s own doc comment:
// "discarding accumulated damage" - see render_tree.cpp's retire_damage()).
// examples/14_popup, 21_focus, 22_dropdown_menu, 23_menu_tooltip_dialog and
// 25_showcase all had exactly this shape at their first frame.
//
// A REAL dg::WindowManager, matching tests/unit/test_clipboard.cpp's own
// precedent (its own comment explains why: a free function/fake would let a
// test construct nothing at all and still compile). SDL_VIDEODRIVER=dummy
// opens an ordinary window headlessly (doc/popup.md section 1's own
// measurement: dummy fails only SDL_CreatePopupWindow, not an ordinary
// window), which is what makes the actual first-frame present path
// reachable here at all.
//
// WindowManager::showing_fill() is a narrow, test-only observation point
// added alongside this test (window_manager.h's own comment on it) onto REAL
// engine state the window manager already tracked for its own purpose
// (whether to redraw the flat WindowSpec::fill on an expose event) - not a
// counter invented only for this test to increment. It is the only way to
// observe, from outside SDL, whether present() actually ran: nothing else
// this class exposes distinguishes "present() was never called" from
// "present() was called and did nothing interesting".

#include <cstdlib>
#include <optional>

#include <doctest/doctest.h>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/graphics/types.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/window/window_manager.h"

namespace {

using dg::Color;
using dg::Expected;
using dg::ImageView;
using dg::PixelFormat;
using dg::PixelSize;
using dg::PixelView;
using dg::RasterSurface;
using dg::RenderTree;
using dg::TreeSpec;
using dg::WindowError;
using dg::WindowId;
using dg::WindowManager;
using dg::WindowSpec;

constexpr PixelSize kViewport{64, 48};

// A window whose flat WindowSpec::fill is visibly different from the scene's
// own root background - not load-bearing for the assertions below (which
// read showing_fill(), not pixels), but it is what a screenshot-based human
// report ("black window") would actually be seeing, so the fixture matches
// the reported bug rather than an arbitrary pair of colours.
WindowManager make_headless_manager() {
  setenv("SDL_VIDEODRIVER", "dummy", 1);
  Expected<WindowManager, WindowError> made = WindowManager::create();
  REQUIRE(made.has_value());
  return std::move(made.value());
}

WindowId open_test_window(WindowManager& manager) {
  WindowSpec spec;
  spec.title = "first-frame-present test";
  spec.width = kViewport.width;
  spec.height = kViewport.height;
  spec.fill = Color::from_argb(0xFF000000);  // the black the bug report saw
  const Expected<WindowId, WindowError> opened = manager.open(spec);
  REQUIRE(opened.has_value());
  return opened.value();
}

RenderTree build_scene() {
  TreeSpec spec;
  spec.viewport = kViewport;
  spec.background.fill = Color::from_argb(0xFFCC3366);  // visibly not black
  return RenderTree{spec};
}

// Mirrors present_painted(), the fix now applied at every affected example's
// first frame: presents unconditionally from painted(), which repaint_full()
// has just set to the whole viewport.
void present_from_painted(WindowManager& manager, WindowId window, RenderTree& tree,
                          RasterSurface& surface) {
  const PixelView view = surface.peek_pixels();
  if (view.pixels == nullptr || !view.is_bgra8888) {
    return;
  }
  const ImageView image{view.pixels, view.width, view.height, view.row_bytes,
                        PixelFormat::kBgra8888};
  (void)manager.present(window, image, tree.painted().rects());
}

}  // namespace

TEST_SUITE("first-frame present (black-window regression)") {
  TEST_CASE("repaint_full() followed by present_from_painted() DOES reach the window") {
    // Given: a fresh window (still showing its flat fill) and a scene whose
    // first frame is drawn with repaint_full().
    WindowManager manager = make_headless_manager();
    const WindowId window = open_test_window(manager);
    RenderTree tree = build_scene();
    std::optional<RasterSurface> surface =
        RasterSurface::create(kViewport.width, kViewport.height);
    REQUIRE(surface.has_value());
    REQUIRE(manager.showing_fill(window).value());

    // When: the first frame uses the fixed pattern - present unconditionally
    // from painted() rather than guarding on the damage repaint_full() has
    // already discarded (RenderTree::repaint_full()'s own doc comment:
    // "discarding accumulated damage").
    tree.repaint_full(*surface);
    present_from_painted(manager, window, tree, *surface);

    // Then: the window has actually received a frame, not just rasterized
    // pixels sitting unseen in `surface`.
    CHECK_FALSE(manager.showing_fill(window).value());
  }
}
