// Unit tests for resolve_popup_placement() - the pure placement math
// PopupHost::show() applies. Pure function, no WindowManager/SDL/surface
// needed (popup_host.h's own comment on resolve_popup_placement() names this
// file by name), so this is the direct oracle: every rectangle below is
// hand-derived, not "call the implementation and compare it to itself".
//
// examples/14_popup/popup_check.cpp exercises the same function through the
// real PopupHost::show() API against a real (dummy-driver) WindowManager;
// this file is the exhaustive matrix that would be unwieldy to drive through
// that heavier path.

#include <doctest/doctest.h>

#include "drawgui/window/popup_host.h"

namespace {

using dg::PixelRect;
using dg::PixelSize;
using dg::PopupPlacement;
using dg::resolve_popup_placement;

// --- Vertical axis -----------------------------------------------------

TEST_CASE("vertical: below fits, stays below") {
  // anchor bottom = 40+24 = 64; 64+104 = 168 <= 300: fits.
  const PixelRect result = resolve_popup_placement(
      PixelRect{40, 40, 100, 24}, PixelSize{160, 104}, PopupPlacement::kBelow, PixelSize{400, 300});
  CHECK(result == PixelRect{40, 64, 160, 104});
}

TEST_CASE("vertical: below does not fit, above does, flips to above") {
  // anchor at y=220: below would end at 220+24+104=348 > 300, overruns.
  // above: anchor.top() - content.height = 220-104 = 116 >= 0, fits.
  const PixelRect result =
      resolve_popup_placement(PixelRect{40, 220, 100, 24}, PixelSize{160, 104},
                              PopupPlacement::kBelow, PixelSize{400, 300});
  CHECK(result == PixelRect{40, 116, 160, 104});
}

TEST_CASE("vertical: neither side fits, clamps into the parent") {
  // anchor at y=50, height 24 -> bottom=74. Parent height 150.
  // below: 74+104=178 > 150, overruns.
  // above: 50-104=-54 < 0, overruns.
  // Neither flip condition triggers (each fallback also fails), so
  // use_below keeps the caller's kBelow preference: top = anchor.bottom() =
  // 74 before clamping. content.height (104) < parent.height (150), so this
  // clamps rather than top-aligning: top = parent.height - content.height =
  // 150 - 104 = 46.
  const PixelRect result =
      resolve_popup_placement(PixelRect{40, 50, 100, 24}, PixelSize{160, 104},
                              PopupPlacement::kBelow, PixelSize{400, 150});
  CHECK(result == PixelRect{40, 46, 160, 104});
}

TEST_CASE("vertical: content taller than the parent top-aligns instead of overrunning") {
  // content.height (104) >= parent.height (90): the clamp interval
  // [0, 90-104] is empty/inverted, so this top-aligns at 0 rather than
  // producing a negative top.
  const PixelRect result =
      resolve_popup_placement(PixelRect{40, 50, 100, 24}, PixelSize{160, 104},
                              PopupPlacement::kBelow, PixelSize{400, 90});
  CHECK(result == PixelRect{40, 0, 160, 104});
}

TEST_CASE("vertical: an anchor already above the parent clamps to 0") {
  // anchor.top()=-20 (already off-screen above). below "fits" by the height
  // check alone (-20+24+104=108<=300) so use_below stays true and
  // top=anchor.bottom()=4, which is not negative here - construct instead
  // an above-preferred anchor that IS off the top: preferred kAbove,
  // anchor.top()=-20. above_fits: -20-104=-124<0, does not fit; below_fits:
  // -20+24+104=108<=300, does fit, so use_below flips to true (the
  // preferred-above branch's own fallback), giving top=anchor.bottom()=4 -
  // still not the case this test wants. To reach top<0 by construction
  // needs BOTH sides to fail so the flip never triggers and the initial
  // (already off-screen) side is kept as-is: anchor.top()=-20, height 24,
  // parent height 10 (shorter than the anchor's own bottom). below_fits:
  // -20+24+104=108<=10? no. above_fits: -20-104=-124>=0? no. preferred
  // kAbove keeps use_below=false, top=anchor.top()-content.height=-124.
  // content.height (104) >= parent.height (10): top-aligns at 0 (the
  // content-taller-than-parent branch, not the plain clamp one - both
  // produce 0 here, which is why this case cannot isolate top<0 alone;
  // see this file's horizontal analog instead, which can).
  const PixelRect result =
      resolve_popup_placement(PixelRect{40, -20, 100, 24}, PixelSize{160, 104},
                              PopupPlacement::kAbove, PixelSize{400, 10});
  CHECK(result.top() == 0);
}

// --- Horizontal axis -----------------------------------------------------

TEST_CASE("horizontal: left-aligned fits, stays left-aligned") {
  // anchor.left() (40) + content.width (160) = 200 <= 400: fits.
  const PixelRect result = resolve_popup_placement(
      PixelRect{40, 40, 100, 24}, PixelSize{160, 104}, PopupPlacement::kBelow, PixelSize{400, 300});
  CHECK(result.left() == 40);
}

TEST_CASE("horizontal: left-aligned does not fit, right-aligned does, flips") {
  // anchor{350,40,20,24}: left-aligned would end at 350+160=510 > 400.
  // right-aligned-to-anchor: anchor.right() - content.width = 370-160=210
  // >= 0, fits.
  const PixelRect result =
      resolve_popup_placement(PixelRect{350, 40, 20, 24}, PixelSize{160, 104},
                              PopupPlacement::kBelow, PixelSize{400, 300});
  CHECK(result == PixelRect{210, 64, 160, 104});
}

TEST_CASE("horizontal: neither side fits, clamps to the parent's right edge") {
  // anchor{350,40,10,24}, content width 380 (narrower than the 400-wide
  // parent). left-aligned: 350+380=730 > 400, overruns.
  // right-aligned-to-anchor: anchor.right()=360; 360-380=-20 < 0, overruns.
  // content.width (380) < parent.width (400), so this clamps rather than
  // left-aligning at 0: left = parent.width - content.width = 400-380 = 20.
  const PixelRect result =
      resolve_popup_placement(PixelRect{350, 40, 10, 24}, PixelSize{380, 40},
                              PopupPlacement::kBelow, PixelSize{400, 300});
  CHECK(result.left() == 20);
}

TEST_CASE("horizontal: an anchor already left of the parent clamps to 0") {
  // anchor.left()=-20 (a widget partially scrolled off the left edge, or
  // otherwise already off-screen). left-aligned "fits" by the width check
  // alone (-20+160=140<=400) but would place content.x at -20, which is
  // itself off the parent: the final clamp catches this even though no
  // flip triggered.
  const PixelRect result =
      resolve_popup_placement(PixelRect{-20, 40, 100, 24}, PixelSize{160, 104},
                              PopupPlacement::kBelow, PixelSize{400, 300});
  CHECK(result.left() == 0);
}

TEST_CASE("horizontal: content wider than the parent left-aligns instead of overrunning") {
  // content.width (450) >= parent.width (400): the clamp interval
  // [0, 400-450] is empty/inverted, so this left-aligns at 0 rather than
  // producing a negative left edge.
  const PixelRect result =
      resolve_popup_placement(PixelRect{10, 40, 20, 24}, PixelSize{450, 40},
                              PopupPlacement::kBelow, PixelSize{400, 300});
  CHECK(result.left() == 0);
}

// --- Combined: both axes flip together -----------------------------------

TEST_CASE("both axes flip together for a bottom-right anchor") {
  // anchor{350,220,20,24} in a 400x300 parent, content 160x104.
  // Vertical: below overruns (220+24+104=348>300); above fits
  // (220-104=116>=0): flips to top=116.
  // Horizontal: left-aligned overruns (350+160=510>400); right-aligned-to-
  // anchor fits (anchor.right()=370; 370-160=210>=0): flips to left=210.
  const PixelRect result =
      resolve_popup_placement(PixelRect{350, 220, 20, 24}, PixelSize{160, 104},
                              PopupPlacement::kBelow, PixelSize{400, 300});
  CHECK(result == PixelRect{210, 116, 160, 104});
}

// --- Zero-size anchor (a real use: examples/23_menu_tooltip_dialog's
// context menu opens at the pointer with a zero-size anchor rect) ---------

TEST_CASE("a zero-size anchor places content flush against its own point") {
  // anchor{100,100,0,0}: bottom==top==100, left==right==100.
  // below fits (100+104=204<=300): top=100 (== anchor.bottom(), which for a
  // zero-height rect is the same as anchor.top(), the pointer's own y).
  // left-aligned fits (100+160=260<=400): left=100 (== anchor.left(), the
  // pointer's own x).
  const PixelRect result = resolve_popup_placement(
      PixelRect{100, 100, 0, 0}, PixelSize{160, 104}, PopupPlacement::kBelow, PixelSize{400, 300});
  CHECK(result == PixelRect{100, 100, 160, 104});
}

}  // namespace
