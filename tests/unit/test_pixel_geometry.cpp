// Unit tests for the integer rectangle algebra damage accumulation is built
// on.
//
// These operations are two-line functions, which is exactly why they get
// tests: a damage system that computes a rectangle slightly wrong does not
// crash, it leaves a row of stale pixels somewhere on screen and keeps
// running. Every case below names a boundary that the obvious implementation
// gets wrong - half-open edges, an empty operand, and area on a rectangle
// large enough to overflow 32 bits.

#include <cstdint>
#include <limits>

#include <doctest/doctest.h>

#include "drawgui/base/pixel_geometry.h"

namespace {

using dg::PixelRect;

TEST_CASE("a default rectangle is empty and has no area") {
  const PixelRect empty;
  CHECK(empty.is_empty());
  CHECK(empty.area() == 0);
  CHECK(empty.right() == 0);
  CHECK(empty.bottom() == 0);
}

TEST_CASE("a rectangle with non-positive extent is empty whatever its origin") {
  CHECK(PixelRect{10, 10, 0, 40}.is_empty());
  CHECK(PixelRect{10, 10, 40, 0}.is_empty());
  CHECK(PixelRect{10, 10, -5, 40}.is_empty());
  CHECK(PixelRect{10, 10, 40, -5}.area() == 0);
}

TEST_CASE("from_edges is half-open and refuses an inverted rectangle") {
  // Given a rectangle built from edges, When the edges are read back, Then
  // the right and bottom edges are one past the last covered pixel.
  const PixelRect rect = PixelRect::from_edges(4, 8, 6, 12);
  CHECK(rect == PixelRect{4, 8, 2, 4});
  CHECK(rect.right() == 6);
  CHECK(rect.bottom() == 12);

  CHECK(PixelRect::from_edges(6, 8, 4, 12).is_empty());
  CHECK(PixelRect::from_edges(4, 12, 6, 8).is_empty());
  CHECK(PixelRect::from_edges(4, 8, 4, 12).is_empty());
}

TEST_CASE("area does not overflow on a rectangle bigger than 32 bits") {
  // 65536 x 65536 is 2^32 pixels, one more than a uint32_t can hold. A
  // 32-bit product would report 0 here and the merge heuristic would then
  // treat the largest possible rectangle as the cheapest one to grow.
  const PixelRect huge{0, 0, 65536, 65536};
  CHECK(huge.area() == std::int64_t{1} << 32);
  CHECK(huge.area() > std::int64_t{std::numeric_limits<std::uint32_t>::max()});
}

TEST_CASE("rectangles that share only an edge do not intersect") {
  const PixelRect left{0, 0, 10, 10};
  const PixelRect right{10, 0, 10, 10};
  const PixelRect below{0, 10, 10, 10};

  CHECK_FALSE(dg::intersects(left, right));
  CHECK_FALSE(dg::intersects(left, below));
  CHECK(dg::intersect(left, right).is_empty());

  // One pixel of genuine overlap is an intersection, and it is the smallest
  // one a damage list must not miss.
  CHECK(dg::intersects(left, PixelRect{9, 9, 10, 10}));
  CHECK(dg::intersect(left, PixelRect{9, 9, 10, 10}) == PixelRect{9, 9, 1, 1});
}

TEST_CASE("intersection with an empty rectangle is empty") {
  const PixelRect rect{4, 4, 20, 20};
  CHECK(dg::intersect(rect, PixelRect{}).is_empty());
  CHECK(dg::intersect(PixelRect{}, rect).is_empty());
  CHECK_FALSE(dg::intersects(rect, PixelRect{}));
}

TEST_CASE("intersection is the overlap, not the bounding box") {
  const PixelRect a{0, 0, 100, 40};
  const PixelRect b{60, 20, 100, 40};
  CHECK(dg::intersect(a, b) == PixelRect{60, 20, 40, 20});
  CHECK(dg::intersect(a, b) == dg::intersect(b, a));
}

TEST_CASE("join with an empty rectangle returns the other one") {
  // The failure this pins: treating an empty rectangle as one at the origin
  // would stretch every union back to (0, 0) and quietly damage the whole
  // top-left of the window.
  const PixelRect rect{400, 300, 20, 20};
  CHECK(dg::join(rect, PixelRect{}) == rect);
  CHECK(dg::join(PixelRect{}, rect) == rect);
  CHECK(dg::join(PixelRect{}, PixelRect{}).is_empty());
}

TEST_CASE("join is the smallest rectangle covering both") {
  const PixelRect a{10, 10, 10, 10};
  const PixelRect b{100, 200, 5, 5};
  const PixelRect joined = dg::join(a, b);

  CHECK(joined == PixelRect{10, 10, 95, 195});
  CHECK(dg::contains(joined, a));
  CHECK(dg::contains(joined, b));
  CHECK(joined == dg::join(b, a));
}

TEST_CASE("containment is inclusive of the far edge and true for an empty inner") {
  const PixelRect outer{0, 0, 100, 100};
  CHECK(dg::contains(outer, outer));
  CHECK(dg::contains(outer, PixelRect{90, 90, 10, 10}));
  CHECK_FALSE(dg::contains(outer, PixelRect{90, 90, 11, 10}));
  CHECK(dg::contains(outer, PixelRect{}));
  CHECK_FALSE(dg::contains(PixelRect{}, outer));
}

TEST_CASE("offset_by moves without resizing") {
  const PixelRect rect{5, 7, 30, 40};
  CHECK(rect.offset_by(10, -3) == PixelRect{15, 4, 30, 40});
  CHECK(rect.offset_by(0, 0) == rect);
}

TEST_CASE("inflated_by grows on every side and leaves an empty rectangle empty") {
  // The slack a clip-atomic node needs around it. An empty rectangle must
  // stay empty: inflating one would otherwise conjure a 2x2 box at the
  // origin and damage a corner of the window nothing asked for.
  const PixelRect rect{10, 20, 30, 40};
  CHECK(rect.inflated_by(1) == PixelRect{9, 19, 32, 42});
  CHECK(rect.inflated_by(0) == rect);
  CHECK(dg::contains(rect.inflated_by(1), rect));
  CHECK(PixelRect{}.inflated_by(1).is_empty());
  CHECK(PixelRect{5, 5, 2, 2}.inflated_by(-1).is_empty());
}

}  // namespace
