// The paint half of border_width_*, checked in pixels rather than in fields.
//
// tests/unit/test_props_parity.cpp asserts that four widths reach NodeStyle
// intact. That is a different claim from "the painter draws four widths", and
// the gap between the two is exactly where the old behaviour lived: the four
// insets were always stored per side, and it was the PAINTER that collapsed
// them to their minimum. A test that only reads the style back would have gone
// on passing through that whole period.
//
// So this measures the frame it actually drew: walk inward from each edge and
// count how many pixels carry the border colour rather than the fill.
//
// Everything here is integer-aligned with square corners, which slice 1
// measured to be bit-exact under any clip - so a pixel is either the border
// colour or the fill colour, with no anti-aliased blend in between to make the
// count ambiguous.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/graphics/types.h"
#include "drawgui/layout/box.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/render_tree.h"

namespace {

using dg::BorderWidths;
using dg::BoxStyle;
using dg::Color;
using dg::EdgeInsets;
using dg::LayoutKind;
using dg::LayoutTree;
using dg::NodeId;
using dg::NodeStyle;
using dg::PixelRect;
using dg::PixelSize;
using dg::RasterSurface;

constexpr int kWidth = 160;
constexpr int kHeight = 120;

// A framebuffer plus the one thing a caller needs from it, so that no test
// has to know whether the surface is BGRA or RGBA. Slice 2 measured that
// kN32_SkColorType disagrees with the archive, so asking a live surface is
// the only trustworthy answer - and comparing one pixel against another
// sidesteps the question entirely.
struct Frame {
  std::vector<std::uint8_t> pixels;
  int width = 0;
  int height = 0;

  [[nodiscard]] std::uint32_t at(int x, int y) const {
    std::uint32_t value = 0;
    std::memcpy(
        &value,
        pixels.data() + (((static_cast<std::size_t>(y) * static_cast<std::size_t>(width)) +
                          static_cast<std::size_t>(x)) *
                         4),
        4);
    return value;
  }
};

Frame capture(const RasterSurface& surface) {
  const dg::PixelView view = surface.peek_pixels();
  Frame frame;
  frame.width = view.width;
  frame.height = view.height;
  const auto row = static_cast<std::size_t>(view.width) * 4;
  frame.pixels.resize(row * static_cast<std::size_t>(view.height));
  for (int y = 0; y < view.height; ++y) {
    std::memcpy(frame.pixels.data() + (static_cast<std::size_t>(y) * row),
                view.pixels + (static_cast<std::size_t>(y) * view.row_bytes), row);
  }
  return frame;
}

// How many pixels from (x, y) in direction (dx, dy) carry `colour`.
int run_length(const Frame& frame, int x, int y, int dx, int dy, std::uint32_t colour) {
  int count = 0;
  while (x >= 0 && x < frame.width && y >= 0 && y < frame.height && frame.at(x, y) == colour) {
    ++count;
    x += dx;
    y += dy;
  }
  return count;
}

struct Painted {
  int left = 0;
  int top = 0;
  int right = 0;
  int bottom = 0;

  friend bool operator==(const Painted&, const Painted&) = default;
};

// Renders one node with the given border and reports the thickness actually
// drawn on each of its four sides.
//
// Returns a sentinel no real measurement can produce, having already failed
// the case, when the surface cannot be allocated - so an unavailable
// framebuffer cannot read as a border of the right width. An optional would
// have said the same thing less well: clang-tidy cannot model doctest's
// REQUIRE, so every read of it is an unchecked access.
Painted paint_and_measure(const EdgeInsets& border) {
  dg::TreeSpec spec;
  spec.viewport = PixelSize{kWidth, kHeight};
  spec.background.fill = Color::from_argb(0xFF101010);
  LayoutTree tree{spec};

  BoxStyle root = tree.box(LayoutTree::root());
  root.kind = LayoutKind::kColumn;
  tree.set_box(LayoutTree::root(), root);

  BoxStyle box;
  box.width = 120;
  box.height = 80;
  box.margin = EdgeInsets::all(10);
  box.border = border;

  NodeStyle style;
  style.fill = Color::from_argb(0xFF3060C0);
  style.border_color = Color::from_argb(0xFFF0B020);
  style.border_width =
      BorderWidths{static_cast<float>(border.left), static_cast<float>(border.top),
                   static_cast<float>(border.right), static_cast<float>(border.bottom)};
  const NodeId node = tree.add_child(LayoutTree::root(), box, style);
  tree.layout();

  std::optional<RasterSurface> surface = RasterSurface::create(kWidth, kHeight);
  if (!surface.has_value()) {
    FAIL_CHECK("could not allocate a raster surface");
    return Painted{-1, -1, -1, -1};
  }
  tree.render().repaint_full(*surface);
  const Frame frame = capture(*surface);

  const PixelRect bounds = tree.bounds(node);
  REQUIRE(bounds == PixelRect{10, 10, 120, 80});

  const int mid_x = bounds.x + (bounds.width / 2);
  const int mid_y = bounds.y + (bounds.height / 2);

  // Taken from the frame rather than from the constant, so the measurement
  // does not depend on knowing the surface's channel order.
  const std::uint32_t ink = frame.at(bounds.left(), mid_y);
  const std::uint32_t fill = frame.at(mid_x, mid_y);
  REQUIRE(ink != fill);

  return Painted{run_length(frame, bounds.left(), mid_y, 1, 0, ink),
                 run_length(frame, mid_x, bounds.top(), 0, 1, ink),
                 run_length(frame, bounds.right() - 1, mid_y, -1, 0, ink),
                 run_length(frame, mid_x, bounds.bottom() - 1, 0, -1, ink)};
}

}  // namespace

TEST_CASE("four unequal border widths are painted as four unequal widths") {
  // The exact four that were asked for. Collapsing them to any single number -
  // the minimum, as the painter used to, or the maximum, or the first - fails
  // every side but one.
  CHECK(paint_and_measure(EdgeInsets{9, 5, 2, 7}) == Painted{9, 5, 2, 7});
}

// The route every existing pixel in this project came from, so it has its own
// case: a uniform border still goes through the centred-stroke path, and the
// two routes must not disagree about what a uniform border looks like.
TEST_CASE("a uniform border is painted at its own width on every side") {
  CHECK(paint_and_measure(EdgeInsets::all(6)) == Painted{6, 6, 6, 6});
}

// A side asked for nothing must be given nothing, which the minimum rule could
// never express: under it, one zero side painted every side at zero.
TEST_CASE("a border on some sides only leaves the others bare") {
  // Nothing is drawn on the top and bottom edges, so the run of border colour
  // starting there has length zero - the fill reaches the edge. The minimum
  // rule could never express this: one zero side painted every side at zero.
  CHECK(paint_and_measure(EdgeInsets{8, 0, 3, 0}) == Painted{8, 0, 3, 0});
}
