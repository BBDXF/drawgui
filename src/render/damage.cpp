#include "drawgui/render/damage.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace dg {

DamageRegion::DamageRegion(std::size_t max_rects)
    : max_rects_(std::max<std::size_t>(1, max_rects)) {}

PixelRect DamageRegion::absorb_overlapping(PixelRect rect) {
  bool grew = true;
  while (grew) {
    grew = false;
    for (std::size_t i = 0; i < rects_.size();) {
      if (intersects(rects_[i], rect)) {
        rect = join(rect, rects_[i]);
        rects_[i] = rects_.back();
        rects_.pop_back();
        grew = true;
      } else {
        ++i;
      }
    }
  }
  return rect;
}

void DamageRegion::merge_cheapest_pair() {
  std::size_t best_a = 0;
  std::size_t best_b = 1;
  std::int64_t best_waste = std::numeric_limits<std::int64_t>::max();

  for (std::size_t a = 0; a < rects_.size(); ++a) {
    for (std::size_t b = a + 1; b < rects_.size(); ++b) {
      // The pixels the merged rectangle would repaint that neither operand
      // asked for. Comparing union area alone would happily fuse the two
      // largest rectangles in the list even when they sit at opposite
      // corners; this compares the damage actually invented.
      const std::int64_t waste =
          join(rects_[a], rects_[b]).area() - rects_[a].area() - rects_[b].area();
      if (waste < best_waste) {
        best_waste = waste;
        best_a = a;
        best_b = b;
      }
    }
  }

  const PixelRect merged = join(rects_[best_a], rects_[best_b]);
  rects_[best_b] = rects_.back();
  rects_.pop_back();
  rects_[best_a] = rects_.back();
  rects_.pop_back();
  rects_.push_back(absorb_overlapping(merged));
}

void DamageRegion::add(const PixelRect& rect) {
  if (rect.is_empty()) {
    return;
  }
  rects_.push_back(absorb_overlapping(rect));
  while (rects_.size() > max_rects_) {
    merge_cheapest_pair();
  }
}

void DamageRegion::add(const DamageRegion& other) {
  // Copied out first: `other` may be `*this`, and add() reallocates.
  const std::vector<PixelRect> incoming = other.rects_;
  for (const PixelRect& rect : incoming) {
    add(rect);
  }
}

void DamageRegion::clear() {
  rects_.clear();
}

PixelRect DamageRegion::bounds() const {
  PixelRect total;
  for (const PixelRect& rect : rects_) {
    total = join(total, rect);
  }
  return total;
}

std::int64_t DamageRegion::area() const {
  std::int64_t total = 0;
  for (const PixelRect& rect : rects_) {
    total += rect.area();
  }
  return total;
}

}  // namespace dg
