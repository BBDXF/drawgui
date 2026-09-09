// The four arrangements. Each lays every child out exactly once, so a pass
// over the tree is O(n) and there is no speculative measurement anywhere in
// this file - see the note about intrinsic sizing in layout_tree.h.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/layout/box.h"

#include "layout/layout_impl.h"

namespace dg {
namespace {

// Where one child of a kAbsolute parent sits on one axis, from design.md
// section 5.4.2's table. `available` is the parent's content extent on that
// axis, already reduced by the child's own margin.
struct AbsoluteAxis {
  int min_extent = 0;
  int max_extent = kUnbounded;

  // Set when only the far edge was given: the position cannot be known until
  // the child has reported a size, so it is computed after layout rather than
  // before it.
  bool pin_to_end = false;
  int position = 0;
  int end = 0;
};

AbsoluteAxis absolute_axis(int available, const std::optional<int>& start,
                           const std::optional<int>& end) {
  const int room = std::max(0, available);
  AbsoluteAxis axis;
  axis.max_extent = room;

  // Both edges pinned: the child is stretched between them and its own width
  // does not get a say, which is what CSS does and what makes a two-edge
  // anchor useful. It is also the case that produces a tight constraint, and
  // therefore a relayout boundary.
  if (start.has_value() && end.has_value()) {
    const int span = std::max(0, room - *start - *end);
    axis.min_extent = span;
    axis.max_extent = span;
    axis.position = *start;
    return axis;
  }
  if (start.has_value()) {
    axis.position = *start;
    axis.max_extent = std::max(0, room - *start);
    return axis;
  }
  if (end.has_value()) {
    axis.pin_to_end = true;
    axis.end = *end;
    axis.max_extent = std::max(0, room - *end);
    return axis;
  }
  return axis;
}

// The border box that holds `content` plus the ring around it, clipped back
// into what the node is allowed to be. The low bound wins over the high one
// when they disagree, because a node that has been promised a minimum has
// already had that space reserved on its behalf.
PixelSize border_box_size(const SizeLimits& limits, const EdgeInsets& insets,
                          PixelSize content) {
  const auto pick = [](int extent, int inset, int low, int high) {
    const int desired = extent + inset;
    return std::max(low, is_bounded(high) ? std::min(desired, high) : desired);
  };
  return PixelSize{
      pick(content.width, insets.horizontal(), limits.low_width, limits.high_width),
      pick(content.height, insets.vertical(), limits.low_height, limits.high_height)};
}

}  // namespace

int aspect_partner(int settled, float ratio, bool settled_is_width) {
  const double scale = static_cast<double>(ratio);
  if (!std::isfinite(scale) || scale <= 0.0) {
    return settled;
  }
  const double raw = settled_is_width ? static_cast<double>(settled) / scale
                                      : static_cast<double>(settled) * scale;
  if (!(raw > 0.0)) {
    return 0;
  }
  if (raw >= static_cast<double>(kMaxAspectExtent)) {
    return kMaxAspectExtent;
  }
  return static_cast<int>(std::lround(raw));
}

void fill_main_axis(const BoxStyle& box, SizeLimits& limits) {
  if (box.main_size != MainSize::kMax || !arranges_children(box.kind)) {
    return;
  }
  const bool horizontal = box.kind == LayoutKind::kRow || box.kind == LayoutKind::kWrapRow;
  const int high = horizontal ? limits.high_width : limits.high_height;
  if (!is_bounded(high)) {
    return;
  }
  (horizontal ? limits.low_width : limits.low_height) = high;
}

void derive_from_aspect(const BoxStyle& box, SizeLimits& limits) {
  if (!box.aspect_ratio.has_value()) {
    return;
  }
  const bool width_settled = limits.tight_width();
  const bool height_settled = limits.tight_height();

  // Neither settled: the content decides the box first and resolved_size()
  // grows it afterwards. Both settled: the constraints win, because the parent
  // already reserved that space, and measure() reports the disagreement.
  //
  // The both-settled half of this guard is PROVABLY INERT and is kept for what
  // it says rather than for what it prevents. Deriving into a settled axis
  // clamps the result into [low, high] where low == high by definition, so it
  // can only produce the value that is already there - measured by injection,
  // which removed this half and changed nothing anywhere. It stays because the
  // clamp is an accident of how the derivation is written and this line is the
  // rule; a later derivation that stopped clamping would need it back.
  if (width_settled == height_settled) {
    return;
  }
  if (width_settled) {
    const int derived = std::clamp(aspect_partner(limits.low_width, *box.aspect_ratio, true),
                                   limits.low_height, limits.high_height);
    limits.low_height = derived;
    limits.high_height = derived;
    return;
  }
  const int derived = std::clamp(aspect_partner(limits.low_height, *box.aspect_ratio, false),
                                 limits.low_width, limits.high_width);
  limits.low_width = derived;
  limits.high_width = derived;
}

PixelSize grow_to_aspect(PixelSize size, float ratio) {
  if (static_cast<double>(size.width) <
      static_cast<double>(size.height) * static_cast<double>(ratio)) {
    return PixelSize{aspect_partner(size.height, ratio, false), size.height};
  }
  return PixelSize{size.width, aspect_partner(size.width, ratio, true)};
}

namespace {

// border_box_size(), plus the aspect ratio in the one case limits_for() cannot
// resolve: neither axis settled, so the content decides the box and the ratio
// then GROWS the deficient side of it.
//
// Only grows. Children were laid out against constraints derived from the
// pre-ratio limits and placed inside the box those produced; a box that only
// ever grows still contains every one of them, so no node ends up painting
// outside the rectangle it declared - which is what damage tracking rests on.
PixelSize resolved_size(const BoxStyle& box, const SizeLimits& limits, const EdgeInsets& insets,
                        PixelSize content) {
  const PixelSize sized = border_box_size(limits, insets, content);
  if (!box.aspect_ratio.has_value() || limits.tight_width() || limits.tight_height()) {
    return sized;
  }
  const PixelSize grown = grow_to_aspect(sized, *box.aspect_ratio);
  return PixelSize{std::clamp(grown.width, limits.low_width, limits.high_width),
                   std::clamp(grown.height, limits.low_height, limits.high_height)};
}

// Hands out `pool` in proportion to the weights seen so far, by prefix sums
// rather than by dividing each share independently. Two properties follow and
// both are load-bearing: the shares add up to exactly `pool`, so no pixel is
// lost to truncation, and the result is a function of the weights alone, so
// an incremental pass and a full pass agree. Truncation error accumulates in
// the running total and is released to the children it reaches, which means
// the extra pixels land on the later ones - 100 across three equal weights is
// 33, 33, 34.
//
// share_upto() is that running total on its own, because a wrapping container
// distributes cross-axis space by POSITION rather than by weight - "how much
// belongs before run 3 of 5" - and computing it any other way is how 100 split
// five ways becomes 99.
int share_upto(int pool, int units, int units_total) {
  if (units_total <= 0) {
    return 0;
  }
  return static_cast<int>((static_cast<std::int64_t>(pool) * units) / units_total);
}

// The same running total for the shrink distribution, whose weights are a
// PRODUCT of two bounded quantities and therefore do not fit in an int. The
// caller guarantees `units_total` has been reduced below 2^31, which is what
// keeps `pool * units` inside 64 bits.
std::int64_t share_upto_wide(int pool, std::int64_t units, std::int64_t units_total) {
  if (units_total <= 0) {
    return 0;
  }
  return (static_cast<std::int64_t>(pool) * units) / units_total;
}

int prefix_share(int pool, int weight_so_far, int weight_total, int already_given) {
  if (weight_total <= 0) {
    return 0;
  }
  return share_upto(pool, weight_so_far, weight_total) - already_given;
}

// `source` folded into the child's own min/max on the main axis.
//
// Deliberately NOT clamped to the container's room: a base larger than the
// room is exactly the deficit `shrink` exists to absorb.
std::optional<int> clamped_main(const BoxStyle& box, const Axis& axis,
                                const std::optional<int>& source) {
  if (!source.has_value()) {
    return std::nullopt;
  }
  const int low = std::max(0, axis.horizontal ? box.min_width : box.min_height);
  const int high = axis.horizontal ? box.max_width : box.max_height;
  const int capped = is_bounded(high) ? std::min(*source, high) : *source;
  return std::max(low, std::max(0, capped));
}

// What a child brings to the sizing pass before anything is measured: its
// declared base if it has one, and whether that base came from an explicit
// `basis` rather than from a definite size.
//
// This is the whole of doc/sizing.md section 1 in one struct. An allotment
// depends on every sibling's base, so a base that has to be measured cannot
// become an allotment without laying the child out a second time; a declared
// base can be turned into an allotment before the child is touched at all.
//
// The two are kept apart because a grow child takes ONLY the explicit basis -
// never its own width, which is what CSS's flex-basis: auto would make it.
// That is not a new decision: it is what this engine has always done, and
// changing it would move every flexible child in every existing scene.
struct DeclaredBase {
  std::optional<int> from_basis;
  std::optional<int> any;
};

DeclaredBase declared_base(const BoxStyle& box, const Axis& axis) {
  DeclaredBase declared;
  declared.from_basis = clamped_main(box, axis, box.basis);
  declared.any = declared.from_basis.has_value()
                     ? declared.from_basis
                     : clamped_main(box, axis, axis.horizontal ? box.width : box.height);
  return declared;
}

// Positive free space, handed to the grow weights. Prefix sums rather than a
// per-child division, so the shares add up to exactly `pool` - 100 across
// three equal weights is 33, 33, 34.
void distribute_growth(std::vector<FlexItem>& items, int pool, int total_grow) {
  if (total_grow <= 0) {
    return;
  }
  int weight_so_far = 0;
  int given = 0;
  for (FlexItem& item : items) {
    if (item.grow <= 0) {
      continue;
    }
    weight_so_far += item.grow;
    const int share = prefix_share(pool, weight_so_far, total_grow, given);
    given += share;
    item.allot = item.base + share;
  }
}

// Negative free space, taken back from the shrink weights.
//
// The weight is `shrink * base`, CSS's scaled shrink factor and what design.md
// section 5.4.3 asks for by name. Weighting by `shrink` alone would take the
// same number of pixels from a 50 px child as from a 500 px one, so the small
// one would reach zero while the large one was barely touched.
//
// A child with a grow weight is a grow child and is excluded: when there is a
// deficit its share of positive free space is zero, so it already receives its
// base and nothing more.
//
// THE REDUCTION is the one piece of arithmetic here that is not exact. A
// weight is a product of two quantities each bounded at 2^24 by the property
// boundary, so a total can exceed what `pool * units` may safely hold in 64
// bits. Dividing every weight by one common divisor brings the total under
// 2^31; the divisor is a pure function of the weights, so an incremental pass
// and a full pass reduce identically, and in any scene anybody will build it
// is 1 and nothing is perturbed.
void distribute_deficit(std::vector<FlexItem>& items, int deficit) {
  std::int64_t total_weight = 0;
  for (FlexItem& item : items) {
    if (!item.deferred || item.grow > 0 || item.shrink <= 0) {
      continue;
    }
    item.shrink_weight = static_cast<std::int64_t>(item.shrink) * item.base;
    total_weight += item.shrink_weight;
  }
  if (total_weight <= 0) {
    return;
  }

  const std::int64_t divisor = (total_weight >> 31) + 1;
  std::int64_t reduced_total = 0;
  for (FlexItem& item : items) {
    item.shrink_weight /= divisor;
    reduced_total += item.shrink_weight;
  }
  if (reduced_total <= 0) {
    return;
  }

  std::int64_t weight_so_far = 0;
  std::int64_t given = 0;
  for (FlexItem& item : items) {
    if (item.shrink_weight <= 0) {
      continue;
    }
    weight_so_far += item.shrink_weight;
    const std::int64_t share = share_upto_wide(deficit, weight_so_far, reduced_total) - given;
    given += share;

    // A child that reaches its declared minimum stops absorbing, and the
    // residual keeps the container overrunning - which the overrun diagnostic
    // already reports. CSS re-runs the distribution over the children that did
    // not freeze; that loop is pure arithmetic and would cost no measurement,
    // but it is a second rule with its own semantics and it is left out until
    // something needs it. doc/sizing.md section 1.8 records the deviation.
    item.allot = std::max(item.floor_main, item.base - static_cast<int>(share));
  }
}

// Where the first child of a run starts, and how much space is shared out
// between the children after it. One function for a flex container and for
// every run of a wrapping one, so the two cannot drift apart.
struct MainSpread {
  int cursor = 0;
  int spread = 0;
};

MainSpread main_spread(MainAlign align, int leftover, int gap_count) {
  MainSpread out;
  switch (align) {
    case MainAlign::kStart:
      break;
    case MainAlign::kCenter:
      out.cursor = leftover / 2;
      break;
    case MainAlign::kEnd:
      out.cursor = leftover;
      break;
    case MainAlign::kSpaceBetween:
      out.spread = gap_count > 0 ? leftover : 0;
      break;
  }
  return out;
}

int cross_offset(CrossAlign align, int slack) {
  switch (align) {
    case CrossAlign::kStart:
    case CrossAlign::kStretch:
      return 0;
    case CrossAlign::kCenter:
      return slack / 2;
    case CrossAlign::kEnd:
      return slack;
  }
  return 0;
}

// Cross-axis space that belongs BEFORE run `index`, cumulatively.
//
// Cumulative rather than per-gap so that the caller advances by the difference
// between two of these, which is what keeps the run positions exact: a
// per-gap value rounded independently would leave the last run short by up to
// one pixel per gap.
int run_lead(AlignContent align, int leftover, int index, int count) {
  switch (align) {
    case AlignContent::kStart:
    case AlignContent::kStretch:
      return 0;
    case AlignContent::kCenter:
      return leftover / 2;
    case AlignContent::kEnd:
      return leftover;
    case AlignContent::kSpaceBetween:
      return share_upto(leftover, index, count - 1);
    case AlignContent::kSpaceAround:
      // Half a share outside the first and last run, a full share between
      // adjacent ones - so the sequence of gaps is 1, 2, 2, ..., 2, 1 in units
      // of half a share, and run `index` is preceded by 2*index + 1 of them.
      return share_upto(leftover, (2 * index) + 1, 2 * count);
  }
  return 0;
}

// Cross-axis space added to the runs THEMSELVES before run `index`,
// cumulatively. Only kStretch grows a run; every other value places the runs
// at the extents they measured.
int run_growth(AlignContent align, int leftover, int index, int count) {
  return align == AlignContent::kStretch ? share_upto(leftover, index, count) : 0;
}

}  // namespace

EdgeInsets content_insets(const BoxStyle& box) {
  return box.border + box.padding;
}

SizeLimits limits_for(const BoxStyle& box, const BoxConstraints& constraints) {
  const auto merge = [](int constraint_low, int constraint_high, int style_low, int style_high,
                        const std::optional<int>& definite, int& low, int& high) {
    const auto into_constraint = [&](int value) {
      const int capped = is_bounded(constraint_high) ? std::min(value, constraint_high) : value;
      return std::max(constraint_low, capped);
    };
    low = into_constraint(std::max(constraint_low, style_low));
    high = std::max(low, into_constraint(std::min(constraint_high, style_high)));
    if (definite.has_value()) {
      low = std::clamp(*definite, low, high);
      high = low;
    }
  };

  SizeLimits limits;
  merge(constraints.min_width, constraints.max_width, box.min_width, box.max_width, box.width,
        limits.low_width, limits.high_width);
  merge(constraints.min_height, constraints.max_height, box.min_height, box.max_height,
        box.height, limits.low_height, limits.high_height);

  // main_size first, then aspect_ratio: filling the main axis settles it, and
  // a settled axis is what the ratio derives the other one from. The reverse
  // order would let a ratio resolve against an extent main_size was about to
  // change.
  fill_main_axis(box, limits);
  derive_from_aspect(box, limits);
  return limits;
}

PixelSize LayoutTree::Impl::measure(std::uint32_t index, const BoxConstraints& constraints) {
  const BoxStyle& box = nodes[index].box;
  const SizeLimits limits = limits_for(box, constraints);
  const EdgeInsets insets = content_insets(box);

  // The aspect-ratio cases that have no correct answer, both reported the same
  // way because a caller can only act on the same fact: the node is settled on
  // both axes at a shape the ratio does not describe. That happens when the
  // parent fixed both - the constraints win, it has already reserved the space
  // - and when the derived axis was then clamped by a bound of its own.
  //
  // Compared against the derived extent rather than reported whenever a ratio
  // meets tight limits, so a ratio that AGREES stays quiet and a stretched
  // square asking for 1:1 does not produce a message every frame.
  if (box.aspect_ratio.has_value() && limits.tight_width() && limits.tight_height()) {
    const int wanted = aspect_partner(limits.low_height, *box.aspect_ratio, false);
    if (wanted != limits.low_width) {
      report(index, "aspect_ratio cannot be honoured; this node is fixed at " +
                        std::to_string(limits.low_width) + "x" +
                        std::to_string(limits.low_height) + " and the ratio needs " +
                        std::to_string(wanted) + "x" + std::to_string(limits.low_height));
    }
  }

  // A tightly sized node passes its minimum down too, so a child told to
  // stretch actually fills the box its parent has already committed to. A
  // node that is free to shrink hands down a zero minimum, which is what
  // shrink-to-fit means.
  const BoxConstraints inner{
      limits.tight_width() ? std::max(0, limits.low_width - insets.horizontal()) : 0,
      shrink_bound(limits.high_width, insets.horizontal()),
      limits.tight_height() ? std::max(0, limits.low_height - insets.vertical()) : 0,
      shrink_bound(limits.high_height, insets.vertical())};

  switch (box.kind) {
    case LayoutKind::kLeaf:
      return measure_leaf(index, limits, inner);
    case LayoutKind::kRow:
      return measure_flex(index, limits, inner, Axis{true});
    case LayoutKind::kColumn:
      return measure_flex(index, limits, inner, Axis{false});
    case LayoutKind::kWrapRow:
      return measure_wrap(index, limits, inner, Axis{true});
    case LayoutKind::kWrapColumn:
      return measure_wrap(index, limits, inner, Axis{false});
    case LayoutKind::kAbsolute:
      return measure_absolute(index, limits, inner);
  }
  return resolved_size(box, limits, insets, PixelSize{});
}

PixelSize LayoutTree::Impl::measure_leaf(std::uint32_t index, const SizeLimits& limits,
                                         const BoxConstraints& inner) {
  const EdgeInsets insets = content_insets(nodes[index].box);
  const std::vector<std::uint32_t>& children = nodes[index].children;

  // A leaf still carries children - that is what a decorated box with one
  // thing in it is - and they are stacked at the content origin rather than
  // arranged. Their sizes are what the leaf shrinks to fit.
  PixelSize content;
  for (const std::uint32_t child : children) {
    const EdgeInsets margin = nodes[child].box.margin;
    const PixelSize size = layout_node(child, inner.loosened().deflate(margin));
    nodes[child].local =
        PixelRect{insets.left + margin.left, insets.top + margin.top, size.width, size.height};
    content.width = std::max(content.width, size.width + margin.horizontal());
    content.height = std::max(content.height, size.height + margin.vertical());
  }
  return resolved_size(nodes[index].box, limits, insets, content);
}

CrossAlign LayoutTree::Impl::cross_align_of(std::uint32_t container,
                                            std::uint32_t child) const {
  const CrossAlign wanted =
      nodes[child].box.align_self.value_or(nodes[container].box.cross_align);

  // The one place the degradation lives, so that the sizing pass and the
  // placing pass cannot disagree about whether a child was stretched. See
  // CrossAlign::kStretch in box.h: a run's extent is not known until the run
  // closes, so honouring it would mean a second layout of every child in the
  // run. size_wrap_children() reports it against the node it happened at.
  return wraps_children(nodes[container].box.kind) && wanted == CrossAlign::kStretch
             ? CrossAlign::kStart
             : wanted;
}

void LayoutTree::Impl::classify_child(FlexItem& item, const BoxStyle& child_box,
                                      const FlexRoom& room, const Axis& axis) {
  const DeclaredBase declared = declared_base(child_box, axis);

  if (item.grow > 0) {
    item.base = declared.from_basis.value_or(0);
    item.deferred = true;
    return;
  }
  if (declared.any.has_value() && (child_box.basis.has_value() || item.shrink > 0)) {
    item.base = *declared.any;
    item.deferred = true;
    return;
  }

  const PixelSize size =
      layout_node(item.node, axis.constraints_of(0, shrink_bound(room.main, item.margin_main),
                                                 item.min_cross, item.max_cross));
  item.base = axis.main_of(size);
  if (item.shrink > 0) {
    report(item.node,
           "shrink was not applied; this child's base main size is its measured natural "
           "size, and shrinking from a measured base needs a second layout of its subtree "
           "(invariant L3, design.md section 5.4.1). Give it a basis, or a definite size on "
           "the container's main axis");
  }
}

int LayoutTree::Impl::size_flex_children(std::uint32_t index, const FlexRoom& room,
                                         const Axis& axis) {
  const std::vector<std::uint32_t>& children = nodes[index].children;
  const int gap = nodes[index].box.gap;
  const int gaps = children.empty() ? 0 : gap * static_cast<int>(children.size() - 1);

  int total_grow = 0;
  for (const std::uint32_t child : children) {
    total_grow += std::max(0, nodes[child].box.grow);
  }

  // Per child rather than per container, which is what align_self buys: a
  // child that opted out of a stretching row must not receive the tight cross
  // constraint, and a child that opted IN inside a non-stretching one must.
  // With no align_self anywhere this is exactly the container's own answer.
  const auto stretches = [&](std::uint32_t child) {
    return is_bounded(room.cross) && cross_align_of(index, child) == CrossAlign::kStretch;
  };

  // ONE walk in child order, sorting each child into measured, declared or
  // grown. Both of the latter are entered only by a property that did not
  // exist before this slice, so a tree that sets neither takes exactly the
  // calls, in exactly the order, that it took before - which matters for
  // diagnostics as well as for pixels, because report() appends to a vector
  // whose order the property parity test compares.
  std::vector<FlexItem> items;
  items.reserve(children.size());

  int used = gaps;
  int grow_base_total = 0;

  for (const std::uint32_t child : children) {
    const BoxStyle& child_box = nodes[child].box;
    const EdgeInsets margin = child_box.margin;

    FlexItem item;
    item.node = child;
    item.margin_main = axis.main_total(margin);
    item.max_cross = shrink_bound(room.cross, axis.cross_total(margin));
    item.min_cross = stretches(child) ? item.max_cross : 0;
    item.grow = std::max(0, child_box.grow);
    item.shrink = std::max(0, child_box.shrink);
    item.floor_main = std::max(0, axis.horizontal ? child_box.min_width : child_box.min_height);

    classify_child(item, child_box, room, axis);
    if (item.grow > 0) {
      // A grow child's margin still comes out of its share rather than out of
      // `used`, which is the split the engine already had; with a base of zero
      // this line and the next reproduce it digit for digit.
      grow_base_total += item.base;
    } else {
      used += item.base + item.margin_main;
    }
    item.allot = item.base;
    items.push_back(item);
  }

  const int free_space = room.main - used - grow_base_total;
  if (free_space > 0) {
    distribute_growth(items, free_space, total_grow);
  } else if (free_space < 0) {
    distribute_deficit(items, -free_space);
  }

  for (const FlexItem& item : items) {
    if (!item.deferred) {
      continue;
    }
    const int extent = item.grow > 0 ? std::max(0, item.allot - item.margin_main) : item.allot;
    const PixelSize size = layout_node(
        item.node, axis.constraints_of(extent, extent, item.min_cross, item.max_cross));

    // A grow child never had its base or its margin counted, so it contributes
    // both here. A declared child already contributed base + margin during the
    // walk, so only the difference the distribution made is outstanding.
    used +=
        item.grow > 0 ? axis.main_of(size) + item.margin_main : axis.main_of(size) - item.base;
  }
  return used;
}

void LayoutTree::Impl::place_flex_children(std::uint32_t index, PixelSize size, int used,
                                           const Axis& axis) {
  const EdgeInsets insets = content_insets(nodes[index].box);
  const int gap = nodes[index].box.gap;
  const std::vector<std::uint32_t>& children = nodes[index].children;

  const int inner_main = std::max(0, axis.main_of(size) - axis.main_total(insets));
  const int inner_cross = std::max(0, axis.cross_of(size) - axis.cross_total(insets));
  const int leftover = std::max(0, inner_main - used);
  const int gap_count = children.size() >= 2 ? static_cast<int>(children.size() - 1) : 0;

  const MainSpread spread = main_spread(nodes[index].box.main_align, leftover, gap_count);
  int cursor = spread.cursor;

  int spread_given = 0;
  int slot = 0;
  for (const std::uint32_t child : children) {
    const EdgeInsets margin = nodes[child].box.margin;
    const PixelSize child_size = nodes[child].size;
    const int room = std::max(0, inner_cross - axis.cross_total(margin));
    const int slack = std::max(0, room - axis.cross_of(child_size));
    const int cross_position = cross_offset(cross_align_of(index, child), slack);

    nodes[child].local = axis.rect_of(
        axis.main_start(insets) + cursor + axis.main_start(margin),
        axis.cross_start(insets) + cross_position + axis.cross_start(margin), child_size);

    cursor += axis.main_of(child_size) + axis.main_total(margin) + gap;
    if (slot < gap_count) {
      const int extra = prefix_share(spread.spread, slot + 1, gap_count, spread_given);
      spread_given += extra;
      cursor += extra;
    }
    ++slot;
  }
}

PixelSize LayoutTree::Impl::measure_flex(std::uint32_t index, const SizeLimits& limits,
                                         const BoxConstraints& inner, const Axis& axis) {
  const EdgeInsets insets = content_insets(nodes[index].box);
  const std::vector<std::uint32_t>& children = nodes[index].children;

  FlexRoom room;
  room.main = axis.main_max(inner);
  room.cross = axis.cross_max(inner);

  // An unbounded axis is not currently constructible: the root is laid out
  // tight to the viewport and every rule in this file derives a child's
  // maximum from its parent's, so boundedness is inherited all the way down.
  // These two fallbacks are what the algorithm WOULD do, defined rather than
  // left to produce a nonsense number, and they are deliberately silent -
  // design.md section 5.4.7 wants a node-path diagnostic for a constraint
  // conflict, and a diagnostic nothing can reach is a message written for a
  // caller that does not exist. The unbounded axis arrives with scrolling.
  room.stretching =
      nodes[index].box.cross_align == CrossAlign::kStretch && is_bounded(room.cross);
  if (!is_bounded(room.main)) {
    room.main = 0;
  }

  const int used = size_flex_children(index, room, axis);

  int content_cross = 0;
  for (const std::uint32_t child : children) {
    content_cross = std::max(content_cross, axis.cross_of(nodes[child].size) +
                                                axis.cross_total(nodes[child].box.margin));
  }
  if (room.stretching) {
    content_cross = room.cross;
  }

  // The constraint conflict that IS reachable here, and the one a shrinking
  // window produces first: inflexible children each fit the room on their
  // own, and together with the gaps between them they do not. Nothing about
  // the resulting frame looks broken - the children simply run past the edge
  // of the box that contains them - which is exactly the silent failure
  // design.md section 5.4.7 refuses to allow, so it is named, measured and
  // attributed to the node it happened at.
  if (used > room.main) {
    report(index, "children overrun the main axis by " + std::to_string(used - room.main) +
                      " px (" + std::to_string(used) + " needed, " + std::to_string(room.main) +
                      " available)");
  }

  const PixelSize content =
      axis.horizontal ? PixelSize{used, content_cross} : PixelSize{content_cross, used};
  const PixelSize size = resolved_size(nodes[index].box, limits, insets, content);
  place_flex_children(index, size, used, axis);
  return size;
}

WrapLayout LayoutTree::Impl::size_wrap_children(std::uint32_t index, const FlexRoom& room,
                                                const Axis& axis) {
  const std::vector<std::uint32_t>& children = nodes[index].children;
  const int gap = nodes[index].box.gap;
  const int run_gap = nodes[index].box.run_gap;
  const CrossAlign container_align = nodes[index].box.cross_align;

  WrapLayout wrapped;
  wrapped.child_main.reserve(children.size());
  wrapped.child_cross.reserve(children.size());

  bool any_grow = false;
  bool any_stretch = false;
  bool any_flex_base = false;

  Run run;
  for (std::size_t slot = 0; slot < children.size(); ++slot) {
    const std::uint32_t child = children[slot];
    const EdgeInsets margin = nodes[child].box.margin;
    any_grow = any_grow || nodes[child].box.grow > 0;
    any_flex_base =
        any_flex_base || nodes[child].box.basis.has_value() || nodes[child].box.shrink > 0;
    any_stretch = any_stretch ||
                  nodes[child].box.align_self.value_or(container_align) == CrossAlign::kStretch;

    // LOOSE on both axes, which is the whole reason one pass is enough. A
    // child measured against the room the container offers has a size that
    // does not depend on which run it lands in, so closing a run never
    // invalidates a size already taken - invariant L3 in design.md section
    // 5.4.1, which design.md section 5.4.4 explicitly requires RenderWrap to
    // preserve.
    const PixelSize size = layout_node(
        child, axis.constraints_of(0, shrink_bound(room.main, axis.main_total(margin)), 0,
                                   shrink_bound(room.cross, axis.cross_total(margin))));
    const int child_main = axis.main_of(size) + axis.main_total(margin);
    const int child_cross = axis.cross_of(size) + axis.cross_total(margin);
    wrapped.child_main.push_back(child_main);
    wrapped.child_cross.push_back(child_cross);

    // A run that is still empty takes the child whatever its size, so a child
    // wider than the whole container overruns its run rather than producing a
    // run with nothing in it and then overrunning the next one anyway.
    const int extended = run.empty() ? child_main : run.main + gap + child_main;
    if (!run.empty() && is_bounded(room.main) && extended > room.main) {
      wrapped.runs.push_back(run);
      run = Run{slot, slot, child_main, 0};
    } else {
      run.main = extended;
    }
    run.end = slot + 1;
    run.cross = std::max(run.cross, child_cross);
  }
  if (!run.empty()) {
    wrapped.runs.push_back(run);
  }

  for (const Run& closed : wrapped.runs) {
    wrapped.content_main = std::max(wrapped.content_main, closed.main);
    wrapped.content_cross += closed.cross;
  }
  if (wrapped.runs.size() >= 2) {
    wrapped.content_cross += run_gap * static_cast<int>(wrapped.runs.size() - 1);
  }

  if (any_grow) {
    report(index,
           "grow is not distributed by a wrapping container; a weight would have to be "
           "resolved against the run the child lands in, which is not known until the run "
           "is closed (design.md section 5.4.4 excludes it)");
  }
  if (any_flex_base) {
    report(index,
           "basis and shrink are not resolved by a wrapping container; both describe a "
           "child's share of ONE main axis, and a wrapping container hands out as many as "
           "it has runs - which run a child lands in is not known until the run is closed "
           "(the table lists both as consumed_by = flex, as it does grow)");
  }
  if (any_stretch) {
    report(index,
           "align=stretch is not honoured by a wrapping container and was treated as "
           "start; the extent to stretch to is the child's RUN, which is not known until "
           "every child in it has been laid out, so honouring it would need a second "
           "layout of each child (invariant L3, design.md section 5.4.1)");
  }
  return wrapped;
}

void LayoutTree::Impl::place_wrap_children(std::uint32_t index, PixelSize size,
                                           const WrapLayout& wrapped, const Axis& axis) {
  const EdgeInsets insets = content_insets(nodes[index].box);
  const int gap = nodes[index].box.gap;
  const int run_gap = nodes[index].box.run_gap;
  const MainAlign main_align = nodes[index].box.main_align;
  const AlignContent align_content = nodes[index].box.align_content;
  const std::vector<std::uint32_t>& children = nodes[index].children;

  const int inner_main = std::max(0, axis.main_of(size) - axis.main_total(insets));
  const int inner_cross = std::max(0, axis.cross_of(size) - axis.cross_total(insets));

  // Against the size the container ENDED UP at, not against the room it was
  // offered. A shrink-to-fit wrap ends up exactly as tall as its runs, so
  // there is nothing for align_content to distribute; a wrap given a definite
  // cross extent has the difference, and that is the space it places.
  const auto run_count = static_cast<int>(wrapped.runs.size());
  const int leftover_cross = std::max(0, inner_cross - wrapped.content_cross);

  int cross_cursor = 0;
  int lead_given = 0;
  int growth_given = 0;
  for (int which = 0; which < run_count; ++which) {
    const Run& run = wrapped.runs[static_cast<std::size_t>(which)];

    const int lead = run_lead(align_content, leftover_cross, which, run_count);
    cross_cursor += lead - lead_given;
    lead_given = lead;

    const int growth = run_growth(align_content, leftover_cross, which + 1, run_count);
    const int run_extent = run.cross + (growth - growth_given);
    growth_given = growth;

    const int leftover_main = std::max(0, inner_main - run.main);
    const int gap_count = run.count() >= 2 ? static_cast<int>(run.count() - 1) : 0;
    const MainSpread spread = main_spread(main_align, leftover_main, gap_count);

    int cursor = spread.cursor;
    int spread_given = 0;
    int within = 0;
    for (std::size_t slot = run.begin; slot < run.end; ++slot) {
      const std::uint32_t child = children[slot];
      const EdgeInsets margin = nodes[child].box.margin;
      const PixelSize child_size = nodes[child].size;
      const int room = std::max(0, run_extent - axis.cross_total(margin));
      const int slack = std::max(0, room - axis.cross_of(child_size));
      const int cross_position = cross_offset(cross_align_of(index, child), slack);

      nodes[child].local = axis.rect_of(
          axis.main_start(insets) + cursor + axis.main_start(margin),
          axis.cross_start(insets) + cross_cursor + cross_position + axis.cross_start(margin),
          child_size);

      cursor += axis.main_of(child_size) + axis.main_total(margin) + gap;
      if (within < gap_count) {
        const int extra = prefix_share(spread.spread, within + 1, gap_count, spread_given);
        spread_given += extra;
        cursor += extra;
      }
      ++within;
    }

    cross_cursor += run_extent;
    if (which + 1 < run_count) {
      cross_cursor += run_gap;
    }
  }
}

PixelSize LayoutTree::Impl::measure_wrap(std::uint32_t index, const SizeLimits& limits,
                                         const BoxConstraints& inner, const Axis& axis) {
  const EdgeInsets insets = content_insets(nodes[index].box);

  FlexRoom room;
  room.main = axis.main_max(inner);
  room.cross = axis.cross_max(inner);

  // No fallback for an unbounded main axis, unlike measure_flex. Wrapping has
  // a defined answer there and it is the useful one: with no bound, nothing
  // ever fails to fit, so every child lands in a single run. is_bounded()
  // guards the only two places that compare against it.
  const WrapLayout wrapped = size_wrap_children(index, room, axis);

  // The overrun a wrapping container can still produce, and the only one:
  // a single child too large for the room, which no amount of breaking can
  // fix. Reported for the same reason the flex overrun is - the frame looks
  // merely odd rather than broken, so it has to be said out loud.
  if (is_bounded(room.main) && wrapped.content_main > room.main) {
    report(index, "a wrapped run overruns the main axis by " +
                      std::to_string(wrapped.content_main - room.main) + " px (" +
                      std::to_string(wrapped.content_main) + " needed, " +
                      std::to_string(room.main) + " available)");
  }

  const PixelSize content = axis.horizontal
                                ? PixelSize{wrapped.content_main, wrapped.content_cross}
                                : PixelSize{wrapped.content_cross, wrapped.content_main};
  const PixelSize size = resolved_size(nodes[index].box, limits, insets, content);
  place_wrap_children(index, size, wrapped, axis);
  return size;
}

PixelSize LayoutTree::Impl::measure_absolute(std::uint32_t index, const SizeLimits& limits,
                                             const BoxConstraints& inner) {
  const EdgeInsets insets = content_insets(nodes[index].box);
  const std::vector<std::uint32_t>& children = nodes[index].children;

  // An absolute container takes all the room it is offered rather than
  // shrinking to its children. design.md section 5.4.2 sizes a Stack from its
  // non-positioned children, which needs a second placement pass; every child
  // here is positioned, so there are no non-positioned children to measure
  // and filling the offer is both the single-pass answer and the useful one -
  // an overlay layer wants to cover what it overlays.
  const PixelSize content{is_bounded(inner.max_width) ? inner.max_width : inner.min_width,
                          is_bounded(inner.max_height) ? inner.max_height : inner.min_height};
  const PixelSize size = resolved_size(nodes[index].box, limits, insets, content);

  const int inner_width = std::max(0, size.width - insets.horizontal());
  const int inner_height = std::max(0, size.height - insets.vertical());

  for (const std::uint32_t child : children) {
    const BoxStyle& child_box = nodes[child].box;
    const EdgeInsets margin = child_box.margin;
    const int room_width = std::max(0, inner_width - margin.horizontal());
    const int room_height = std::max(0, inner_height - margin.vertical());

    const AbsoluteAxis horizontal = absolute_axis(room_width, child_box.left, child_box.right);
    const AbsoluteAxis vertical = absolute_axis(room_height, child_box.top, child_box.bottom);
    const PixelSize child_size =
        layout_node(child, BoxConstraints{horizontal.min_extent, horizontal.max_extent,
                                          vertical.min_extent, vertical.max_extent});

    const int x = horizontal.pin_to_end
                      ? std::max(0, room_width - horizontal.end - child_size.width)
                      : horizontal.position;
    const int y = vertical.pin_to_end
                      ? std::max(0, room_height - vertical.end - child_size.height)
                      : vertical.position;
    nodes[child].local = PixelRect{insets.left + margin.left + x, insets.top + margin.top + y,
                                   child_size.width, child_size.height};
  }

  return size;
}

}  // namespace dg
