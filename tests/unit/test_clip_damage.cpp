// Partial repaint, with clips in the scene.
//
// test_damage_repaint.cpp asks this question of the demo scene, which has no
// clip in it, so none of its frames can answer it. A clip changes the damage
// system in two ways that could each break the equivalence on their own:
//
//   a clipped node no longer paints where it says it does, so the damage it
//   raises has to be intersected with the clip - and an intersection that is
//   one pixel too small leaves a stale pixel that only a partial repaint
//   shows;
//
//   a rounded clip is not clip-invariant, measured (see tree_paint.cpp), so a
//   damage rectangle that cut one would produce different pixels inside
//   itself than a full repaint produces at the same coordinates.
//
// Same technique as next door and for the same reason: two independent copies
// of one scene, one repainting only its damage and one repainting everything,
// required to be byte-identical after every mutation. The mutations here are
// the ones a clip makes possible - a change under a clip, a child crossing the
// boundary, and a change to the clip itself.
//
// What this CANNOT check is whether the clip is in the right place; it drives
// two copies of the same code, so a wrong clip corrupts both equally.
// test_clip.cpp is where the geometry is pinned.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/graphics/types.h"
#include "drawgui/render/render_tree.h"

namespace {

using dg::Color;
using dg::NodeId;
using dg::Overflow;
using dg::PixelRect;
using dg::PixelSize;
using dg::RasterSurface;
using dg::RenderTree;

// Odd on purpose, as next door: the surface pads its rows, so anything that
// assumed a row is width * 4 bytes fails here rather than elsewhere.
constexpr int kWidth = 241;
constexpr int kHeight = 181;

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

dg::NodeStyle styled(std::uint32_t argb, Overflow overflow = Overflow::kVisible,
                     float radius = 0.0F) {
  dg::NodeStyle style;
  style.fill = Color::from_argb(argb);
  style.overflow = overflow;
  style.radii = dg::Radii::all(radius);
  return style;
}

// Every shape the clip code branches on, in one scene, so that one script
// exercises all of them rather than one per test:
//
//   square    a square clip with children cut by it and one outside it
//   round     a ROUNDED clip, which is the clip-invariance case
//   nested    a clip inside a clip
//   loose     an unclipped subtree overlapping the others, so a defect that
//             simply stopped painting after a clip is visible
struct Handles {
  NodeId square;
  NodeId square_cut;
  NodeId square_gone;
  NodeId round;
  NodeId round_cut;
  NodeId nested_outer;
  NodeId nested_inner;
  NodeId nested_leaf;
  NodeId loose;
};

struct Scene {
  RenderTree tree;
  Handles handles;
};

Scene build(const dg::TreeSpec& spec) {
  RenderTree tree{spec};
  Handles handles;

  handles.square = tree.add_child(RenderTree::root(), PixelRect{15, 15, 80, 60},
                                  styled(0xFF2C3644, Overflow::kClip));
  handles.square_cut =
      tree.add_child(handles.square, PixelRect{30, 20, 90, 30}, styled(0xFF3C78D8));
  handles.square_gone =
      tree.add_child(handles.square, PixelRect{95, 5, 30, 20}, styled(0xFF6AA84F));

  handles.round = tree.add_child(RenderTree::root(), PixelRect{120, 15, 90, 60},
                                 styled(0xFF44343C, Overflow::kClip, 14.0F));
  handles.round_cut =
      tree.add_child(handles.round, PixelRect{-15, 20, 130, 28}, styled(0xFFE8B45A));

  handles.nested_outer = tree.add_child(RenderTree::root(), PixelRect{20, 95, 90, 60},
                                        styled(0xFF243040, Overflow::kClip));
  handles.nested_inner = tree.add_child(handles.nested_outer, PixelRect{40, 10, 80, 60},
                                        styled(0xFF35506E, Overflow::kClip, 9.0F));
  handles.nested_leaf =
      tree.add_child(handles.nested_inner, PixelRect{-25, -5, 120, 50}, styled(0xFFCC4125));

  handles.loose =
      tree.add_child(RenderTree::root(), PixelRect{100, 60, 55, 55}, styled(0xFF8E7CC3));
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

  // One mutation applied to both copies, then one incremental repaint against
  // one full one. Taking a member function pointer would not do: the
  // mutations differ in their arguments, so the caller passes a body that is
  // handed a tree and its handles.
  template <typename Body>
  void step(const Body& body) {
    body(damaged.tree, damaged.handles);
    body(reference.tree, reference.handles);
    damaged.tree.repaint(damaged_surface);
    reference.tree.repaint_full(reference_surface);
  }
};

dg::TreeSpec spec_for(std::size_t max_rects, dg::PaintMode mode) {
  dg::TreeSpec spec;
  spec.viewport = PixelSize{kWidth, kHeight};
  spec.background = styled(0xFF14171C);
  spec.max_damage_rects = max_rects;
  spec.paint_mode = mode;
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

// The mutations, each one a class of change a clip makes possible.
//
// `frame` drives them so that one script covers every class repeatedly and
// at several offsets, rather than each being probed once at a position that
// might happen to be benign.
void mutate(RenderTree& tree, const Handles& handles, int frame) {
  const auto tint = [frame](std::uint32_t base, int step) {
    return base ^ (static_cast<std::uint32_t>((frame * step) % 200) << 8U);
  };

  // A change strictly INSIDE a clip, which is the case whose damage must not
  // escape it.
  tree.set_fill(handles.square_cut, Color::from_argb(tint(0xFF3C78D8, 7)));
  tree.set_fill(handles.round_cut, Color::from_argb(tint(0xFFE8B45A, 11)));
  tree.set_fill(handles.nested_leaf, Color::from_argb(tint(0xFFCC4125, 5)));

  // A child crossing the clip boundary, so that pixels enter and leave the
  // visible region on consecutive frames.
  tree.set_local_origin(handles.square_cut, 30 - ((frame * 3) % 70), 20);
  tree.set_local_origin(handles.round_cut, -15 + ((frame * 5) % 60), 20);

  // A node that is entirely clipped away moving around behind the clip. Its
  // damage must be empty, and a repaint driven by it must change nothing.
  tree.set_local_origin(handles.square_gone, 95 + (frame % 9), 5 + (frame % 7));

  // The CLIP ITSELF changing, in the three ways it can: the shape turning on
  // and off, its radii, and its box.
  dg::NodeStyle square = tree.style(handles.square);
  square.overflow = (frame % 12) < 6 ? Overflow::kClip : Overflow::kVisible;
  tree.set_style(handles.square, square);

  dg::NodeStyle round = tree.style(handles.round);
  round.radii = dg::Radii::all(static_cast<float>(4 + ((frame * 3) % 22)));
  tree.set_style(handles.round, round);

  tree.set_local_bounds(handles.nested_inner, PixelRect{40 - ((frame * 4) % 50), 10, 80, 60});

  tree.set_fill(handles.loose, Color::from_argb(tint(0xFF8E7CC3, 3)));
}

constexpr int kScriptFrames = 120;

void run_script(const dg::TreeSpec& spec) {
  with_pair(spec, [](Pair& pair) {
    for (int frame = 0; frame < kScriptFrames; ++frame) {
      pair.step(
          [frame](RenderTree& tree, const Handles& handles) { mutate(tree, handles, frame); });
      REQUIRE_MESSAGE(pair.identical(), "frame " << frame << " of " << kScriptFrames);
    }
  });
}

// The bodies of the damage-tightness cases, as named functions.
//
// Hoisted out of their TEST_CASEs because doctest expands each assertion into
// branches and a body of them sits over clang-tidy's cognitive-complexity
// threshold - the same reason test_props_boundary.cpp and test_text_damage.cpp
// hoist theirs. Naming them also makes a failure report say which claim broke.
void check_damage_inside_clip(Pair& pair) {
  RenderTree& tree = pair.damaged.tree;
  const Handles& handles = pair.damaged.handles;
  tree.repaint(pair.damaged_surface);
  REQUIRE(tree.damage().area() == 0);

  // The child overruns the clip by a wide margin, so an unintersected damage
  // rectangle would be visibly larger than the clipper.
  const PixelRect clipper = tree.absolute_bounds(handles.square);
  const PixelRect child = tree.absolute_bounds(handles.square_cut);
  REQUIRE(child.right() > clipper.right());

  tree.set_fill(handles.square_cut, Color::from_argb(0xFFFF0000));
  CHECK(dg::contains(clipper, tree.damage().bounds()));
  CHECK(tree.damage().bounds().right() == clipper.right());
}

void check_removed_node_is_silent(Pair& pair) {
  RenderTree& tree = pair.damaged.tree;
  const Handles& handles = pair.damaged.handles;
  tree.repaint(pair.damaged_surface);
  REQUIRE(tree.damage().area() == 0);

  const PixelRect clipper = tree.absolute_bounds(handles.square);
  const PixelRect hidden = tree.absolute_bounds(handles.square_gone);
  REQUIRE_FALSE(dg::intersects(clipper, hidden));

  tree.set_fill(handles.square_gone, Color::from_argb(0xFFFF0000));
  CHECK(tree.damage().area() == 0);
}

void check_nested_damage_intersects(Pair& pair) {
  RenderTree& tree = pair.damaged.tree;
  const Handles& handles = pair.damaged.handles;
  tree.repaint(pair.damaged_surface);
  REQUIRE(tree.damage().area() == 0);

  const PixelRect outer = tree.absolute_bounds(handles.nested_outer);
  const PixelRect inner = tree.absolute_bounds(handles.nested_inner);
  const PixelRect leaf = tree.absolute_bounds(handles.nested_leaf);
  REQUIRE(inner.right() > outer.right());
  REQUIRE(leaf.right() > inner.right());

  tree.set_fill(handles.nested_leaf, Color::from_argb(0xFFFF0000));
  const PixelRect damaged = tree.damage().bounds();

  // Reported against the OUTER edge because that is the one only an
  // intersection can enforce - the innermost clip alone would allow damage
  // out to the inner box's right edge, which is outside the outer clip.
  CHECK(dg::contains(dg::intersect(outer, inner), damaged));
  CHECK(damaged.right() <= outer.right());
}

void check_closing_a_clip_damages_the_overflow(Pair& pair) {
  RenderTree& tree = pair.damaged.tree;
  const Handles& handles = pair.damaged.handles;

  dg::NodeStyle open = tree.style(handles.square);
  open.overflow = Overflow::kVisible;
  tree.set_style(handles.square, open);
  tree.repaint(pair.damaged_surface);
  REQUIRE(tree.damage().area() == 0);

  const PixelRect clipper = tree.absolute_bounds(handles.square);
  const PixelRect child = tree.absolute_bounds(handles.square_cut);
  REQUIRE(child.right() > clipper.right());

  dg::NodeStyle shut = tree.style(handles.square);
  shut.overflow = Overflow::kClip;
  tree.set_style(handles.square, shut);

  // The pixels between the clipper's right edge and the child's are the ones
  // that just became invisible, and they must be in the damage.
  CHECK(tree.damage().bounds().right() >= child.right());
}

// A rounded node parked outside a clip, and an ordinary neighbour whose box
// overlaps where it would have been.
//
// Split from the assertions below only so that each function stays inside
// clang-tidy's cognitive-complexity budget; the arrangement is the whole
// point of the case and reads better named anyway.
struct HiddenRounded {
  RenderTree tree;
  NodeId clipper;
  NodeId hidden;
  NodeId neighbour;
};

HiddenRounded build_hidden_rounded(const dg::TreeSpec& spec) {
  RenderTree tree{spec};
  const NodeId clipper = tree.add_child(RenderTree::root(), PixelRect{10, 10, 60, 40},
                                        styled(0xFF2C3644, Overflow::kClip));
  dg::NodeStyle rounded = styled(0xFF3C78D8);
  rounded.radii = dg::Radii::all(6.0F);
  const NodeId hidden = tree.add_child(clipper, PixelRect{70, 0, 40, 40}, rounded);
  const NodeId neighbour =
      tree.add_child(RenderTree::root(), PixelRect{85, 15, 20, 20}, styled(0xFF6AA84F));

  // The arrangement is asserted where it is built, so that a future edit to
  // these coordinates fails as "the scene stopped having the shape" rather
  // than as a mysterious damage figure.
  REQUIRE_FALSE(dg::intersects(tree.absolute_bounds(clipper), tree.absolute_bounds(hidden)));
  REQUIRE(dg::intersects(tree.absolute_bounds(neighbour), tree.absolute_bounds(hidden)));
  return HiddenRounded{std::move(tree), clipper, hidden, neighbour};
}

void check_hidden_rounded_node_does_not_inflate() {
  const dg::TreeSpec spec = spec_for(8, dg::PaintMode::kDirect);
  std::optional<RasterSurface> surface =
      RasterSurface::create(spec.viewport.width, spec.viewport.height);
  if (!surface.has_value()) {
    FAIL("could not allocate a raster surface");
    return;
  }
  HiddenRounded built = build_hidden_rounded(spec);
  RenderTree& tree = built.tree;

  tree.repaint(*surface);
  REQUIRE(tree.damage().area() == 0);

  tree.set_fill(built.neighbour, Color::from_argb(0xFFFF0000));
  const PixelRect asked = tree.absolute_bounds(built.neighbour);
  tree.repaint(*surface);

  // The hidden node is 40 wide against the neighbour's 20; swallowing it
  // would more than double the rectangle. One pixel of halo is allowed
  // because a repaint is free to grow for a node that IS visible.
  CHECK(tree.painted().bounds().width <= asked.width + 2);
}

}  // namespace

TEST_SUITE("clipping and damage") {
  TEST_CASE("a clipped scene repaints incrementally exactly as it repaints fully") {
    SUBCASE("one damage rectangle - the always-union policy") {
      run_script(spec_for(1, dg::PaintMode::kDirect));
    }
    SUBCASE("eight damage rectangles") {
      run_script(spec_for(8, dg::PaintMode::kDirect));
    }
    SUBCASE("replaying a recorded picture under the clip") {
      run_script(spec_for(8, dg::PaintMode::kPicture));
    }
  }

  // A comparison that cannot fail proves nothing. The damage side misses the
  // LAST frame, which is the only frame that can be skipped visibly - damage
  // accumulates, so a dropped middle frame is caught up by the next repaint.
  TEST_CASE("the clipped equivalence check is armed") {
    constexpr int kFrames = 8;
    with_pair(spec_for(8, dg::PaintMode::kDirect), [](Pair& pair) {
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

  // Byte identity is satisfied by damaging too MUCH, so the other half of the
  // requirement needs its own assertion: a change under a clip must not ask
  // for a repaint of pixels the clip removes. Stated against the clipper's
  // own box, which is the largest region the change can legitimately reach.
  //
  // The square clip is used because a rounded one is clip-atomic and its
  // damage is deliberately grown to its whole box plus a pixel of halo - that
  // expansion is measured and necessary, and asserting containment against it
  // would be asserting the halo away.
  TEST_CASE("damage from a change under a clip stays inside the clip") {
    with_pair(spec_for(8, dg::PaintMode::kDirect), check_damage_inside_clip);
  }

  // A node an ancestor clip removes entirely has no pixels, so changing it
  // must ask for no repaint at all. Without the intersection this raises the
  // node's whole box - a repaint, and a present, of a region the user cannot
  // see.
  TEST_CASE("a change to a node the clip removes raises no damage") {
    with_pair(spec_for(8, dg::PaintMode::kDirect), check_removed_node_is_silent);
  }

  // NESTED clips have to tighten damage TOGETHER, and this needed its own
  // case: taking the innermost ancestor clip instead of intersecting all of
  // them survived every other test in this slice. It could, because
  // `clip_bounds` is not what makes the picture right - the canvas clip stack
  // intersects correctly on its own, and hit testing walks the ancestors
  // itself - so a wrong value here only makes damage too LARGE, which byte
  // identity is blind to by construction.
  //
  // That is the third diagnosis this project has recorded for a surviving
  // injection: the defect was aimed at a term two other mechanisms already
  // cover, so the assertion has to be about the term's own job, which is
  // tightness.
  //
  // The geometry is chosen so the intersection is strictly smaller than the
  // inner clip: the inner box reaches past the outer one on the right, and
  // the leaf reaches past both.
  TEST_CASE("damage under two clips is bounded by their intersection, not the innermost") {
    with_pair(spec_for(8, dg::PaintMode::kDirect), check_nested_damage_intersects);
  }

  // A node an ancestor clip has removed entirely cannot be cut by a damage
  // rectangle, because it has no pixels to cut - so it must not drag one
  // wider. Only a ROUNDED node can, which is why this case builds one: a
  // square node is never clip-atomic and would prove nothing.
  //
  // This also survived everything else, for a related reason: growing damage
  // is never INCORRECT, so no pixel comparison can object. Only a statement
  // about the size can.
  TEST_CASE("a rounded node the clip removes does not inflate a damage rectangle") {
    check_hidden_rounded_node_does_not_inflate();
  }

  // Turning a clip ON hides pixels that were painted outside it, and those
  // pixels are only reachable from the state BEFORE the change. Damaging
  // after the update would ask about a region the new clip has already
  // shrunk, and the overflow would stay on screen forever.
  TEST_CASE("turning a clip on damages the overflow it just hid") {
    with_pair(spec_for(8, dg::PaintMode::kDirect), check_closing_a_clip_damages_the_overflow);
  }
}
