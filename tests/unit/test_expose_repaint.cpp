// Regression test for the B-2 defect: "the popup window
// was painted once and never again ... no example loop consumed
// PumpResult::needs_repaint - a permanently black popup". The mechanism is
// in src/platform/sdl3/window_manager.cpp's own paint(): its first line is
// `if (!owned.shows_fill) { return; }`, and present() sets shows_fill false
// FOREVER, the first time it is called. After that, an expose reaching the
// platform layer does not paint anything - `needs_repaint` on PumpResult is
// the ONLY notification a caller gets that a window needs a fresh frame.
// Miss that vector and the window is exactly the tooltip B-2 describes:
// alive, receiving real expose events, and never drawing again.
//
// This pairs with test_first_frame_present.cpp, which pins the FIRST frame
// (repaint_full() reaching the window at all); this file pins every expose
// AFTER that first frame, when the platform's own paint() has already
// degraded to a no-op and the caller is the only thing left that can react.
//
// The dummy video driver never generates a real
// SDL_EVENT_WINDOW_EXPOSED on its own (window_manager.h's own comment on
// post_expose() explains why) - there is no compositor under dummy to send
// one. WindowManager::post_expose() is the ONLY way to drive this path
// under test: it pushes a real SDL_EVENT_WINDOW_EXPOSED onto SDL's own
// event queue, the same route post_pointer_button()/post_key() already use
// and that 23 examples' own --verify-* have exercised - not a shortcut
// around dispatch(), a real trip through it.
//
// A REAL dg::WindowManager, matching tests/unit/test_clipboard.cpp's own
// precedent (its own comment explains why: a fake here would prove only
// that the fake agrees with itself). SDL_VIDEODRIVER=dummy is forced
// before every WindowManager::create() call below, and each TEST_CASE
// builds its own WindowManager so no state can leak between cases.

#include <cstdlib>
#include <optional>

#include <doctest/doctest.h>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/graphics/types.h"
#include "drawgui/window/window_manager.h"

namespace {

using dg::Color;
using dg::Expected;
using dg::ImageView;
using dg::PixelFormat;
using dg::PixelSize;
using dg::PixelView;
using dg::PointerButton;
using dg::PumpResult;
using dg::RasterSurface;
using dg::WindowError;
using dg::WindowId;
using dg::WindowManager;
using dg::WindowSpec;

constexpr PixelSize kViewport{64, 48};

WindowManager make_headless_manager() {
  setenv("SDL_VIDEODRIVER", "dummy", 1);
  Expected<WindowManager, WindowError> made = WindowManager::create();
  REQUIRE(made.has_value());
  return std::move(made.value());
}

WindowId open_test_window(WindowManager& manager) {
  WindowSpec spec;
  spec.title = "expose-repaint test";
  spec.width = kViewport.width;
  spec.height = kViewport.height;
  spec.fill = Color::from_argb(0xFF000000);
  const Expected<WindowId, WindowError> opened = manager.open(spec);
  REQUIRE(opened.has_value());
  return opened.value();
}

bool contains(const std::vector<WindowId>& ids, WindowId id) {
  for (const WindowId& candidate : ids) {
    if (candidate.value == id.value) {
      return true;
    }
  }
  return false;
}

// Presents one real frame so showing_fill() flips to false and the
// platform's own paint() (src/platform/sdl3/window_manager.cpp) becomes a
// no-op for this window from then on - matching
// test_first_frame_present.cpp's own present_from_painted() helper, whose
// comment explains why present() (not paint(), not a direct field flip) is
// what real code path this needs to go through.
void present_one_frame(WindowManager& manager, WindowId window) {
  std::optional<RasterSurface> surface =
      RasterSurface::create(kViewport.width, kViewport.height);
  REQUIRE(surface.has_value());
  const PixelView view = surface->peek_pixels();
  REQUIRE(view.pixels != nullptr);
  REQUIRE(view.is_bgra8888);
  const ImageView image{view.pixels, view.width, view.height, view.row_bytes,
                        PixelFormat::kBgra8888};
  const std::vector<dg::PixelRect> whole_viewport{
      dg::PixelRect{0, 0, kViewport.width, kViewport.height}};
  const Expected<void, WindowError> presented =
      manager.present(window, image, whole_viewport);
  REQUIRE(presented.has_value());
}

}  // namespace

TEST_SUITE("expose -> needs_repaint (B-2 permanently-black-popup regression)") {
  TEST_CASE("expose on a never-presented window reports needs_repaint") {
    // Given: a freshly opened window that has never had present() called on
    // it - showing_fill() should still be true, matching how a window looks
    // right after open().
    WindowManager manager = make_headless_manager();
    const WindowId window = open_test_window(manager);
    REQUIRE(manager.showing_fill(window).value());

    // When: a real expose is pushed and pumped.
    manager.post_expose(window);
    const PumpResult result = manager.pump(50);

    // Then: the caller is told this window needs a repaint, and the window
    // is unchanged otherwise (still showing its flat fill - nothing else
    // painted it).
    CHECK(contains(result.needs_repaint, window));
    CHECK(manager.showing_fill(window).value());
  }

  TEST_CASE("expose AFTER the first present still reports needs_repaint (B-2)") {
    // Given: a window that has already been presented once - the platform
    // layer's own paint() is now permanently a no-op for it (its first line
    // is `if (!owned.shows_fill) { return; }`, and present() clears
    // shows_fill forever the first time it runs).
    WindowManager manager = make_headless_manager();
    const WindowId window = open_test_window(manager);

    present_one_frame(manager, window);
    REQUIRE_FALSE(manager.showing_fill(window).value());

    // When: a SECOND real expose arrives - exactly what a compositor sends
    // when, say, another window is dragged across this one.
    manager.post_expose(window);
    const PumpResult result = manager.pump(50);

    // Then: needs_repaint STILL reports this window. The platform's own
    // paint() did nothing this time (shows_fill is false) - if this
    // assertion is the one that fails, `needs_repaint` is the ONLY signal a
    // real caller has left, and losing it here is exactly the permanently
    // black tooltip B-2 describes.
    CHECK(contains(result.needs_repaint, window));
  }

  TEST_CASE("expose on an unknown window id is silently ignored") {
    // Given: no window with this id has ever been opened.
    WindowManager manager = make_headless_manager();
    (void)open_test_window(manager);  // a real window exists, just not this one
    const WindowId unknown_window{0xFFFFFFFFu};

    // When: post_expose() is asked to expose it anyway. post_expose()'s own
    // implementation returns early (src/platform/sdl3/window_manager.cpp,
    // the `if (impl_->find(id.value) == impl_->windows.end()) { return; }`
    // guard) rather than pushing a bogus event.
    manager.post_expose(unknown_window);
    const PumpResult result = manager.pump(20);

    // Then: nothing is reported for it - and nothing else broke either.
    CHECK_FALSE(contains(result.needs_repaint, unknown_window));
  }

  TEST_CASE("needs_repaint and a pointer event can both be reported from one pump()") {
    // Given: window_manager.h's own comment on PumpResult::pointer states an
    // ordering RULE ("A caller must handle needs_repaint BEFORE these"), not
    // a claim about vector layout - both vectors are independent fields on
    // the same struct, and nothing here can observe interleaving between
    // them. What this pins is only that both CAN be non-empty out of the
    // same pump() call - a caller that only checked one field, assuming the
    // other could never also fire this round, would be relying on a
    // guarantee this struct never made.
    WindowManager manager = make_headless_manager();
    const WindowId window = open_test_window(manager);

    // When: an expose and a pointer press are posted in the same round.
    manager.post_expose(window);
    manager.post_pointer_button(window, /*down=*/true, 5, 5, PointerButton::kPrimary);
    const PumpResult result = manager.pump(50);

    // Then: both vectors carry their own event out of the one pump() call.
    CHECK(contains(result.needs_repaint, window));
    CHECK_FALSE(result.pointer.empty());
  }
}
