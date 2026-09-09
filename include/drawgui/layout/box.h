// The box model: what a parent tells a child it may be, and what a child is
// allowed to want.
//
// Constraints go down, sizes come up, and every node is laid out exactly once
// per pass. That is Flutter's protocol and design.md section 5.4.1 asks for it
// by name (invariants L1-L3). It was chosen here over CSS-style multi-pass
// reflow for one reason that is specific to this project: sub-step 1 measured
// partial repaint at 27x, which makes incremental re-layout a precondition
// rather than a nicety, and a protocol where a node's size is a pure function
// of (constraints, own style, children) is one where "this subtree's inputs
// did not change, so its outputs cannot have" is a two-line check. A reflow
// model in which a later sibling can retroactively change an earlier one has
// no such check, and every attempt to add one is a cache with a correctness
// obligation nobody can discharge.
//
// EVERYTHING HERE IS INTEGER DEVICE PIXELS, and that is a deliberate
// deviation from design.md section 5.4.9, which keeps layout in float logical
// pixels so that a DPI change repaints without re-laying-out. The reason for
// the deviation is measured: sub-step 1's damage system is only correct when
// the rectangle a node declares is exactly the rectangle it paints, and a
// float layout has to round somewhere. A node whose left edge slides from
// 10.4 to 10.6 rounds from 10 to 11, so its damage rectangle and its painted
// rectangle disagree by a pixel for one frame, which is precisely the class of
// bug the byte-identity test exists to catch - and it would catch it only on
// the frames where the fraction happened to cross a boundary.
//
// What the deviation costs is that a DPI change re-lays-out rather than only
// repainting. That is close to free, because a DPI change also changes the
// framebuffer size, which forces a full repaint anyway; the relayout rides
// along on a frame that was already the most expensive one in the session.
// doc/layout.md records this trade in full.

#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>

#include "drawgui/base/pixel_geometry.h"

namespace dg {

// "As large as you like." A sentinel rather than a separate optional field,
// because every arithmetic site would otherwise have to branch on presence
// anyway - but note that it is INT_MAX, so nothing may ever be added to it.
// shrink_bound() below is the only sanctioned way to make a bound smaller,
// and it is what keeps UBSan quiet about signed overflow.
inline constexpr int kUnbounded = std::numeric_limits<int>::max();

[[nodiscard]] constexpr bool is_bounded(int value) {
  return value != kUnbounded;
}

// A bound reduced by `amount`, saturating at zero and leaving an unbounded
// bound unbounded. Subtracting from a bound is the single most common
// operation in this file (padding, border and margin all do it), and doing it
// by hand is how INT_MAX - 8 ends up meaning "bounded at 2147483639".
[[nodiscard]] constexpr int shrink_bound(int bound, int amount) {
  if (!is_bounded(bound)) {
    return kUnbounded;
  }
  return std::max(0, bound - amount);
}

// Space on the four sides of a box. Used for margin, border and padding, which
// differ in what they mean but not in their shape.
struct EdgeInsets {
  int left = 0;
  int top = 0;
  int right = 0;
  int bottom = 0;

  [[nodiscard]] static constexpr EdgeInsets all(int amount) {
    return EdgeInsets{amount, amount, amount, amount};
  }

  [[nodiscard]] static constexpr EdgeInsets symmetric(int horizontal, int vertical) {
    return EdgeInsets{horizontal, vertical, horizontal, vertical};
  }

  [[nodiscard]] constexpr int horizontal() const { return left + right; }
  [[nodiscard]] constexpr int vertical() const { return top + bottom; }

  friend bool operator==(EdgeInsets, EdgeInsets) = default;
};

[[nodiscard]] constexpr EdgeInsets operator+(const EdgeInsets& a, const EdgeInsets& b) {
  return EdgeInsets{a.left + b.left, a.top + b.top, a.right + b.right, a.bottom + b.bottom};
}

// What a parent permits a child to be.
//
// The pair (min, max) per axis, with max possibly unbounded. `min > max` is
// not representable in a well-formed constraint and every operation below
// preserves that, because a node asked to be at least 40 and at most 30 has
// no correct size and the failure would surface as a negative rectangle three
// layers away from its cause.
struct BoxConstraints {
  int min_width = 0;
  int max_width = kUnbounded;
  int min_height = 0;
  int max_height = kUnbounded;

  // Exactly this size and no other. What a parent passes when it has already
  // decided; also, and more importantly here, the condition that makes a node
  // a relayout boundary - see LayoutTree.
  [[nodiscard]] static constexpr BoxConstraints tight(PixelSize size) {
    return BoxConstraints{size.width, size.width, size.height, size.height};
  }

  // At most this size, and as small as you like.
  [[nodiscard]] static constexpr BoxConstraints loose(PixelSize size) {
    return BoxConstraints{0, size.width, 0, size.height};
  }

  [[nodiscard]] constexpr bool has_bounded_width() const { return is_bounded(max_width); }
  [[nodiscard]] constexpr bool has_bounded_height() const { return is_bounded(max_height); }

  [[nodiscard]] constexpr bool is_tight_width() const {
    return has_bounded_width() && min_width >= max_width;
  }
  [[nodiscard]] constexpr bool is_tight_height() const {
    return has_bounded_height() && min_height >= max_height;
  }
  [[nodiscard]] constexpr bool is_tight() const {
    return is_tight_width() && is_tight_height();
  }

  [[nodiscard]] constexpr int constrain_width(int width) const {
    return std::max(min_width, has_bounded_width() ? std::min(width, max_width) : width);
  }
  [[nodiscard]] constexpr int constrain_height(int height) const {
    return std::max(min_height, has_bounded_height() ? std::min(height, max_height) : height);
  }
  [[nodiscard]] constexpr PixelSize constrain(PixelSize size) const {
    return PixelSize{constrain_width(size.width), constrain_height(size.height)};
  }

  // The same maxima with the minima dropped. What a parent passes to a child
  // it is not stretching.
  [[nodiscard]] constexpr BoxConstraints loosened() const {
    return BoxConstraints{0, max_width, 0, max_height};
  }

  // Room left over after `insets` are taken out of every side. The minima
  // shrink too: a node told "be at least 100 wide" whose padding is 8 a side
  // must hand its child at least 84, or the child underfills a box its parent
  // has already committed to.
  [[nodiscard]] constexpr BoxConstraints deflate(const EdgeInsets& insets) const {
    return BoxConstraints{std::max(0, min_width - insets.horizontal()),
                          shrink_bound(max_width, insets.horizontal()),
                          std::max(0, min_height - insets.vertical()),
                          shrink_bound(max_height, insets.vertical())};
  }

  friend bool operator==(BoxConstraints, BoxConstraints) = default;
};

// How a node arranges its children.
//
// Six values, not an open set and not a virtual method. design.md section
// 5.4.11 estimates the whole self-written layout subset at under a thousand
// lines and this slice implements the part of it that a widget layer cannot
// be built without; a further arrangement is a further enumerator and a
// further case in one switch, which the compiler will demand.
//
// Direction is folded into the enumerator rather than carried beside it, so
// `direction` turns a row into a column and a wrapping row into a wrapping
// column, while nothing turns a leaf into a container. doc/properties.md
// section 3.1 records that deviation from the table, which models box / flex /
// wrap / stack as node KINDS and gives `direction` to flex and wrap.
enum class LayoutKind : std::uint8_t {
  // Sizes itself from its own style and its constraints, and positions no
  // children. A leaf may still HAVE children - they are laid out at the
  // content origin with loose constraints - which is what makes a decorated
  // box with one thing in it not need a container kind of its own.
  kLeaf,

  // Children stacked along x, then along y.
  kRow,
  kColumn,

  // The same two axes, but a child that does not fit starts a new run.
  // design.md section 5.4.3 keeps wrapping out of Flex on purpose - "mixing it
  // into Flex would make the single-run path, which is 99% of use, carry the
  // multi-run branches" - so these are separate kinds rather than a flag, and
  // `grow` is deliberately not honoured under them (section 5.4.4).
  kWrapRow,
  kWrapColumn,

  // Children placed by their own left/top/right/bottom/width, relative to
  // this node's content box. design.md section 5.4.2's table.
  kAbsolute,
};

[[nodiscard]] constexpr bool wraps_children(LayoutKind kind) {
  return kind == LayoutKind::kWrapRow || kind == LayoutKind::kWrapColumn;
}

[[nodiscard]] constexpr bool arranges_children(LayoutKind kind) {
  return kind == LayoutKind::kRow || kind == LayoutKind::kColumn || wraps_children(kind);
}

// Where the leftover main-axis space goes in a row or a column.
enum class MainAlign : std::uint8_t {
  kStart,
  kCenter,
  kEnd,
  kSpaceBetween,
};

// What a child does with the cross axis it was not given.
enum class CrossAlign : std::uint8_t {
  kStart,
  kCenter,
  kEnd,

  // Tight cross constraint equal to the container's content extent. This is
  // the alignment that produces relayout boundaries, because a stretched
  // child with a flex weight is constrained tightly on BOTH axes and its size
  // therefore cannot depend on anything inside it.
  //
  // A WRAPPING container cannot honour it, and that is a structural limit
  // rather than an omission: the extent a child would be stretched to is its
  // RUN's, and a run is not closed until every child in it has reported a
  // size. Stretching would mean laying those children out a second time,
  // which is invariant L3 in design.md section 5.4.1. The wrapping
  // arrangement reports that at layout time and places the child as kStart.
  kStretch,
};

// Whether a row, column or wrapping container hugs its content on the main
// axis or fills the room it was offered.
//
// kMin is what every arrangement in this file did before it existed, so it is
// the default and nothing that does not name kMax changes by a pixel.
//
// kMax is resolved in limits_for() rather than in an arrangement, and that is
// load-bearing rather than tidy: layout_node() decides whether a node is a
// relayout boundary from limits_for()'s answer, so a container that fills its
// main axis becomes settled on that axis - and therefore a boundary when its
// cross axis is settled too - without anybody declaring it one. Resolving it
// inside measure_flex() would have left the boundary test reading the
// pre-resolution limits, which is wrong in the direction nothing observes
// until something moves.
enum class MainSize : std::uint8_t {
  kMin,
  kMax,
};

// Whether a kLeaf hands its single child an UNBOUNDED constraint on one axis,
// which is what lets that child (typically a column or row of many items) be
// measured at its own natural size instead of being squeezed to fit - the
// unbounded constraint doc/layout.md and doc/sizing.md both named as arriving
// "with scrolling" (neither built one; this is that slice).
//
// kNone is what every kLeaf did before this existed: both axes bounded by
// whatever this node's own limits resolve to, exactly as `inner.loosened()`
// already computed. kVertical/kHorizontal override ONLY that one axis's
// maximum, so the cross axis is unaffected and a node with a huge child still
// reports a size clamped by its own constraints - overflow, not growth. A
// node whose own extent on the freed axis is unbounded too gets a layout
// diagnostic (there would be nothing to scroll within), and a `grow` child
// under the freed axis gets one as well (design.md section 5.4.7's "grow
// cannot allocate an infinite amount of space", finally reachable).
//
// This is a LayoutTree-only concept: it decides the constraint this node
// hands its child, nothing else. Clipping the overflow and moving it are
// unrelated to this field - `overflow` (already in NodeStyle) and
// RenderTree::set_scroll_offset (runtime state, not a property) do those.
enum class ScrollAxis : std::uint8_t {
  kNone,
  kVertical,
  kHorizontal,
};

// Where a wrapping container puts its stack of runs on the cross axis.
//
// The direct counterpart of MainAlign one axis over, plus a stretch that
// grows the runs themselves. Meaningless with a single run, which is why
// design.md section 5.4.3 gives it to RenderWrap alone.
enum class AlignContent : std::uint8_t {
  kStart,
  kCenter,
  kEnd,

  // The leftover cross space is handed to the runs rather than placed around
  // them, so each run's extent grows. This one IS expressible in a single
  // pass, unlike CrossAlign::kStretch: a run's extent is decided after every
  // child in it has been laid out, and growing it moves children within the
  // run without re-constraining any of them.
  kStretch,

  kSpaceBetween,
  kSpaceAround,
};

// Everything about a node that layout reads.
//
// A plain struct of plain fields, deliberately: design.md section 5.15.3
// requires render-object state to be compact POD reachable by a switch rather
// than a per-node property map, and the same argument applies a layer up.
struct BoxStyle {
  LayoutKind kind = LayoutKind::kLeaf;

  // Applied by the PARENT, never by this node - design.md section 5.9.4. If a
  // node subtracted its own margin from its own size, `width` would stop
  // meaning the box the background fills, and whether the background reaches
  // into the margin would become a question with no answer.
  EdgeInsets margin;

  // border-box: `width` and `height` include both of these, and exclude
  // margin. Always border-box, never content-box (design.md section 5.9.1).
  EdgeInsets border;
  EdgeInsets padding;

  // A definite border-box size. Still clamped by the incoming constraints: a
  // parent that says "at most 60 wide" wins over a child that asked for 200,
  // because the parent has already reserved the space.
  std::optional<int> width;
  std::optional<int> height;

  // width / height. One axis derived from the other, once that other one is
  // settled - design.md section 5.4.5.
  //
  // Resolved in limits_for(), which runs before any child is touched, so this
  // is arithmetic on a constraint rather than a second measurement. The case
  // design.md's sentence does not cover is when NEITHER axis is settled: the
  // content then decides the box and the ratio GROWS the deficient axis. Only
  // ever grows, never shrinks, because children were already laid out against
  // the constraints the pre-ratio limits produced and a box that only grows
  // still contains all of them - a node painting outside the rectangle it
  // declared is the one thing damage tracking cannot survive.
  std::optional<float> aspect_ratio;

  int min_width = 0;
  int max_width = kUnbounded;
  int min_height = 0;
  int max_height = kUnbounded;

  // Share of the leftover main-axis space in the parent row or column.
  //
  // An INTEGER weight, not a float ratio, and that is a correctness decision
  // rather than a stylistic one: the distribution is exact integer division
  // with the remainder handed out to the earliest children, so re-running a
  // layout produces the same pixels rather than the same pixels up to
  // rounding. Byte-identity between an incremental and a full layout is the
  // acceptance bar for this slice, and float weights would make it a coin
  // toss on the frames where a sum lands near a half.
  int grow = 0;

  // Weight for absorbing NEGATIVE free main-axis space, the counterpart of
  // `grow`. An integer for the same reason `grow` is one.
  //
  // The deficit is split by `shrink * base`, CSS's scaled shrink factor, which
  // design.md section 5.4.3 asks for by name. Weighting by `shrink` alone
  // would take the same number of pixels from a 50 px child as from a 500 px
  // one, so the small one reaches zero while the large one is barely touched.
  //
  // ONLY A CHILD WHOSE BASE IS DECLARED CAN SHRINK - see `basis` below. That
  // restriction is what keeps every node laid out exactly once; doc/sizing.md
  // section 1 is the argument in full, and size_flex_children() reports the
  // node it refused to shrink rather than leaving it to be noticed.
  int shrink = 0;

  // The child's base main-axis size, before free space is handed out or a
  // deficit is taken back. parentData, consumed by a flex parent.
  //
  // A DECLARED number, never a measured one, and that is the whole of this
  // slice's answer to intrinsic sizing. An allotment depends on every
  // sibling's base, so a base that has to be measured cannot be turned into an
  // allotment without measuring the child a second time - once loose to learn
  // the base, once tight at the answer - and nesting that makes a pass
  // exponential rather than linear. A declared base needs no measurement at
  // all, so the allotment is computed first and the child is laid out once,
  // already at its answer.
  //
  // Deliberately NOT clamped to the room the container has. A base larger than
  // the room is exactly what produces the deficit `shrink` exists to absorb.
  //
  // Absent means the child's natural size, which is what the engine measured
  // before this field existed - so a tree that sets no basis is measured
  // exactly as it was.
  std::optional<int> basis;

  // Space between adjacent children of a row or column. design.md section
  // 5.9.4: gap and margin STACK, they do not collapse, so the real distance
  // between two children is gap + left margin + right margin.
  int gap = 0;

  // Space between adjacent RUNS of a wrapping container, on the cross axis.
  // Read only by kWrapRow and kWrapColumn; a container with one run has no
  // pair of runs to separate.
  int run_gap = 0;

  MainAlign main_align = MainAlign::kStart;
  CrossAlign cross_align = CrossAlign::kStart;

  // Read by kRow, kColumn, kWrapRow and kWrapColumn. kMin hugs the content
  // within this node's limits; kMax fills the main axis it was offered, which
  // is what gives `justify` leftover space to place on a container that would
  // otherwise shrink-wrap.
  MainSize main_size = MainSize::kMin;

  // Read only by kWrapRow and kWrapColumn, for the same reason `align` is not
  // enough there: `align` positions a child inside its own run, and this
  // positions the stack of runs inside the container.
  AlignContent align_content = AlignContent::kStart;

  // Read only by kLeaf. See ScrollAxis above: which axis, if any, this node's
  // single child is measured under an unbounded constraint on, which is what
  // lets a scrolling viewport's content grow past the viewport instead of
  // being squeezed to fit it.
  ScrollAxis scroll_axis = ScrollAxis::kNone;

  // This child's own cross-axis alignment, overriding the container's.
  //
  // parentData, consumed by whichever container holds this node, so
  // LayoutTree::set_box has to mark the PARENT when it changes - the same
  // trap margin and grow already fell into (doc/properties.md section 3.7).
  //
  // Absent is the table's `auto`: defer to the container. A separate
  // enumerator would have made `auto` a value CrossAlign has to carry into
  // every switch that positions anything, when the whole meaning of `auto` is
  // that this node has no opinion.
  std::optional<CrossAlign> align_self;

  // Consumed by a kAbsolute PARENT, ignored everywhere else. Setting any of
  // the four is what makes a child positioned; design.md section 5.4.2.
  std::optional<int> left;
  std::optional<int> top;
  std::optional<int> right;
  std::optional<int> bottom;

  friend bool operator==(const BoxStyle&, const BoxStyle&) = default;
};

}  // namespace dg
