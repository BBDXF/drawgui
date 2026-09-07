// The four arrangements. Each lays every child out exactly once, so a pass
// over the tree is O(n) and there is no speculative measurement anywhere in
// this file - see the note about intrinsic sizing in layout_tree.h.

#include <algorithm>
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

int prefix_share(int pool, int weight_so_far, int weight_total, int already_given) {
  if (weight_total <= 0) {
    return 0;
  }
  return share_upto(pool, weight_so_far, weight_total) - already_given;
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
  return limits;
}

PixelSize LayoutTree::Impl::measure(std::uint32_t index, const BoxConstraints& constraints) {
  const BoxStyle& box = nodes[index].box;
  const SizeLimits limits = limits_for(box, constraints);
  const EdgeInsets insets = content_insets(box);

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
  return border_box_size(limits, insets, PixelSize{});
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
  return border_box_size(limits, insets, content);
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

int LayoutTree::Impl::size_flex_children(std::uint32_t index, const FlexRoom& room,
                                         const Axis& axis) {
  const std::vector<std::uint32_t>& children = nodes[index].children;
  const int gap = nodes[index].box.gap;
  const int gaps = children.empty() ? 0 : gap * static_cast<int>(children.size() - 1);

  int total_grow = 0;
  for (const std::uint32_t child : children) {
    total_grow += std::max(0, nodes[child].box.grow);
  }
  const bool flexible = total_grow > 0;

  const auto cross_constraint = [&](const EdgeInsets& margin) {
    return shrink_bound(room.cross, axis.cross_total(margin));
  };

  // Per child rather than per container, which is what align_self buys: a
  // child that opted out of a stretching row must not receive the tight cross
  // constraint, and a child that opted IN inside a non-stretching one must.
  // With no align_self anywhere this is exactly the container's own answer.
  const auto stretches = [&](std::uint32_t child) {
    return is_bounded(room.cross) && cross_align_of(index, child) == CrossAlign::kStretch;
  };

  int used = gaps;
  for (const std::uint32_t child : children) {
    if (flexible && nodes[child].box.grow > 0) {
      continue;
    }
    const EdgeInsets margin = nodes[child].box.margin;
    const int cross = cross_constraint(margin);
    const PixelSize size = layout_node(
        child, axis.constraints_of(0, shrink_bound(room.main, axis.main_total(margin)),
                                   stretches(child) ? cross : 0, cross));
    used += axis.main_of(size) + axis.main_total(margin);
  }
  if (!flexible) {
    return used;
  }

  const int free_space = std::max(0, room.main - used);
  int weight_so_far = 0;
  int given = 0;
  for (const std::uint32_t child : children) {
    const int weight = nodes[child].box.grow;
    if (weight <= 0) {
      continue;
    }
    weight_so_far += weight;
    const int share = prefix_share(free_space, weight_so_far, total_grow, given);
    given += share;

    const EdgeInsets margin = nodes[child].box.margin;
    const int main_extent = std::max(0, share - axis.main_total(margin));
    const int cross = cross_constraint(margin);
    const PixelSize size = layout_node(
        child,
        axis.constraints_of(main_extent, main_extent, stretches(child) ? cross : 0, cross));
    used += axis.main_of(size) + axis.main_total(margin);
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
  const PixelSize size = border_box_size(limits, insets, content);
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

  Run run;
  for (std::size_t slot = 0; slot < children.size(); ++slot) {
    const std::uint32_t child = children[slot];
    const EdgeInsets margin = nodes[child].box.margin;
    any_grow = any_grow || nodes[child].box.grow > 0;
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
  const PixelSize size = border_box_size(limits, insets, content);
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
  const PixelSize size = border_box_size(limits, insets, content);

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
