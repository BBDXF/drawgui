// The layout tree's internal representation, shared by the two translation
// units that implement it.
//
// layout_tree.cpp owns structure, invalidation and the walk that turns
// computed boxes into render-tree bounds; box_layout.cpp owns the four
// arrangements. Neither is an interface: this header lives under src/, nothing
// outside the library can include it, and it exists because two translation
// units share a struct.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/layout/box.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/render_tree.h"

namespace dg {

struct LayoutNode {
  BoxStyle box;
  std::uint32_t parent = 0;
  std::vector<std::uint32_t> children;

  // Used to order the dirty list so that an ancestor is always laid out
  // before a descendant, which is what lets the descendant be recognised as
  // already-clean and skipped instead of laid out twice.
  std::uint32_t depth = 0;

  // The constraints this node was last laid out under, and the size it
  // returned. Together they are the incremental pass's entire cache: entered
  // again with the same constraints and no dirty mark, a node returns `size`
  // and its subtree is never visited.
  BoxConstraints constraints;
  PixelSize size;
  bool has_cached = false;

  // The border box, relative to the parent's border-box origin - exactly what
  // RenderTree::set_local_bounds wants. Its origin is written by the parent
  // while positioning, its extent by this node's own measure.
  PixelRect local;

  // The nearest ancestor-or-self whose size cannot change, which is where a
  // dirty mark starting here stops climbing.
  std::uint32_t relayout_boundary = 0;

  bool needs_layout = true;
};

// The main-axis and cross-axis view of one node, so that a row and a column
// are one algorithm rather than two that drift apart. Everything inside a
// flex pass is computed in (main, cross) and mapped back to (x, y) exactly
// once, at the point where a child's local rectangle is written.
struct Axis {
  bool horizontal = true;

  [[nodiscard]] int main_of(PixelSize size) const {
    return horizontal ? size.width : size.height;
  }
  [[nodiscard]] int cross_of(PixelSize size) const {
    return horizontal ? size.height : size.width;
  }
  [[nodiscard]] int main_start(const EdgeInsets& insets) const {
    return horizontal ? insets.left : insets.top;
  }
  [[nodiscard]] int main_total(const EdgeInsets& insets) const {
    return horizontal ? insets.horizontal() : insets.vertical();
  }
  [[nodiscard]] int cross_start(const EdgeInsets& insets) const {
    return horizontal ? insets.top : insets.left;
  }
  [[nodiscard]] int cross_total(const EdgeInsets& insets) const {
    return horizontal ? insets.vertical() : insets.horizontal();
  }
  [[nodiscard]] int main_max(const BoxConstraints& constraints) const {
    return horizontal ? constraints.max_width : constraints.max_height;
  }
  [[nodiscard]] int cross_max(const BoxConstraints& constraints) const {
    return horizontal ? constraints.max_height : constraints.max_width;
  }
  [[nodiscard]] BoxConstraints constraints_of(int min_main, int max_main, int min_cross,
                                              int max_cross) const {
    return horizontal ? BoxConstraints{min_main, max_main, min_cross, max_cross}
                      : BoxConstraints{min_cross, max_cross, min_main, max_main};
  }
  [[nodiscard]] PixelRect rect_of(int main, int cross, PixelSize size) const {
    return horizontal ? PixelRect{main, cross, size.width, size.height}
                      : PixelRect{cross, main, size.width, size.height};
  }
};

// The room a row or column has to hand out, and whether its children are
// being stretched across it. Computed once from the container's inner
// constraints and then read by both the sizing pass and the placing pass, so
// the two cannot disagree about it.
struct FlexRoom {
  int main = kUnbounded;
  int cross = kUnbounded;
  bool stretching = false;
};

// One run of a wrapping container: a half-open slice of the child list, plus
// the extents it occupies.
//
// It holds INDICES INTO THE CHILD LIST rather than node ids, because run
// membership is decided by walking that list in order and a slice is the only
// representation that cannot describe a run whose children are not adjacent.
//
// `main` and `cross` include each child's margins, because the run is a box
// the placing pass positions things inside and margins are part of what a
// child occupies there.
struct Run {
  std::size_t begin = 0;
  std::size_t end = 0;
  int main = 0;
  int cross = 0;

  [[nodiscard]] bool empty() const { return begin == end; }
  [[nodiscard]] std::size_t count() const { return end - begin; }
};

// One child of a row or column, as the sizing pass sees it. `base` is the main
// extent before free space is handed out, `allot` the one the child is finally
// laid out at; margins are excluded from both.
//
// `deferred` says the child has NOT been laid out yet, because its extent is
// not decided until every sibling's base is known. That is the only state in
// this slice a reader has to hold: a deferred child is laid out exactly once,
// later, already at its answer.
struct FlexItem {
  std::uint32_t node = 0;
  int margin_main = 0;
  int min_cross = 0;
  int max_cross = 0;
  int base = 0;
  int allot = 0;
  int floor_main = 0;
  int grow = 0;
  int shrink = 0;
  std::int64_t shrink_weight = 0;
  bool deferred = false;
};

// What one wrapping pass measured, handed from the sizing half to the placing
// half so that neither re-derives the other's numbers. A second computation
// of "which children are in which run" is a second computation that can
// disagree, and the two halves run far enough apart for that to go unnoticed.
struct WrapLayout {
  std::vector<Run> runs;

  // Per child, in child-list order: the main and cross extent it occupies,
  // margins included.
  std::vector<int> child_main;
  std::vector<int> child_cross;

  int content_main = 0;
  int content_cross = 0;
};

// The width and height a node is allowed to end up at, after its own style
// has been folded into what its parent permitted. The parent always wins:
// a child asking for 200 inside a box that offers at most 60 gets 60, because
// the parent has already reserved that space from its own parent.
struct SizeLimits {
  int low_width = 0;
  int high_width = kUnbounded;
  int low_height = 0;
  int high_height = kUnbounded;

  [[nodiscard]] bool tight_width() const { return low_width == high_width; }
  [[nodiscard]] bool tight_height() const { return low_height == high_height; }
};

[[nodiscard]] SizeLimits limits_for(const BoxStyle& box, const BoxConstraints& constraints);

// The largest extent an aspect ratio may derive. Same bound the property
// boundary puts on a length, for the same reason: layout adds extents together
// and a value near INT_MAX overflows on the first addition. A ratio near zero
// or near infinity would otherwise turn a modest settled axis into one.
inline constexpr int kMaxAspectExtent = 1 << 24;

// The extent of the axis an aspect ratio derives, given the settled other one.
// `ratio` is width/height, so a settled WIDTH divides and a settled height
// multiplies.
//
// Declared here rather than left inside box_layout.cpp so that the arithmetic
// can be tested directly instead of only through the pixels it produces. Slice
// 4-4 learned that lesson from fit_radii(), whose two consumers both degraded
// a bad radius in the same way and therefore could not disagree with it.
[[nodiscard]] int aspect_partner(int settled, float ratio, bool settled_is_width);

// Settles the main axis at the maximum the parent permits, when the node asked
// to fill it. Reads BoxStyle::kind to know which axis is the main one, and does
// nothing on a node that arranges no children.
void fill_main_axis(const BoxStyle& box, SizeLimits& limits);

// Derives the unsettled axis from the settled one. Does nothing when both are
// settled (the constraints win, and measure() reports the disagreement) or
// when neither is (the content decides first, and the ratio grows the result).
void derive_from_aspect(const BoxStyle& box, SizeLimits& limits);

// The smallest box of this ratio that CONTAINS `size`. Never smaller than
// `size` on either axis, which is what keeps children inside the box they were
// laid out against.
[[nodiscard]] PixelSize grow_to_aspect(PixelSize size, float ratio);

// Border plus padding: the ring between the border box a node reports and the
// content box its children live in. Margin is not part of it, by design -
// margin belongs to the parent (design.md section 5.9.4).
[[nodiscard]] EdgeInsets content_insets(const BoxStyle& box);

struct LayoutTree::Impl {
  explicit Impl(const TreeSpec& spec);

  RenderTree render;
  std::vector<LayoutNode> nodes;
  std::vector<std::uint32_t> dirty;
  std::vector<std::string> diagnostics;
  LayoutStats stats;
  std::size_t max_damage_rects = DamageRegion::kDefaultMaxRects;

  // `own_style_changed` distinguishes the two reasons a node can be dirty,
  // and they stop in different places. When something INSIDE a node changed,
  // a node whose size is already determined absorbs the mark. When the node's
  // OWN box changed, that is no longer true - a size determined by a style
  // that just changed can change - so only a tight incoming constraint, which
  // the style cannot override, still absorbs it.
  void mark_needs_layout(std::uint32_t index, bool own_style_changed);

  [[nodiscard]] bool is_boundary(std::uint32_t index) const;

  // Returns the node's border-box size. Recursive, once per node per pass -
  // there is no speculative second call, which is what makes the pass O(n).
  PixelSize layout_node(std::uint32_t index, const BoxConstraints& constraints);

  // Dispatches on LayoutKind and writes every child's local rectangle. Split
  // out so that layout_node() is only about the cache and the boundary.
  PixelSize measure(std::uint32_t index, const BoxConstraints& constraints);

  PixelSize measure_flex(std::uint32_t index, const SizeLimits& limits,
                         const BoxConstraints& inner, const Axis& axis);

  // Decides where one child's base main extent comes from, and lays the child
  // out NOW when the only way to learn it is to measure it. A child whose base
  // is DECLARED is left deferred, so that its allotment can be computed from
  // every sibling's base first and the child laid out once, at that answer.
  void classify_child(FlexItem& item, const BoxStyle& child_box, const FlexRoom& room,
                      const Axis& axis);

  // Lays out every child of a row or column exactly once - the inflexible
  // ones under the room available, then the flexible ones under the share of
  // what is left that their weight earns - and returns the main extent they
  // occupy, gaps included.
  int size_flex_children(std::uint32_t index, const FlexRoom& room, const Axis& axis);

  // Writes each child's local rectangle. Separate from sizing because the
  // leftover space main_align distributes only exists once the container has
  // been told how big it ended up, which is after every child has a size.
  void place_flex_children(std::uint32_t index, PixelSize size, int used, const Axis& axis);

  PixelSize measure_wrap(std::uint32_t index, const SizeLimits& limits,
                         const BoxConstraints& inner, const Axis& axis);

  // Lays out every child of a wrapping container exactly once, under LOOSE
  // constraints on both axes, and breaks them into runs as it goes. Loose is
  // what makes one pass enough: a child's size is then a function of the room
  // the container offers rather than of the run it turns out to land in, so
  // no child has to be re-constrained once a run closes.
  WrapLayout size_wrap_children(std::uint32_t index, const FlexRoom& room, const Axis& axis);

  void place_wrap_children(std::uint32_t index, PixelSize size, const WrapLayout& wrapped,
                           const Axis& axis);

  PixelSize measure_leaf(std::uint32_t index, const SizeLimits& limits,
                         const BoxConstraints& inner);
  PixelSize measure_absolute(std::uint32_t index, const SizeLimits& limits,
                             const BoxConstraints& inner);

  // The container's `align`, unless this child overrode it with `align_self`.
  // One function, called by both the sizing pass and the placing pass, because
  // the two must agree: a child stretched during sizing and positioned as if
  // it were not would sit in the wrong place by exactly its own slack.
  [[nodiscard]] CrossAlign cross_align_of(std::uint32_t container, std::uint32_t child) const;

  // Copies computed boxes into the render tree, but only for the nodes whose
  // box actually moved - which is what makes the damage a function of the
  // change rather than of the subtree. `parent_moved` suppresses double
  // counting only in the reported region: RenderTree::set_local_bounds
  // already damaged this whole subtree when the parent moved.
  void apply_bounds(std::uint32_t index, DamageRegion& moved, bool parent_moved);

  void add_subtree(std::uint32_t index, DamageRegion& region) const;

  LayoutStats run(bool full);

  // "root(column) > #2(row) > #7(leaf)". design.md section 5.4.7 wants a
  // constraint conflict to name the node it happened at; without widget names
  // this is what the tree can say about itself.
  [[nodiscard]] std::string path_of(std::uint32_t index) const;
  void report(std::uint32_t index, const std::string& message);
};

}  // namespace dg
