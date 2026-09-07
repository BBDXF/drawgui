// The gate that stops the two vocabularies drifting apart.
//
// Slices 2 and 3 built BoxStyle and NodeStyle; P0 built the property table.
// Both now describe the same box, and doc/properties.md argues that two
// descriptions of one thing are worse than either alone unless something
// forces them to agree. This is that something.
//
// The technique is to build ONE scene TWICE - once by assigning the structs a
// caller has always been able to assign, once by writing nothing but property
// ids and values - and require the two to agree on every rectangle and on
// every pixel.
//
// This is NOT the weak form of a byte-identity test that doc/damage-repaint.md
// warns about. That warning is about a comparison driving two copies of the
// SAME code, where a wrong decision corrupts both sides equally. Here the two
// sides are different code: one writes struct members, the other goes through
// the generated dispatch, the type check, the range checks and the applies_to
// gates. A defect in any of those shows up as a disagreement.
//
// Even so, agreement alone would not prove the numbers are RIGHT - both paths
// could be wrong together if the scene were built to expect whatever came out.
// So the concrete-expectations case at the bottom pins real rectangles that
// were derived from the box model by hand.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/graphics/types.h"
#include "drawgui/layout/box.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/props/node_props.h"
#include "drawgui/render/prop_ids.generated.h"
#include "drawgui/render/render_tree.h"

namespace {

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
using dg::PropValue;
using dg::RasterSurface;

// Odd dimensions on purpose, matching test_damage_repaint: the surface pads
// its rows, so anything assuming a row is width * 4 bytes fails here.
constexpr int kWidth = 361;
constexpr int kHeight = 259;

constexpr std::uint32_t kPanelFill = 0xFF2A3140;
constexpr std::uint32_t kCardFill = 0xFF4C6EF5;
constexpr std::uint32_t kBadgeFill = 0xFFF59F00;
constexpr std::uint32_t kBorder = 0xFFE9ECEF;

dg::TreeSpec spec_of() {
  dg::TreeSpec spec;
  spec.viewport = PixelSize{kWidth, kHeight};
  spec.background.fill = Color::from_argb(0xFF14171C);
  return spec;
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

std::vector<PixelRect> all_bounds(const LayoutTree& tree) {
  std::vector<PixelRect> bounds;
  bounds.reserve(tree.node_count());
  for (std::uint32_t index = 0; index < tree.node_count(); ++index) {
    bounds.push_back(tree.bounds(NodeId{index}));
  }
  return bounds;
}

std::vector<PixelRect> all_content_bounds(const LayoutTree& tree) {
  std::vector<PixelRect> bounds;
  bounds.reserve(tree.node_count());
  for (std::uint32_t index = 0; index < tree.node_count(); ++index) {
    bounds.push_back(tree.content_bounds(NodeId{index}));
  }
  return bounds;
}

// The same six-node scene, described the way a caller has always been able to.
//
// It is built to contain one instance of every shape the property path can get
// wrong: a row inside a column, padding and border and margin all non-zero and
// all DIFFERENT per side (so a transposed side shows up), a flexible child
// beside an inflexible one, a non-start main and cross alignment, an absolute
// container with a two-edge-pinned child, rounded corners on some nodes and
// square on others, and a border whose four widths are unequal.
struct Scene {
  NodeId toolbar;
  NodeId spacer;
  NodeId card;
  NodeId overlay;
  NodeId badge;
};

Scene build_direct(LayoutTree& tree) {
  BoxStyle root = tree.box(LayoutTree::root());
  root.kind = LayoutKind::kColumn;
  root.padding = EdgeInsets{7, 5, 3, 11};
  root.gap = 6;
  root.cross_align = CrossAlign::kStretch;
  tree.set_box(LayoutTree::root(), root);

  BoxStyle toolbar_box;
  toolbar_box.kind = LayoutKind::kRow;
  toolbar_box.height = 44;
  toolbar_box.padding = EdgeInsets{9, 4, 13, 2};
  toolbar_box.border = EdgeInsets{3, 3, 3, 3};
  toolbar_box.gap = 8;
  toolbar_box.main_align = MainAlign::kSpaceBetween;
  toolbar_box.cross_align = CrossAlign::kCenter;
  NodeStyle toolbar_style;
  toolbar_style.fill = Color::from_argb(kPanelFill);
  toolbar_style.border_color = Color::from_argb(kBorder);
  toolbar_style.border_width = dg::BorderWidths::all(3.0F);
  const NodeId toolbar = tree.add_child(LayoutTree::root(), toolbar_box, toolbar_style);

  BoxStyle spacer_box;
  spacer_box.width = 60;
  spacer_box.height = 20;
  spacer_box.margin = EdgeInsets{2, 1, 4, 3};
  NodeStyle spacer_style;
  spacer_style.fill = Color::from_argb(kBadgeFill);
  spacer_style.radii = dg::Radii{4, 4, 4, 4};
  const NodeId spacer = tree.add_child(toolbar, spacer_box, spacer_style);

  BoxStyle card_box;
  card_box.kind = LayoutKind::kColumn;
  card_box.grow = 3;
  card_box.min_height = 30;
  card_box.max_height = 400;
  card_box.padding = EdgeInsets{5, 5, 5, 5};
  NodeStyle card_style;
  card_style.fill = Color::from_argb(kCardFill);
  card_style.radii = dg::Radii{9, 9, 9, 9};
  const NodeId card = tree.add_child(LayoutTree::root(), card_box, card_style);

  BoxStyle overlay_box;
  overlay_box.kind = LayoutKind::kAbsolute;
  overlay_box.grow = 1;
  NodeStyle overlay_style;
  overlay_style.fill = Color::from_argb(0x00000000);
  const NodeId overlay = tree.add_child(LayoutTree::root(), overlay_box, overlay_style);

  BoxStyle badge_box;
  badge_box.left = 12;
  badge_box.top = 6;
  badge_box.right = 20;
  badge_box.height = 18;
  NodeStyle badge_style;
  badge_style.fill = Color::from_argb(kBadgeFill);
  const NodeId badge = tree.add_child(overlay, badge_box, badge_style);

  return Scene{toolbar, spacer, card, overlay, badge};
}

// Every write goes through set_prop, and every one is required to succeed.
//
// Requiring ok() matters independently of the pixel comparison: a write that
// was silently rejected would leave the node at its default, and while that
// WOULD show up as a difference here, the failure would read as "the pixels
// differ" rather than as "gap was refused on a row". The status check names
// the property.
struct PropScene {
  Scene nodes;
  bool all_applied = true;
};

class Writer {
 public:
  explicit Writer(LayoutTree& tree) : tree_(&tree) {}

  void set(NodeId node, dg_prop_id prop, const PropValue& value) {
    const dg::PropWrite write = dg::set_prop(*tree_, node, prop, value);
    INFO("prop id ", prop, " -> ", write.message);
    CHECK(write.ok());
    ok_ = ok_ && write.ok();
  }

  [[nodiscard]] bool ok() const { return ok_; }

 private:
  LayoutTree* tree_;
  bool ok_ = true;
};

PropScene build_via_props(LayoutTree& tree) {
  Writer write{tree};
  const NodeId root = LayoutTree::root();

  // The root is already a column, so `direction` is applicable; the kind of a
  // node is not itself a property (see doc/properties.md - the table models
  // box/flex/wrap/stack as node KINDS, chosen at construction).
  write.set(root, DG_PROP_DIRECTION, PropValue::option(DG_DIRECTION_COLUMN));
  write.set(root, DG_PROP_PADDING_L, PropValue::number(7));
  write.set(root, DG_PROP_PADDING_T, PropValue::number(5));
  write.set(root, DG_PROP_PADDING_R, PropValue::number(3));
  write.set(root, DG_PROP_PADDING_B, PropValue::number(11));
  write.set(root, DG_PROP_GAP, PropValue::number(6));
  write.set(root, DG_PROP_ALIGN, PropValue::option(DG_ALIGN_STRETCH));

  BoxStyle row;
  row.kind = LayoutKind::kRow;
  const NodeId toolbar = tree.add_child(root, row, NodeStyle{});
  write.set(toolbar, DG_PROP_HEIGHT, PropValue::length(44));
  write.set(toolbar, DG_PROP_PADDING_L, PropValue::number(9));
  write.set(toolbar, DG_PROP_PADDING_T, PropValue::number(4));
  write.set(toolbar, DG_PROP_PADDING_R, PropValue::number(13));
  write.set(toolbar, DG_PROP_PADDING_B, PropValue::number(2));
  write.set(toolbar, DG_PROP_BORDER_WIDTH_L, PropValue::number(3));
  write.set(toolbar, DG_PROP_BORDER_WIDTH_T, PropValue::number(3));
  write.set(toolbar, DG_PROP_BORDER_WIDTH_R, PropValue::number(3));
  write.set(toolbar, DG_PROP_BORDER_WIDTH_B, PropValue::number(3));
  write.set(toolbar, DG_PROP_GAP, PropValue::number(8));
  write.set(toolbar, DG_PROP_JUSTIFY, PropValue::option(DG_JUSTIFY_SPACE_BETWEEN));
  write.set(toolbar, DG_PROP_ALIGN, PropValue::option(DG_ALIGN_CENTER));
  write.set(toolbar, DG_PROP_BACKGROUND_COLOR, PropValue::color(Color::from_argb(kPanelFill)));
  write.set(toolbar, DG_PROP_BORDER_COLOR, PropValue::color(Color::from_argb(kBorder)));

  const NodeId spacer = tree.add_child(toolbar, BoxStyle{}, NodeStyle{});
  write.set(spacer, DG_PROP_WIDTH, PropValue::length(60));
  write.set(spacer, DG_PROP_HEIGHT, PropValue::length(20));
  write.set(spacer, DG_PROP_MARGIN_L, PropValue::number(2));
  write.set(spacer, DG_PROP_MARGIN_T, PropValue::number(1));
  write.set(spacer, DG_PROP_MARGIN_R, PropValue::number(4));
  write.set(spacer, DG_PROP_MARGIN_B, PropValue::number(3));
  write.set(spacer, DG_PROP_BACKGROUND_COLOR, PropValue::color(Color::from_argb(kBadgeFill)));
  write.set(spacer, DG_PROP_BORDER_RADIUS_TL, PropValue::number(4));
  write.set(spacer, DG_PROP_BORDER_RADIUS_TR, PropValue::number(4));
  write.set(spacer, DG_PROP_BORDER_RADIUS_BR, PropValue::number(4));
  write.set(spacer, DG_PROP_BORDER_RADIUS_BL, PropValue::number(4));

  BoxStyle column;
  column.kind = LayoutKind::kColumn;
  const NodeId card = tree.add_child(root, column, NodeStyle{});
  write.set(card, DG_PROP_GROW, PropValue::number(3));
  write.set(card, DG_PROP_MIN_HEIGHT, PropValue::length(30));
  write.set(card, DG_PROP_MAX_HEIGHT, PropValue::length(400));
  write.set(card, DG_PROP_PADDING_L, PropValue::number(5));
  write.set(card, DG_PROP_PADDING_T, PropValue::number(5));
  write.set(card, DG_PROP_PADDING_R, PropValue::number(5));
  write.set(card, DG_PROP_PADDING_B, PropValue::number(5));
  write.set(card, DG_PROP_BACKGROUND_COLOR, PropValue::color(Color::from_argb(kCardFill)));
  write.set(card, DG_PROP_BORDER_RADIUS_TL, PropValue::number(9));
  write.set(card, DG_PROP_BORDER_RADIUS_TR, PropValue::number(9));
  write.set(card, DG_PROP_BORDER_RADIUS_BR, PropValue::number(9));
  write.set(card, DG_PROP_BORDER_RADIUS_BL, PropValue::number(9));

  BoxStyle absolute;
  absolute.kind = LayoutKind::kAbsolute;
  const NodeId overlay = tree.add_child(root, absolute, NodeStyle{});
  write.set(overlay, DG_PROP_GROW, PropValue::number(1));
  write.set(overlay, DG_PROP_BACKGROUND_COLOR, PropValue::color(Color::from_argb(0x00000000)));

  const NodeId badge = tree.add_child(overlay, BoxStyle{}, NodeStyle{});
  write.set(badge, DG_PROP_LEFT, PropValue::number(12));
  write.set(badge, DG_PROP_TOP, PropValue::number(6));
  write.set(badge, DG_PROP_RIGHT, PropValue::number(20));
  write.set(badge, DG_PROP_HEIGHT, PropValue::length(18));
  write.set(badge, DG_PROP_BACKGROUND_COLOR, PropValue::color(Color::from_argb(kBadgeFill)));

  return PropScene{Scene{toolbar, spacer, card, overlay, badge}, write.ok()};
}

}  // namespace

TEST_CASE("properties reach layout the same way the box struct does") {
  LayoutTree direct{spec_of()};
  build_direct(direct);
  direct.layout();

  LayoutTree via_props{spec_of()};
  const PropScene written = build_via_props(via_props);
  via_props.layout();

  CHECK(written.all_applied);
  REQUIRE(direct.node_count() == via_props.node_count());
  CHECK(all_bounds(direct) == all_bounds(via_props));
  CHECK(all_content_bounds(direct) == all_content_bounds(via_props));
  CHECK(direct.diagnostics() == via_props.diagnostics());
}

TEST_CASE("properties reach paint the same way the node style struct does") {
  LayoutTree direct{spec_of()};
  build_direct(direct);
  direct.layout();

  LayoutTree via_props{spec_of()};
  build_via_props(via_props);
  via_props.layout();

  std::optional<RasterSurface> a = RasterSurface::create(kWidth, kHeight);
  std::optional<RasterSurface> b = RasterSurface::create(kWidth, kHeight);
  if (!a.has_value() || !b.has_value()) {
    FAIL("could not allocate a raster surface");
    return;
  }

  direct.render().repaint_full(*a);
  via_props.render().repaint_full(*b);

  CHECK(snapshot(*a) == snapshot(*b));
}

// Concrete numbers, derived by hand from the box model rather than read back
// out of the implementation.
//
// The two cases above compare one path against another, and doc/layout.md
// records why that is not enough on its own: two paths that agree can still
// agree on the wrong answer. These are the values the border-box rules
// require, worked out independently.
TEST_CASE("a property-built box lands where the box model says it should") {
  LayoutTree tree{spec_of()};

  BoxStyle column;
  column.kind = LayoutKind::kColumn;
  const NodeId root = LayoutTree::root();

  // Root: 361x259 viewport, padding 10 all round -> content box is
  // (10, 10)..(351, 249), i.e. 341 x 239.
  REQUIRE(dg::set_prop(tree, root, DG_PROP_PADDING_L, PropValue::number(10)).ok());
  REQUIRE(dg::set_prop(tree, root, DG_PROP_PADDING_T, PropValue::number(10)).ok());
  REQUIRE(dg::set_prop(tree, root, DG_PROP_PADDING_R, PropValue::number(10)).ok());
  REQUIRE(dg::set_prop(tree, root, DG_PROP_PADDING_B, PropValue::number(10)).ok());

  // A child with border 4 a side and padding 6 a side, sized 100x50 as a
  // BORDER box. Its content box is therefore inset by 10 a side: 80x30.
  const NodeId child = tree.add_child(root, BoxStyle{}, NodeStyle{});
  REQUIRE(dg::set_prop(tree, child, DG_PROP_WIDTH, PropValue::length(100)).ok());
  REQUIRE(dg::set_prop(tree, child, DG_PROP_HEIGHT, PropValue::length(50)).ok());
  REQUIRE(dg::set_prop(tree, child, DG_PROP_BORDER_WIDTH_L, PropValue::number(4)).ok());
  REQUIRE(dg::set_prop(tree, child, DG_PROP_BORDER_WIDTH_T, PropValue::number(4)).ok());
  REQUIRE(dg::set_prop(tree, child, DG_PROP_BORDER_WIDTH_R, PropValue::number(4)).ok());
  REQUIRE(dg::set_prop(tree, child, DG_PROP_BORDER_WIDTH_B, PropValue::number(4)).ok());
  REQUIRE(dg::set_prop(tree, child, DG_PROP_PADDING_L, PropValue::number(6)).ok());
  REQUIRE(dg::set_prop(tree, child, DG_PROP_PADDING_T, PropValue::number(6)).ok());
  REQUIRE(dg::set_prop(tree, child, DG_PROP_PADDING_R, PropValue::number(6)).ok());
  REQUIRE(dg::set_prop(tree, child, DG_PROP_PADDING_B, PropValue::number(6)).ok());

  // Margin 8 on the left and 12 on top, applied by the PARENT, excluded from
  // the child's own size: the border box stays 100x50 and moves to
  // (10 + 8, 10 + 12) = (18, 22).
  REQUIRE(dg::set_prop(tree, child, DG_PROP_MARGIN_L, PropValue::number(8)).ok());
  REQUIRE(dg::set_prop(tree, child, DG_PROP_MARGIN_T, PropValue::number(12)).ok());

  tree.layout();

  CHECK(tree.bounds(root) == PixelRect{0, 0, 361, 259});
  CHECK(tree.content_bounds(root) == PixelRect{10, 10, 341, 239});
  CHECK(tree.bounds(child) == PixelRect{18, 22, 100, 50});
  CHECK(tree.content_bounds(child) == PixelRect{28, 32, 80, 30});

  // The painted border now carries all four widths rather than the smallest
  // of them. They agree here, so it is 4 on every side.
  CHECK(tree.render().style(child).border_width == dg::BorderWidths::all(4.0F));
}

// The rule that used to be the one place border_width_* was not exact.
//
// Until this slice `BoxStyle::border` reserved four insets while
// `NodeStyle::border_width` was a single uniform stroke, so the painter took
// the MINIMUM of the four - it was the only value guaranteed to sit inside
// every side's reserved space. doc/properties.md section 3.3 recorded that as
// a table-versus-engine disagreement; the four widths now travel intact and
// the reserved layout space and the painted stroke are the same numbers.
TEST_CASE("unequal border widths reserve per side and paint per side") {
  LayoutTree tree{spec_of()};
  const NodeId child = tree.add_child(LayoutTree::root(), BoxStyle{}, NodeStyle{});

  REQUIRE(dg::set_prop(tree, child, DG_PROP_BORDER_WIDTH_L, PropValue::number(9)).ok());
  REQUIRE(dg::set_prop(tree, child, DG_PROP_BORDER_WIDTH_T, PropValue::number(5)).ok());
  REQUIRE(dg::set_prop(tree, child, DG_PROP_BORDER_WIDTH_R, PropValue::number(2)).ok());
  REQUIRE(dg::set_prop(tree, child, DG_PROP_BORDER_WIDTH_B, PropValue::number(7)).ok());

  CHECK(tree.box(child).border == EdgeInsets{9, 5, 2, 7});
  CHECK(tree.render().style(child).border_width == dg::BorderWidths{9, 5, 2, 7});

  // Setting one side at a time is what made rejecting unequal sides
  // impossible, and it is still the ordinary way a caller arrives here: after
  // the first write of the four the node IS unequal.
  REQUIRE(dg::set_prop(tree, child, DG_PROP_BORDER_WIDTH_R, PropValue::number(9)).ok());
  REQUIRE(dg::set_prop(tree, child, DG_PROP_BORDER_WIDTH_T, PropValue::number(9)).ok());
  REQUIRE(dg::set_prop(tree, child, DG_PROP_BORDER_WIDTH_B, PropValue::number(9)).ok());
  CHECK(tree.render().style(child).border_width == dg::BorderWidths::all(9.0F));
  CHECK(tree.render().style(child).border_width.is_uniform());
}

// Every supported enum ordinal, pinned to the geometry it must produce.
//
// These exist because a defect injection survived without them: swapping
// DG_JUSTIFY_START and DG_JUSTIFY_END in the applier left every test green.
// Nothing was wrong with the tests that existed - the parity scene simply uses
// space_between and the damage script uses center, so no scene in the suite
// contained a start or an end. An ordinal-to-enum mapping with no instance in
// the input cannot be exercised however many properties are set.
//
// The ordinals are an ABI contract (the order of each `values` list in the
// TOML), so the mapping from ordinal to arrangement is exactly the kind of
// thing that must be pinned to numbers rather than to another code path.
struct Bar {
  LayoutTree tree{spec_of()};
  NodeId row;
  NodeId first;
  NodeId middle;
  NodeId last;

  Bar() {
    BoxStyle root = tree.box(LayoutTree::root());
    root.kind = LayoutKind::kColumn;
    tree.set_box(LayoutTree::root(), root);

    BoxStyle row_box;
    row_box.kind = LayoutKind::kRow;
    row_box.width = 200;
    row_box.height = 50;
    row = tree.add_child(LayoutTree::root(), row_box, NodeStyle{});

    BoxStyle cell;
    cell.width = 40;
    cell.height = 20;
    first = tree.add_child(row, cell, NodeStyle{});
    middle = tree.add_child(row, cell, NodeStyle{});
    last = tree.add_child(row, cell, NodeStyle{});
    tree.layout();
  }

  std::vector<int> xs() {
    tree.layout();
    return {tree.bounds(first).x, tree.bounds(middle).x, tree.bounds(last).x};
  }

  std::vector<int> ys() {
    tree.layout();
    return {tree.bounds(first).y, tree.bounds(middle).y, tree.bounds(last).y};
  }

  bool justify(std::uint32_t ordinal) {
    return dg::set_prop(tree, row, DG_PROP_JUSTIFY, PropValue::option(ordinal)).ok();
  }

  bool align(std::uint32_t ordinal) {
    return dg::set_prop(tree, row, DG_PROP_ALIGN, PropValue::option(ordinal)).ok();
  }
};

// A 200px row holding three 40px cells: 120px used, 80px of slack.
TEST_CASE("justify ordinals place the row's slack where the table says") {
  Bar bar;
  REQUIRE(bar.justify(DG_JUSTIFY_START));
  CHECK(bar.xs() == std::vector<int>{0, 40, 80});

  REQUIRE(bar.justify(DG_JUSTIFY_END));
  CHECK(bar.xs() == std::vector<int>{80, 120, 160});
}

TEST_CASE("justify centre and space-between are distinct and both exact") {
  Bar bar;
  REQUIRE(bar.justify(DG_JUSTIFY_CENTER));
  CHECK(bar.xs() == std::vector<int>{40, 80, 120});

  REQUIRE(bar.justify(DG_JUSTIFY_SPACE_BETWEEN));
  CHECK(bar.xs() == std::vector<int>{0, 80, 160});
}

// A 50px-tall row holding 20px cells: 30px of cross-axis slack.
TEST_CASE("align ordinals place a cell on the cross axis where the table says") {
  Bar bar;
  REQUIRE(bar.align(DG_ALIGN_START));
  CHECK(bar.ys() == std::vector<int>{0, 0, 0});

  REQUIRE(bar.align(DG_ALIGN_CENTER));
  CHECK(bar.ys() == std::vector<int>{15, 15, 15});
}

TEST_CASE("align end and stretch differ from each other and from start") {
  Bar bar;
  REQUIRE(bar.align(DG_ALIGN_END));
  CHECK(bar.ys() == std::vector<int>{30, 30, 30});

  REQUIRE(bar.align(DG_ALIGN_STRETCH));
  CHECK(bar.ys() == std::vector<int>{0, 0, 0});
  CHECK(bar.tree.bounds(bar.first).height == 50);
}

TEST_CASE("the direction ordinals select the axis the table names") {
  Bar bar;
  REQUIRE(
      dg::set_prop(bar.tree, bar.row, DG_PROP_DIRECTION, PropValue::option(DG_DIRECTION_ROW))
          .ok());
  CHECK(bar.xs() == std::vector<int>{0, 40, 80});

  REQUIRE(
      dg::set_prop(bar.tree, bar.row, DG_PROP_DIRECTION, PropValue::option(DG_DIRECTION_COLUMN))
          .ok());
  CHECK(bar.ys() == std::vector<int>{0, 20, 40});
}
