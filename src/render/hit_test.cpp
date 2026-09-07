// Turning a pointer position into the node the user aimed at.
//
// The whole of the specification is one sentence: hit testing is painting read
// backwards. Painting walks the tree depth-first pre-order - a parent, then
// each child in the order it was added - so the LAST node to cover a pixel is
// the one visible at it. Reversing that sequence gives this traversal: each
// child in reverse order first, then the node itself, first match wins.
//
// It is written as its own recursion rather than as a reversed walk of the
// painter's traversal on purpose. `paint_subtree` in tree_paint.cpp walks the
// other way and applies the clip through the canvas; this walks backwards and
// applies the same clip through `clip_contains`. The exhaustive test compares
// them against each other and against the rasterizer for every pixel of a
// scene - so the two being separate implementations of one rule is what gives
// that test something to disagree about. Hit testing that simply replayed the
// painter's list backwards would be a restatement of it, and its test a
// tautology.
//
// A CLIP IS THE ONE THING THAT PRUNES A SUBTREE HERE. Being someone's child
// does not confine a node: an overflowing child of an ordinary parent is
// painted in the overflow and is clickable there, and pruning against the
// parent's box would be exactly the defect that makes a visible widget
// unclickable. A node carrying `Overflow::kClip` is different - the painter
// really does remove those pixels, so a hit there would be a click on
// something the user cannot see. One property, two readers; the deliberately
// deferred decision render_tree.h used to record.
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

#include "render/clip_shape.h"
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
  // against the parent's box unless the parent asked for it. Ancestor clips
  // need no separate handling: the walk never reaches a descendant through a
  // clip the point failed, so a grandparent's clip is honoured by the same
  // line as a parent's.
  //
  // The node itself is still hit when the point is inside its own bounds. A
  // clip removes what is UNDER a node, not the node, which is what makes the
  // empty case behave: a clipping node with no area removes its whole subtree
  // and remains hittable nowhere, because its own bounds are empty too.
  const bool descendable = !clips_subtree(node.style) ||
                           detail::clip_contains(node.absolute, node.style.radii, point);
  if (descendable) {
    for (auto child = node.children.rbegin(); child != node.children.rend(); ++child) {
      const std::uint32_t hit = descend(nodes, *child, point, miss);
      if (hit != miss) {
        return hit;
      }
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
