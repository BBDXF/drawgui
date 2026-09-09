// The second sizing stage: basis, shrink, main_size and aspect_ratio.
//
// These are the tests that say what the new sizing MEANS. The identity gates
// next door say the incremental path computes the same thing as the full one,
// which is a stronger statement but an empty one when both are wrong in the
// same way - so every rule here is pinned to numbers worked out on paper, in
// the comment above its assertions, where a wrong answer is a wrong answer
// rather than a consistent one.
//
// The first case in the file is the one that matters most. doc/sizing.md
// section 1 claims this slice adds no second measurement of anything, and
// LayoutStats reports nodes entered and nodes recomputed separately, so the
// claim is directly checkable: on a full pass of a tree using all four
// properties at once, both must equal the node count. A design that measured a
// child to learn its base and again at its allotment would report more.

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/layout/box.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/render_tree.h"

#include "layout/layout_impl.h"

namespace {

using dg::BoxStyle;
using dg::CrossAlign;
using dg::LayoutKind;
using dg::LayoutStats;
using dg::LayoutTree;
using dg::MainAlign;
using dg::MainSize;
using dg::NodeId;
using dg::NodeStyle;
using dg::PixelRect;
using dg::PixelSize;

dg::TreeSpec spec_of(int width, int height) {
  dg::TreeSpec spec;
  spec.viewport = PixelSize{width, height};
  return spec;
}

BoxStyle row_of(int width, int height) {
  BoxStyle box;
  box.kind = LayoutKind::kRow;
  box.width = width;
  box.height = height;
  return box;
}

// A child with a declared base and a declared height, which is the shape the
// deficit distribution is defined over.
BoxStyle based(int basis, int shrink) {
  BoxStyle box;
  box.basis = basis;
  box.shrink = shrink;
  box.height = 20;
  return box;
}

std::vector<PixelRect> all_bounds(const LayoutTree& tree) {
  std::vector<PixelRect> bounds;
  bounds.reserve(tree.node_count());
  for (std::uint32_t index = 0; index < tree.node_count(); ++index) {
    bounds.push_back(tree.bounds(NodeId{index}));
  }
  return bounds;
}

std::vector<int> child_widths(const LayoutTree& tree, const std::vector<NodeId>& nodes) {
  std::vector<int> widths;
  widths.reserve(nodes.size());
  for (const NodeId node : nodes) {
    widths.push_back(tree.bounds(node).width);
  }
  return widths;
}

std::vector<int> child_lefts(const LayoutTree& tree, const std::vector<NodeId>& nodes) {
  std::vector<int> lefts;
  lefts.reserve(nodes.size());
  for (const NodeId node : nodes) {
    lefts.push_back(tree.bounds(node).left());
  }
  return lefts;
}

bool any_diagnostic_contains(const LayoutTree& tree, const std::string& fragment) {
  return std::ranges::any_of(tree.diagnostics(), [&fragment](const std::string& line) {
    return line.find(fragment) != std::string::npos;
  });
}

// --------------------------------------------------------------------------
// L3: every node laid out exactly once, with all four properties in play.
// --------------------------------------------------------------------------

TEST_CASE("a tree using the whole second sizing stage is still laid out exactly once") {
  LayoutTree tree{spec_of(400, 240)};

  BoxStyle filling = row_of(360, 60);
  filling.main_size = MainSize::kMax;
  filling.cross_align = CrossAlign::kStretch;
  const NodeId row = tree.add_child(LayoutTree::root(), filling, NodeStyle{});

  // One of each kind of child the sizing pass sorts into: declared-and-
  // shrinking, grown, aspect-derived, and plainly measured. The measured one
  // carries a child of its own so that its subtree is real rather than a leaf
  // the walk could skip.
  tree.add_child(row, based(200, 1), NodeStyle{});
  tree.add_child(row, based(200, 3), NodeStyle{});

  BoxStyle grown;
  grown.grow = 1;
  tree.add_child(row, grown, NodeStyle{});

  BoxStyle aspect;
  aspect.aspect_ratio = 1.5F;
  tree.add_child(row, aspect, NodeStyle{});

  BoxStyle measured;
  const NodeId host = tree.add_child(row, measured, NodeStyle{});
  BoxStyle inner;
  inner.width = 40;
  inner.height = 18;
  tree.add_child(host, inner, NodeStyle{});

  const LayoutStats stats = tree.layout_full();

  // A full pass visits every node once and recomputes every node once. A
  // second measurement of any child - the loose-then-tight shape doc/sizing.md
  // section 1.3 costs out - shows up here as a count larger than the tree.
  CHECK(stats.nodes_total == tree.node_count());
  CHECK(stats.nodes_visited == stats.nodes_total);
  CHECK(stats.nodes_relaid_out == stats.nodes_total);
}

// --------------------------------------------------------------------------
// basis
// --------------------------------------------------------------------------

// Room 300, three bases of 100 and no weights: the bases add up exactly, so
// every child is laid out tight at the number it declared and they sit at
// 0, 100, 200.
TEST_CASE("a declared basis replaces the size a child would have measured") {
  LayoutTree tree{spec_of(400, 200)};
  const NodeId row = tree.add_child(LayoutTree::root(), row_of(300, 40), NodeStyle{});

  std::vector<NodeId> children;
  children.reserve(3);
  for (int slot = 0; slot < 3; ++slot) {
    children.push_back(tree.add_child(row, based(100, 0), NodeStyle{}));
  }
  tree.layout();

  CHECK(child_widths(tree, children) == std::vector<int>{100, 100, 100});
  CHECK(child_lefts(tree, children) == std::vector<int>{0, 100, 200});
}

// A basis is deliberately NOT clamped to the container's room - that is what
// produces the deficit shrink exists to absorb. With no shrink to absorb it,
// the children overrun and the container says so.
TEST_CASE("a basis larger than the room overruns rather than being clamped") {
  LayoutTree tree{spec_of(400, 200)};
  const NodeId row = tree.add_child(LayoutTree::root(), row_of(200, 40), NodeStyle{});
  const NodeId only = tree.add_child(row, based(260, 0), NodeStyle{});
  tree.layout();

  CHECK(tree.bounds(only).width == 260);
  CHECK(any_diagnostic_contains(tree, "children overrun the main axis by 60 px"));
}

// The basis of a GROW child is added to what the free space is measured
// against, so its final extent is base plus share rather than share alone.
//
// Room 300, one child with basis 60 and grow 1, one plain child of width 100:
// used = 60 + 100 = 160, free = 140, the single weight takes all of it, so the
// flexible child ends at 60 + 140 = 200 and the plain one follows at 200.
TEST_CASE("a grow child starts from its basis and takes its share on top") {
  LayoutTree tree{spec_of(400, 200)};
  const NodeId row = tree.add_child(LayoutTree::root(), row_of(300, 40), NodeStyle{});

  BoxStyle flexible;
  flexible.basis = 60;
  flexible.grow = 1;
  flexible.height = 20;
  const NodeId grows = tree.add_child(row, flexible, NodeStyle{});

  BoxStyle plain;
  plain.width = 100;
  plain.height = 20;
  const NodeId fixed = tree.add_child(row, plain, NodeStyle{});
  tree.layout();

  CHECK(tree.bounds(grows).width == 200);
  CHECK(tree.bounds(fixed).left() == 200);
  CHECK(tree.bounds(fixed).width == 100);
}

// A grow child's base is its explicit `basis` and NOTHING ELSE - never its own
// width, which is what CSS's flex-basis: auto would make it. doc/sizing.md
// section 1.8 item 3 records the deviation; this is what makes it observable.
//
// One grow child cannot show it, because the total is conserved either way:
// extent = base + (room - used - base). It takes TWO weights.
//
// Room 300, a rigid child of 100, and two flexible children weighted 1 and 3,
// the first of which also declares a width of 80. Bases are 0, so used = 100
// and free = 200; prefix sums give cumulative 200x1/4 = 50 and 200x4/4 = 200,
// so the shares are 50 and 150.
//
// Had the width counted as a base, free would have been 120 and the extents
// 80+30 = 110 and 0+90 = 90 - a different split of the same total.
TEST_CASE("a grow child's own width is not its base") {
  LayoutTree tree{spec_of(500, 200)};
  const NodeId row = tree.add_child(LayoutTree::root(), row_of(300, 40), NodeStyle{});

  BoxStyle narrow_but_flexible;
  narrow_but_flexible.grow = 1;
  narrow_but_flexible.width = 80;
  narrow_but_flexible.height = 20;
  const NodeId light = tree.add_child(row, narrow_but_flexible, NodeStyle{});

  BoxStyle flexible;
  flexible.grow = 3;
  flexible.height = 20;
  const NodeId heavy = tree.add_child(row, flexible, NodeStyle{});

  BoxStyle rigid;
  rigid.width = 100;
  rigid.height = 20;
  const NodeId fixed = tree.add_child(row, rigid, NodeStyle{});
  tree.layout();

  CHECK(tree.bounds(light).width == 50);
  CHECK(tree.bounds(heavy).width == 150);
  CHECK(tree.bounds(fixed).left() == 200);
}

// --------------------------------------------------------------------------
// shrink
// --------------------------------------------------------------------------

// Room 200, three bases of 100 with equal weights. Deficit 100, weights
// 1x100 = 100 each, total 300. Prefix sums give cumulative shares
// 100*100/300 = 33, 100*200/300 = 66, 100*300/300 = 100, so the per-child
// shares are 33, 33, 34 and the extents are 67, 67, 66.
//
// They add up to exactly 200. The extra pixel lands on the LAST child rather
// than being lost, which is the mirror image of grow's 33/33/34 - and losing
// it is precisely the defect doc/layout.md records against a per-child
// division.
TEST_CASE("a deficit is split by prefix sums so no pixel is invented or lost") {
  LayoutTree tree{spec_of(400, 200)};
  const NodeId row = tree.add_child(LayoutTree::root(), row_of(200, 40), NodeStyle{});

  std::vector<NodeId> children;
  children.reserve(3);
  for (int slot = 0; slot < 3; ++slot) {
    children.push_back(tree.add_child(row, based(100, 1), NodeStyle{}));
  }
  tree.layout();

  CHECK(child_widths(tree, children) == std::vector<int>{67, 67, 66});
  CHECK(child_lefts(tree, children) == std::vector<int>{0, 67, 134});
  CHECK(tree.diagnostics().empty());
}

// The scaled shrink factor, and the reason it is `shrink x base` rather than
// `shrink` alone.
//
// Room 300. Bases 300 and 100, weights 1 and 3, so the products are 300 and
// 300 - equal - and a deficit of 100 splits 50/50: extents 250 and 50.
//
// Weighting by `shrink` alone would have given shares of 25 and 75, taking
// three quarters of the small child and a twelfth of the large one.
TEST_CASE("the deficit is weighted by shrink times base, not by shrink alone") {
  LayoutTree tree{spec_of(500, 200)};
  const NodeId row = tree.add_child(LayoutTree::root(), row_of(300, 40), NodeStyle{});

  const NodeId wide = tree.add_child(row, based(300, 1), NodeStyle{});
  const NodeId narrow = tree.add_child(row, based(100, 3), NodeStyle{});
  tree.layout();

  CHECK(tree.bounds(wide).width == 250);
  CHECK(tree.bounds(narrow).width == 50);
  CHECK(tree.bounds(narrow).left() == 250);
}

// A definite size on the container's main axis is a declared base too, which
// is what makes shrink reachable without anybody writing `basis`.
//
// Room 300, three children of width 160: used 480, deficit 180, weights 160
// each and a total of 480, so each takes 180*160/480 = 60 and ends at 100.
TEST_CASE("a definite main-axis size serves as a base without a basis") {
  LayoutTree tree{spec_of(500, 200)};
  const NodeId row = tree.add_child(LayoutTree::root(), row_of(300, 40), NodeStyle{});

  std::vector<NodeId> children;
  children.reserve(3);
  for (int slot = 0; slot < 3; ++slot) {
    BoxStyle button;
    button.width = 160;
    button.height = 20;
    button.shrink = 1;
    children.push_back(tree.add_child(row, button, NodeStyle{}));
  }
  tree.layout();

  CHECK(child_widths(tree, children) == std::vector<int>{100, 100, 100});
  CHECK(child_lefts(tree, children) == std::vector<int>{0, 100, 200});
}

// A child that reaches its declared minimum stops absorbing, and the residual
// keeps the container overrunning rather than being silently redistributed.
//
// Room 150, two bases of 100 with equal weights. Deficit 50, shares 25 each.
// The first child floors at min_width 80 instead of taking 75, so it gives up
// 20 rather than 25; the second ends at 75. Total 155, five past the room, and
// the overrun diagnostic reports exactly that five.
TEST_CASE("a child that hits its minimum stops absorbing and the residual is reported") {
  LayoutTree tree{spec_of(400, 200)};
  const NodeId row = tree.add_child(LayoutTree::root(), row_of(150, 40), NodeStyle{});

  BoxStyle floored;
  floored.width = 100;
  floored.height = 20;
  floored.shrink = 1;
  floored.min_width = 80;
  const NodeId stops = tree.add_child(row, floored, NodeStyle{});

  BoxStyle free_to_shrink;
  free_to_shrink.width = 100;
  free_to_shrink.height = 20;
  free_to_shrink.shrink = 1;
  const NodeId keeps_going = tree.add_child(row, free_to_shrink, NodeStyle{});
  tree.layout();

  CHECK(tree.bounds(stops).width == 80);
  CHECK(tree.bounds(keeps_going).width == 75);
  CHECK(any_diagnostic_contains(tree, "children overrun the main axis by 5 px"));
}

// The refusal doc/sizing.md section 1.6 is built around, and the reason the
// whole slice keeps L3.
//
// The first child has no declared base at all - it shrink-wraps a 150-wide
// grandchild - so shrinking it would mean measuring it a second time. It is
// left at its natural size and named. The whole deficit therefore lands on the
// sibling that CAN take it: room 200, bases 150 (measured, excluded) and 150
// (declared), deficit 100, so the declared child ends at 50 and the row fits
// exactly.
TEST_CASE("shrink is refused for a measured base, by name, and the sibling absorbs it all") {
  LayoutTree tree{spec_of(400, 200)};
  const NodeId row = tree.add_child(LayoutTree::root(), row_of(200, 40), NodeStyle{});

  BoxStyle wrapper;
  wrapper.shrink = 1;
  const NodeId measured = tree.add_child(row, wrapper, NodeStyle{});
  BoxStyle inner;
  inner.width = 150;
  inner.height = 20;
  tree.add_child(measured, inner, NodeStyle{});

  BoxStyle declared;
  declared.width = 150;
  declared.height = 20;
  declared.shrink = 1;
  const NodeId shrinks = tree.add_child(row, declared, NodeStyle{});
  tree.layout();

  CHECK(tree.bounds(measured).width == 150);
  CHECK(tree.bounds(shrinks).width == 50);
  CHECK(any_diagnostic_contains(tree, "shrink was not applied"));

  // Named against the CHILD it refused, not against the container, because the
  // fix - give this node a basis - is the child's.
  CHECK(any_diagnostic_contains(tree, tree.path_of(measured)));
}

// The one piece of arithmetic in the distribution that is not exact, driven
// past the point where it engages.
//
// A weight is shrink x base and both are bounded at 2^24, so two children at
// the maximum give a total of 2^49 - and pool * units would then overflow 64
// bits. The distribution divides every weight by one common divisor first.
// With both weights equal the reduction is symmetric, so the 33554332-pixel
// deficit still splits exactly in half and the two children end at 50 each,
// filling the 100-pixel room precisely.
//
// Run under the sanitized build this is also the case that would report a
// signed overflow if the reduction were removed.
TEST_CASE("a weight total past 31 bits is reduced and still splits the deficit exactly") {
  LayoutTree tree{spec_of(400, 200)};
  const NodeId row = tree.add_child(LayoutTree::root(), row_of(100, 40), NodeStyle{});

  constexpr int kMaxLength = 1 << 24;
  std::vector<NodeId> children;
  children.reserve(2);
  for (int slot = 0; slot < 2; ++slot) {
    BoxStyle huge = based(kMaxLength, kMaxLength);
    children.push_back(tree.add_child(row, huge, NodeStyle{}));
  }
  tree.layout();

  CHECK(child_widths(tree, children) == std::vector<int>{50, 50});
  CHECK(child_lefts(tree, children) == std::vector<int>{0, 50});
}

// --------------------------------------------------------------------------
// Order independence
// --------------------------------------------------------------------------

// doc/sizing.md section 1.7 claims the result does not depend on the order the
// children are measured in. This is the case that would expose it if it did:
// the same two children, added the other way round, must produce mirrored
// extents. A distribution leaking state between children - a running remainder
// carried the wrong way, a base read after a sibling had been rewritten - does
// not survive it.
TEST_CASE("reversing the children mirrors the deficit split rather than changing it") {
  LayoutTree forward{spec_of(500, 200)};
  const NodeId forward_row =
      forward.add_child(LayoutTree::root(), row_of(300, 40), NodeStyle{});
  const NodeId forward_wide = forward.add_child(forward_row, based(300, 1), NodeStyle{});
  const NodeId forward_narrow = forward.add_child(forward_row, based(100, 3), NodeStyle{});
  forward.layout();

  LayoutTree reversed{spec_of(500, 200)};
  const NodeId reversed_row =
      reversed.add_child(LayoutTree::root(), row_of(300, 40), NodeStyle{});
  const NodeId reversed_narrow = reversed.add_child(reversed_row, based(100, 3), NodeStyle{});
  const NodeId reversed_wide = reversed.add_child(reversed_row, based(300, 1), NodeStyle{});
  reversed.layout();

  CHECK(forward.bounds(forward_wide).width == reversed.bounds(reversed_wide).width);
  CHECK(forward.bounds(forward_narrow).width == reversed.bounds(reversed_narrow).width);
  CHECK(reversed.bounds(reversed_narrow).left() == 0);
  CHECK(reversed.bounds(reversed_wide).left() == 50);
}

// Where the remainder lands IS positional, and that is deliberate rather than
// an accident of the loop: with equal weights the sequence is 67/67/66 read
// forwards and 67/67/66 read backwards, because the extra pixel belongs to the
// last SLOT and not to any particular child.
TEST_CASE("the deficit remainder belongs to a slot, not to a child") {
  const auto widths_with = [](int first, int second, int third) {
    LayoutTree tree{spec_of(400, 200)};
    const NodeId row = tree.add_child(LayoutTree::root(), row_of(200, 40), NodeStyle{});
    std::vector<NodeId> children;
    children.reserve(3);
    for (const int weight : {first, second, third}) {
      children.push_back(tree.add_child(row, based(100, weight), NodeStyle{}));
    }
    tree.layout();
    return child_widths(tree, children);
  };

  CHECK(widths_with(1, 1, 1) == std::vector<int>{67, 67, 66});
  CHECK(widths_with(2, 2, 2) == std::vector<int>{67, 67, 66});
}

// --------------------------------------------------------------------------
// main_size
// --------------------------------------------------------------------------

// A shrink-wrapping row has no leftover space, so `justify` has nothing to
// place; filling the main axis is what gives it some. Room 400, two children
// of 50: with kMin the row is 100 wide and the children sit at 0 and 50, with
// kMax the row is 400 wide and justify=end puts them at 300 and 350.
TEST_CASE("main_size max fills the main axis and gives justify something to place") {
  const auto lefts_with = [](MainSize mode) {
    LayoutTree tree{spec_of(400, 200)};
    BoxStyle row;
    row.kind = LayoutKind::kRow;
    row.height = 40;
    row.main_align = MainAlign::kEnd;
    row.main_size = mode;
    const NodeId node = tree.add_child(LayoutTree::root(), row, NodeStyle{});

    std::vector<NodeId> children;
    children.reserve(2);
    for (int slot = 0; slot < 2; ++slot) {
      BoxStyle child;
      child.width = 50;
      child.height = 20;
      children.push_back(tree.add_child(node, child, NodeStyle{}));
    }
    tree.layout();
    return std::pair{tree.bounds(node).width, child_lefts(tree, children)};
  };

  const auto hugging = lefts_with(MainSize::kMin);
  CHECK(hugging.first == 100);
  CHECK(hugging.second == std::vector<int>{0, 50});

  const auto filling = lefts_with(MainSize::kMax);
  CHECK(filling.first == 400);
  CHECK(filling.second == std::vector<int>{300, 350});
}

// main_size is resolved in limits_for(), which is also what layout_node()
// reads to decide whether a node is a relayout boundary - so a filled row
// whose cross axis is settled stops a dirty mark that a shrink-wrapping one
// would have let through to the root.
TEST_CASE("a filled container becomes a relayout boundary without declaring one") {
  const auto dirty_roots_with = [](MainSize mode) {
    LayoutTree tree{spec_of(400, 200)};
    BoxStyle row;
    row.kind = LayoutKind::kRow;
    row.height = 40;
    row.main_size = mode;
    const NodeId node = tree.add_child(LayoutTree::root(), row, NodeStyle{});

    BoxStyle child;
    child.width = 50;
    child.height = 20;
    const NodeId leaf = tree.add_child(node, child, NodeStyle{});
    tree.layout();

    BoxStyle moved = tree.box(leaf);
    moved.width = 70;
    tree.set_box(leaf, moved);
    return tree.layout().nodes_relaid_out;
  };

  // Hugging, the row's own width follows its child, so the mark climbs past
  // the row to the root and all three nodes are recomputed. Filled, the row's
  // width cannot move whatever its child does, so the mark stops at the row
  // and the root is never entered.
  CHECK(dirty_roots_with(MainSize::kMin) == 3);
  CHECK(dirty_roots_with(MainSize::kMax) == 2);
}

// --------------------------------------------------------------------------
// aspect_ratio
// --------------------------------------------------------------------------

// The arithmetic on its own, reachable because src/ is on the unit test's
// include path. Slice 4-4 learned this from fit_radii(), whose only two
// consumers degraded a bad radius identically and therefore could not
// disagree with it.
TEST_CASE("the aspect partner divides for a settled width and multiplies for a height") {
  CHECK(dg::aspect_partner(200, 2.0F, true) == 100);
  CHECK(dg::aspect_partner(100, 2.0F, false) == 200);
  CHECK(dg::aspect_partner(100, 0.5F, true) == 200);
  CHECK(dg::aspect_partner(100, 0.5F, false) == 50);

  // Rounds to nearest rather than truncating: 100 at 3:1 is 33.33, which is 33
  // either way, but 100 at 3:2 is 66.67 and truncation would say 66.
  CHECK(dg::aspect_partner(100, 3.0F, true) == 33);
  CHECK(dg::aspect_partner(100, 1.5F, true) == 67);

  // Bounded on both sides. A ratio the property boundary refuses can still
  // arrive through the struct API, and layout adds extents together.
  CHECK(dg::aspect_partner(1 << 20, 1.0e6F, false) == (1 << 24));
  CHECK(dg::aspect_partner(0, 2.0F, true) == 0);
}

// The plain direction: a definite width, and the height follows.
TEST_CASE("a settled width derives the height") {
  LayoutTree tree{spec_of(400, 200)};
  BoxStyle box;
  box.width = 200;
  box.aspect_ratio = 2.0F;
  const NodeId node = tree.add_child(LayoutTree::root(), box, NodeStyle{});
  tree.layout();

  CHECK(tree.bounds(node) == PixelRect{0, 0, 200, 100});
  CHECK(tree.diagnostics().empty());
}

// The sharp direction, and the one doc/sizing.md section 4.1 is written
// around: the settled axis is the CROSS one, handed down by a stretching row,
// and the ratio derives the MAIN one from it. Row height 40, ratio 1.5, so the
// child is 60 wide - and the row's own content width becomes 60 with it.
TEST_CASE("a stretched child derives its main axis from the cross axis it was given") {
  LayoutTree tree{spec_of(400, 200)};
  BoxStyle row;
  row.kind = LayoutKind::kRow;
  row.width = 300;
  row.height = 40;
  row.cross_align = CrossAlign::kStretch;
  const NodeId node = tree.add_child(LayoutTree::root(), row, NodeStyle{});

  BoxStyle aspect;
  aspect.aspect_ratio = 1.5F;
  const NodeId child = tree.add_child(node, aspect, NodeStyle{});
  tree.layout();

  CHECK(tree.bounds(child) == PixelRect{0, 0, 60, 40});
  CHECK(tree.diagnostics().empty());
}

// Both axes fixed by the parent: the constraints win, because the parent has
// already reserved that space, and the disagreement is named rather than
// absorbed. A ratio that AGREES stays silent, so a stretched square asking for
// 1:1 does not produce a message every frame.
TEST_CASE("an aspect ratio that fights fixed constraints is reported, one that agrees is not") {
  const auto diagnostics_for = [](float ratio) {
    LayoutTree tree{spec_of(400, 200)};
    BoxStyle box;
    box.width = 120;
    box.height = 100;
    box.aspect_ratio = ratio;
    const NodeId node = tree.add_child(LayoutTree::root(), box, NodeStyle{});
    tree.layout();
    return std::pair{tree.bounds(node), tree.diagnostics().size()};
  };

  const auto fighting = diagnostics_for(1.6F);
  CHECK(fighting.first == PixelRect{0, 0, 120, 100});
  CHECK(fighting.second == 1U);

  const auto agreeing = diagnostics_for(1.2F);
  CHECK(agreeing.first == PixelRect{0, 0, 120, 100});
  CHECK(agreeing.second == 0U);
}

// Neither axis settled: the content decides the box and the ratio then GROWS
// the deficient side of it. A 100x20 child inside a shrink-wrapping node gives
// a 100x20 box; at 1:1 the height grows to 100, at 10:1 the width grows to
// 200. Never the other way round - shrinking would push the child it was laid
// out around outside the rectangle its parent declares.
TEST_CASE("with neither axis settled the ratio grows the finished box") {
  const auto size_for = [](float ratio) {
    LayoutTree tree{spec_of(400, 300)};
    BoxStyle box;
    box.aspect_ratio = ratio;
    const NodeId node = tree.add_child(LayoutTree::root(), box, NodeStyle{});

    BoxStyle inner;
    inner.width = 100;
    inner.height = 20;
    tree.add_child(node, inner, NodeStyle{});
    tree.layout();
    return tree.bounds(node);
  };

  CHECK(size_for(1.0F) == PixelRect{0, 0, 100, 100});
  CHECK(size_for(10.0F) == PixelRect{0, 0, 200, 20});
}

// The two resolutions are ORDERED, and the order is load-bearing: filling the
// main axis SETTLES it, and a settled axis is what the ratio derives the other
// one from.
//
// A row that fills a 400-wide page and asks for 2:1 is 400x200. Resolve the
// ratio first and it finds neither axis settled, declines, and leaves the row
// to shrink-wrap its (absent) content - 400x0, a row that has vanished.
TEST_CASE("main_size settles the main axis before the ratio derives from it") {
  LayoutTree tree{spec_of(400, 300)};
  BoxStyle row;
  row.kind = LayoutKind::kRow;
  row.main_size = MainSize::kMax;
  row.aspect_ratio = 2.0F;
  const NodeId node = tree.add_child(LayoutTree::root(), row, NodeStyle{});
  tree.layout();

  CHECK(tree.bounds(node) == PixelRect{0, 0, 400, 200});
  CHECK(tree.diagnostics().empty());
}

TEST_CASE("growing to an aspect never returns a smaller box than it was given") {
  for (const float ratio : {0.25F, 0.5F, 1.0F, 1.5F, 4.0F}) {
    for (const PixelSize size : {PixelSize{100, 20}, PixelSize{20, 100}, PixelSize{60, 60},
                                 PixelSize{0, 0}, PixelSize{7, 3}}) {
      const PixelSize grown = dg::grow_to_aspect(size, ratio);
      CHECK(grown.width >= size.width);
      CHECK(grown.height >= size.height);
    }
  }
}

// --------------------------------------------------------------------------
// Incremental equals full, on the four transitions the new sizing introduces.
// --------------------------------------------------------------------------

// Two children with declared bases inside a row, plus a wrapping band holding
// a shrinking row of its own. The nested case is the one that composes this
// slice's arithmetic with slice 4-3's loose-constraint arrangement, where a
// child's size is decided against the room the band offers rather than against
// the run it lands in.
struct SizingScene {
  NodeId row;
  NodeId first;
  NodeId second;
  NodeId aspect;
};

SizingScene build_sizing_scene(LayoutTree& tree) {
  BoxStyle column;
  column.kind = LayoutKind::kColumn;
  column.cross_align = CrossAlign::kStretch;
  const NodeId body = tree.add_child(LayoutTree::root(), column, NodeStyle{});

  BoxStyle toolbar;
  toolbar.kind = LayoutKind::kRow;
  toolbar.height = 48;
  toolbar.gap = 6;
  toolbar.cross_align = CrossAlign::kStretch;
  const NodeId row = tree.add_child(body, toolbar, NodeStyle{});

  const NodeId first = tree.add_child(row, based(140, 1), NodeStyle{});
  const NodeId second = tree.add_child(row, based(200, 2), NodeStyle{});

  BoxStyle aspect;
  aspect.aspect_ratio = 1.5F;
  const NodeId derived = tree.add_child(row, aspect, NodeStyle{});

  BoxStyle band;
  band.kind = LayoutKind::kWrapRow;
  band.gap = 8;
  band.run_gap = 6;
  band.height = 120;
  const NodeId wrapped = tree.add_child(body, band, NodeStyle{});

  for (int slot = 0; slot < 4; ++slot) {
    BoxStyle chip;
    chip.kind = LayoutKind::kRow;
    chip.width = 120;
    chip.height = 26;
    const NodeId chip_row = tree.add_child(wrapped, chip, NodeStyle{});
    tree.add_child(chip_row, based(80, 1), NodeStyle{});
    tree.add_child(chip_row, based(70, 1), NodeStyle{});
  }
  return SizingScene{row, first, second, derived};
}

void expect_identical_after(void (*mutate)(LayoutTree&, const SizingScene&), PixelSize viewport,
                            PixelSize resized) {
  LayoutTree incremental{spec_of(viewport.width, viewport.height)};
  const SizingScene scene = build_sizing_scene(incremental);
  incremental.layout();
  if (resized.width > 0) {
    incremental.resize(resized);
  }
  mutate(incremental, scene);
  incremental.layout();

  LayoutTree full{spec_of(viewport.width, viewport.height)};
  const SizingScene same = build_sizing_scene(full);
  if (resized.width > 0) {
    full.resize(resized);
  }
  mutate(full, same);
  full.layout_full();

  CHECK(all_bounds(incremental) == all_bounds(full));
}

void mutate_nothing(LayoutTree& /*tree*/, const SizingScene& /*scene*/) {}

void mutate_flip_to_shrink(LayoutTree& tree, const SizingScene& scene) {
  BoxStyle box = tree.box(scene.first);
  box.grow = 0;
  box.shrink = 4;
  tree.set_box(scene.first, box);
}

void mutate_flip_to_grow(LayoutTree& tree, const SizingScene& scene) {
  BoxStyle box = tree.box(scene.second);
  box.shrink = 0;
  box.grow = 1;
  tree.set_box(scene.second, box);
}

void mutate_basis(LayoutTree& tree, const SizingScene& scene) {
  BoxStyle box = tree.box(scene.second);
  box.basis = 420;
  tree.set_box(scene.second, box);
}

void mutate_row_height(LayoutTree& tree, const SizingScene& scene) {
  BoxStyle box = tree.box(scene.row);
  box.height = 96;
  tree.set_box(scene.row, box);
}

// A change to the amount of free space, without touching a single node's own
// box: the viewport narrows until the toolbar's declared bases no longer fit.
TEST_CASE("incremental equals full when surplus space turns into a deficit") {
  expect_identical_after(&mutate_nothing, PixelSize{760, 400}, PixelSize{380, 400});
  expect_identical_after(&mutate_nothing, PixelSize{380, 400}, PixelSize{760, 400});
}

// A container flipping from growing to shrinking, on an unchanged tree: which
// distribution runs changes, and it changes through a parentData field, which
// is the delivery that differs_in_parent_data() had to be taught about.
TEST_CASE("incremental equals full when a child flips between growing and shrinking") {
  expect_identical_after(&mutate_flip_to_shrink, PixelSize{380, 400}, PixelSize{});
  expect_identical_after(&mutate_flip_to_grow, PixelSize{760, 400}, PixelSize{});
  expect_identical_after(&mutate_flip_to_shrink, PixelSize{760, 400}, PixelSize{380, 400});
}

// A basis change is parentData too, and the child it lands on is tight on the
// main axis BY CONSTRUCTION - laid out at exactly its declared base - so it is
// the shape that absorbs its own change unless the parent is marked as well.
TEST_CASE("incremental equals full when a basis moves") {
  expect_identical_after(&mutate_basis, PixelSize{760, 400}, PixelSize{});
  expect_identical_after(&mutate_basis, PixelSize{380, 400}, PixelSize{});
}

// The aspect child's CROSS axis changes, because the row it stretches inside
// gets taller. Its main axis has to follow through the cache.
TEST_CASE("incremental equals full when an aspect child's cross axis changes") {
  expect_identical_after(&mutate_row_height, PixelSize{760, 400}, PixelSize{});
  expect_identical_after(&mutate_row_height, PixelSize{380, 400}, PixelSize{});
}

// Every chip in the wrapping band is a shrinking row of its own, so a resize
// that re-breaks the band also re-runs the deficit distribution inside the
// chips that moved. The ladder covers enough widths to produce more than one
// run count.
TEST_CASE("incremental equals full when a shrinking row rides inside a wrapped run") {
  for (const int width : {760, 520, 400, 300, 250, 900}) {
    expect_identical_after(&mutate_nothing, PixelSize{760, 400}, PixelSize{width, 400});
  }
}

// --------------------------------------------------------------------------
// The wrapping container refuses both, and says so.
// --------------------------------------------------------------------------

// consumed_by = ["flex"] for basis and shrink, exactly as for grow: which run
// a child lands in is not known until the run closes, so there is no single
// main axis to hand out shares of. The property boundary refuses it; the
// struct API cannot, so layout reports it.
TEST_CASE("a wrapping container reports basis and shrink rather than resolving them") {
  LayoutTree tree{spec_of(400, 200)};
  BoxStyle band;
  band.kind = LayoutKind::kWrapRow;
  band.width = 200;
  band.height = 100;
  const NodeId wrapped = tree.add_child(LayoutTree::root(), band, NodeStyle{});

  tree.add_child(wrapped, based(140, 1), NodeStyle{});
  tree.add_child(wrapped, based(140, 1), NodeStyle{});
  tree.layout();

  CHECK(any_diagnostic_contains(tree, "basis and shrink are not resolved by a wrapping"));
}

}  // namespace
