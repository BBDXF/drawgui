// DamageRegion - the set of pixels that have to be redrawn this frame.
//
// Step 2 measured a full 1080p repaint at 3.91 ms of rasterization plus
// 7.71 ms of presentation, and the same scene under a 260x72 clip at 0.12 ms.
// That 32x is the whole reason this type exists: with it, the cost of a
// change is a function of the change; without it, every repaint is a function
// of the window and nothing else. doc/cpu-raster-findings.md has the ladder.
//
// The interesting question is not whether to track damage but how to combine
// it. Two dirty nodes at opposite corners of a window have a bounding box
// that is the whole window, so a region that always unions is a region that
// has quietly switched itself off in exactly the case that matters. A list of
// rectangles avoids that and costs one extra clip-and-traverse pass per
// rectangle. Which one wins is measured rather than argued: `max_rects` is a
// construction parameter, `max_rects == 1` IS the always-union policy, and
// examples/03_damage_repaint reports both.
//
// The list is kept pairwise disjoint. That is not tidiness - overlapping
// rectangles would repaint the shared pixels twice and, worse, blend an
// alpha-bearing node onto itself, so a frame assembled from an overlapping
// region would not match a full repaint of the same scene.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "drawgui/base/pixel_geometry.h"

namespace dg {

class DamageRegion {
 public:
  // Eight is a starting point, not a measurement: it is comfortably more than
  // the number of independently animating things a frame usually has, and
  // small enough that the O(n^2) merge below is free. The demo reports what
  // different caps actually cost.
  static constexpr std::size_t kDefaultMaxRects = 8;

  DamageRegion() = default;

  // A cap of zero is meaningless and is raised to one, because a region that
  // can hold nothing would silently discard damage - the one failure mode
  // this whole file exists to prevent.
  explicit DamageRegion(std::size_t max_rects);

  // Empty rectangles are ignored rather than stored. A node with zero area
  // has no pixels to invalidate, and letting one into the list would give
  // bounds() a corner it does not have.
  void add(const PixelRect& rect);

  void add(const DamageRegion& other);

  void clear();

  [[nodiscard]] bool is_empty() const { return rects_.empty(); }
  [[nodiscard]] std::size_t size() const { return rects_.size(); }
  [[nodiscard]] std::size_t max_rects() const { return max_rects_; }

  // Pairwise disjoint, in no particular order. Every rectangle ever added is
  // covered by this list; the list may cover more, which is the price of the
  // cap.
  [[nodiscard]] const std::vector<PixelRect>& rects() const { return rects_; }

  // The smallest rectangle covering the whole region, or an empty rectangle
  // when there is no damage.
  [[nodiscard]] PixelRect bounds() const;

  // Total damaged area. Meaningful precisely because the list is disjoint -
  // it is a plain sum, with no inclusion-exclusion correction, and it is the
  // number that says how much a cap is costing.
  [[nodiscard]] std::int64_t area() const;

 private:
  // Absorbs into `rect` every stored rectangle it touches, removing them, and
  // repeats until nothing touches it. Repetition is required: joining two
  // rectangles produces a larger one that may now reach a third.
  [[nodiscard]] PixelRect absorb_overlapping(PixelRect rect);

  // Joins the pair whose union wastes the least area. Called only when the
  // cap has been exceeded, so the choice is between bad options and the job
  // is to pick the least bad one.
  void merge_cheapest_pair();

  std::vector<PixelRect> rects_;
  std::size_t max_rects_ = kDefaultMaxRects;
};

}  // namespace dg
