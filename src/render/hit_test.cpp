// Turning a pointer position into the node the user aimed at.
//
// The whole of the specification is one sentence: hit testing is painting read
// backwards. Painting walks the tree depth-first pre-order - a parent, then
// each child in the order it was added - so the LAST node to cover a pixel is
// the one visible at it. Reversing that sequence gives this traversal: each
// child in reverse order first, then the node itself, first match wins.
//
// It is written as its own recursion rather than as a loop over the render
// tree's `paint_order` vector on purpose. `paint_order` is built by a
// different function walking the other way, and the exhaustive test compares
// this traversal against a linear scan of that vector for every pixel of a
// scene - so the two being separate implementations is what gives that test
// something to disagree about. A hit test that simply read `paint_order`
// backwards would be a restatement of it, and its test a tautology.
//
// THERE IS NO SPATIAL INDEX AND NO SUBTREE-EXTENT PRUNING. The obvious
// optimisation - cache each subtree's union extent and skip a subtree that
// cannot contain the point - was considered and rejected. The owner has
// directed that performance is not an acceptance condition for this slice, and
// a cached extent is a second copy of the geometry carrying an invalidation
// obligation on every move, resize and insertion. This project has already
// deleted one speculative structure. When a scene is large enough for the
// linear walk to be measurable, that measurement is what buys the index.

#include <cstdint>
#include <vector>

#include "drawgui/base/pixel_geometry.h"

#include "render/tree_impl.h"

namespace dg {
namespace {

// `miss` rather than an optional so the recursion has one comparison to make
// and the caller has one out-of-range value to test. Node indices are dense
// from zero, so the node count is the smallest value that cannot name one.
std::uint32_t descend(const std::vector<Node>& nodes, std::uint32_t index, PixelPoint point,
                      std::uint32_t miss) {
  const Node& node = nodes[index];

  // Children are searched before the node itself, and none of them is pruned
  // against the parent's box. A child is not clipped to its parent - it is
  // painted in the overflow region, and the layout tree reports the overrun as
  // a diagnostic rather than hiding it - so pruning here is exactly the defect
  // that makes an overflowing child visible but unclickable.
  for (auto child = node.children.rbegin(); child != node.children.rend(); ++child) {
    const std::uint32_t hit = descend(nodes, *child, point, miss);
    if (hit != miss) {
      return hit;
    }
  }
  return contains(node.absolute, point) ? index : miss;
}

}  // namespace

std::uint32_t RenderTree::Impl::hit_test(PixelPoint point) const {
  const auto miss = static_cast<std::uint32_t>(nodes.size());
  return descend(nodes, 0, point, miss);
}

}  // namespace dg
