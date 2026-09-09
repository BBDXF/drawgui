// What an untrusted caller can send, and what must happen to it.
//
// The property id is the first thing in this project that a HOST LANGUAGE will
// choose. Everything else - a NodeId, a PixelRect, a Color - is produced by
// drawgui and handed back; a prop_id is produced by somebody else's code,
// which may be buggy, may be a version ahead, or may be hostile. So the set of
// values that can arrive is the whole uint16_t range and the set that means
// anything is 45 of them.
//
// The notepad predicted this would be where clang-analyzer's
// EnumCastOutOfRange detonated. It measured otherwise - that checker is not
// armed in this toolchain - but two others were, and obeying either would have
// made the ABI worse. prop_ids.generated.h records the measurement. The design
// answer is that there is no enumeration and no cast: dispatch on a uint16_t
// and let the generated default arm decide. These tests are what make that
// answer more than an assertion.
//
// The rule every case below shares: a rejected write must be OBSERVABLE and
// must change NOTHING.

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/types.h"
#include "drawgui/layout/box.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/props/node_props.h"
#include "drawgui/render/prop_ids.generated.h"
#include "drawgui/render/render_tree.h"

namespace {

using dg::BoxStyle;
using dg::Color;
using dg::LayoutKind;
using dg::LayoutTree;
using dg::NodeId;
using dg::NodeStyle;
using dg::PixelSize;
using dg::PropStatus;
using dg::PropType;
using dg::PropValue;
using dg::PropWrite;

dg::TreeSpec spec_of() {
  dg::TreeSpec spec;
  spec.viewport = PixelSize{200, 150};
  return spec;
}

// A tree whose shapes cover every gate: a row (so container properties
// apply), a leaf child of that row (so `grow` has a consumer), a wrapping
// container and a child of it (so run_gap / align_content have a consumer and
// `grow` can be seen to have none), an absolute container, a child of it (so
// left/top have a consumer), and a leaf with a leaf parent (so both gates can
// be seen to REFUSE).
struct Fixture {
  LayoutTree tree{spec_of()};
  NodeId row;
  NodeId row_child;
  NodeId wrap;
  NodeId wrap_child;
  NodeId absolute;
  NodeId absolute_child;
  NodeId leaf;
  NodeId leaf_child;

  Fixture() {
    BoxStyle row_box;
    row_box.kind = LayoutKind::kRow;
    row = tree.add_child(LayoutTree::root(), row_box, NodeStyle{});
    row_child = tree.add_child(row, BoxStyle{}, NodeStyle{});

    BoxStyle wrap_box;
    wrap_box.kind = LayoutKind::kWrapRow;
    wrap = tree.add_child(LayoutTree::root(), wrap_box, NodeStyle{});
    wrap_child = tree.add_child(wrap, BoxStyle{}, NodeStyle{});

    BoxStyle absolute_box;
    absolute_box.kind = LayoutKind::kAbsolute;
    absolute = tree.add_child(LayoutTree::root(), absolute_box, NodeStyle{});
    absolute_child = tree.add_child(absolute, BoxStyle{}, NodeStyle{});

    leaf = tree.add_child(LayoutTree::root(), BoxStyle{}, NodeStyle{});
    leaf_child = tree.add_child(leaf, BoxStyle{}, NodeStyle{});
    tree.layout();
  }
};

// Everything a write could possibly have touched, so that "changed nothing"
// is checked against the whole node rather than against the field the test
// happened to think of.
struct NodeSnapshot {
  BoxStyle box;
  Color fill;
  Color border_color;
  dg::BorderWidths border_width;
  dg::Radii radii;

  friend bool operator==(const NodeSnapshot&, const NodeSnapshot&) = default;
};

NodeSnapshot snapshot_of(const LayoutTree& tree, NodeId node) {
  const NodeStyle& style = tree.render().style(node);
  return NodeSnapshot{tree.box(node), style.fill, style.border_color, style.border_width,
                      style.radii};
}

// The whole point: assert the status, assert the message names the node, and
// assert the node is untouched.
void expect_rejected(LayoutTree& tree, NodeId node, dg_prop_id prop, const PropValue& value,
                     PropStatus expected) {
  const NodeSnapshot before = snapshot_of(tree, node);
  const PropWrite write = dg::set_prop(tree, node, prop, value);
  INFO("prop id ", prop, " -> ", write.message);
  CHECK(write.status == expected);
  CHECK_FALSE(write.ok());
  CHECK_FALSE(write.message.empty());
  CHECK(write.message.find("at root") != std::string::npos);
  CHECK(snapshot_of(tree, node) == before);
}

// Kept out of the test case on purpose: inlined, the loop plus its branches
// sits over clang-tidy's cognitive-complexity threshold once doctest's macro
// expansion is counted.
std::vector<int> recognised_ids(LayoutTree& tree, NodeId node) {
  std::vector<int> known;
  for (std::uint32_t raw = 0; raw <= 0xFFFF; ++raw) {
    const auto prop = static_cast<dg_prop_id>(raw);
    const PropWrite write = dg::set_prop(tree, node, prop, PropValue::color(Color{}));
    if (write.status != PropStatus::kUnknownId) {
      known.push_back(static_cast<int>(raw));
    }
  }
  return known;
}

std::vector<int> ids_with_a_type() {
  std::vector<int> ids;
  for (std::uint32_t raw = 0; raw <= 0xFFFF; ++raw) {
    if (dg::prop_type(static_cast<dg_prop_id>(raw)).has_value()) {
      ids.push_back(static_cast<int>(raw));
    }
  }
  return ids;
}

std::vector<int> defined_ids() {
  std::vector<int> ids;
  for (std::uint32_t raw = 1; raw <= kDgPropMaxId; ++raw) {
    ids.push_back(static_cast<int>(raw));
  }
  return ids;
}

// Hoisted out of its TEST_CASE for the reason this file's neighbours already
// record: doctest expands every assertion into branches, and a body with two
// loops of them runs past clang-tidy's cognitive-complexity budget.
void check_opacity_lands(Fixture& fixture, float value) {
  CHECK(
      dg::set_prop(fixture.tree, fixture.leaf, DG_PROP_OPACITY, PropValue::number(value)).ok());
  CHECK(fixture.tree.render().style(fixture.leaf).opacity == value);
}

void check_opacity_refuses_out_of_range(Fixture& fixture) {
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();
  for (const float bad : {nan, inf, -inf, -0.01F, 1.01F, 2.0F}) {
    expect_rejected(fixture.tree, fixture.leaf, DG_PROP_OPACITY, PropValue::number(bad),
                    PropStatus::kValueOutOfRange);
  }
}

}  // namespace

// --------------------------------------------------------------------------
// Ids.
// --------------------------------------------------------------------------

// The exhaustive one. Every value a uint16_t can hold is sent, and exactly the
// 45 the table defines may be recognised.
//
// This is the case that would have caught the service-id defect the notepad
// records, where an ABI value of 0x0101 folded onto a valid enumerator because
// the id type had been narrowed to uint8_t: 0x0101 is in this sweep, and if
// anything anywhere truncated the id it would answer for property 1 (`width`)
// instead of reporting kUnknownId.
TEST_CASE("every uint16_t is either a defined property or is rejected") {
  Fixture fixture;
  const NodeSnapshot before = snapshot_of(fixture.tree, fixture.leaf);

  CHECK(recognised_ids(fixture.tree, fixture.leaf) == defined_ids());

  // Only background_color and border_color take a colour, and both were
  // applied during the sweep; everything else was refused. Restoring those two
  // is what makes "the sweep changed nothing else" a meaningful check.
  CHECK(dg::set_prop(fixture.tree, fixture.leaf, DG_PROP_BACKGROUND_COLOR,
                     PropValue::color(before.fill))
            .ok());
  CHECK(dg::set_prop(fixture.tree, fixture.leaf, DG_PROP_BORDER_COLOR,
                     PropValue::color(before.border_color))
            .ok());
  CHECK(snapshot_of(fixture.tree, fixture.leaf) == before);
}

TEST_CASE("the reserved and out-of-range ids are named, not dropped") {
  Fixture fixture;
  for (const std::uint32_t raw :
       {std::uint32_t{0}, std::uint32_t{kDgPropMaxId} + 1, std::uint32_t{0x0101},
        std::uint32_t{1000}, std::uint32_t{0xFFFF}}) {
    expect_rejected(fixture.tree, fixture.leaf, static_cast<dg_prop_id>(raw),
                    PropValue::length(10), PropStatus::kUnknownId);
  }
}

TEST_CASE("prop_type answers for exactly the ids the table defines") {
  CHECK(ids_with_a_type() == defined_ids());
}

// Spot checks rather than a full expected list: a hand-written copy of all 45
// types here would be the second table this slice exists to avoid.
TEST_CASE("prop_type reports the type the table declares") {
  CHECK(dg::prop_type(DG_PROP_WIDTH) == PropType::k_length);
  CHECK(dg::prop_type(DG_PROP_GAP) == PropType::k_float);
  CHECK(dg::prop_type(DG_PROP_BACKGROUND_COLOR) == PropType::k_color);
  CHECK(dg::prop_type(DG_PROP_DIRECTION) == PropType::k_enum);
}

TEST_CASE("prop_type reports the three types that cannot travel as a scalar") {
  CHECK(dg::prop_type(DG_PROP_SHADOW) == PropType::k_shadow);
  CHECK(dg::prop_type(DG_PROP_BACKGROUND_GRADIENT) == PropType::k_gradient);
  CHECK(dg::prop_type(DG_PROP_TRANSFORM) == PropType::k_transform);
}

// --------------------------------------------------------------------------
// Types.
// --------------------------------------------------------------------------

TEST_CASE("a value of the wrong type is refused for a real id") {
  Fixture fixture;
  const NodeId node = fixture.leaf;

  expect_rejected(fixture.tree, node, DG_PROP_WIDTH, PropValue::color(Color::rgba(1, 2, 3)),
                  PropStatus::kTypeMismatch);
  expect_rejected(fixture.tree, node, DG_PROP_BACKGROUND_COLOR, PropValue::number(12),
                  PropStatus::kTypeMismatch);
  expect_rejected(fixture.tree, node, DG_PROP_BACKGROUND_COLOR, PropValue::option(0),
                  PropStatus::kTypeMismatch);
  expect_rejected(fixture.tree, fixture.row, DG_PROP_DIRECTION, PropValue::number(1),
                  PropStatus::kTypeMismatch);

  // length and float are distinct types in the table and are kept distinct
  // here. The table went to the trouble of separating them - a length may one
  // day carry a percentage and a float never will - so accepting either for
  // both would quietly discard that distinction at the only boundary that
  // could ever enforce it.
  expect_rejected(fixture.tree, node, DG_PROP_WIDTH, PropValue::number(10),
                  PropStatus::kTypeMismatch);
  expect_rejected(fixture.tree, node, DG_PROP_PADDING_L, PropValue::length(10),
                  PropStatus::kTypeMismatch);
}

// --------------------------------------------------------------------------
// Values.
// --------------------------------------------------------------------------

// static_cast<int>(NaN) is undefined behaviour, so this is not a taste test.
// Under -DDG_SANITIZE=ON these cases are what proves the conversion is never
// reached with a value it cannot represent.
TEST_CASE("a non-finite or unrepresentable length is refused") {
  Fixture fixture;
  const NodeId node = fixture.leaf;
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();

  for (const float bad : {nan, inf, -inf, 1e30F, -1e30F, 1e9F}) {
    expect_rejected(fixture.tree, node, DG_PROP_WIDTH, PropValue::length(bad),
                    PropStatus::kValueOutOfRange);
    expect_rejected(fixture.tree, node, DG_PROP_PADDING_L, PropValue::number(bad),
                    PropStatus::kValueOutOfRange);
  }
  expect_rejected(fixture.tree, node, DG_PROP_BORDER_RADIUS_TL, PropValue::number(nan),
                  PropStatus::kValueOutOfRange);
  expect_rejected(fixture.tree, node, DG_PROP_BORDER_RADIUS_TL, PropValue::number(inf),
                  PropStatus::kValueOutOfRange);
}

TEST_CASE("a negative length is refused where the engine has no meaning for one") {
  Fixture fixture;
  expect_rejected(fixture.tree, fixture.leaf, DG_PROP_WIDTH, PropValue::length(-1),
                  PropStatus::kValueOutOfRange);
  expect_rejected(fixture.tree, fixture.leaf, DG_PROP_PADDING_T, PropValue::number(-1),
                  PropStatus::kValueOutOfRange);
  expect_rejected(fixture.tree, fixture.leaf, DG_PROP_BORDER_WIDTH_L, PropValue::number(-1),
                  PropStatus::kValueOutOfRange);
  expect_rejected(fixture.tree, fixture.leaf, DG_PROP_BORDER_RADIUS_BL, PropValue::number(-1),
                  PropStatus::kValueOutOfRange);
  expect_rejected(fixture.tree, fixture.row_child, DG_PROP_MARGIN_L, PropValue::number(-1),
                  PropStatus::kValueOutOfRange);
  expect_rejected(fixture.tree, fixture.row, DG_PROP_GAP, PropValue::number(-1),
                  PropStatus::kValueOutOfRange);

  // left/top/right/bottom DO accept a negative inset: the absolute
  // arrangement positions with it directly, so the behaviour is defined.
  CHECK(dg::set_prop(fixture.tree, fixture.absolute_child, DG_PROP_LEFT, PropValue::number(-5))
            .ok());
}

TEST_CASE("a fractional grow weight is refused rather than rounded") {
  Fixture fixture;
  expect_rejected(fixture.tree, fixture.row_child, DG_PROP_GROW, PropValue::number(0.5F),
                  PropStatus::kValueOutOfRange);
  expect_rejected(fixture.tree, fixture.row_child, DG_PROP_GROW, PropValue::number(1.5F),
                  PropStatus::kValueOutOfRange);
  expect_rejected(fixture.tree, fixture.row_child, DG_PROP_GROW, PropValue::number(-2),
                  PropStatus::kValueOutOfRange);
  CHECK(dg::set_prop(fixture.tree, fixture.row_child, DG_PROP_GROW, PropValue::number(2)).ok());
  CHECK(fixture.tree.box(fixture.row_child).grow == 2);
}

TEST_CASE("an enum ordinal outside the property's value list is refused") {
  Fixture fixture;
  expect_rejected(fixture.tree, fixture.row, DG_PROP_DIRECTION, PropValue::option(4),
                  PropStatus::kValueOutOfRange);
  expect_rejected(fixture.tree, fixture.row, DG_PROP_DIRECTION, PropValue::option(0xFFFFFFFFU),
                  PropStatus::kValueOutOfRange);
  expect_rejected(fixture.tree, fixture.row, DG_PROP_JUSTIFY, PropValue::option(6),
                  PropStatus::kValueOutOfRange);
  expect_rejected(fixture.tree, fixture.row, DG_PROP_ALIGN, PropValue::option(5),
                  PropStatus::kValueOutOfRange);
  expect_rejected(fixture.tree, fixture.leaf, DG_PROP_OVERFLOW, PropValue::option(2),
                  PropStatus::kValueOutOfRange);
}

// `overflow` moved out of the refusal list above with this slice, so both of
// its ordinals are asserted to land rather than merely to stop being refused -
// "no longer kUnsupported" is also satisfied by a handler that applies the
// wrong one.
TEST_CASE("both overflow ordinals reach the node style") {
  Fixture fixture;
  CHECK(dg::set_prop(fixture.tree, fixture.leaf, DG_PROP_OVERFLOW,
                     PropValue::option(DG_OVERFLOW_CLIP))
            .ok());
  CHECK(fixture.tree.render().style(fixture.leaf).overflow == dg::Overflow::kClip);
  CHECK(dg::set_prop(fixture.tree, fixture.leaf, DG_PROP_OVERFLOW,
                     PropValue::option(DG_OVERFLOW_VISIBLE))
            .ok());
  CHECK(fixture.tree.render().style(fixture.leaf).overflow == dg::Overflow::kVisible);
}

// --------------------------------------------------------------------------
// applies_to and consumed_by.
// --------------------------------------------------------------------------

TEST_CASE("a container property on a node that arranges nothing is refused") {
  Fixture fixture;
  for (const dg_prop_id prop : {DG_PROP_DIRECTION, DG_PROP_JUSTIFY, DG_PROP_ALIGN}) {
    expect_rejected(fixture.tree, fixture.leaf, prop, PropValue::option(0),
                    PropStatus::kNotApplicable);
    expect_rejected(fixture.tree, fixture.absolute, prop, PropValue::option(0),
                    PropStatus::kNotApplicable);
  }
  expect_rejected(fixture.tree, fixture.leaf, DG_PROP_GAP, PropValue::number(4),
                  PropStatus::kNotApplicable);
}

// run_gap and align_content are the two the table gives to `wrap` ALONE, so a
// plain row is the shape that must refuse them. Before this slice both were
// kUnsupported everywhere, which is a different sentence: the engine now has
// runs, and a row simply does not have any.
TEST_CASE("a wrap-only container property on a flex row is refused") {
  Fixture fixture;
  for (const NodeId node : {fixture.row, fixture.leaf, fixture.absolute}) {
    expect_rejected(fixture.tree, node, DG_PROP_RUN_GAP, PropValue::number(4),
                    PropStatus::kNotApplicable);
    expect_rejected(fixture.tree, node, DG_PROP_ALIGN_CONTENT,
                    PropValue::option(DG_ALIGN_CONTENT_CENTER), PropStatus::kNotApplicable);
  }
  CHECK(dg::set_prop(fixture.tree, fixture.wrap, DG_PROP_RUN_GAP, PropValue::number(4)).ok());
  CHECK(dg::set_prop(fixture.tree, fixture.wrap, DG_PROP_ALIGN_CONTENT,
                     PropValue::option(DG_ALIGN_CONTENT_CENTER))
            .ok());
}

// The two parentData properties whose consumers differ, which is the whole
// reason they need separate gates: a wrapping container aligns a child on the
// cross axis exactly as a flex does, and distributes no free space at all.
TEST_CASE("a wrapping parent consumes align_self and refuses grow") {
  Fixture fixture;
  CHECK(dg::set_prop(fixture.tree, fixture.wrap_child, DG_PROP_ALIGN_SELF,
                     PropValue::option(DG_ALIGN_SELF_CENTER))
            .ok());
  expect_rejected(fixture.tree, fixture.wrap_child, DG_PROP_GROW, PropValue::number(1),
                  PropStatus::kNotApplicable);

  CHECK(dg::set_prop(fixture.tree, fixture.row_child, DG_PROP_ALIGN_SELF,
                     PropValue::option(DG_ALIGN_SELF_CENTER))
            .ok());
  expect_rejected(fixture.tree, fixture.leaf_child, DG_PROP_ALIGN_SELF,
                  PropValue::option(DG_ALIGN_SELF_CENTER), PropStatus::kNotApplicable);
  expect_rejected(fixture.tree, fixture.absolute_child, DG_PROP_ALIGN_SELF,
                  PropValue::option(DG_ALIGN_SELF_CENTER), PropStatus::kNotApplicable);
}

TEST_CASE("a parentData property whose parent does not consume it is refused") {
  Fixture fixture;

  // grow is consumed by a row or a column. leaf_child's parent is a leaf and
  // absolute_child's parent arranges absolutely; neither reads a weight.
  expect_rejected(fixture.tree, fixture.leaf_child, DG_PROP_GROW, PropValue::number(1),
                  PropStatus::kNotApplicable);
  expect_rejected(fixture.tree, fixture.absolute_child, DG_PROP_GROW, PropValue::number(1),
                  PropStatus::kNotApplicable);

  // left/top/right/bottom are consumed only by an absolute parent.
  for (const dg_prop_id prop : {DG_PROP_LEFT, DG_PROP_TOP, DG_PROP_RIGHT, DG_PROP_BOTTOM}) {
    expect_rejected(fixture.tree, fixture.row_child, prop, PropValue::number(3),
                    PropStatus::kNotApplicable);
    expect_rejected(fixture.tree, fixture.leaf_child, prop, PropValue::number(3),
                    PropStatus::kNotApplicable);
  }

  // The root has no parent, so nothing can consume its parentData.
  expect_rejected(fixture.tree, LayoutTree::root(), DG_PROP_GROW, PropValue::number(1),
                  PropStatus::kNotApplicable);
  expect_rejected(fixture.tree, LayoutTree::root(), DG_PROP_LEFT, PropValue::number(1),
                  PropStatus::kNotApplicable);

  // margin is parentData too, but its scope is `base`: every container
  // applies it, including a leaf placing its children, so it is never refused
  // for want of a consumer.
  CHECK(dg::set_prop(fixture.tree, fixture.leaf_child, DG_PROP_MARGIN_L, PropValue::number(3))
            .ok());
  CHECK(
      dg::set_prop(fixture.tree, fixture.absolute_child, DG_PROP_MARGIN_T, PropValue::number(3))
          .ok());
}

// --------------------------------------------------------------------------
// Honest refusals.
// --------------------------------------------------------------------------

// kUnsupported is a statement about drawgui rather than about the caller, and
// it is separated from kNotApplicable for that reason: the same write becomes
// kApplied when a later slice lands the capability, whereas `gap` on a leaf
// will always be wrong. doc/properties.md is the list.
TEST_CASE("a property the engine does not implement says so") {
  Fixture fixture;
  const auto refuse = [&fixture](NodeId node, dg_prop_id prop, const PropValue& value) {
    expect_rejected(fixture.tree, node, prop, value, PropStatus::kUnsupported);
  };

  refuse(fixture.row_child, DG_PROP_ALIGN_SELF, PropValue::option(DG_ALIGN_SELF_BASELINE));

  // The three complex types cannot travel in the scalar union at all
  // (design.md section 5.9.5), and none of the three has a dedicated setter
  // yet because none is implemented.
  refuse(fixture.leaf, DG_PROP_BACKGROUND_GRADIENT, PropValue::number(0));
  refuse(fixture.leaf, DG_PROP_SHADOW, PropValue::number(0));
  refuse(fixture.leaf, DG_PROP_TRANSFORM, PropValue::number(0));
}

// The four that moved out of the refusal list above with this slice. Asserted
// to LAND on the field they name rather than merely to stop being refused -
// "no longer kUnsupported" is equally satisfied by a handler that writes the
// wrong member, which is the trap the overflow case above already names.
TEST_CASE("the second sizing stage's four properties reach the box style") {
  Fixture fixture;

  CHECK(dg::set_prop(fixture.tree, fixture.leaf, DG_PROP_ASPECT_RATIO, PropValue::number(1.5F))
            .ok());
  REQUIRE(fixture.tree.box(fixture.leaf).aspect_ratio.has_value());
  CHECK(static_cast<double>(fixture.tree.box(fixture.leaf).aspect_ratio.value_or(0.0F)) ==
        doctest::Approx(1.5));

  CHECK(dg::set_prop(fixture.tree, fixture.row, DG_PROP_MAIN_SIZE,
                     PropValue::option(DG_MAIN_SIZE_MAX))
            .ok());
  CHECK(fixture.tree.box(fixture.row).main_size == dg::MainSize::kMax);
  CHECK(dg::set_prop(fixture.tree, fixture.row, DG_PROP_MAIN_SIZE,
                     PropValue::option(DG_MAIN_SIZE_MIN))
            .ok());
  CHECK(fixture.tree.box(fixture.row).main_size == dg::MainSize::kMin);

  CHECK(
      dg::set_prop(fixture.tree, fixture.row_child, DG_PROP_SHRINK, PropValue::number(3)).ok());
  CHECK(fixture.tree.box(fixture.row_child).shrink == 3);

  CHECK(
      dg::set_prop(fixture.tree, fixture.row_child, DG_PROP_BASIS, PropValue::number(40)).ok());
  CHECK(fixture.tree.box(fixture.row_child).basis == 40);
}

// The ratio is neither a length nor a fraction, so it has a bound of its own
// on BOTH sides - a ratio near zero derives an extent as unusable as one near
// infinity does, and w/h and h/w have to be equally expressible.
TEST_CASE("aspect_ratio refuses a ratio with no geometry") {
  Fixture fixture;
  for (const float bad : {0.0F, -1.5F, std::numeric_limits<float>::infinity(),
                          std::numeric_limits<float>::quiet_NaN(), 1.0e30F, 1.0e-30F}) {
    expect_rejected(fixture.tree, fixture.leaf, DG_PROP_ASPECT_RATIO, PropValue::number(bad),
                    PropStatus::kValueOutOfRange);
  }
  CHECK(dg::set_prop(fixture.tree, fixture.leaf, DG_PROP_ASPECT_RATIO,
                     PropValue::number(1.0F / 16777216.0F))
            .ok());
  CHECK(dg::set_prop(fixture.tree, fixture.leaf, DG_PROP_ASPECT_RATIO,
                     PropValue::number(16777216.0F))
            .ok());
}

// shrink is a weight, exactly as grow is, and a fractional one is refused for
// the same reason: the deficit split is exact integer division.
TEST_CASE("shrink refuses a fractional weight and a parent that hands out no space") {
  Fixture fixture;
  expect_rejected(fixture.tree, fixture.row_child, DG_PROP_SHRINK, PropValue::number(0.5F),
                  PropStatus::kValueOutOfRange);
  expect_rejected(fixture.tree, fixture.row_child, DG_PROP_SHRINK, PropValue::number(-1.0F),
                  PropStatus::kValueOutOfRange);

  // Both are consumed_by = ["flex"] in the table, exactly as grow is, so a
  // wrapping parent refuses them at the boundary rather than at layout time.
  for (const dg_prop_id prop : {DG_PROP_SHRINK, DG_PROP_BASIS}) {
    expect_rejected(fixture.tree, fixture.wrap_child, prop, PropValue::number(1),
                    PropStatus::kNotApplicable);
    expect_rejected(fixture.tree, fixture.leaf_child, prop, PropValue::number(1),
                    PropStatus::kNotApplicable);
    expect_rejected(fixture.tree, fixture.absolute_child, prop, PropValue::number(1),
                    PropStatus::kNotApplicable);
    expect_rejected(fixture.tree, LayoutTree::root(), prop, PropValue::number(1),
                    PropStatus::kNotApplicable);
  }
}

// main_size is applies_to = ["flex", "wrap"], so it is the same gate `gap`
// takes, and a node that arranges nothing refuses it.
TEST_CASE("main_size is refused on a node that arranges nothing") {
  Fixture fixture;
  for (const NodeId node : {fixture.leaf, fixture.absolute}) {
    expect_rejected(fixture.tree, node, DG_PROP_MAIN_SIZE, PropValue::option(DG_MAIN_SIZE_MAX),
                    PropStatus::kNotApplicable);
  }
  CHECK(dg::set_prop(fixture.tree, fixture.wrap, DG_PROP_MAIN_SIZE,
                     PropValue::option(DG_MAIN_SIZE_MAX))
            .ok());
  expect_rejected(fixture.tree, fixture.row, DG_PROP_MAIN_SIZE, PropValue::option(2),
                  PropStatus::kValueOutOfRange);
}

// Kept out of the test case for the same reason recognised_ids() is: the
// three repeats plus doctest's own macro expansion push the loop over
// clang-tidy's cognitive-complexity threshold once inlined.
void check_scroll_axis_lands(Fixture& fixture, std::uint32_t ordinal, dg::ScrollAxis expected) {
  CHECK(
      dg::set_prop(fixture.tree, fixture.leaf, DG_PROP_SCROLL_AXIS, PropValue::option(ordinal))
          .ok());
  CHECK(fixture.tree.box(fixture.leaf).scroll_axis == expected);
}

// scroll_axis is applies_to = ["box"], which this table's "box" is the engine
// side's kLeaf - the opposite gate main_size takes, since a leaf is exactly
// the node that has no arrangement of its own to conflict with a scroll axis.
TEST_CASE("scroll_axis reaches the box style on a leaf, and is refused where a node arranges") {
  Fixture fixture;
  for (const NodeId node : {fixture.row, fixture.wrap, fixture.absolute}) {
    expect_rejected(fixture.tree, node, DG_PROP_SCROLL_AXIS,
                    PropValue::option(DG_SCROLL_AXIS_VERTICAL), PropStatus::kNotApplicable);
  }

  check_scroll_axis_lands(fixture, DG_SCROLL_AXIS_VERTICAL, dg::ScrollAxis::kVertical);
  check_scroll_axis_lands(fixture, DG_SCROLL_AXIS_HORIZONTAL, dg::ScrollAxis::kHorizontal);
  check_scroll_axis_lands(fixture, DG_SCROLL_AXIS_NONE, dg::ScrollAxis::kNone);

  expect_rejected(fixture.tree, fixture.leaf, DG_PROP_SCROLL_AXIS, PropValue::option(3),
                  PropStatus::kValueOutOfRange);
}

// `opacity` moved out of the refusal list with this slice, so the value has to
// be asserted to LAND rather than merely to stop being refused - "no longer
// kUnsupported" is equally satisfied by a handler that writes the wrong
// number, or writes it and then forgets to commit the style.
//
// Both ends of the range are refused rather than clamped, and both are
// checked: an easing curve that overshoots produces 1.02 at one end and -0.02
// at the other, and a clamp would answer kApplied to both.
TEST_CASE("opacity reaches the node style, and out of 0..1 is refused") {
  Fixture fixture;

  // Both ENDS as well as the middle: 0 and 1 are the two values the painter
  // treats specially, so a range check that stopped short of them would leave
  // the two branches that matter untested.
  for (const float value : {0.25F, 0.0F, 1.0F}) {
    check_opacity_lands(fixture, value);
  }
  check_opacity_refuses_out_of_range(fixture);

  // The last accepted write was 1.0, and a rejection must not have moved it.
  CHECK(fixture.tree.render().style(fixture.leaf).opacity == 1.0F);
}

// The values a wired property has that the arrangement does not, which is a
// different statement from "the property is unimplemented".
TEST_CASE("an enum value the arrangement lacks is refused separately from a bad ordinal") {
  Fixture fixture;
  expect_rejected(fixture.tree, fixture.row, DG_PROP_DIRECTION,
                  PropValue::option(DG_DIRECTION_ROW_REVERSE), PropStatus::kUnsupported);
  expect_rejected(fixture.tree, fixture.row, DG_PROP_DIRECTION,
                  PropValue::option(DG_DIRECTION_COLUMN_REVERSE), PropStatus::kUnsupported);
  expect_rejected(fixture.tree, fixture.row, DG_PROP_JUSTIFY,
                  PropValue::option(DG_JUSTIFY_SPACE_AROUND), PropStatus::kUnsupported);
  expect_rejected(fixture.tree, fixture.row, DG_PROP_JUSTIFY,
                  PropValue::option(DG_JUSTIFY_SPACE_EVENLY), PropStatus::kUnsupported);
  expect_rejected(fixture.tree, fixture.row, DG_PROP_ALIGN,
                  PropValue::option(DG_ALIGN_BASELINE), PropStatus::kUnsupported);
}
