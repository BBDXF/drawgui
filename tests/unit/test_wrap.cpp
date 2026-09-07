// The wrapping arrangement: what it MEANS, and that doing less computes it.
//
// Two kinds of test live here and they answer different questions.
//
// The first kind pins geometry to numbers worked out by hand from the box
// model. That is not redundant with the identity gate next door - the identity
// gate drives two copies of the SAME code, so a wrong decision corrupts both
// sides equally and the comparison still passes. Slice 3 learned that about
// widget semantics and slice 4-2 learned it again when a start/end swap in
// `justify` survived because no test used those ordinals. Every align_content
// value therefore gets a case, and the values it produces differ from each
// other.
//
// The second kind is the exactly-once invariant under wrapping, which is the
// genuinely new risk. Wrapping is the first arrangement whose child positions
// depend on other children's sizes in a NON-PREFIX way: a run's cross extent
// is the tallest child in that run, and which children are in that run depends
// on sizes that come later in the list. So a change to one child can move its
// siblings into different runs, and the incremental pass has to notice.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/graphics/types.h"
#include "drawgui/layout/box.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/render_tree.h"

namespace {

using dg::AlignContent;
using dg::BoxStyle;
using dg::Color;
using dg::CrossAlign;
using dg::EdgeInsets;
using dg::LayoutKind;
using dg::LayoutTree;
using dg::MainAlign;
using dg::NodeId;
using dg::NodeStyle;
using dg::PixelRect;
using dg::PixelSize;
using dg::RasterSurface;

constexpr int kViewportWidth = 400;
constexpr int kViewportHeight = 300;

dg::TreeSpec spec_of(std::size_t damage_cap = dg::DamageRegion::kDefaultMaxRects) {
  dg::TreeSpec spec;
  spec.viewport = PixelSize{kViewportWidth, kViewportHeight};
  spec.background.fill = Color::from_argb(0xFF14171C);
  spec.max_damage_rects = damage_cap;
  return spec;
}

std::vector<PixelRect> all_bounds(const LayoutTree& tree) {
  std::vector<PixelRect> bounds;
  bounds.reserve(tree.node_count());
  for (std::uint32_t index = 0; index < tree.node_count(); ++index) {
    bounds.push_back(tree.bounds(NodeId{index}));
  }
  return bounds;
}

std::vector<int> ys_of(const LayoutTree& tree, const std::vector<NodeId>& nodes) {
  std::vector<int> ys;
  ys.reserve(nodes.size());
  for (const NodeId node : nodes) {
    ys.push_back(tree.bounds(node).y);
  }
  return ys;
}

std::vector<int> xs_of(const LayoutTree& tree, const std::vector<NodeId>& nodes) {
  std::vector<int> xs;
  xs.reserve(nodes.size());
  for (const NodeId node : nodes) {
    xs.push_back(tree.bounds(node).x);
  }
  return xs;
}

std::vector<std::uint8_t> snapshot(const RasterSurface& surface) {
  const dg::PixelView view = surface.peek_pixels();
  const auto row = static_cast<std::size_t>(view.width) * 4;
  std::vector<std::uint8_t> pixels(row * static_cast<std::size_t>(view.height));
  for (int y = 0; y < view.height; ++y) {
    std::memcpy(pixels.data() + (static_cast<std::size_t>(y) * row),
                view.pixels + (static_cast<std::size_t>(y) * view.row_bytes), row);
  }
  return pixels;
}

BoxStyle cell(int width, int height) {
  BoxStyle box;
  box.width = width;
  box.height = height;
  return box;
}

// Six 50x20 cells in a 200-wide wrapping row with a gap of 10.
//
// The arithmetic, by hand: three cells and the two gaps between them occupy
// 50 + 10 + 50 + 10 + 50 = 170, and a fourth would need 170 + 10 + 50 = 230,
// which is past 200. So the break lands after every third child, giving two
// runs of three, each 170 wide and 20 tall. Stacked with run_gap 6 they occupy
// 20 + 6 + 20 = 46 of the container's 100, leaving 54 for align_content.
struct Grid {
  LayoutTree tree{spec_of()};
  NodeId wrap;
  std::vector<NodeId> cells;

  static constexpr int kRoom = 200;
  static constexpr int kHeight = 100;
  static constexpr int kGap = 10;
  static constexpr int kRunGap = 6;
  static constexpr int kRunCross = 20;

  Grid() {
    BoxStyle root = tree.box(LayoutTree::root());
    root.kind = LayoutKind::kColumn;
    tree.set_box(LayoutTree::root(), root);

    BoxStyle wrap_box;
    wrap_box.kind = LayoutKind::kWrapRow;
    wrap_box.width = kRoom;
    wrap_box.height = kHeight;
    wrap_box.gap = kGap;
    wrap_box.run_gap = kRunGap;
    wrap = tree.add_child(LayoutTree::root(), wrap_box, NodeStyle{});

    cells.reserve(6);
    for (int i = 0; i < 6; ++i) {
      cells.push_back(tree.add_child(wrap, cell(50, kRunCross), NodeStyle{}));
    }
    tree.layout_full();
  }

  void align_content(AlignContent value) {
    BoxStyle box = tree.box(wrap);
    box.align_content = value;
    tree.set_box(wrap, box);
    tree.layout();
  }

  [[nodiscard]] std::vector<int> ys() const { return ys_of(tree, cells); }
  [[nodiscard]] std::vector<int> xs() const { return xs_of(tree, cells); }
};

}  // namespace

TEST_CASE("a wrapping row breaks where the main axis runs out") {
  Grid grid;

  // Three per run, and the fourth starts the second one. The x positions
  // repeat because each run is placed from the start of the container's
  // content box independently.
  CHECK(grid.xs() == std::vector<int>{0, 60, 120, 0, 60, 120});

  // Two runs, 20 tall, separated by run_gap 6.
  CHECK(grid.ys() == std::vector<int>{0, 0, 0, 26, 26, 26});
}

TEST_CASE("run_gap spaces the runs and nothing else") {
  Grid grid;
  BoxStyle box = grid.tree.box(grid.wrap);
  box.run_gap = 0;
  grid.tree.set_box(grid.wrap, box);
  grid.tree.layout();
  CHECK(grid.ys() == std::vector<int>{0, 0, 0, 20, 20, 20});

  box.run_gap = 30;
  grid.tree.set_box(grid.wrap, box);
  grid.tree.layout();
  CHECK(grid.ys() == std::vector<int>{0, 0, 0, 50, 50, 50});

  // The main axis is untouched by it, which is the half of this that a single
  // run could not distinguish.
  CHECK(grid.xs() == std::vector<int>{0, 60, 120, 0, 60, 120});
}

// Every align_content ordinal, pinned to a number rather than to another code
// path. 54 pixels of cross-axis slack, two runs of 20, run_gap 6.
TEST_CASE("align_content start, centre and end place the run stack") {
  Grid grid;
  grid.align_content(AlignContent::kStart);
  CHECK(grid.ys() == std::vector<int>{0, 0, 0, 26, 26, 26});

  // 54 / 2 = 27, then the second run 26 further on.
  grid.align_content(AlignContent::kCenter);
  CHECK(grid.ys() == std::vector<int>{27, 27, 27, 53, 53, 53});

  // The whole 54 goes in front, so the last run ends exactly at 100.
  grid.align_content(AlignContent::kEnd);
  CHECK(grid.ys() == std::vector<int>{54, 54, 54, 80, 80, 80});
  CHECK(grid.tree.bounds(grid.cells[5]).y + 20 == Grid::kHeight);
}

TEST_CASE("align_content space-between pushes the runs to the two edges") {
  Grid grid;
  grid.align_content(AlignContent::kSpaceBetween);

  // First run at the top, last run flush with the bottom: 100 - 20 = 80.
  CHECK(grid.ys() == std::vector<int>{0, 0, 0, 80, 80, 80});
}

TEST_CASE("align_content space-around leaves half a share at each edge") {
  Grid grid;
  grid.align_content(AlignContent::kSpaceAround);

  // 54 across four half-shares: cumulative 13 before the first run and 40
  // before the second, so the second sits at 13 + 20 + 6 + (40 - 13) = 66.
  // The edges get 13 and 14 - the odd pixel lands at the end rather than
  // being lost, which is the property the cumulative form exists for.
  CHECK(grid.ys() == std::vector<int>{13, 13, 13, 66, 66, 66});
  CHECK(Grid::kHeight - (66 + Grid::kRunCross) == 14);
}

TEST_CASE("align_content stretch grows the runs instead of spacing them") {
  Grid grid;
  grid.align_content(AlignContent::kStretch);

  // 54 split two ways is 27 each, so each run is 20 + 27 = 47 tall. The
  // children keep their own height and sit at the start of their run, so the
  // second run's origin moves to 47 + 6 = 53 and the stack ends at 100.
  CHECK(grid.ys() == std::vector<int>{0, 0, 0, 53, 53, 53});
  CHECK(grid.tree.bounds(grid.cells[0]).height == Grid::kRunCross);
  CHECK(53 + 47 == Grid::kHeight);
}

// The distinction the whole slice turns on: `align` is scoped to a run.
//
// Two runs of DIFFERENT cross extent, so a rule applied across the container
// instead of within each run lands the second run's short child somewhere
// else entirely.
namespace {

struct Ragged {
  LayoutTree tree{spec_of()};
  NodeId wrap;
  std::vector<NodeId> cells;

  Ragged() {
    BoxStyle root = tree.box(LayoutTree::root());
    root.kind = LayoutKind::kColumn;
    tree.set_box(LayoutTree::root(), root);

    BoxStyle wrap_box;
    wrap_box.kind = LayoutKind::kWrapRow;
    wrap_box.width = 200;
    wrap_box.gap = 10;
    wrap_box.run_gap = 6;
    wrap = tree.add_child(LayoutTree::root(), wrap_box, NodeStyle{});

    // Run 0 is 170 wide and 40 tall; run 1 is 110 wide and 30 tall.
    for (const int height : {20, 40, 20, 30, 10}) {
      cells.push_back(tree.add_child(wrap, cell(50, height), NodeStyle{}));
    }
    tree.layout_full();
  }

  void align(CrossAlign value) {
    BoxStyle box = tree.box(wrap);
    box.cross_align = value;
    tree.set_box(wrap, box);
    tree.layout();
  }

  [[nodiscard]] std::vector<int> ys() const { return ys_of(tree, cells); }
};

}  // namespace

TEST_CASE("the container shrinks to its runs and each run is as tall as its tallest child") {
  Ragged ragged;

  // 40 + 6 + 30 = 76, and the container was given no height of its own.
  CHECK(ragged.tree.bounds(ragged.wrap).height == 76);
  CHECK(ragged.ys() == std::vector<int>{0, 0, 0, 46, 46});
}

TEST_CASE("align positions a child inside its own run, not inside the container") {
  Ragged ragged;

  // Run 0 is 40 tall: the 20-tall cells get 10 of slack, the 40-tall one none.
  // Run 1 starts at 46 and is 30 tall: the 30-tall cell gets none, the 10-tall
  // one gets 10. Centring across the container's whole 76 instead would put
  // the last cell at 33.
  ragged.align(CrossAlign::kCenter);
  CHECK(ragged.ys() == std::vector<int>{10, 0, 10, 46, 56});

  ragged.align(CrossAlign::kEnd);
  CHECK(ragged.ys() == std::vector<int>{20, 0, 20, 46, 66});

  ragged.align(CrossAlign::kStart);
  CHECK(ragged.ys() == std::vector<int>{0, 0, 0, 46, 46});
}

// design.md section 5.4.1 invariant L3 and section 5.4.3 step 5 cannot both
// hold for a wrapping container, and this is what the engine does about it.
TEST_CASE("align stretch on a wrapping container is refused out loud, not silently") {
  Ragged ragged;
  ragged.align(CrossAlign::kStretch);

  // Placed exactly as start.
  CHECK(ragged.ys() == std::vector<int>{0, 0, 0, 46, 46});
  CHECK(ragged.tree.bounds(ragged.cells[0]).height == 20);

  REQUIRE_FALSE(ragged.tree.diagnostics().empty());
  const std::string& message = ragged.tree.diagnostics().front();
  CHECK(message.find("align=stretch is not honoured") != std::string::npos);
  CHECK(message.find("wrap-row") != std::string::npos);

  // And it stops being said once nothing asks for it.
  ragged.align(CrossAlign::kStart);
  ragged.tree.layout_full();
  CHECK(ragged.tree.diagnostics().empty());
}

TEST_CASE("grow under a wrapping container is refused out loud, not silently") {
  Ragged ragged;
  BoxStyle box = ragged.tree.box(ragged.cells[0]);
  box.grow = 1;
  ragged.tree.set_box(ragged.cells[0], box);
  ragged.tree.layout();

  CHECK(ragged.tree.bounds(ragged.cells[0]).width == 50);
  REQUIRE_FALSE(ragged.tree.diagnostics().empty());
  CHECK(ragged.tree.diagnostics().front().find("grow is not distributed") != std::string::npos);
}

// The overrun a wrapping container can still produce, and the ONLY one.
//
// A child's own width cannot do it: the container hands out a loose main
// constraint bounded by its room, and limits_for() clamps a definite width to
// the constraint, so a child asking for 260 inside 200 simply becomes 200. A
// MARGIN is the exception, because the constraint is reduced by it while the
// space the child occupies adds it back - so a margin wider than the whole
// container yields a zero-width child that still occupies more than there is.
TEST_CASE("a child whose margins exceed the container overruns its run and is reported") {
  Ragged ragged;
  BoxStyle box = ragged.tree.box(ragged.cells[0]);
  box.margin = EdgeInsets{130, 0, 130, 0};
  ragged.tree.set_box(ragged.cells[0], box);
  ragged.tree.layout();

  // It does not produce an empty run in front of itself.
  CHECK(ragged.tree.bounds(ragged.cells[0]).x == 130);
  REQUIRE_FALSE(ragged.tree.diagnostics().empty());
  CHECK(ragged.tree.diagnostics().front().find("overruns the main axis by 60 px") !=
        std::string::npos);
}

// The other half of that argument, asserted rather than merely claimed: a
// definite width past the container's room is clamped, so it wraps normally
// and says nothing.
TEST_CASE("a child wider than the container is clamped rather than reported") {
  Ragged ragged;
  BoxStyle box = ragged.tree.box(ragged.cells[0]);
  box.width = 260;
  ragged.tree.set_box(ragged.cells[0], box);
  ragged.tree.layout();

  CHECK(ragged.tree.bounds(ragged.cells[0]) == PixelRect{0, 0, 200, 20});
  CHECK(ragged.tree.diagnostics().empty());
}

// --------------------------------------------------------------------------
// align_self
// --------------------------------------------------------------------------

namespace {

// A plain flex row, 200x50, holding three 40x20 cells: 30 of cross slack, and
// the container centres by default so an override is visible in both
// directions.
struct Bar {
  LayoutTree tree{spec_of()};
  NodeId row;
  std::vector<NodeId> cells;

  Bar() {
    BoxStyle root = tree.box(LayoutTree::root());
    root.kind = LayoutKind::kColumn;
    tree.set_box(LayoutTree::root(), root);

    BoxStyle row_box;
    row_box.kind = LayoutKind::kRow;
    row_box.width = 200;
    row_box.height = 50;
    row_box.cross_align = CrossAlign::kCenter;
    row = tree.add_child(LayoutTree::root(), row_box, NodeStyle{});

    for (int i = 0; i < 3; ++i) {
      cells.push_back(tree.add_child(row, cell(40, 20), NodeStyle{}));
    }
    tree.layout_full();
  }

  void align_self(std::size_t which, std::optional<CrossAlign> value) {
    BoxStyle box = tree.box(cells[which]);
    box.align_self = value;
    tree.set_box(cells[which], box);
    tree.layout();
  }

  [[nodiscard]] std::vector<int> ys() const { return ys_of(tree, cells); }
};

}  // namespace

TEST_CASE("align_self overrides the container for one child and leaves the others alone") {
  Bar bar;
  CHECK(bar.ys() == std::vector<int>{15, 15, 15});

  bar.align_self(0, CrossAlign::kStart);
  CHECK(bar.ys() == std::vector<int>{0, 15, 15});

  bar.align_self(2, CrossAlign::kEnd);
  CHECK(bar.ys() == std::vector<int>{0, 15, 30});

  bar.align_self(1, CrossAlign::kCenter);
  CHECK(bar.ys() == std::vector<int>{0, 15, 30});
}

// The one align_self value that changes a child's SIZE rather than only its
// position, which is why it has to be honoured while children are being sized
// and not merely while they are being placed.
TEST_CASE("align_self stretch resizes the child, and auto hands it back") {
  Bar bar;

  bar.align_self(1, CrossAlign::kStretch);
  CHECK(bar.tree.bounds(bar.cells[1]) == PixelRect{40, 0, 40, 50});
  CHECK(bar.tree.bounds(bar.cells[0]).height == 20);

  bar.align_self(1, std::nullopt);
  CHECK(bar.tree.bounds(bar.cells[1]) == PixelRect{40, 15, 40, 20});
}

// The mirror image: a child opting OUT of a stretching container must stop
// receiving the tight cross constraint, or it keeps the container's height
// while being positioned as though it had its own.
TEST_CASE("align_self opts a child out of a stretching row") {
  Bar bar;
  BoxStyle row = bar.tree.box(bar.row);
  row.cross_align = CrossAlign::kStretch;
  bar.tree.set_box(bar.row, row);
  bar.tree.layout();
  CHECK(bar.tree.bounds(bar.cells[0]).height == 50);

  bar.align_self(0, CrossAlign::kEnd);
  CHECK(bar.tree.bounds(bar.cells[0]) == PixelRect{0, 30, 40, 20});
  CHECK(bar.tree.bounds(bar.cells[1]).height == 50);
}

// Inside a wrap the slack a child aligns against is its RUN's, and a run is
// exactly as tall as its tallest member - so the container's own height plays
// no part. Ragged's first run is 40 tall around a 20-tall cell, which is where
// the 10 comes from.
TEST_CASE("align_self works inside a run of a wrapping container too") {
  Ragged ragged;
  ragged.align(CrossAlign::kCenter);
  CHECK(ragged.ys() == std::vector<int>{10, 0, 10, 46, 56});

  BoxStyle box = ragged.tree.box(ragged.cells[0]);
  box.align_self = CrossAlign::kStart;
  ragged.tree.set_box(ragged.cells[0], box);
  ragged.tree.layout();
  CHECK(ragged.ys() == std::vector<int>{0, 0, 10, 46, 56});

  box.align_self = CrossAlign::kEnd;
  ragged.tree.set_box(ragged.cells[0], box);
  ragged.tree.layout();
  CHECK(ragged.ys() == std::vector<int>{20, 0, 10, 46, 56});
}

// --------------------------------------------------------------------------
// The exactly-once invariant, under a re-break.
// --------------------------------------------------------------------------

namespace {

// Four 50-wide cells in a 200-wide wrapping row with gap 10, so the first
// three fill a run and the fourth starts the next one. Every mutation below
// moves that boundary.
struct Rewrap {
  LayoutTree tree;
  NodeId wrap;
  std::vector<NodeId> cells;
  NodeId nested;

  explicit Rewrap(std::size_t damage_cap = dg::DamageRegion::kDefaultMaxRects)
      : tree{spec_of(damage_cap)} {
    BoxStyle root = tree.box(LayoutTree::root());
    root.kind = LayoutKind::kColumn;
    tree.set_box(LayoutTree::root(), root);

    BoxStyle wrap_box;
    wrap_box.kind = LayoutKind::kWrapRow;
    wrap_box.width = 200;
    wrap_box.gap = 10;
    wrap_box.run_gap = 6;
    wrap = tree.add_child(LayoutTree::root(), wrap_box, NodeStyle{});

    for (int i = 0; i < 3; ++i) {
      cells.push_back(tree.add_child(wrap, cell(50, 20), NodeStyle{}));
    }

    // Shrink-to-fit, so its width is its child's and a change two levels down
    // re-breaks the runs. Without it every mutation would be a change to a
    // direct child, and the mark would never have to climb.
    BoxStyle wrapper;
    const NodeId host = tree.add_child(wrap, wrapper, NodeStyle{});
    cells.push_back(host);
    nested = tree.add_child(host, cell(50, 20), NodeStyle{});

    tree.layout_full();
  }
};

// Replays one mutation against a fresh tree that is laid out from scratch, and
// requires every rectangle in the whole tree to agree.
template <typename Mutate>
void check_incremental_equals_full(const Mutate& mutate) {
  Rewrap incremental;
  mutate(incremental);
  incremental.tree.layout();

  Rewrap full;
  mutate(full);
  full.tree.layout_full();

  CHECK(all_bounds(incremental.tree) == all_bounds(full.tree));
}

void set_width(Rewrap& scene, std::size_t which, int width) {
  BoxStyle box = scene.tree.box(scene.cells[which]);
  box.width = width;
  scene.tree.set_box(scene.cells[which], box);
}

}  // namespace

TEST_CASE("the starting arrangement is three in the first run and one in the second") {
  Rewrap scene;
  CHECK(xs_of(scene.tree, scene.cells) == std::vector<int>{0, 60, 120, 0});
  CHECK(ys_of(scene.tree, scene.cells) == std::vector<int>{0, 0, 0, 26});
}

// The case the brief names: a size change that moves a SIBLING into a
// different run. Widening cell 1 to 90 makes 50 + 10 + 90 = 150, and cell 2
// would need 210, so cell 2 falls into the second run. Cell 2's own box never
// changed.
TEST_CASE("a size change that moves a sibling into another run is still exact") {
  Rewrap scene;
  set_width(scene, 1, 90);
  scene.tree.layout();

  CHECK(xs_of(scene.tree, scene.cells) == std::vector<int>{0, 60, 0, 60});
  CHECK(ys_of(scene.tree, scene.cells) == std::vector<int>{0, 0, 26, 26});

  check_incremental_equals_full([](Rewrap& tree) { set_width(tree, 1, 90); });
}

// And the case that changes how MANY runs there are, which resizes the
// container and moves everything after it.
TEST_CASE("a size change that changes the number of runs is still exact") {
  Rewrap scene;
  const int two_runs = scene.tree.bounds(scene.wrap).height;
  set_width(scene, 1, 160);
  scene.tree.layout();

  // 50 alone, then 160 alone, then 50 + 10 + 50 together: three runs.
  CHECK(ys_of(scene.tree, scene.cells) == std::vector<int>{0, 26, 52, 52});
  CHECK(scene.tree.bounds(scene.wrap).height == two_runs + 26);

  check_incremental_equals_full([](Rewrap& tree) { set_width(tree, 1, 160); });
}

TEST_CASE("a re-break driven from two levels down is still exact") {
  // The nested cell is inside a shrink-to-fit wrapper, so the mark has to
  // climb out of the wrapper and reach the container that forms the runs.
  Rewrap scene;
  BoxStyle box = scene.tree.box(scene.nested);
  box.width = 130;
  scene.tree.set_box(scene.nested, box);
  scene.tree.layout();

  CHECK(scene.tree.bounds(scene.cells[3]).width == 130);
  check_incremental_equals_full([](Rewrap& tree) {
    BoxStyle nested = tree.tree.box(tree.nested);
    nested.width = 130;
    tree.tree.set_box(tree.nested, nested);
  });
}

// margin and align_self are parentData: the node that reads them is the
// container, so a change has to be delivered one level up. margin additionally
// changes run membership, which is the shape that makes forgetting it visible.
TEST_CASE("a parentData change on a wrapped child reaches the container") {
  check_incremental_equals_full([](Rewrap& tree) {
    BoxStyle box = tree.tree.box(tree.cells[1]);
    box.margin = EdgeInsets{12, 0, 14, 0};
    tree.tree.set_box(tree.cells[1], box);
  });

  check_incremental_equals_full([](Rewrap& tree) {
    BoxStyle box = tree.tree.box(tree.cells[0]);
    box.align_self = CrossAlign::kEnd;
    tree.tree.set_box(tree.cells[0], box);
  });
}

TEST_CASE("a container property that re-breaks the runs is still exact") {
  check_incremental_equals_full([](Rewrap& tree) {
    BoxStyle box = tree.tree.box(tree.wrap);
    box.gap = 40;
    tree.tree.set_box(tree.wrap, box);
  });

  check_incremental_equals_full([](Rewrap& tree) {
    BoxStyle box = tree.tree.box(tree.wrap);
    box.width = 130;
    tree.tree.set_box(tree.wrap, box);
  });
}

// Sequences, not single steps. A run boundary that lands correctly from a
// clean tree can still land wrongly when it arrives from another arrangement,
// because that is the path where a stale cached size would be reused.
TEST_CASE("a sequence of re-breaks lands where a full layout would") {
  Rewrap scene;
  Rewrap reference;

  for (const int width : {90, 160, 50, 120, 200, 30, 50}) {
    set_width(scene, 1, width);
    scene.tree.layout();

    set_width(reference, 1, width);
    reference.tree.layout_full();

    INFO("cell 1 width ", width);
    CHECK(all_bounds(scene.tree) == all_bounds(reference.tree));
  }
}

// The damage half of the same claim. A re-break moves several nodes at once,
// and a node that moves has to damage the pixels it vacated as well as the
// ones it now occupies - otherwise the old ones keep last frame's paint and a
// still frame does not show it.
TEST_CASE("a re-break repaints exactly the pixels a full repaint would") {
  std::optional<RasterSurface> incremental =
      RasterSurface::create(kViewportWidth, kViewportHeight);
  std::optional<RasterSurface> full = RasterSurface::create(kViewportWidth, kViewportHeight);
  if (!incremental.has_value() || !full.has_value()) {
    FAIL("could not allocate a raster surface");
    return;
  }

  Rewrap scene;
  for (std::size_t which = 0; which < scene.cells.size(); ++which) {
    NodeStyle style;
    style.fill = Color::rgba(static_cast<std::uint8_t>(40 + (which * 50)), 0x6E, 0xF5);
    scene.tree.render().set_style(scene.cells[which], style);
  }
  scene.tree.layout();
  scene.tree.render().repaint_full(*incremental);

  for (const int width : {90, 160, 50}) {
    set_width(scene, 1, width);
    scene.tree.layout();
    scene.tree.render().repaint(*incremental);
    scene.tree.render().repaint_full(*full);

    INFO("cell 1 width ", width);
    CHECK(snapshot(*incremental) == snapshot(*full));
  }
}

// LayoutStats::damage_area is what the demo reports and what a scope table
// reads, and it is derived SEPARATELY from the damage the render tree
// accumulates - apply_bounds adds each moved subtree's old and new extents by
// hand. Two derivations of one quantity can disagree, and the pixel test above
// cannot see it: RenderTree::set_local_bounds damages the vacated box on its
// own account whatever apply_bounds chooses to report.
//
// Checked under a ONE-rectangle damage cap, which is what makes the assertion
// exact rather than directional. With the cap at one the region collapses to
// the bounding box of everything damaged, so its area is comparable against a
// bounding box computed here; under the default cap of eight the rectangles
// stay disjoint and their total area is deliberately far SMALLER than any
// bounding box, so nothing could be concluded from it.
TEST_CASE("a re-break reports damage covering both where a node was and where it went") {
  Rewrap scene{1};
  const PixelRect before = scene.tree.bounds(scene.cells[2]);

  set_width(scene, 1, 90);
  const dg::LayoutStats stats = scene.tree.layout();
  const PixelRect after = scene.tree.bounds(scene.cells[2]);
  REQUIRE(before != after);
  REQUIRE(stats.damage_rects == 1);

  // Reporting only the destination leaves the vacated box outside the box the
  // stats describe, and the demo would then under-report every reflow.
  CHECK(stats.damage_area >= join(before, after).area());
}

// Hit testing reads the live tree rather than a cached rectangle, so a rewrap
// cannot leave a stale one behind - but that is a claim about the code, and
// wrapping is the arrangement that moves the most nodes at once, so it is
// worth an instance.
TEST_CASE("hit testing follows the children a re-break moved") {
  Rewrap scene;
  set_width(scene, 1, 90);
  scene.tree.layout();

  // Cell 2 has moved into the second run. Its new box answers, and the pixels
  // it vacated - which are now bare container - answer with the container
  // rather than with a rectangle nobody updated.
  const PixelRect moved = scene.tree.bounds(scene.cells[2]);
  REQUIRE(moved == PixelRect{0, 26, 50, 20});
  CHECK(scene.tree.render().hit_test(dg::PixelPoint{moved.x + 2, moved.y + 2}) ==
        std::optional<NodeId>{scene.cells[2]});
  CHECK(scene.tree.render().hit_test(dg::PixelPoint{165, 5}) ==
        std::optional<NodeId>{scene.wrap});
}

// A column that wraps is the same algorithm with the axes exchanged, and it is
// the case a row-only implementation passes every other test without.
TEST_CASE("a wrapping column breaks down the y axis and stacks its runs across x") {
  LayoutTree tree{spec_of()};
  BoxStyle root = tree.box(LayoutTree::root());
  root.kind = LayoutKind::kColumn;
  tree.set_box(LayoutTree::root(), root);

  BoxStyle wrap_box;
  wrap_box.kind = LayoutKind::kWrapColumn;
  wrap_box.width = 300;
  wrap_box.height = 110;
  wrap_box.gap = 10;
  wrap_box.run_gap = 6;
  const NodeId wrap = tree.add_child(LayoutTree::root(), wrap_box, NodeStyle{});

  std::vector<NodeId> cells;
  cells.reserve(4);
  for (int i = 0; i < 4; ++i) {
    cells.push_back(tree.add_child(wrap, cell(40, 30), NodeStyle{}));
  }
  tree.layout_full();

  // 30 + 10 + 30 + 10 + 30 = 110 exactly fits; a fourth would need 150. So the
  // runs are three and one, and the second is stacked 40 + 6 across.
  CHECK(ys_of(tree, cells) == std::vector<int>{0, 40, 80, 0});
  CHECK(xs_of(tree, cells) == std::vector<int>{0, 0, 0, 46});
  CHECK(tree.bounds(wrap) == PixelRect{0, 0, 300, 110});
}

// justify distributes what is left over IN EACH RUN, so two runs of different
// length get different amounts - the property a single-run container cannot
// distinguish from distributing once.
TEST_CASE("justify distributes each run's own leftover") {
  Rewrap scene;
  set_width(scene, 1, 90);
  BoxStyle box = scene.tree.box(scene.wrap);
  box.main_align = MainAlign::kEnd;
  scene.tree.set_box(scene.wrap, box);
  scene.tree.layout();

  // Run 0 is 50 + 10 + 90 = 150 with 50 to spare; run 1 is 50 + 10 + 50 = 110
  // with 90 to spare. Ending each run flush right puts them at different x.
  CHECK(xs_of(scene.tree, scene.cells) == std::vector<int>{50, 110, 90, 150});
}
