// The box model and the four arrangements, checked one property at a time.
//
// These are the tests that say what the layout MEANS. The identity test next
// door says that the incremental path computes the same thing as the full
// one, which is a much stronger statement but an empty one if both are wrong
// in the same way - so the arrangements are pinned to concrete numbers here,
// where a wrong answer is a wrong answer rather than a consistent one.

#include <cstdint>
#include <optional>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/layout/box.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/render_tree.h"

namespace {

using dg::BoxConstraints;
using dg::BoxStyle;
using dg::CrossAlign;
using dg::EdgeInsets;
using dg::LayoutKind;
using dg::LayoutStats;
using dg::LayoutTree;
using dg::MainAlign;
using dg::NodeId;
using dg::NodeStyle;
using dg::PixelRect;
using dg::PixelSize;

dg::TreeSpec spec_of(int width, int height) {
  dg::TreeSpec spec;
  spec.viewport = PixelSize{width, height};
  return spec;
}

BoxStyle stack_of(LayoutKind kind) {
  BoxStyle box;
  box.kind = kind;
  return box;
}

BoxStyle sized(int width, int height) {
  BoxStyle box;
  box.width = width;
  box.height = height;
  return box;
}

BoxStyle grown(int weight) {
  BoxStyle box;
  box.grow = weight;
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

TEST_CASE("a bound shrinks by subtraction but infinity stays infinity") {
  CHECK(dg::shrink_bound(100, 30) == 70);
  CHECK(dg::shrink_bound(20, 30) == 0);
  CHECK(dg::shrink_bound(dg::kUnbounded, 30) == dg::kUnbounded);

  // The whole reason shrink_bound exists. kUnbounded is INT_MAX, so plain
  // subtraction turns "as large as you like" into a very specific large
  // number and every later comparison against kUnbounded stops matching.
  CHECK(dg::is_bounded(dg::shrink_bound(dg::kUnbounded, 1)) == false);
}

TEST_CASE("constraints clamp, loosen and deflate") {
  const BoxConstraints constraints{40, 120, 10, 60};

  CHECK(constraints.constrain_width(200) == 120);
  CHECK(constraints.constrain_width(5) == 40);
  CHECK(constraints.constrain(PixelSize{200, 5}) == PixelSize{120, 10});

  CHECK(constraints.loosened() == BoxConstraints{0, 120, 0, 60});

  // The minimum shrinks with the maximum. A node promised at least 40 whose
  // padding is 8 a side must hand its child at least 24, or the child
  // underfills a box the parent has already committed to.
  CHECK(constraints.deflate(EdgeInsets::all(8)) == BoxConstraints{24, 104, 0, 44});

  CHECK(BoxConstraints::tight(PixelSize{30, 40}).is_tight());
  CHECK_FALSE(BoxConstraints::loose(PixelSize{30, 40}).is_tight());

  // An unbounded axis is never tight, however large its minimum.
  CHECK_FALSE(BoxConstraints{30, dg::kUnbounded, 0, 0}.is_tight_width());
}

TEST_CASE("width is border-box: it includes border and padding, not margin") {
  LayoutTree tree{spec_of(400, 300)};
  tree.set_box(LayoutTree::root(), stack_of(LayoutKind::kColumn));

  BoxStyle box = sized(200, 100);
  box.kind = LayoutKind::kColumn;
  box.cross_align = CrossAlign::kStretch;
  box.border = EdgeInsets::all(2);
  box.padding = EdgeInsets::all(10);
  box.margin = EdgeInsets{20, 5, 0, 0};
  const NodeId outer = tree.add_child(LayoutTree::root(), box, NodeStyle{});

  BoxStyle inner = stack_of(LayoutKind::kColumn);
  inner.cross_align = CrossAlign::kStretch;
  inner.grow = 1;
  const NodeId filler = tree.add_child(outer, inner, NodeStyle{});
  tree.layout_full();

  // 200x100 exactly, and the margin moved it rather than shrinking it.
  CHECK(tree.bounds(outer) == PixelRect{20, 5, 200, 100});

  // Content box is inside 2 of border plus 10 of padding on every side.
  CHECK(tree.content_bounds(outer) == PixelRect{32, 17, 176, 76});
  CHECK(tree.bounds(filler) == PixelRect{32, 17, 176, 76});
}

TEST_CASE("a row hands out leftover space by weight, losing no pixel") {
  LayoutTree tree{spec_of(400, 300)};
  tree.set_box(LayoutTree::root(), stack_of(LayoutKind::kColumn));

  BoxStyle row = stack_of(LayoutKind::kRow);
  row.width = 100;
  row.height = 20;
  const NodeId host = tree.add_child(LayoutTree::root(), row, NodeStyle{});

  std::vector<NodeId> children;
  children.reserve(3);
  for (int i = 0; i < 3; ++i) {
    children.push_back(tree.add_child(host, grown(1), NodeStyle{}));
  }
  tree.layout_full();

  // 100 across three equal weights. Truncation accumulates in the running
  // total and is released to the later children, so the answer is 33/33/34
  // and the three add up to exactly 100 rather than to 99.
  CHECK(tree.bounds(children[0]) == PixelRect{0, 0, 33, 0});
  CHECK(tree.bounds(children[1]) == PixelRect{33, 0, 33, 0});
  CHECK(tree.bounds(children[2]) == PixelRect{66, 0, 34, 0});
}

TEST_CASE("gap and margin stack rather than collapsing") {
  LayoutTree tree{spec_of(400, 300)};
  tree.set_box(LayoutTree::root(), stack_of(LayoutKind::kColumn));

  BoxStyle row = stack_of(LayoutKind::kRow);
  row.width = 300;
  row.height = 30;
  row.gap = 10;
  const NodeId host = tree.add_child(LayoutTree::root(), row, NodeStyle{});

  BoxStyle first = sized(40, 10);
  first.margin.right = 5;
  BoxStyle second = sized(40, 10);
  second.margin.left = 7;
  const NodeId a = tree.add_child(host, first, NodeStyle{});
  const NodeId b = tree.add_child(host, second, NodeStyle{});
  tree.layout_full();

  CHECK(tree.bounds(a) == PixelRect{0, 0, 40, 10});

  // design.md section 5.9.4: the real distance is gap + left margin + right
  // margin, not the largest of the three.
  CHECK(tree.bounds(b) == PixelRect{40 + 5 + 10 + 7, 0, 40, 10});
}

TEST_CASE("cross alignment places, stretch constrains") {
  LayoutTree tree{spec_of(400, 300)};
  tree.set_box(LayoutTree::root(), stack_of(LayoutKind::kColumn));

  BoxStyle row = stack_of(LayoutKind::kRow);
  row.width = 200;
  row.height = 100;
  const NodeId host = tree.add_child(LayoutTree::root(), row, NodeStyle{});
  const NodeId child = tree.add_child(host, sized(20, 30), NodeStyle{});

  SUBCASE("start") {
    tree.layout_full();
    CHECK(tree.bounds(child).y == 0);
  }
  SUBCASE("center") {
    row.cross_align = CrossAlign::kCenter;
    tree.set_box(host, row);
    tree.layout_full();
    CHECK(tree.bounds(child) == PixelRect{0, 35, 20, 30});
  }
  SUBCASE("end") {
    row.cross_align = CrossAlign::kEnd;
    tree.set_box(host, row);
    tree.layout_full();
    CHECK(tree.bounds(child) == PixelRect{0, 70, 20, 30});
  }
  SUBCASE("stretch overrides the child's own height") {
    row.cross_align = CrossAlign::kStretch;
    tree.set_box(host, row);
    tree.layout_full();
    CHECK(tree.bounds(child) == PixelRect{0, 0, 20, 100});
  }
}

TEST_CASE("main alignment distributes what is left over") {
  LayoutTree tree{spec_of(400, 300)};
  tree.set_box(LayoutTree::root(), stack_of(LayoutKind::kColumn));

  BoxStyle row = stack_of(LayoutKind::kRow);
  row.width = 100;
  row.height = 20;
  const NodeId host = tree.add_child(LayoutTree::root(), row, NodeStyle{});
  const NodeId a = tree.add_child(host, sized(20, 10), NodeStyle{});
  const NodeId b = tree.add_child(host, sized(20, 10), NodeStyle{});

  SUBCASE("center") {
    row.main_align = MainAlign::kCenter;
    tree.set_box(host, row);
    tree.layout_full();
    CHECK(tree.bounds(a).x == 30);
    CHECK(tree.bounds(b).x == 50);
  }
  SUBCASE("end") {
    row.main_align = MainAlign::kEnd;
    tree.set_box(host, row);
    tree.layout_full();
    CHECK(tree.bounds(a).x == 60);
    CHECK(tree.bounds(b).x == 80);
  }
  SUBCASE("space-between pushes the pair to the two ends") {
    row.main_align = MainAlign::kSpaceBetween;
    tree.set_box(host, row);
    tree.layout_full();
    CHECK(tree.bounds(a).x == 0);
    CHECK(tree.bounds(b).x == 80);
  }
}

TEST_CASE("absolute positioning covers design.md section 5.4.2's table") {
  LayoutTree tree{spec_of(400, 300)};
  tree.set_box(LayoutTree::root(), stack_of(LayoutKind::kColumn));

  BoxStyle layer = stack_of(LayoutKind::kAbsolute);
  layer.width = 200;
  layer.height = 100;
  const NodeId host = tree.add_child(LayoutTree::root(), layer, NodeStyle{});

  BoxStyle near_corner = sized(30, 12);
  near_corner.left = 5;
  near_corner.top = 7;

  BoxStyle far_corner = sized(30, 12);
  far_corner.right = 5;
  far_corner.bottom = 7;

  BoxStyle spanning = sized(0, 6);
  spanning.width.reset();
  spanning.left = 10;
  spanning.right = 20;
  spanning.top = 40;

  BoxStyle unanchored = sized(25, 8);

  const NodeId a = tree.add_child(host, near_corner, NodeStyle{});
  const NodeId b = tree.add_child(host, far_corner, NodeStyle{});
  const NodeId c = tree.add_child(host, spanning, NodeStyle{});
  const NodeId d = tree.add_child(host, unanchored, NodeStyle{});
  tree.layout_full();

  CHECK(tree.bounds(a) == PixelRect{5, 7, 30, 12});
  CHECK(tree.bounds(b) == PixelRect{200 - 5 - 30, 100 - 7 - 12, 30, 12});

  // Pinned by both horizontal edges: the child is stretched between them and
  // its own width would not have been consulted even if it had one.
  CHECK(tree.bounds(c) == PixelRect{10, 40, 200 - 10 - 20, 6});
  CHECK(tree.bounds(d) == PixelRect{0, 0, 25, 8});
}

TEST_CASE("an absolute layer fills the room it is offered") {
  LayoutTree tree{spec_of(400, 300)};
  BoxStyle root_box = stack_of(LayoutKind::kColumn);
  root_box.cross_align = CrossAlign::kStretch;
  tree.set_box(LayoutTree::root(), root_box);

  BoxStyle layer = stack_of(LayoutKind::kAbsolute);
  layer.grow = 1;
  const NodeId host = tree.add_child(LayoutTree::root(), layer, NodeStyle{});
  tree.layout_full();

  CHECK(tree.bounds(host) == PixelRect{0, 0, 400, 300});
}

TEST_CASE("a leaf shrinks to fit whatever it carries") {
  LayoutTree tree{spec_of(400, 300)};
  tree.set_box(LayoutTree::root(), stack_of(LayoutKind::kColumn));

  BoxStyle wrapper;
  wrapper.padding = EdgeInsets::all(6);
  const NodeId host = tree.add_child(LayoutTree::root(), wrapper, NodeStyle{});
  tree.add_child(host, sized(50, 20), NodeStyle{});
  tree.layout_full();

  CHECK(tree.bounds(host) == PixelRect{0, 0, 50 + 12, 20 + 12});
}

TEST_CASE("children that overrun their row are reported, not left to be noticed") {
  LayoutTree tree{spec_of(400, 300)};
  tree.set_box(LayoutTree::root(), stack_of(LayoutKind::kColumn));

  BoxStyle row = stack_of(LayoutKind::kRow);
  row.width = 100;
  row.height = 20;
  row.gap = 10;
  const NodeId host = tree.add_child(LayoutTree::root(), row, NodeStyle{});
  tree.add_child(host, sized(60, 10), NodeStyle{});
  tree.add_child(host, sized(60, 10), NodeStyle{});
  tree.layout_full();

  // 60 + 10 + 60 into 100. Each child fits on its own, which is why nothing
  // upstream catches it and why the sum has to be checked.
  REQUIRE_FALSE(tree.diagnostics().empty());
  const std::string& message = tree.diagnostics().front();
  CHECK(message.find("overrun the main axis by 30 px") != std::string::npos);
  CHECK(message.find("root(column) > #1(row)") != std::string::npos);

  // Cleared per pass, so the list describes the most recent layout rather
  // than the session.
  BoxStyle roomy = row;
  roomy.width = 200;
  tree.set_box(host, roomy);
  tree.layout_full();
  CHECK(tree.diagnostics().empty());
}

TEST_CASE("a boundary nested inside a dirty boundary is skipped, not laid out again") {
  // The arrangement the dirty-list sort in run() exists for, and the one the
  // demo scene does not contain: its four boundaries - toolbar, card, sidebar
  // item, badge layer - sit in four different subtrees, so no order of them
  // can be wrong. Here `inner` is a boundary INSIDE `outer`, both are dirtied
  // on the same frame, and laying the ancestor out first is what lets the
  // descendant be recognised as already clean.
  LayoutTree tree{spec_of(400, 300)};
  BoxStyle root_box = stack_of(LayoutKind::kColumn);
  root_box.cross_align = CrossAlign::kStretch;
  tree.set_box(LayoutTree::root(), root_box);

  // Tight width from the root's stretch, definite height of its own: settled
  // on both axes, so a mark from anywhere inside stops here.
  BoxStyle outer_box = stack_of(LayoutKind::kColumn);
  outer_box.cross_align = CrossAlign::kStretch;
  outer_box.height = 200;
  const NodeId outer = tree.add_child(LayoutTree::root(), outer_box, NodeStyle{});

  // Same again, one level down.
  BoxStyle inner_box = stack_of(LayoutKind::kRow);
  inner_box.cross_align = CrossAlign::kStretch;
  inner_box.height = 60;
  const NodeId inner = tree.add_child(outer, inner_box, NodeStyle{});
  const NodeId inner_leaf = tree.add_child(inner, sized(40, 12), NodeStyle{});

  // No definite height, so this one's size follows its child and its mark
  // climbs past itself to `outer`.
  const NodeId sibling = tree.add_child(outer, BoxStyle{}, NodeStyle{});
  const NodeId sibling_leaf = tree.add_child(sibling, sized(30, 20), NodeStyle{});
  tree.layout_full();

  const PixelRect outer_before = tree.bounds(outer);
  const PixelRect inner_before = tree.bounds(inner);

  // The premise. Without these two the combined case below could be passing
  // because there was only ever one dirty root to order.
  tree.set_box(inner_leaf, sized(44, 12));
  const LayoutStats inner_only = tree.layout();
  CHECK(inner_only.dirty_roots == 1);
  CHECK(tree.bounds(outer) == outer_before);
  CHECK(tree.bounds(sibling) == PixelRect{0, 60, 400, 20});

  tree.set_box(sibling_leaf, sized(34, 26));
  const LayoutStats sibling_only = tree.layout();
  CHECK(sibling_only.dirty_roots == 1);
  CHECK(tree.bounds(outer) == outer_before);
  CHECK(tree.bounds(inner) == inner_before);
  CHECK(tree.bounds(sibling) == PixelRect{0, 60, 400, 26});

  // Both on one frame: two dirty roots go into the list, one nested in the
  // other. Laying `outer` out first re-lays `inner` on the way through, so
  // by the time `inner`'s own turn comes it is clean and is dropped - which
  // is why the pass reports one root and not two.
  tree.set_box(inner_leaf, sized(48, 12));
  tree.set_box(sibling_leaf, sized(34, 32));
  const LayoutStats both = tree.layout();
  CHECK(both.dirty_roots == 1);

  CHECK(tree.bounds(inner_leaf) == PixelRect{0, 0, 48, 60});
  CHECK(tree.bounds(sibling) == PixelRect{0, 60, 400, 32});

  // And the arrangement it arrived at is the one a pass that ignored every
  // cache would have produced.
  const std::vector<PixelRect> incremental = all_bounds(tree);
  tree.layout_full();
  CHECK(incremental == all_bounds(tree));
}

TEST_CASE("an ancestor that stops being a boundary re-keys the nodes under it") {
  // The other half of layout_node()'s cache key. A node can be re-entered
  // with byte-identical constraints while the boundary it sits under has
  // changed, because the boundary is a property of an ANCESTOR's tightness
  // rather than of this node's constraints - and that needs no resize, only a
  // style change one level up.
  LayoutTree tree{spec_of(400, 300)};
  BoxStyle root_box = stack_of(LayoutKind::kRow);
  root_box.cross_align = CrossAlign::kStretch;
  tree.set_box(LayoutTree::root(), root_box);

  // Flexible and stretched: tight on both axes, so it is a boundary, and the
  // shrink-to-fit node under it is keyed to it.
  BoxStyle panel_box = stack_of(LayoutKind::kColumn);
  panel_box.grow = 1;
  const NodeId panel = tree.add_child(LayoutTree::root(), panel_box, NodeStyle{});

  const NodeId wrapper = tree.add_child(panel, BoxStyle{}, NodeStyle{});
  const NodeId item = tree.add_child(wrapper, sized(30, 10), NodeStyle{});
  tree.layout_full();
  CHECK(tree.bounds(panel) == PixelRect{0, 0, 400, 300});
  CHECK(tree.bounds(wrapper) == PixelRect{0, 0, 30, 10});

  // The root stops stretching. `panel` keeps its tight main axis from the
  // grow weight but loses its tight cross axis, so it stops being settled and
  // its boundary becomes the root. `wrapper` meanwhile is handed exactly the
  // constraints it already had - the maxima on both axes are unchanged, only
  // the height minimum moved, and that was already zero for a non-stretched
  // child. So this is the re-entry the boundary term in the early-out is
  // written for.
  root_box.cross_align = CrossAlign::kStart;
  tree.set_box(LayoutTree::root(), root_box);
  tree.layout();
  CHECK(tree.bounds(panel) == PixelRect{0, 0, 400, 10});
  CHECK(tree.bounds(wrapper) == PixelRect{0, 0, 30, 10});

  // A later change under the re-keyed node still climbs to the right place:
  // `panel` is no longer allowed to absorb it, so `panel` resizes too.
  tree.set_box(item, sized(30, 40));
  tree.layout();
  CHECK(tree.bounds(wrapper) == PixelRect{0, 0, 30, 40});
  CHECK(tree.bounds(panel) == PixelRect{0, 0, 400, 40});

  const std::vector<PixelRect> incremental = all_bounds(tree);
  tree.layout_full();
  CHECK(incremental == all_bounds(tree));
}

TEST_CASE("a resize re-lays-out and the render tree agrees about the bounds") {
  LayoutTree tree{spec_of(400, 300)};
  BoxStyle root_box = stack_of(LayoutKind::kColumn);
  root_box.cross_align = CrossAlign::kStretch;
  tree.set_box(LayoutTree::root(), root_box);

  const NodeId filler = tree.add_child(LayoutTree::root(), grown(1), NodeStyle{});
  tree.layout_full();
  CHECK(tree.bounds(filler) == PixelRect{0, 0, 400, 300});

  tree.resize(PixelSize{640, 200});
  tree.layout();
  CHECK(tree.bounds(filler) == PixelRect{0, 0, 640, 200});

  // Layout's only output is where the boxes are, and the render tree is where
  // it puts them. A second place holding the same rectangles is a second
  // place that can disagree.
  CHECK(tree.render().absolute_bounds(filler) == tree.bounds(filler));
}

// parentData changes on a child whose constraints are tight.
//
// This shape was missing from every layout test and from
// layout.incremental_equals_full, and it hid a real divergence between the
// incremental and full passes for the whole of slices 2 and 3. It was found
// while wiring the property table, because the table is what says out loud
// that margin, grow and left/top/right/bottom are consumed by the PARENT.
//
// Why the gap existed: a flexible, stretched child is tight on both axes, so
// mark_needs_layout() absorbed its own style change at the child - correct
// reasoning for a style the child consumes, wrong for one whose tight
// constraint was DERIVED from the value that just changed. The row that hands
// out weights was never told. Nothing looked broken; the child simply kept its
// old size until something else forced a full pass.
//
// The demo scene could not have caught it either: it never changes a weight,
// a margin or an inset after the first layout. That is the same lesson the
// dirty-root comparator taught - enumerate the shapes the ALGORITHM branches
// on, rather than trusting a scene written for a different purpose.
NodeId build_flex_row(LayoutTree& tree) {
  BoxStyle root_box = stack_of(LayoutKind::kColumn);
  root_box.cross_align = CrossAlign::kStretch;
  tree.set_box(LayoutTree::root(), root_box);

  BoxStyle row = stack_of(LayoutKind::kRow);
  row.height = 90;
  row.gap = 6;
  row.cross_align = CrossAlign::kStretch;
  const NodeId container = tree.add_child(LayoutTree::root(), row, NodeStyle{});
  tree.add_child(container, sized(70, 0), NodeStyle{});
  const NodeId flexible = tree.add_child(container, grown(1), NodeStyle{});
  tree.add_child(container, sized(70, 0), NodeStyle{});
  return flexible;
}

NodeId build_absolute_overlay(LayoutTree& tree) {
  BoxStyle root_box = stack_of(LayoutKind::kColumn);
  root_box.cross_align = CrossAlign::kStretch;
  tree.set_box(LayoutTree::root(), root_box);

  BoxStyle overlay = stack_of(LayoutKind::kAbsolute);
  overlay.grow = 1;
  const NodeId container = tree.add_child(LayoutTree::root(), overlay, NodeStyle{});
  BoxStyle pinned;
  pinned.left = 10;
  pinned.top = 10;
  pinned.right = 10;
  pinned.bottom = 10;
  return tree.add_child(container, pinned, NodeStyle{});
}

TEST_CASE("a weight change reaches the row that hands weights out") {
  LayoutTree incremental{spec_of(317, 223)};
  const NodeId flexible = build_flex_row(incremental);
  incremental.layout();

  // Tight on both axes: the flex child of a stretched row is exactly the node
  // mark_needs_layout() used to absorb the change at.
  REQUIRE(incremental.bounds(flexible).width == 165);

  BoxStyle settled = incremental.box(flexible);
  settled.grow = 0;
  settled.width = 70;
  incremental.set_box(flexible, settled);
  incremental.layout();

  CHECK(incremental.bounds(flexible).width == 70);

  LayoutTree full{spec_of(317, 223)};
  const NodeId same = build_flex_row(full);
  BoxStyle full_settled = full.box(same);
  full_settled.grow = 0;
  full_settled.width = 70;
  full.set_box(same, full_settled);
  full.layout_full();

  CHECK(all_bounds(incremental) == all_bounds(full));
}

TEST_CASE("a margin change reaches the container that applies it") {
  LayoutTree incremental{spec_of(317, 223)};
  const NodeId flexible = build_flex_row(incremental);
  incremental.layout();

  BoxStyle moved = incremental.box(flexible);
  moved.margin = EdgeInsets{20, 0, 10, 0};
  incremental.set_box(flexible, moved);
  incremental.layout();

  LayoutTree full{spec_of(317, 223)};
  const NodeId same = build_flex_row(full);
  BoxStyle full_moved = full.box(same);
  full_moved.margin = EdgeInsets{20, 0, 10, 0};
  full.set_box(same, full_moved);
  full.layout_full();

  CHECK(all_bounds(incremental) == all_bounds(full));
}

TEST_CASE("an inset change reaches the absolute container that positions with it") {
  LayoutTree incremental{spec_of(317, 223)};
  const NodeId pinned = build_absolute_overlay(incremental);
  incremental.layout();

  BoxStyle repinned = incremental.box(pinned);
  repinned.left = 40;
  repinned.top = 25;
  incremental.set_box(pinned, repinned);
  incremental.layout();

  LayoutTree full{spec_of(317, 223)};
  const NodeId same = build_absolute_overlay(full);
  BoxStyle full_repinned = full.box(same);
  full_repinned.left = 40;
  full_repinned.top = 25;
  full.set_box(same, full_repinned);
  full.layout_full();

  CHECK(all_bounds(incremental) == all_bounds(full));
}

// The fourth parentData field, and the one whose shape is easiest to get wrong.
//
// A defect injection - dropping align_self from differs_in_parent_data - was
// NOT caught by the wrapping tests, and the reason is structural rather than
// accidental: a wrapping container hands every child a LOOSE constraint, so a
// child's own style change never satisfies mark_needs_layout()'s absorb rule
// and climbs to the container regardless. The delivery is redundant there.
//
// It is not redundant here. A flexible, stretched child of a row is tight on
// BOTH axes, which is exactly the state that absorbs a child's own style
// change - so without the parentData rule the row that decides whether to
// stretch this child is never told to reconsider, and the child keeps a height
// a full pass would have taken away. That is the same shape doc/properties.md
// section 3.7 records for `grow`, one field over.
TEST_CASE("an align_self change reaches the row that would have stretched it") {
  LayoutTree incremental{spec_of(317, 223)};
  const NodeId flexible = build_flex_row(incremental);
  incremental.layout();

  // The premise: tight on both axes, so the child is a relayout boundary and
  // its own style change stops there unless something says otherwise.
  REQUIRE(incremental.bounds(flexible).height == 90);

  BoxStyle opted_out = incremental.box(flexible);
  opted_out.align_self = CrossAlign::kStart;
  incremental.set_box(flexible, opted_out);
  incremental.layout();

  // No height of its own, so once it stops being stretched it shrinks to fit
  // the nothing it contains.
  CHECK(incremental.bounds(flexible).height == 0);

  LayoutTree full{spec_of(317, 223)};
  const NodeId same = build_flex_row(full);
  BoxStyle full_opted_out = full.box(same);
  full_opted_out.align_self = CrossAlign::kStart;
  full.set_box(same, full_opted_out);
  full.layout_full();

  CHECK(all_bounds(incremental) == all_bounds(full));
}

}  // namespace
