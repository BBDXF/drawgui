// Unit tests for DamageRegion.
//
// Two invariants carry the whole type, and both are checked directly rather
// than inferred from a rectangle count:
//
//   coverage    every rectangle ever added is inside the region. Violating
//               this loses damage, which shows up as a stale patch on screen.
//   disjoint    no two stored rectangles overlap. Violating this repaints
//               shared pixels twice, which blends a translucent node onto
//               itself and makes a damage frame differ from a full one.
//
// The cases that matter most are the ones where merging is forced: a cap of
// one is the always-union policy, and the cheapest-pair choice is what stops
// a full list from fusing two far-apart rectangles when two adjacent ones
// were available.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/render/damage.h"

namespace {

using dg::DamageRegion;
using dg::PixelRect;

bool pairwise_disjoint(const DamageRegion& region) {
  const std::vector<PixelRect>& rects = region.rects();
  for (std::size_t a = 0; a < rects.size(); ++a) {
    for (std::size_t b = a + 1; b < rects.size(); ++b) {
      if (dg::intersects(rects[a], rects[b])) {
        return false;
      }
    }
  }
  return true;
}

bool covers(const DamageRegion& region, const PixelRect& rect) {
  return std::ranges::any_of(
      region.rects(), [&rect](const PixelRect& stored) { return dg::contains(stored, rect); });
}

// Order-independent: the merge swaps entries to the back of the vector as it
// removes them, so asserting on a position would pin an implementation detail
// instead of the result.
bool holds(const DamageRegion& region, const PixelRect& rect) {
  return std::ranges::any_of(region.rects(),
                             [&rect](const PixelRect& stored) { return stored == rect; });
}

TEST_CASE("a fresh region is empty and reports nothing") {
  const DamageRegion region;
  CHECK(region.is_empty());
  CHECK(region.size() == 0);
  CHECK(region.area() == 0);
  CHECK(region.bounds().is_empty());
}

TEST_CASE("an empty rectangle is not damage") {
  DamageRegion region;
  region.add(PixelRect{10, 10, 0, 50});
  region.add(PixelRect{10, 10, 50, -1});
  region.add(PixelRect{});
  CHECK(region.is_empty());
  CHECK(region.bounds().is_empty());
}

TEST_CASE("far-apart rectangles stay apart under a list policy") {
  // The case the whole cap parameter exists for: the union of these two is
  // 1920x1080, while their combined area is 10000 pixels.
  DamageRegion region{8};
  region.add(PixelRect{0, 0, 100, 50});
  region.add(PixelRect{1820, 1030, 100, 50});

  CHECK(region.size() == 2);
  CHECK(region.area() == 10000);
  CHECK(region.bounds() == PixelRect{0, 0, 1920, 1080});
  CHECK(pairwise_disjoint(region));
}

TEST_CASE("a cap of one is the always-union policy") {
  DamageRegion region{1};
  region.add(PixelRect{0, 0, 100, 50});
  region.add(PixelRect{1820, 1030, 100, 50});

  CHECK(region.size() == 1);
  CHECK(region.rects().front() == PixelRect{0, 0, 1920, 1080});
  CHECK(region.area() == 1920 * 1080);
}

TEST_CASE("a cap of zero is raised to one rather than discarding damage") {
  DamageRegion region{0};
  CHECK(region.max_rects() == 1);
  region.add(PixelRect{5, 5, 10, 10});
  CHECK(region.size() == 1);
  CHECK(covers(region, PixelRect{5, 5, 10, 10}));
}

TEST_CASE("overlapping rectangles merge into their union") {
  DamageRegion region;
  region.add(PixelRect{0, 0, 100, 100});
  region.add(PixelRect{50, 50, 100, 100});

  CHECK(region.size() == 1);
  CHECK(region.rects().front() == PixelRect{0, 0, 150, 150});
  CHECK(pairwise_disjoint(region));
}

TEST_CASE("a rectangle sharing only an edge is kept separate") {
  DamageRegion region;
  region.add(PixelRect{0, 0, 100, 100});
  region.add(PixelRect{100, 0, 100, 100});

  CHECK(region.size() == 2);
  CHECK(region.area() == 20000);
}

TEST_CASE("a contained rectangle adds no area") {
  DamageRegion region;
  region.add(PixelRect{0, 0, 100, 100});
  region.add(PixelRect{20, 20, 10, 10});

  CHECK(region.size() == 1);
  CHECK(region.rects().front() == PixelRect{0, 0, 100, 100});
}

TEST_CASE("a bridging rectangle absorbs both of the ones it touches") {
  // Absorption has to repeat: joining the bridge with the left rectangle
  // produces something that now reaches the right one. A single pass would
  // leave two overlapping entries behind.
  DamageRegion region{8};
  region.add(PixelRect{0, 0, 40, 40});
  region.add(PixelRect{200, 0, 40, 40});
  REQUIRE(region.size() == 2);

  region.add(PixelRect{30, 10, 180, 10});
  CHECK(region.size() == 1);
  CHECK(region.rects().front() == PixelRect{0, 0, 240, 40});
  CHECK(pairwise_disjoint(region));
}

TEST_CASE("exceeding the cap merges the pair that wastes the least area") {
  // Two neighbours 10 pixels apart and one rectangle 2000 pixels away. The
  // cap forces one merge; merging by union area alone would happily fuse a
  // neighbour with the distant rectangle.
  DamageRegion region{2};
  region.add(PixelRect{0, 0, 50, 50});
  region.add(PixelRect{60, 0, 50, 50});
  region.add(PixelRect{2000, 0, 50, 50});

  REQUIRE(region.size() == 2);
  CHECK(covers(region, PixelRect{0, 0, 50, 50}));
  CHECK(covers(region, PixelRect{60, 0, 50, 50}));
  CHECK(covers(region, PixelRect{2000, 0, 50, 50}));
  CHECK(holds(region, PixelRect{0, 0, 110, 50}));
  CHECK(holds(region, PixelRect{2000, 0, 50, 50}));
}

// A deterministic scatter rather than a random one: a failure has to be
// reproducible from the test name alone.
std::vector<PixelRect> scattered_rects(int count) {
  std::vector<PixelRect> rects;
  rects.reserve(static_cast<std::size_t>(count));
  std::uint32_t state = 0x1234'5678;
  for (int i = 0; i < count; ++i) {
    state = (state * 1'664'525U) + 1'013'904'223U;
    rects.push_back(PixelRect{static_cast<int>((state >> 8) % 1900),
                              static_cast<int>((state >> 20) % 1000),
                              static_cast<int>(1 + ((state >> 4) % 120)),
                              static_cast<int>(1 + ((state >> 16) % 90))});
  }
  return rects;
}

// Checked after every add rather than once at the end, so a violation is
// reported at the add that caused it.
void add_all_upholding_invariants(DamageRegion& region, const std::vector<PixelRect>& rects) {
  for (const PixelRect& rect : rects) {
    region.add(rect);
    REQUIRE(region.size() <= region.max_rects());
    REQUIRE(pairwise_disjoint(region));
  }
}

TEST_CASE("the cap is never exceeded and coverage survives every add") {
  DamageRegion region{4};
  const std::vector<PixelRect> scatter = scattered_rects(200);

  add_all_upholding_invariants(region, scatter);

  for (const PixelRect& rect : scatter) {
    CHECK(covers(region, rect));
  }
}

TEST_CASE("area is a plain sum because the list is disjoint") {
  DamageRegion region{8};
  region.add(PixelRect{0, 0, 10, 10});
  region.add(PixelRect{500, 500, 20, 20});
  region.add(PixelRect{5, 5, 10, 10});

  // The third overlaps the first, so the answer is 15x15 + 20x20, not
  // 100 + 400 + 100.
  CHECK(region.size() == 2);
  CHECK(region.area() == (15 * 15) + (20 * 20));
}

TEST_CASE("merging one region into another preserves coverage") {
  DamageRegion left{8};
  left.add(PixelRect{0, 0, 10, 10});
  DamageRegion right{8};
  right.add(PixelRect{800, 600, 10, 10});
  right.add(PixelRect{5, 5, 10, 10});

  left.add(right);
  CHECK(covers(left, PixelRect{0, 0, 10, 10}));
  CHECK(covers(left, PixelRect{800, 600, 10, 10}));
  CHECK(covers(left, PixelRect{5, 5, 10, 10}));
  CHECK(pairwise_disjoint(left));
}

TEST_CASE("merging a region into itself is a no-op rather than a hang") {
  DamageRegion region{8};
  region.add(PixelRect{0, 0, 10, 10});
  region.add(PixelRect{100, 100, 10, 10});

  region.add(region);
  CHECK(region.size() == 2);
  CHECK(region.area() == 200);
}

TEST_CASE("clear removes every rectangle but keeps the cap") {
  DamageRegion region{3};
  region.add(PixelRect{0, 0, 10, 10});
  region.clear();

  CHECK(region.is_empty());
  CHECK(region.max_rects() == 3);
}

}  // namespace
