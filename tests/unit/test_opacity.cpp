// Group opacity, pinned against arithmetic done by hand rather than against a
// second run of the same code.
//
// There are two different things "opacity" can mean, they look similar on a
// scene without overlaps, and only one of them is the CSS property:
//
//   PER-OBJECT ALPHA draws every operation translucently. Two overlapping
//   children blend with each other, so the overlap ends up more opaque than
//   either child alone.
//
//   GROUP OPACITY composites the whole subtree into an offscreen buffer at
//   full opacity - resolving the overlaps there - and then draws that single
//   image translucently. The overlap looks exactly like the rest of the group.
//
// A scene with no overlapping content cannot tell them apart, and a scene with
// overlapping content tells them apart unambiguously. That is the entire
// design of this file: one geometry, two ways of asking for a half-visible
// picture, and a pixel inside the overlap.
//
// THE EXPECTED VALUES ARE DERIVED, NOT RECORDED. Byte identity against another
// render would be satisfied by an implementation that gets the meaning wrong
// in both copies - this project has already written that lesson down twice -
// so the numbers below come from the blend arithmetic and are checked against
// Skia, rather than the other way round. With an opaque black backdrop, white
// content and a group alpha of exactly 128/255:
//
//   per-object, one layer of content   0 * (1 - 128/255) + 128       = 128
//   per-object, two layers of content  128 + 128 * (1 - 128/255)     = 192
//   group, one or two layers           255 * 128/255                 = 128
//   two nested groups                  255 * 128/255 * 128/255       =  64
//
// 128/255 rather than 0.5 so that the group alpha is exactly representable in
// eight bits and every expected value is an integer - the point of the test is
// the semantics, not Skia's rounding.

#include <cstddef>
#include <cstdint>
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
using dg::PixelRect;
using dg::PixelSize;
using dg::RasterSurface;
using dg::RenderTree;
using dg::RepaintStats;

constexpr int kWidth = 140;
constexpr int kHeight = 90;

// Exactly representable in eight bits, so `round(255 * kHalf) == 128` with no
// argument about which way a tie goes.
constexpr float kHalf = 128.0F / 255.0F;

constexpr std::uint32_t kBackdrop = 0xFF000000;
constexpr std::uint32_t kOpaqueWhite = 0xFFFFFFFF;
constexpr std::uint32_t kHalfWhite = 0x80FFFFFF;

// Two rectangles that overlap in their middle third. The overlap is what
// distinguishes the two meanings of opacity, so it is declared here once and
// every panel below is built from it.
constexpr PixelRect kLeftBar{4, 10, 40, 30};
constexpr PixelRect kRightBar{24, 10, 40, 30};
constexpr PixelRect kOverlap{24, 10, 20, 30};

// A group narrower than the child it holds, so that the layer's own extent -
// rather than the node's box - is what decides whether the overflow survives.
constexpr PixelRect kSmallHolder{10, 10, 30, 30};
constexpr PixelRect kEscapee{20, 10, 60, 30};
constexpr PixelRect kFadedInsideClip{5, 5, 60, 20};

dg::NodeStyle filled(std::uint32_t argb) {
  dg::NodeStyle style;
  style.fill = Color::from_argb(argb);
  return style;
}

dg::NodeStyle group(float opacity) {
  dg::NodeStyle style;
  style.opacity = opacity;
  return style;
}

// The 32-bit pixel as 0xAARRGGBB, read out of the BGRA surface.
//
// Read as a whole colour rather than as one channel so that a defect swapping
// two channels is a failure here instead of an invisible pass - the content is
// grey, but the backdrop and the alpha are not.
std::uint32_t pixel_at(const RasterSurface& surface, int x, int y) {
  const dg::PixelView view = surface.peek_pixels();
  const std::size_t offset =
      (static_cast<std::size_t>(y) * view.row_bytes) + (static_cast<std::size_t>(x) * 4);
  return (static_cast<std::uint32_t>(view.pixels[offset + 3]) << 24U) |
         (static_cast<std::uint32_t>(view.pixels[offset + 2]) << 16U) |
         (static_cast<std::uint32_t>(view.pixels[offset + 1]) << 8U) |
         static_cast<std::uint32_t>(view.pixels[offset]);
}

std::uint32_t grey(std::uint32_t level) {
  return 0xFF000000U | (level << 16U) | (level << 8U) | level;
}

std::vector<std::uint8_t> snapshot(const RasterSurface& surface) {
  const dg::PixelView view = surface.peek_pixels();
  const auto row = static_cast<std::size_t>(view.width) * 4;
  std::vector<std::uint8_t> pixels(row * static_cast<std::size_t>(view.height));
  for (int y = 0; y < view.height; ++y) {
    for (std::size_t byte = 0; byte < row; ++byte) {
      pixels[(static_cast<std::size_t>(y) * row) + byte] =
          view.pixels[(static_cast<std::size_t>(y) * view.row_bytes) + byte];
    }
  }
  return pixels;
}

dg::TreeSpec backdrop_spec() {
  dg::TreeSpec spec;
  spec.viewport = PixelSize{kWidth, kHeight};
  spec.background = filled(kBackdrop);
  return spec;
}

// Two overlapping bars under `holder`, offset so several panels fit in one
// viewport.
void add_bars(RenderTree& tree, NodeId holder, std::uint32_t argb) {
  tree.add_child(holder, kLeftBar, filled(argb));
  tree.add_child(holder, kRightBar, filled(argb));
}

struct Painted {
  RenderTree tree;
  RasterSurface surface;
  RepaintStats stats;
};

// One scene, painted once, with the statistics of that repaint kept.
//
// `repaint_full` rather than `repaint`, because this file is about what the
// picture MEANS; whether the damage path reproduces it is a different claim
// and tests/unit/test_opacity_damage.cpp is where it is made.
//
// The allocation failure is handled HERE rather than in each case, so that no
// case body carries an `if`. That is not tidiness: doctest expands every
// assertion into branches, and a guard plus half a dozen CHECKs runs past
// clang-tidy's cognitive-complexity threshold on its own.
template <typename Build, typename Body>
void with_scene(const Build& build, const Body& body) {
  std::optional<RasterSurface> surface = RasterSurface::create(kWidth, kHeight);
  if (!surface.has_value()) {
    FAIL("could not allocate a raster surface");
    return;
  }
  RenderTree tree{backdrop_spec()};
  build(tree);
  RasterSurface target = *std::move(surface);
  const RepaintStats stats = tree.repaint_full(target);
  const Painted painted{std::move(tree), std::move(target), stats};
  body(painted);
}

// Two scenes at the same size, for the cases whose whole claim is a
// comparison between them.
template <typename Pair, typename Body>
void with_two_scenes(const Pair& builds, const Body& body) {
  with_scene(builds.first, [&builds, &body](const Painted& first) {
    with_scene(builds.second, [&first, &body](const Painted& second) { body(first, second); });
  });
}

int overlap_x() {
  return kOverlap.left() + (kOverlap.width / 2);
}
int overlap_y() {
  return kOverlap.top() + (kOverlap.height / 2);
}

// A column inside the left bar and outside the overlap, so "one layer of
// content" is a real sample rather than a hope.
int single_x() {
  return kLeftBar.left() + 4;
}

// The two scenes the headline case compares: the same two overlapping bars,
// the same amount of translucency, asked for in the two different ways.
auto per_object_and_grouped() {
  return std::pair{[](RenderTree& tree) { add_bars(tree, RenderTree::root(), kHalfWhite); },
                   [](RenderTree& tree) {
                     const NodeId holder = tree.add_child(
                         RenderTree::root(), PixelRect{0, 0, kWidth, kHeight}, group(kHalf));
                     add_bars(tree, holder, kOpaqueWhite);
                   }};
}

// Each claim is its own function, holding at most four assertions.
//
// doctest expands each assertion into branches, so a case that stated all of
// them together sat at more than twice clang-tidy's cognitive-complexity
// threshold - the same reason test_clip_damage.cpp and test_props_boundary.cpp
// hoist their bodies. Splitting by claim also makes a failure report name the
// sentence that broke rather than the case it lived in.

// Where the content does not overlap the two agree, which is exactly why a
// scene without an overlap proves nothing.
void check_agreement_outside_the_overlap(const Painted& per_object, const Painted& grouped) {
  CHECK(pixel_at(per_object.surface, single_x(), overlap_y()) == grey(128));
  CHECK(pixel_at(grouped.surface, single_x(), overlap_y()) == grey(128));
}

// Where it overlaps they must not: two half-transparent bars blend with each
// other and reach 192, while the group resolves the overlap at full strength
// and then fades the result to the same 128 as everywhere else.
void check_disagreement_inside_the_overlap(const Painted& per_object, const Painted& grouped) {
  CHECK(pixel_at(per_object.surface, overlap_x(), overlap_y()) == grey(192));
  CHECK(pixel_at(grouped.surface, overlap_x(), overlap_y()) == grey(128));
}

// The same claim stated as a relation rather than as two numbers, so that a
// defect producing some third value cannot pass by making both sides wrong
// together.
void check_the_group_is_uniform_and_the_other_is_not(const Painted& per_object,
                                                     const Painted& grouped) {
  CHECK(pixel_at(grouped.surface, single_x(), overlap_y()) ==
        pixel_at(grouped.surface, overlap_x(), overlap_y()));
  CHECK(pixel_at(per_object.surface, single_x(), overlap_y()) !=
        pixel_at(per_object.surface, overlap_x(), overlap_y()));
}

void check_only_the_group_opened_a_layer(const Painted& per_object, const Painted& grouped) {
  CHECK(per_object.stats.layers == 0);
  CHECK(grouped.stats.layers == 1);
}

void check_nested_multiplies(const Painted& nested) {
  CHECK(pixel_at(nested.surface, single_x(), overlap_y()) == grey(64));

  // The inner group still resolves its own overlap first, so the nested case
  // is uniform for the same reason the single one is.
  CHECK(pixel_at(nested.surface, overlap_x(), overlap_y()) == grey(64));
  CHECK(nested.stats.layers == 2);
}

void check_no_layer_and_same_pixels(const Painted& plain, const Painted& defaulted) {
  CHECK(plain.stats.layers == 0);
  CHECK(defaulted.stats.layers == 0);
  CHECK(snapshot(plain.surface) == snapshot(defaulted.surface));
  CHECK(pixel_at(plain.surface, overlap_x(), overlap_y()) == grey(255));
}

void check_zero_is_the_same_as_absent(const Painted& faded, const Painted& absent) {
  CHECK(faded.stats.layers == 0);
  CHECK(snapshot(faded.surface) == snapshot(absent.surface));
  CHECK(pixel_at(faded.surface, overlap_x(), overlap_y()) == grey(0));
}

// 128 * 128/255 = 64, and 255 * 128/255 = 128. A group painted outside its own
// layer would answer 128 and 128; one whose layer was sized to its children
// would answer 0 in the corner.
void check_one_layer(const Painted& painted) {
  CHECK(painted.stats.layers == 1);
}

void check_own_fill_faded(const Painted& painted) {
  const PixelRect holder = painted.tree.absolute_bounds(NodeId{1});
  const PixelRect child = painted.tree.absolute_bounds(NodeId{2});
  REQUIRE(child.right() < holder.right());
  CHECK(pixel_at(painted.surface, holder.right() - 1, holder.bottom() - 1) == grey(64));
  CHECK(pixel_at(painted.surface, child.left() + 1, child.top() + 1) == grey(128));
}

// The last column of the overflow, which is the furthest a too-small layer
// would have cut away.
void check_overflow_survived(const Painted& painted) {
  const PixelRect escapee = painted.tree.absolute_bounds(NodeId{2});
  const PixelRect holder = painted.tree.absolute_bounds(NodeId{1});
  REQUIRE(escapee.right() > holder.right());
  CHECK(pixel_at(painted.surface, escapee.right() - 1, escapee.top() + 4) == grey(128));
  CHECK(pixel_at(painted.surface, holder.right() + 1, escapee.top() + 4) == grey(128));
}

void check_cut_at_the_clip(const Painted& painted, const PixelRect& clipper, int row) {
  CHECK(pixel_at(painted.surface, clipper.right() - 1, row) == grey(128));
  CHECK(pixel_at(painted.surface, clipper.right(), row) == grey(0));
  CHECK(painted.stats.layers == 1);
}

void check_fade_outside_clip(const Painted& painted) {
  const PixelRect holder = painted.tree.absolute_bounds(NodeId{1});
  const PixelRect escapee = painted.tree.absolute_bounds(NodeId{2});
  const int row = escapee.top() + 4;
  REQUIRE(dg::contains(holder, dg::PixelPoint{holder.right() - 1, row}));
  check_cut_at_the_clip(painted, holder, row);
}

void check_fade_inside_clip(const Painted& painted) {
  const PixelRect clipper = painted.tree.absolute_bounds(NodeId{1});
  check_cut_at_the_clip(painted, clipper, clipper.top() + 8);
}

}  // namespace

TEST_SUITE("group opacity") {
  // The whole point of the slice, in one case. Same geometry, same colours,
  // same backdrop; the only difference is where the translucency is asked for.
  TEST_CASE("a faded group resolves its overlaps before it fades") {
    with_two_scenes(per_object_and_grouped(),
                    [](const Painted& per_object, const Painted& grouped) {
                      check_agreement_outside_the_overlap(per_object, grouped);
                      check_disagreement_inside_the_overlap(per_object, grouped);
                      check_the_group_is_uniform_and_the_other_is_not(per_object, grouped);
                      check_only_the_group_opened_a_layer(per_object, grouped);
                    });
  }

  // Nested groups multiply. A layer that composited its parent's alpha a
  // second time, or one that let the inner alpha replace the outer, would both
  // land on 128 here rather than 64.
  TEST_CASE("a faded group inside a faded group multiplies") {
    with_scene(
        [](RenderTree& tree) {
          const NodeId outer = tree.add_child(RenderTree::root(),
                                              PixelRect{0, 0, kWidth, kHeight}, group(kHalf));
          const NodeId inner =
              tree.add_child(outer, PixelRect{0, 0, kWidth, kHeight}, group(kHalf));
          add_bars(tree, inner, kOpaqueWhite);
        },
        check_nested_multiplies);
  }

  // saveLayer allocates, and compositing at alpha 1 is the identity - so a
  // layer opened for a fully opaque node is invisible to every pixel
  // comparison in this project. The counter is the only place it shows.
  TEST_CASE("a fully opaque group opens no layer") {
    with_two_scenes(
        std::pair{[](RenderTree& tree) {
                    const NodeId holder = tree.add_child(
                        RenderTree::root(), PixelRect{0, 0, kWidth, kHeight}, group(1.0F));
                    add_bars(tree, holder, kOpaqueWhite);
                  },
                  [](RenderTree& tree) {
                    const NodeId holder = tree.add_child(
                        RenderTree::root(), PixelRect{0, 0, kWidth, kHeight}, filled(0));
                    add_bars(tree, holder, kOpaqueWhite);
                  }},
        check_no_layer_and_same_pixels);
  }

  // At zero the subtree is skipped rather than composited into a buffer that
  // is then multiplied away. The two are required to be pixel-identical, which
  // is what makes the skip an optimisation rather than a behaviour.
  TEST_CASE("a group at zero paints nothing, and opens no layer to do it") {
    with_two_scenes(std::pair{[](RenderTree& tree) {
                                const NodeId holder = tree.add_child(
                                    RenderTree::root(), PixelRect{0, 0, kWidth, kHeight},
                                    group(0.0F));
                                add_bars(tree, holder, kOpaqueWhite);
                              },
                              [](RenderTree&) {}},
                    check_zero_is_the_same_as_absent);
  }

  // The group's OWN paint is inside the layer, not merely its descendants -
  // which is where `opacity` and `overflow` part company. A clipping node does
  // not clip itself (doc/clipping.md), because its own paint is inside its box
  // by construction and clipping it would move measured pixels for nothing. A
  // FADING node does fade itself, because CSS says so and because a group
  // whose background stayed solid while its contents faded would be a very
  // strange thing to look at.
  //
  // The child is smaller than the group, so the group's own fill is visible
  // outside it: a layer whose extent was built from the children alone would
  // cut that fill away, and a painter that drew the node before opening the
  // layer would leave it at full strength.
  TEST_CASE("a faded group fades its own fill, not only its children") {
    with_scene(
        [](RenderTree& tree) {
          dg::NodeStyle style = group(kHalf);
          style.fill = Color::from_argb(0xFF808080);
          const NodeId holder =
              tree.add_child(RenderTree::root(), PixelRect{10, 10, 60, 40}, style);
          tree.add_child(holder, PixelRect{4, 4, 20, 20}, filled(kOpaqueWhite));
        },
        [](const Painted& painted) {
          check_own_fill_faded(painted);
          check_one_layer(painted);
        });
  }

  // The layer is sized to the SUBTREE, not to the node that carries the
  // opacity. Skia treats saveLayer bounds as a clip rather than as a mere
  // allocation hint, so a layer sized to the group's own box would silently cut
  // off a child that overflows it - and `overflow` deliberately does not
  // confine a child, so an overflowing child is an ordinary scene here.
  TEST_CASE("a child overflowing its faded group is still inside the layer") {
    with_scene(
        [](RenderTree& tree) {
          const NodeId holder = tree.add_child(RenderTree::root(), kSmallHolder, group(kHalf));
          tree.add_child(holder, kEscapee, filled(kOpaqueWhite));
        },
        [](const Painted& painted) {
          check_overflow_survived(painted);
          check_one_layer(painted);
        });
  }

  // A group that also clips. The two compose in one direction only: the layer
  // is opened first and the clip is applied inside it, so what is composited is
  // the clipped picture rather than the whole subtree faded and then cut.
  TEST_CASE("a faded group that also clips fades only what survives the clip") {
    with_scene(
        [](RenderTree& tree) {
          dg::NodeStyle style = group(kHalf);
          style.overflow = dg::Overflow::kClip;
          const NodeId holder = tree.add_child(RenderTree::root(), kSmallHolder, style);
          tree.add_child(holder, kEscapee, filled(kOpaqueWhite));
        },
        check_fade_outside_clip);
  }

  // A clip OUTSIDE a fade, which is the other nesting order and reaches a
  // different line of the traversal: the clip is pushed by the ancestor and the
  // layer is opened underneath it, so the layer's own bounds have to have been
  // shrunk by the ancestor clip rather than by the node's box.
  TEST_CASE("a faded group inside a clip is cut by the clip") {
    with_scene(
        [](RenderTree& tree) {
          dg::NodeStyle clipping = filled(0);
          clipping.overflow = dg::Overflow::kClip;
          const NodeId clipper = tree.add_child(RenderTree::root(), kSmallHolder, clipping);
          const NodeId faded = tree.add_child(clipper, kFadedInsideClip, group(kHalf));
          tree.add_child(faded, PixelRect{0, 0, 60, 20}, filled(kOpaqueWhite));
        },
        check_fade_inside_clip);
  }
}
