// LayoutTree - constraints down, sizes up, and only the part that changed.
//
// This is the smallest thing that can answer sub-step 2's question: when one
// leaf changes, how much of the tree has to be laid out again, and does doing
// less produce the same answer as doing everything?
//
// It owns a RenderTree rather than sitting beside one. Layout's whole output
// is "where every box is", the render tree's whole input is "where every box
// is", and a second structure holding the same rectangles is a second
// structure that can disagree. Node indices are shared: layout node `i` IS
// render NodeId{i}, because both are append-only vectors built by the same
// call, so there is no mapping table to keep in step.
//
// Movement damage falls out of that ownership for free and is not re-derived
// here. RenderTree::set_local_bounds already damages the pixels a node
// vacated AND the pixels it now occupies - that is what sub-step 1 built and
// what its byte-identity test verifies - so layout's job is to notice which
// boxes moved and call it for exactly those. Re-deriving the rule here would
// be a second implementation of the thing most likely to leave trails.
//
// TWO MECHANISMS KEEP RE-LAYOUT SMALL, and they work in opposite directions:
//
//   Upward, a relayout boundary stops invalidation. A node whose constraints
//   are tight cannot change size no matter what happens inside it, so its
//   parent has nothing to reconsider and the dirty mark stops there. A
//   flexible, stretched child of a row is tight on both axes, which is why
//   the common "panel that fills the space it was given" is a boundary
//   without anyone declaring it one.
//
//   Flutter's protocol has a second source of boundaries - a parent that does
//   not read the size it asked for - and this implementation deliberately has
//   no such case. Every arrangement here reads its children's sizes except
//   where it has already constrained them tightly: a row reads its
//   inflexible children's main extent and its unstretched children's cross
//   extent, a leaf shrinks to fit, and an absolute child whose size the parent
//   ignores is one pinned by two edges, which is tight. So `parent_uses_size`
//   would be a parameter that is always true, and it is absent rather than
//   present-and-ignored. Sub-step 3 should re-add it the moment an
//   arrangement genuinely stops reading a size.
//
//   Downward, constraint equality stops recursion. A node re-entered with the
//   constraints it was last laid out under, and not itself marked dirty,
//   returns its cached size and its subtree is never touched. This is the one
//   that does the heavy lifting: re-laying out a row of twenty children when
//   one of them is dirty costs nineteen integer comparisons and one recursion.
//
// The demo reports both numbers - nodes entered versus nodes recomputed -
// because reporting only the second would flatter the design.
//
// NO INTRINSIC SIZING. `intrinsic_width(height)` and friends are speculative
// layout: they break the exactly-once invariant and design.md section 5.4.6
// puts them at O(n^2) worst case. Nothing in this slice needs them - a row
// obtains its inflexible children's natural sizes by laying them out with an
// unbounded main axis, which is the ordinary pass and not a second one. Any
// later layout that genuinely needs a child's size before allocating (table
// column auto-fit, baseline alignment) has to add them, pay the caching that
// section 5.4.6 requires, and measure what a second pass costs. Sub-step 3
// should assume they do not exist.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/layout/box.h"
#include "drawgui/render/render_tree.h"

namespace dg {

// What one layout pass actually did.
//
// The pair (visited, relaid_out) is the point of this struct: a design that
// walks the whole tree and cheaply decides to do nothing has a small second
// number and a large first one, and calling that "incremental" would be a
// claim the numbers do not support.
struct LayoutStats {
  std::size_t nodes_total = 0;

  // Nodes whose layout() was entered, including the ones that immediately
  // returned a cached size.
  std::size_t nodes_visited = 0;

  // Nodes whose size was actually recomputed - the cache misses.
  std::size_t nodes_relaid_out = 0;

  // Nodes whose border box ended up somewhere other than where it was, and
  // which therefore damaged two rectangles rather than none.
  std::size_t nodes_moved = 0;

  // Relayout boundaries the pass started from. One dirty leaf usually means
  // one; the number rises when changes land in unrelated subtrees.
  std::size_t dirty_roots = 0;

  // Pixels the movement invalidated, and how many rectangles they arrived in.
  // Measured over the old and new boxes of the nodes that moved, under the
  // same rectangle cap the render tree uses, so it is the layout-induced part
  // of the frame's damage rather than the whole of it - a node that only
  // changed colour is not layout's doing and is not counted here.
  std::int64_t damage_area = 0;
  std::size_t damage_rects = 0;
};

class LayoutTree {
 public:
  // Builds the render tree too. The root is laid out under constraints tight
  // to `spec.viewport`, which makes it a relayout boundary and gives the
  // dirty-list walk a guaranteed place to stop.
  explicit LayoutTree(const TreeSpec& spec);

  LayoutTree(LayoutTree&&) noexcept;
  LayoutTree& operator=(LayoutTree&&) noexcept;
  LayoutTree(const LayoutTree&) = delete;
  LayoutTree& operator=(const LayoutTree&) = delete;
  ~LayoutTree();

  [[nodiscard]] static constexpr NodeId root() { return RenderTree::root(); }

  // Appends a child. Its position is layout's business, not the caller's -
  // the `bounds` argument RenderTree::add_child takes is filled in by the
  // next pass, and reading absolute_bounds() before then answers with zero.
  NodeId add_child(NodeId parent, const BoxStyle& box, const NodeStyle& style);

  [[nodiscard]] std::size_t node_count() const;
  [[nodiscard]] const BoxStyle& box(NodeId id) const;

  // Marks the node dirty and bubbles the mark up to the nearest relayout
  // boundary. Cheap to call redundantly: a node already marked stops the walk
  // immediately.
  void set_box(NodeId id, const BoxStyle& box);

  // The render tree this layout drives. Painting, damage and styling go
  // through it directly; there is no wrapper here, because a wrapper would be
  // an interface written for one caller.
  [[nodiscard]] RenderTree& render();
  [[nodiscard]] const RenderTree& render() const;

  // Absolute border box, in device pixels - the same rectangle the render
  // tree paints, which is the whole point of layout being integral.
  [[nodiscard]] PixelRect bounds(NodeId id) const;

  // The content box: inside border and padding, where children are placed.
  [[nodiscard]] PixelRect content_bounds(NodeId id) const;

  // Lays out every dirty boundary and nothing else.
  LayoutStats layout();

  // Lays out the whole tree from the root, ignoring every cache. This is the
  // reference the incremental path is verified against, exactly as
  // repaint_full() is for damage.
  LayoutStats layout_full();

  void resize(PixelSize viewport);
  [[nodiscard]] PixelSize viewport() const;

  // Layout errors, in the node-path form design.md section 5.4.7 asks for.
  // One kind is reachable today: children that overrun the main axis of the
  // row or column holding them. Nothing about that frame looks broken - the
  // children simply run past the edge - which is why it has to be said out
  // loud rather than left to be noticed.
  //
  // The other conflicts section 5.4.7 lists are all downstream of an
  // unbounded constraint, and no unbounded constraint is constructible here:
  // the root is tight to the viewport and every rule derives a child's
  // maximum from its parent's, so boundedness is inherited by induction.
  // Those messages arrive with scrolling, together with the thing that can
  // produce them.
  //
  // Collected rather than thrown or aborted: a demo that dies on a window
  // size nobody tried is worse at surfacing the problem than one that prints
  // it. Cleared at the start of every pass, so the list describes the most
  // recent layout and not the session.
  [[nodiscard]] const std::vector<std::string>& diagnostics() const;

  // "root(column) > #2(row) > #7(leaf)" - the node path design.md section
  // 5.4.7 wants a diagnostic to carry.
  //
  // Public because layout is not the only thing that has to name a node it is
  // complaining about: a rejected property write needs the same string, and
  // deriving it a second time from RenderTree::parent() would be useful only
  // for as long as the two derivations agreed.
  [[nodiscard]] std::string path_of(NodeId id) const;

 private:
  struct Impl;

  std::unique_ptr<Impl> impl_;
};

}  // namespace dg
