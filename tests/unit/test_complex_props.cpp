// The dedicated-setter channel (design.md section 5.9.5), and the damage
// consequence of the one property it lands that paints outside its own
// declared bounds.
//
// Two independent things live in this file because they are checked by two
// independent techniques:
//
//   the CHANNEL - dg::set_image()/set_gradient()/set_shadow()/set_transform()
//   - is checked the way node_props.cpp's own boundary tests already check
//   dg::set_prop(): a rejection must be observable, must name the node, and
//   must change nothing, and prop_id validation must agree with
//   dg::prop_type() exactly as the scalar door does.
//
//   SHADOW'S DAMAGE RULE - a shadow paints outside the node's declared box,
//   which is new in this project - is checked the way test_opacity_damage.cpp
//   and test_clip_damage.cpp already check their own damage rules: two
//   independent copies of one scene, one repainting only its damage and one
//   repainting everything, required to be byte-identical after every
//   mutation. That is the "same kind of test that pins the rounded-rect rule"
//   this slice was asked to build, because a shadow's atomicity IS the
//   rounded-rect rule, generalised to a term that reaches beyond the node's
//   own box rather than merely inside it.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <utility>
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

using dg::Color;
using dg::GradientStop;
using dg::ImageStyle;
using dg::LayoutTree;
using dg::LinearGradientStyle;
using dg::NodeId;
using dg::NodeStyle;
using dg::Overflow;
using dg::PixelPoint;
using dg::PixelRect;
using dg::PixelSize;
using dg::PropStatus;
using dg::PropWrite;
using dg::RasterSurface;
using dg::RenderTree;
using dg::ShadowStyle;
using dg::TransformDesc;

// --------------------------------------------------------------------------
// The channel: id validation.
// --------------------------------------------------------------------------

dg::TreeSpec spec_of() {
  dg::TreeSpec spec;
  spec.viewport = PixelSize{200, 150};
  return spec;
}

struct Fixture {
  LayoutTree tree{spec_of()};
  NodeId leaf = tree.add_child(LayoutTree::root(), dg::BoxStyle{}, NodeStyle{});
};

LinearGradientStyle two_stop_gradient() {
  LinearGradientStyle gradient;
  gradient.angle_deg = 90.0F;
  gradient.stops = {GradientStop{0.0F, Color::from_argb(0xFFE74C3C)},
                    GradientStop{1.0F, Color::from_argb(0xFF2E86DE)}};
  return gradient;
}

ShadowStyle plain_shadow() {
  ShadowStyle shadow;
  shadow.offset_x = 2.0F;
  shadow.offset_y = 3.0F;
  shadow.blur_radius = 6.0F;
  shadow.spread = 1.0F;
  shadow.color = Color::from_argb(0xC0000000);
  return shadow;
}

TEST_CASE("each dedicated setter accepts exactly its own complex type's id") {
  Fixture fixture;

  // A bogus id is kUnknownId, exactly as the scalar door reports it - the
  // same generated kDgPropMaxId bound, not a second one this channel invented.
  CHECK(dg::set_image(fixture.tree, fixture.leaf, 0, ImageStyle{}).status ==
        PropStatus::kUnknownId);
  CHECK(dg::set_gradient(fixture.tree, fixture.leaf, kDgPropMaxId + 1, two_stop_gradient())
            .status == PropStatus::kUnknownId);

  // A REAL id of the WRONG complex type is kTypeMismatch: calling
  // set_shadow() with the gradient property's id, and vice versa.
  CHECK(dg::set_shadow(fixture.tree, fixture.leaf, DG_PROP_BACKGROUND_GRADIENT, plain_shadow())
            .status == PropStatus::kTypeMismatch);
  CHECK(dg::set_gradient(fixture.tree, fixture.leaf, DG_PROP_SHADOW, two_stop_gradient())
            .status == PropStatus::kTypeMismatch);
  CHECK(dg::set_image(fixture.tree, fixture.leaf, DG_PROP_TRANSFORM, ImageStyle{}).status ==
        PropStatus::kTypeMismatch);

  // A SCALAR property's id is also the wrong type for every complex setter.
  CHECK(
      dg::set_gradient(fixture.tree, fixture.leaf, DG_PROP_WIDTH, two_stop_gradient()).status ==
      PropStatus::kTypeMismatch);

  // The correct id for each setter is accepted.
  CHECK(dg::set_gradient(fixture.tree, fixture.leaf, DG_PROP_BACKGROUND_GRADIENT,
                         two_stop_gradient())
            .ok());
  CHECK(dg::set_shadow(fixture.tree, fixture.leaf, DG_PROP_SHADOW, plain_shadow()).ok());
  CHECK(dg::set_transform(fixture.tree, fixture.leaf, DG_PROP_TRANSFORM, TransformDesc{})
            .status == PropStatus::kUnsupported);
}

// --------------------------------------------------------------------------
// The prototype: set_image().
// --------------------------------------------------------------------------

TEST_CASE("set_image lands the style exactly as RenderTree::set_image already does") {
  Fixture fixture;

  ImageStyle image;
  image.fit = dg::ImageFit::kCover;
  image.placeholder = Color::from_argb(0xFF334455);
  const PropWrite write =
      dg::set_image(fixture.tree, fixture.leaf, DG_PROP_IMAGE_SOURCE, image);
  CHECK(write.ok());
  CHECK(fixture.tree.render().style(fixture.leaf).image.placeholder == image.placeholder);
  CHECK(fixture.tree.render().style(fixture.leaf).image.fit == dg::ImageFit::kCover);
}

// --------------------------------------------------------------------------
// Gradient value validation.
// --------------------------------------------------------------------------

TEST_CASE(
    "set_gradient refuses fewer than two stops, offsets outside 0..1, and "
    "non-increasing offsets") {
  Fixture fixture;

  LinearGradientStyle one_stop;
  one_stop.stops = {GradientStop{0.0F, Color{}}};
  CHECK(dg::set_gradient(fixture.tree, fixture.leaf, DG_PROP_BACKGROUND_GRADIENT, one_stop)
            .status == PropStatus::kValueOutOfRange);

  LinearGradientStyle out_of_range;
  out_of_range.stops = {GradientStop{-0.1F, Color{}}, GradientStop{1.1F, Color{}}};
  CHECK(dg::set_gradient(fixture.tree, fixture.leaf, DG_PROP_BACKGROUND_GRADIENT, out_of_range)
            .status == PropStatus::kValueOutOfRange);

  LinearGradientStyle not_increasing;
  not_increasing.stops = {GradientStop{0.5F, Color{}}, GradientStop{0.5F, Color{}}};
  CHECK(
      dg::set_gradient(fixture.tree, fixture.leaf, DG_PROP_BACKGROUND_GRADIENT, not_increasing)
          .status == PropStatus::kValueOutOfRange);

  // A rejection must change nothing - the node's gradient stays whatever it
  // was before any of the three bad writes above.
  CHECK_FALSE(fixture.tree.render().style(fixture.leaf).background_gradient.has_value());
}

TEST_CASE("set_gradient applies a valid multi-stop gradient") {
  Fixture fixture;
  const LinearGradientStyle gradient = two_stop_gradient();
  CHECK(
      dg::set_gradient(fixture.tree, fixture.leaf, DG_PROP_BACKGROUND_GRADIENT, gradient).ok());
  REQUIRE(fixture.tree.render().style(fixture.leaf).background_gradient.has_value());
  CHECK(fixture.tree.render().style(fixture.leaf).background_gradient.value() == gradient);
}

// --------------------------------------------------------------------------
// Shadow value validation - the budget.
// --------------------------------------------------------------------------

TEST_CASE("set_shadow refuses a blur radius past the budgeted cap, and non-finite values") {
  Fixture fixture;

  ShadowStyle too_much_blur = plain_shadow();
  too_much_blur.blur_radius = 100.0F;
  CHECK(dg::set_shadow(fixture.tree, fixture.leaf, DG_PROP_SHADOW, too_much_blur).status ==
        PropStatus::kValueOutOfRange);

  ShadowStyle negative_blur = plain_shadow();
  negative_blur.blur_radius = -1.0F;
  CHECK(dg::set_shadow(fixture.tree, fixture.leaf, DG_PROP_SHADOW, negative_blur).status ==
        PropStatus::kValueOutOfRange);

  ShadowStyle non_finite = plain_shadow();
  non_finite.offset_x = std::numeric_limits<float>::infinity();
  CHECK(dg::set_shadow(fixture.tree, fixture.leaf, DG_PROP_SHADOW, non_finite).status ==
        PropStatus::kValueOutOfRange);

  CHECK_FALSE(fixture.tree.render().style(fixture.leaf).shadow.has_value());
}

TEST_CASE("set_shadow applies a valid shadow at exactly the budget's edge") {
  Fixture fixture;
  ShadowStyle at_cap = plain_shadow();
  at_cap.blur_radius = 48.0F;
  CHECK(dg::set_shadow(fixture.tree, fixture.leaf, DG_PROP_SHADOW, at_cap).ok());
  REQUIRE(fixture.tree.render().style(fixture.leaf).shadow.has_value());
  CHECK(fixture.tree.render().style(fixture.leaf).shadow.value() == at_cap);
}

// `spread` specifically - the one shadow parameter examples/16_complex_
// properties's own byte-exact oracle does NOT exercise (its demo panel uses
// spread=0, precisely to keep the sliver's width equal to the offset alone).
// A dedicated, hard-edged (blur=0) scene closes that gap: spread grows the
// shape BEFORE the offset is applied, so the exact-colour region extends
// `spread` pixels further past the node's own box than offset alone would.
TEST_CASE("set_shadow's spread grows the exact-colour region by exactly its own amount") {
  dg::TreeSpec spec;
  spec.viewport = dg::PixelSize{160, 160};
  spec.background.fill = Color::from_argb(0xFF14171C);
  RenderTree tree{spec};

  const PixelRect box{20, 20, 60, 60};
  NodeStyle style;
  style.fill = Color::from_argb(0xFF20262F);
  const NodeId node = tree.add_child(RenderTree::root(), box, style);

  ShadowStyle shadow;
  shadow.offset_x = 0.0F;
  shadow.offset_y = 0.0F;
  shadow.blur_radius = 0.0F;
  shadow.spread = 6.0F;
  shadow.color = Color::from_argb(0xFFAA3355);
  NodeStyle shadowed_style = style;
  shadowed_style.shadow = shadow;
  tree.set_style(node, shadowed_style);

  std::optional<RasterSurface> surface = RasterSurface::create(160, 160);
  REQUIRE(surface.has_value());
  tree.repaint_full(*surface);
  const dg::PixelView view = surface->peek_pixels();

  const auto pixel = [&](int x, int y) {
    const std::size_t off =
        (static_cast<std::size_t>(y) * view.row_bytes) + (static_cast<std::size_t>(x) * 4);
    return Color::from_argb((static_cast<std::uint32_t>(view.pixels[off + 3]) << 24U) |
                            (static_cast<std::uint32_t>(view.pixels[off + 2]) << 16U) |
                            (static_cast<std::uint32_t>(view.pixels[off + 1]) << 8U) |
                            static_cast<std::uint32_t>(view.pixels[off]));
  };

  // 3px past the box's own right edge, well inside the spread-6 sliver: the
  // shadow colour, exactly.
  CHECK(pixel(box.right() + 3, box.top() + 30) == shadow.color);
  // 9px past the box's own right edge - past a spread of 6, so back to
  // background. If the painter ignored `spread` entirely this pixel would
  // already be background regardless, which is why the first assertion above
  // is the one that actually exercises the parameter.
  CHECK(pixel(box.right() + 9, box.top() + 30) == spec.background.fill);
}

// --------------------------------------------------------------------------
// transform: the fourth client, always declined, but through the SAME door.
// --------------------------------------------------------------------------

TEST_CASE("set_transform validates its id like the other three, then reports kUnsupported") {
  Fixture fixture;
  const PropWrite bad_id =
      dg::set_transform(fixture.tree, fixture.leaf, DG_PROP_SHADOW, TransformDesc{});
  CHECK(bad_id.status == PropStatus::kTypeMismatch);

  const PropWrite write =
      dg::set_transform(fixture.tree, fixture.leaf, DG_PROP_TRANSFORM, TransformDesc{});
  CHECK(write.status == PropStatus::kUnsupported);
  CHECK_FALSE(write.message.empty());
  CHECK(write.message.find("at root") != std::string::npos);
}

}  // namespace

// ============================================================================
// Shadow damage: byte-identity, the same technique test_opacity_damage.cpp
// and test_clip_damage.cpp already use.
// ============================================================================

namespace {

// Odd, matching every damage test in this project: the surface pads its
// rows, so anything that assumed a row is width * 4 bytes fails here.
constexpr int kWidth = 231;
constexpr int kHeight = 177;

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

NodeStyle filled(std::uint32_t argb) {
  NodeStyle style;
  style.fill = Color::from_argb(argb);
  return style;
}

NodeStyle shadowed(std::uint32_t argb, float blur, float offset) {
  NodeStyle style = filled(argb);
  ShadowStyle shadow;
  shadow.offset_x = offset;
  shadow.offset_y = offset;
  shadow.blur_radius = blur;
  shadow.spread = 2.0F;
  shadow.color = Color::from_argb(0xB0000000);
  style.shadow = shadow;
  return style;
}

// One shadowed node whose outset region overlaps a NEIGHBOUR, one plain
// unrelated node far away (so a change to it must NOT be grown to the
// shadow), and one rounded node (so the two atomicity rules - clip_atomic
// and shadow_atomic - coexist in the same scene, exactly as slice 5-4's task
// requires them to).
struct Handles {
  NodeId shadowed_node;
  NodeId neighbour;
  NodeId far_away;
  NodeId rounded;
};

struct Scene {
  RenderTree tree;
  Handles handles;
};

Scene build(const dg::TreeSpec& spec) {
  RenderTree tree{spec};
  Handles handles;

  handles.shadowed_node = tree.add_child(RenderTree::root(), PixelRect{20, 20, 60, 40},
                                         shadowed(0xFF3C78D8, 8.0F, 4.0F));
  handles.neighbour =
      tree.add_child(RenderTree::root(), PixelRect{90, 24, 40, 30}, filled(0xFFE8B45A));
  handles.far_away =
      tree.add_child(RenderTree::root(), PixelRect{170, 120, 40, 30}, filled(0xFF6AA84F));

  NodeStyle rounded_style = filled(0xFFCC4125);
  rounded_style.radii = dg::Radii::all(10.0F);
  handles.rounded =
      tree.add_child(RenderTree::root(), PixelRect{20, 100, 70, 50}, rounded_style);
  return Scene{std::move(tree), handles};
}

struct Pair {
  Scene damaged;
  Scene reference;
  RasterSurface damaged_surface;
  RasterSurface reference_surface;

  [[nodiscard]] bool identical() const {
    return snapshot(damaged_surface) == snapshot(reference_surface);
  }

  template <typename Body>
  void step(const Body& body) {
    body(damaged.tree, damaged.handles);
    body(reference.tree, reference.handles);
    damaged.tree.repaint(damaged_surface);
    reference.tree.repaint_full(reference_surface);
  }
};

dg::TreeSpec spec_for(std::size_t max_rects) {
  dg::TreeSpec spec;
  spec.viewport = PixelSize{kWidth, kHeight};
  spec.background = filled(0xFF14171C);
  spec.max_damage_rects = max_rects;
  return spec;
}

template <typename Body>
void with_pair(const dg::TreeSpec& spec, const Body& body) {
  std::optional<RasterSurface> damaged =
      RasterSurface::create(spec.viewport.width, spec.viewport.height);
  std::optional<RasterSurface> reference =
      RasterSurface::create(spec.viewport.width, spec.viewport.height);
  if (!damaged.has_value() || !reference.has_value()) {
    FAIL("could not allocate a raster surface");
    return;
  }
  Pair pair{build(spec), build(spec), *std::move(damaged), *std::move(reference)};
  body(pair);
}

void mutate(RenderTree& tree, const Handles& handles, int frame) {
  const auto tint = [frame](std::uint32_t base, int step) {
    return base ^ (static_cast<std::uint32_t>((frame * step) % 200) << 8U);
  };

  // The shadow itself changes shape and moves - the case that most threatens
  // byte identity, since both the fill AND the outset region move together.
  NodeStyle style = shadowed(tint(0xFF3C78D8, 5), 4.0F + static_cast<float>(frame % 5),
                             2.0F + static_cast<float>((frame * 3) % 6));
  tree.set_style(handles.shadowed_node, style);
  tree.set_local_origin(handles.shadowed_node, 20 - ((frame * 2) % 15), 20);

  // An unrelated neighbour changes colour - close enough to sit inside the
  // shadow's likely halo on some frames, which is exactly the case worth
  // exercising rather than avoiding.
  tree.set_fill(handles.neighbour, Color::from_argb(tint(0xFFE8B45A, 7)));

  // Something far away, so a defect that over-grows damage to the whole
  // viewport would still pass the two cases above but fail this one's
  // tightness check.
  tree.set_fill(handles.far_away, Color::from_argb(tint(0xFF6AA84F, 3)));

  // The pre-existing rounded-clip atomicity rule, coexisting in the same
  // scene as the new shadow rule.
  tree.set_fill(handles.rounded, Color::from_argb(tint(0xFFCC4125, 11)));
}

constexpr int kScriptFrames = 100;

void run_script(std::size_t max_rects) {
  with_pair(spec_for(max_rects), [](Pair& pair) {
    for (int frame = 0; frame < kScriptFrames; ++frame) {
      pair.step(
          [frame](RenderTree& tree, const Handles& handles) { mutate(tree, handles, frame); });
      REQUIRE_MESSAGE(pair.identical(), "frame " << frame << " of " << kScriptFrames);
    }
  });
}

// The rule itself, stated as the same shape doc/damage-repaint.md already
// states it for a rounded node: any damage rectangle touching a shadowed node
// grows to contain its OUTSET box (the node's own bounds plus the shadow's
// reach), not merely its declared bounds. The growth happens inside
// repaint()'s expand() step, so it shows up in painted() - what was actually
// repainted - rather than in the raw damage() a caller queued.
void check_damage_grows_to_the_outset_box(Pair& pair) {
  RenderTree& tree = pair.damaged.tree;
  const Handles& handles = pair.damaged.handles;
  tree.repaint(pair.damaged_surface);
  REQUIRE(tree.damage().area() == 0);

  const PixelRect declared = tree.absolute_bounds(handles.shadowed_node);
  // A single pixel right at the node's own edge is enough to trigger the
  // atomicity rule; the growth is asserted, not merely "damage is non-empty".
  tree.damage_rect(PixelRect{declared.left(), declared.top(), 1, 1});
  tree.repaint(pair.damaged_surface);
  CHECK(dg::contains(tree.painted().bounds(), declared));
  // The grown region also has to be STRICTLY larger than the declared box on
  // at least one side - otherwise this would pass even if the outset were
  // never applied at all, which is the exact gap doc/compositing.md's own
  // injection G warns a byte-identity check alone cannot see.
  CHECK(tree.painted().bounds().width > declared.width);
}

// The other half, and the one that keeps the rule from being satisfied by
// damaging everything: a change to a node FAR from the shadow must not pull
// the shadowed node's halo (or anything else) into the damage region.
void check_a_far_change_is_not_grown_into_the_shadow(Pair& pair) {
  RenderTree& tree = pair.damaged.tree;
  const Handles& handles = pair.damaged.handles;
  tree.repaint(pair.damaged_surface);
  REQUIRE(tree.damage().area() == 0);

  tree.set_fill(handles.far_away, Color::from_argb(0xFFFF00FF));
  tree.repaint(pair.damaged_surface);
  const PixelRect shadow_area = tree.absolute_bounds(handles.shadowed_node).inflated_by(30);
  CHECK_FALSE(dg::intersects(tree.painted().bounds(), shadow_area));
}

}  // namespace

TEST_SUITE("shadow damage") {
  TEST_CASE("a scene with a shadow repaints incrementally exactly as it repaints fully") {
    SUBCASE("one damage rectangle - the always-union policy") {
      run_script(1);
    }
    SUBCASE("eight damage rectangles") {
      run_script(8);
    }
  }

  // A comparison that cannot fail proves nothing - the damage side misses the
  // LAST frame on purpose, same technique as the neighbouring damage tests.
  TEST_CASE("the shadow equivalence check is armed") {
    constexpr int kFrames = 6;
    with_pair(spec_for(8), [](Pair& pair) {
      for (int frame = 0; frame < kFrames; ++frame) {
        mutate(pair.damaged.tree, pair.damaged.handles, frame);
        mutate(pair.reference.tree, pair.reference.handles, frame);
        if (frame + 1 < kFrames) {
          pair.damaged.tree.repaint(pair.damaged_surface);
        }
        pair.reference.tree.repaint_full(pair.reference_surface);
      }
      CHECK_FALSE(pair.identical());
    });
  }

  TEST_CASE("damage touching a shadowed node grows to its outset box") {
    with_pair(spec_for(8), check_damage_grows_to_the_outset_box);
  }

  TEST_CASE("a change far from the shadow is not grown into it") {
    with_pair(spec_for(8), check_a_far_change_is_not_grown_into_the_shadow);
  }
}
