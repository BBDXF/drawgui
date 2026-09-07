// Partial repaint, with compositing layers in the scene.
//
// A layer is the one thing in this engine that draws a subtree somewhere other
// than the framebuffer and then copies it back, so it is the obvious candidate
// for a damage rule of its own: "a layer is composited as a unit, therefore a
// change anywhere inside it recomposites the whole layer" is what a rounded
// clip already forces, and it is what a shadow or a transform WILL force.
//
// It is not what a scalar alpha forces, and that is the finding this file
// exists to establish rather than assume. Compositing a layer at alpha `a`
// computes, for each pixel independently,
//
//     out(p) = layer(p) * a + backdrop(p) * (1 - a * layerAlpha(p))
//
// with no term from any other pixel. So the picture decomposes by pixel
// exactly as an ordinary draw does, and a damage rectangle inside a layer
// produces the same bytes as a full repaint - PROVIDED the content inside the
// layer is itself clip-invariant, which is the rule this engine already has
// (see NodeStyle::radii and kAntiAliasSlack) and already enforces through
// clip_atomic. What would break the decomposition is a term that reads
// NEIGHBOURING pixels - a blur, a drop shadow, a non-axis-aligned transform -
// and none of those exists yet.
//
// So: no new damage rule, and this file is the measurement that says so. Same
// technique as test_clip_damage.cpp next door - two independent copies of one
// scene, one repainting only its damage and one repainting everything,
// required to be byte-identical after every mutation.
//
// Byte identity alone would also be satisfied by damaging too MUCH, so two
// TIGHTNESS cases sit at the bottom: a change inside a layer must NOT be grown
// to the layer, and a change to the opacity itself MUST cover the whole
// subtree, because at that moment every pixel of it really does change.

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

// Odd, as in the neighbouring damage tests: the surface pads its rows, so
// anything that assumed a row is width * 4 bytes fails here rather than later.
constexpr int kWidth = 251;
constexpr int kHeight = 187;

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

dg::NodeStyle styled(std::uint32_t argb, float opacity = 1.0F, float radius = 0.0F,
                     Overflow overflow = Overflow::kVisible) {
  dg::NodeStyle style;
  style.fill = Color::from_argb(argb);
  style.opacity = opacity;
  style.radii = dg::Radii::all(radius);
  style.overflow = overflow;
  return style;
}

// Every shape the layer code branches on, in one scene:
//
//   plain      a faded group of OVERLAPPING children, which is the shape that
//              distinguishes group opacity from per-object alpha and is
//              therefore the shape a wrong composite corrupts
//   rounded    a faded group holding a ROUNDED child, so a clip-atomic node
//              lives inside a layer and the damage rectangle that swallows it
//              also decides where the layer is cut
//   nested     a faded group inside a faded group
//   clipped    a faded group that ALSO clips, so the layer and the clip are
//              pushed and popped in the same traversal
//   escapee    a child that overflows its faded group, so the layer's extent
//              is larger than the node carrying the opacity, and MOVES
//   loose      an ordinary neighbour overlapping the groups, so a defect that
//              simply stopped painting after a restore is visible
struct Handles {
  NodeId plain;
  NodeId plain_left;
  NodeId plain_right;
  NodeId rounded;
  NodeId rounded_child;
  NodeId nested_outer;
  NodeId nested_inner;
  NodeId nested_leaf;
  NodeId clipped;
  NodeId escapee;
  NodeId loose;
};

struct Scene {
  RenderTree tree;
  Handles handles;
};

Scene build(const dg::TreeSpec& spec) {
  RenderTree tree{spec};
  Handles handles;

  handles.plain =
      tree.add_child(RenderTree::root(), PixelRect{10, 10, 90, 55}, styled(0x00000000, 0.5F));
  handles.plain_left =
      tree.add_child(handles.plain, PixelRect{4, 6, 50, 40}, styled(0xFF3C78D8));
  handles.plain_right =
      tree.add_child(handles.plain, PixelRect{34, 6, 50, 40}, styled(0xFFE8B45A));

  handles.rounded =
      tree.add_child(RenderTree::root(), PixelRect{115, 10, 90, 55}, styled(0x40202830, 0.7F));
  handles.rounded_child =
      tree.add_child(handles.rounded, PixelRect{8, 6, 60, 40}, styled(0xFF6AA84F, 1.0F, 12.0F));

  handles.nested_outer =
      tree.add_child(RenderTree::root(), PixelRect{10, 80, 90, 60}, styled(0x00000000, 0.6F));
  handles.nested_inner =
      tree.add_child(handles.nested_outer, PixelRect{6, 6, 70, 46}, styled(0x00000000, 0.4F));
  handles.nested_leaf =
      tree.add_child(handles.nested_inner, PixelRect{4, 4, 55, 34}, styled(0xFFCC4125));

  handles.clipped = tree.add_child(RenderTree::root(), PixelRect{115, 80, 70, 55},
                                   styled(0xFF243040, 0.55F, 9.0F, Overflow::kClip));
  handles.escapee =
      tree.add_child(handles.clipped, PixelRect{20, 8, 90, 36}, styled(0xFF8E7CC3));

  handles.loose =
      tree.add_child(RenderTree::root(), PixelRect{85, 55, 60, 45}, styled(0x9935506E));
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

void set_opacity(RenderTree& tree, NodeId node, float opacity) {
  dg::NodeStyle style = tree.style(node);
  style.opacity = opacity;
  tree.set_style(node, style);
}

// A fade that visits every interesting value rather than sweeping a narrow
// band: the cycle reaches 0 and 1 exactly, so the layer appears and disappears
// during the script instead of merely changing strength.
float fade(int frame, int step) {
  return static_cast<float>((frame * step) % 21) / 20.0F;
}

void mutate(RenderTree& tree, const Handles& handles, int frame) {
  const auto tint = [frame](std::uint32_t base, int step) {
    return base ^ (static_cast<std::uint32_t>((frame * step) % 200) << 8U);
  };

  // A change strictly INSIDE a layer, which is the case the per-pixel
  // decomposition has to survive.
  tree.set_fill(handles.plain_left, Color::from_argb(tint(0xFF3C78D8, 7)));
  tree.set_fill(handles.nested_leaf, Color::from_argb(tint(0xFFCC4125, 5)));

  // A ROUNDED node inside a layer. It is clip-atomic, so the damage rectangle
  // grows to swallow it - and that expansion now also decides where the layer
  // is cut.
  tree.set_fill(handles.rounded_child, Color::from_argb(tint(0xFF6AA84F, 11)));

  // The OPACITY ITSELF changing, at three depths, including through 0 and 1.
  set_opacity(tree, handles.plain, fade(frame, 1));
  set_opacity(tree, handles.nested_inner, fade(frame, 3));
  set_opacity(tree, handles.clipped, fade(frame + 7, 2));

  // A child crossing its group's own boundary, so the layer's extent grows
  // and shrinks while the group is faded.
  tree.set_local_origin(handles.plain_right, 34 - ((frame * 3) % 60), 6);
  tree.set_local_origin(handles.escapee, 20 - ((frame * 5) % 70), 8);

  tree.set_fill(handles.loose, Color::from_argb(tint(0x9935506E, 3)));
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

// The three case bodies below are hoisted out of their TEST_CASEs for the
// reason the neighbouring files record: doctest expands every assertion into
// branches, and a body of them runs past clang-tidy's cognitive-complexity
// budget.

// The rule, stated as an assertion rather than as a comment: A LAYER IS NOT
// DAMAGE-ATOMIC. A change to one square child of a faded group damages that
// child and nothing else - not the group, not the sibling it overlaps.
//
// This is the case that fails if a later slice makes layers atomic without
// saying so, and it is deliberately the OPPOSITE shape of the rounded clip's
// rule so that the two cannot be confused for one another.
void check_a_change_inside_a_layer_is_not_grown_to_it(Pair& pair) {
  RenderTree& tree = pair.damaged.tree;
  const Handles& handles = pair.damaged.handles;
  tree.repaint(pair.damaged_surface);
  REQUIRE(tree.damage().area() == 0);

  const PixelRect group = tree.absolute_bounds(handles.plain);
  const PixelRect child = tree.absolute_bounds(handles.plain_left);
  REQUIRE(child.area() * 3 < group.area() * 2);

  tree.set_fill(handles.plain_left, Color::from_argb(0xFFFF0000));
  CHECK(tree.damage().bounds() == child);
}

// The other half, asserted separately because it is the one change that really
// does touch every pixel of the group: multiplying the whole composited image
// by a new alpha changes the colour of every pixel the subtree covers,
// including the ones nothing inside it moved.
void check_changing_the_opacity_damages_the_whole_subtree(Pair& pair) {
  RenderTree& tree = pair.damaged.tree;
  const Handles& handles = pair.damaged.handles;
  tree.repaint(pair.damaged_surface);
  REQUIRE(tree.damage().area() == 0);

  const PixelRect left = tree.absolute_bounds(handles.plain_left);
  const PixelRect right = tree.absolute_bounds(handles.plain_right);
  const PixelRect subtree =
      dg::join(dg::join(tree.absolute_bounds(handles.plain), left), right);

  set_opacity(tree, handles.plain, 0.25F);
  CHECK(dg::contains(tree.damage().bounds(), subtree));
}

// A layer opened for a node nothing asked to fade is invisible to every pixel
// comparison in this file, because compositing at alpha 1 is the identity. The
// count is the only observer there is, so the placement of the layers is
// asserted through it - otherwise "the layers are where the scene asked for
// them" rests on nothing at all.
void check_only_faded_nodes_open_layers(Pair& pair) {
  RenderTree& tree = pair.damaged.tree;
  const Handles& handles = pair.damaged.handles;

  for (const NodeId node : {handles.plain, handles.rounded, handles.nested_outer,
                            handles.nested_inner, handles.clipped}) {
    set_opacity(tree, node, 1.0F);
  }
  CHECK(tree.repaint(pair.damaged_surface).layers == 0);

  tree.damage_all();
  set_opacity(tree, handles.plain, 0.5F);
  CHECK(tree.repaint(pair.damaged_surface).layers == 1);

  tree.damage_all();
  set_opacity(tree, handles.nested_outer, 0.5F);
  set_opacity(tree, handles.nested_inner, 0.5F);
  CHECK(tree.repaint(pair.damaged_surface).layers == 3);

  // And a group faded to nothing opens none, because its subtree is skipped
  // rather than composited into a buffer that is then multiplied away.
  tree.damage_all();
  set_opacity(tree, handles.nested_outer, 0.0F);
  CHECK(tree.repaint(pair.damaged_surface).layers == 1);
}

// A faded group that is ALSO the clipping node, with a child entirely outside
// its box, and the damage rectangle over the place that child would have been.
//
// This is the shape that gives `subtree_extent`'s use of `visible_bounds` an
// observable consequence, and it took two attempts to build. Joining the
// DECLARED bounds instead makes the extent a superset, and a superset is never
// wrong for pixels: Skia intersects a layer with the clip stack anyway, so the
// allocation and every pixel are identical, and byte identity is blind to it
// by construction. That is the third diagnosis this project has recorded for a
// surviving injection, and the same one slice 4-4 reached about `clip_bounds`
// - the assertion has to be about the term's own job.
//
// Its own job is to answer "may this subtree put a pixel in the region being
// repainted", and a wrong answer opens an offscreen buffer for a subtree that
// cannot paint anything. `RepaintStats::layers` is where that shows.
//
// THE FADE AND THE CLIP ARE THE SAME NODE on purpose. The first attempt hung
// the fade one level below a separate clipper and the injection still
// survived, because the clipper's own culling returned before the traversal
// ever reached the layer - the scene could not exercise the term even though
// it contained it.
struct HiddenGroup {
  RenderTree tree;
  NodeId faded;
  NodeId hidden;
};

HiddenGroup build_hidden_group(const dg::TreeSpec& spec) {
  RenderTree tree{spec};
  const NodeId faded = tree.add_child(RenderTree::root(), PixelRect{10, 10, 60, 40},
                                      styled(0xFF2C3644, 0.5F, 0.0F, Overflow::kClip));
  const NodeId hidden = tree.add_child(faded, PixelRect{100, 0, 40, 40}, styled(0xFF3C78D8));

  REQUIRE_FALSE(dg::intersects(tree.absolute_bounds(faded), tree.absolute_bounds(hidden)));
  return HiddenGroup{std::move(tree), faded, hidden};
}

void check_a_clipped_away_group_opens_no_layer(Pair& pair) {
  const dg::TreeSpec spec = spec_for(8, dg::PaintMode::kDirect);
  HiddenGroup built = build_hidden_group(spec);
  RenderTree& tree = built.tree;
  RasterSurface& surface = pair.damaged_surface;

  tree.repaint(surface);
  REQUIRE(tree.damage().area() == 0);

  // Where the hidden child WOULD be, which the clip has removed.
  tree.damage_rect(tree.absolute_bounds(built.hidden));
  CHECK(tree.repaint(surface).layers == 0);

  // The control: damage where the group really is visible, and the layer must
  // be opened - otherwise "no layer" would pass by never opening one at all.
  tree.damage_rect(tree.absolute_bounds(built.faded));
  CHECK(tree.repaint(surface).layers == 1);
}

}  // namespace

TEST_SUITE("compositing and damage") {
  TEST_CASE("a scene with layers repaints incrementally exactly as it repaints fully") {
    SUBCASE("one damage rectangle - the always-union policy") {
      run_script(spec_for(1, dg::PaintMode::kDirect));
    }
    SUBCASE("eight damage rectangles") {
      run_script(spec_for(8, dg::PaintMode::kDirect));
    }
    SUBCASE("replaying a recorded picture containing layers") {
      run_script(spec_for(8, dg::PaintMode::kPicture));
    }
  }

  // A comparison that cannot fail proves nothing. The damage side misses the
  // LAST frame, which is the only frame that can be skipped visibly - damage
  // accumulates, so a dropped middle frame is caught up by the next repaint.
  TEST_CASE("the layered equivalence check is armed") {
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

  TEST_CASE("a change inside a layer is not grown to the layer") {
    with_pair(spec_for(8, dg::PaintMode::kDirect),
              check_a_change_inside_a_layer_is_not_grown_to_it);
  }

  TEST_CASE("changing the opacity damages the whole subtree") {
    with_pair(spec_for(8, dg::PaintMode::kDirect),
              check_changing_the_opacity_damages_the_whole_subtree);
  }

  TEST_CASE("a repaint opens exactly as many layers as the scene asked for") {
    with_pair(spec_for(8, dg::PaintMode::kDirect), check_only_faded_nodes_open_layers);
  }

  TEST_CASE("a faded group an ancestor clip removes opens no layer") {
    with_pair(spec_for(8, dg::PaintMode::kDirect), check_a_clipped_away_group_opens_no_layer);
  }
}
