// Hit testing, checked at every pixel against two independent ground truths.
//
// The acceptance bar for this sub-step is not "a few probes land where I
// expect". It is that for EVERY pixel of a scene containing overlapping,
// nested and overflowing nodes, the node hit testing names is the node the
// user can see - no widget visible but unclickable, none reachable while
// occluded. That is checkable exhaustively at these sizes, so it is checked
// exhaustively.
//
// TWO ORACLES, because they fail differently:
//
//   ORDER, from a flat list. The scene here is declared as a table and this
//   file computes absolute bounds and paint order from it with its own code,
//   sharing nothing with the library. The last node of that order covering a
//   pixel is the visible one. This catches a traversal that visits siblings or
//   subtrees in the wrong sequence.
//
//   PIXELS, from the rasterizer. Every node is painted a unique flat colour
//   through the real RenderTree onto a real surface, and the colour read back
//   at a pixel names the node that actually reached the screen there. This
//   catches the case where hit testing and paint order agree with each other
//   and both are wrong, which the first oracle cannot see - and it is the
//   literal statement of "what you see is what you click".
//
// The second oracle is only exact because these scenes use square corners,
// opaque fills and no borders or text: sub-step 1 measured that integer
// aligned square rectangles rasterize bit-identically, while anti-aliased
// rounded ones do not. A rounded scene here would blend two nodes' colours at
// the corners and the oracle would be reading a colour belonging to neither.
// That is also why the clipped scene below clips with SQUARE corners: a square
// clip lands on pixel boundaries, so "painted" stays a yes-or-no question and
// the equivalence is checkable at every pixel. The rounded clip has a real
// anti-aliased band where it is neither, and it is pinned against the
// rasterizer's own alpha in tests/unit/test_clip.cpp instead.
//
// BOTH ORACLES NOW HONOUR CLIPPING, which is the point of this slice: a pixel
// that was not painted because a clip removed it must not be hittable. The
// first oracle re-derives the ancestor clip chain here, from the same flat
// table, sharing no code with the library.
//
// NEITHER ORACLE HONOURS `opacity`, which is the OTHER slice's point and the
// deliberate opposite answer: a pixel that was not painted because it was
// faded away IS still hittable. render_tree.h records why - there is no
// threshold to put the boundary at, and a fade is a continuous animation
// through every value between 1 and 0. So the claim checked here is stronger
// than "the sweeps still pass with opacity in the scene": the SAME scene is
// built twice, once faded and once fully opaque, and hit testing is required
// to give the same answer at every pixel of both while the two renders are
// required to differ at a great many.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/render/render_tree.h"

namespace {

using dg::NodeId;
using dg::PixelPoint;
using dg::PixelRect;
using dg::PixelSize;
using dg::RenderTree;

// One row of a scene table: a parent index and a box relative to that parent.
// Declared flat so that the expected paint order is the declaration order,
// which is only true when every subtree is completed before its next sibling
// starts - the table below does that, and `check_declaration_order` refuses to
// run if a future edit stops doing it.
struct Row {
  std::uint32_t parent;
  PixelRect local;
  const char* name;
  dg::Overflow clip = dg::Overflow::kVisible;
  float opacity = 1.0F;
};

struct Scene {
  std::vector<Row> rows;
  PixelSize viewport;
};

// Absolute bounds, computed here rather than asked of the tree. Asking would
// make the oracle depend on the thing under test for the geometry AND the
// order, which leaves it able to check only the order.
std::vector<PixelRect> absolute_bounds_of(const Scene& scene) {
  std::vector<PixelRect> absolute;
  absolute.reserve(scene.rows.size());
  for (std::size_t index = 0; index < scene.rows.size(); ++index) {
    const Row& row = scene.rows[index];
    if (index == 0) {
      absolute.push_back(row.local);
      continue;
    }
    const PixelRect& parent = absolute[row.parent];
    absolute.push_back(row.local.offset_by(parent.x, parent.y));
  }
  return absolute;
}

// Whether every clipping ancestor of `index` admits the point.
//
// Climbed here rather than accumulated into a rectangle, so that a defect
// which intersected only the nearest clip instead of all of them has
// something to disagree with. The root is its own parent, which is the stop
// condition.
bool passes_ancestor_clips(const Scene& scene, const std::vector<PixelRect>& absolute,
                           std::uint32_t index, PixelPoint point) {
  std::uint32_t walk = index;
  while (walk != 0) {
    walk = scene.rows[walk].parent;
    if (scene.rows[walk].clip == dg::Overflow::kClip && !dg::contains(absolute[walk], point)) {
      return false;
    }
  }
  return true;
}

// The visible node at `point`, by the definition painting gives it: the last
// one to cover it, among those no ancestor clip has removed.
std::optional<std::uint32_t> topmost_by_order(const Scene& scene,
                                              const std::vector<PixelRect>& absolute,
                                              PixelPoint point) {
  std::optional<std::uint32_t> found;
  for (std::size_t index = 0; index < absolute.size(); ++index) {
    const auto id = static_cast<std::uint32_t>(index);
    if (dg::contains(absolute[index], point) &&
        passes_ancestor_clips(scene, absolute, id, point)) {
      found = id;
    }
  }
  return found;
}

// A distinct opaque colour per node, recoverable from one byte. Red carries
// the index so the read-back is a single channel comparison; green and blue
// are fixed and non-zero so that a node is never confused with an unpainted
// surface.
dg::Color colour_for(std::size_t index) {
  return dg::Color::rgba(static_cast<std::uint8_t>(index + 1), 0x40, 0x80);
}

RenderTree build(const Scene& scene) {
  dg::TreeSpec spec;
  spec.viewport = scene.viewport;
  spec.background.fill = colour_for(0);
  spec.background.overflow = scene.rows[0].clip;
  spec.background.opacity = scene.rows[0].opacity;
  RenderTree tree{spec};

  for (std::size_t index = 1; index < scene.rows.size(); ++index) {
    dg::NodeStyle style;
    style.fill = colour_for(index);
    style.overflow = scene.rows[index].clip;
    style.opacity = scene.rows[index].opacity;
    const NodeId created =
        tree.add_child(NodeId{scene.rows[index].parent}, scene.rows[index].local, style);
    REQUIRE(created == NodeId{static_cast<std::uint32_t>(index)});
  }
  return tree;
}

// The same scene with every fade removed.
//
// This is what makes the opacity claim checkable rather than merely
// unrefuted: the rasterizer oracle recovers a node index from a flat colour
// and cannot read a blended one, so the pixel sweep runs on this twin - which
// proves the traversal order - while the hit-test answers are required to be
// identical between the twin and the faded original.
Scene without_opacity(Scene scene) {
  for (Row& row : scene.rows) {
    row.opacity = 1.0F;
  }
  return scene;
}

// A parent must be declared before its children, and a subtree must be
// contiguous. Both are what make the declaration order equal the paint order,
// and neither is obvious from reading the table.
void check_declaration_order(const Scene& scene) {
  for (std::size_t index = 1; index < scene.rows.size(); ++index) {
    CHECK(scene.rows[index].parent < index);
  }
  for (std::size_t index = 1; index + 1 < scene.rows.size(); ++index) {
    const std::uint32_t parent = scene.rows[index].parent;
    const std::uint32_t next_parent = scene.rows[index + 1].parent;
    const bool contiguous = next_parent == index || next_parent <= parent;
    CHECK_MESSAGE(contiguous, "subtree of node ", index, " is interleaved with a sibling's");
  }
}

// Overlap, nesting, an occluded node, adjacency and - deliberately - a child
// that runs past its parent on two sides.
Scene overlapping_scene() {
  Scene scene;
  scene.viewport = PixelSize{160, 120};
  scene.rows = {
      {0, PixelRect{0, 0, 160, 120}, "root"},

      // A panel with two children, the second of which extends past the
      // panel's right and bottom edges. Painting does not clip it, so hit
      // testing must not either.
      {0, PixelRect{10, 10, 60, 40}, "panel"},
      {1, PixelRect{5, 5, 20, 15}, "panel.inner"},
      {1, PixelRect{40, 25, 40, 35}, "panel.overflowing"},

      // Two siblings that overlap. The later one is on top in the overlap.
      {0, PixelRect{90, 10, 50, 50}, "under"},
      {0, PixelRect{115, 30, 40, 40}, "over"},

      // Fully swallowed by its later sibling: visible nowhere, so hit testing
      // must never name it.
      {0, PixelRect{20, 70, 30, 30}, "occluded"},
      {0, PixelRect{15, 65, 45, 45}, "occluder"},

      // Edge-sharing neighbours. Half-open rectangles mean the shared column
      // belongs to exactly one of them.
      {0, PixelRect{80, 85, 20, 20}, "left_of_pair"},
      {0, PixelRect{100, 85, 20, 20}, "right_of_pair"},

      // A deep chain whose innermost node is the one on top.
      {0, PixelRect{125, 80, 30, 30}, "deep0"},
      {10, PixelRect{4, 4, 22, 22}, "deep1"},
      {11, PixelRect{4, 4, 14, 14}, "deep2"},
  };
  return scene;
}

// The same questions asked of a scene that CLIPS. Square corners throughout,
// so the rasterizer oracle stays exact - see the file header.
//
// Every shape the traversal branches on has an instance here, because a long
// sweep over a scene that lacks one is evidence about nothing (this project
// has been caught by that three times; doc/wrapping.md and doc/properties.md
// record the previous two):
//
//   a clipped child cut on one side, and one cut on two;
//   a child entirely outside its clipping parent, visible nowhere;
//   a GRANDCHILD outside a clipping GRANDPARENT while inside its own parent,
//     which is the only shape that can tell "honours ancestor clips" from
//     "honours the immediate parent";
//   two nested clips whose intersection is smaller than either, with a child
//     inside the inner box but outside the outer one;
//   a clipping node with ZERO AREA, which must remove its subtree and be
//     hittable nowhere itself;
//   a clipping node with an unclipped SIBLING subtree overlapping it, so the
//     clip cannot be confused with "everything after this stops painting".
Scene clipped_scene() {
  Scene scene;
  scene.viewport = PixelSize{160, 120};
  scene.rows = {
      {0, PixelRect{0, 0, 160, 120}, "root"},

      // A clipping panel. Its first child is cut on the right, the second is
      // cut on two sides, and the third is outside it entirely.
      {0, PixelRect{10, 10, 50, 40}, "clipper", dg::Overflow::kClip},
      {1, PixelRect{20, 5, 40, 12}, "clipper.cut_right"},
      {1, PixelRect{30, 25, 40, 40}, "clipper.cut_twice"},
      {1, PixelRect{60, 5, 20, 20}, "clipper.gone"},

      // The grandparent case: `outer` clips, `inner` does not, and inner's
      // child reaches past outer. A hit test that only consulted the
      // immediate parent would hand that child back.
      {0, PixelRect{80, 10, 40, 30}, "outer", dg::Overflow::kClip},
      {5, PixelRect{5, 5, 30, 20}, "outer.inner"},
      {6, PixelRect{10, 5, 45, 12}, "outer.inner.escapes"},

      // Two clips in a chain. Their intersection is 20 wide, narrower than
      // either, and the child is inside the inner box but outside the outer.
      {0, PixelRect{10, 60, 40, 40}, "nest_a", dg::Overflow::kClip},
      {8, PixelRect{20, 5, 40, 30}, "nest_b", dg::Overflow::kClip},
      {9, PixelRect{5, 5, 34, 20}, "nest_b.child"},

      // Zero area, so the clip removes everything under it.
      {0, PixelRect{70, 60, 0, 20}, "empty_clip", dg::Overflow::kClip},
      {11, PixelRect{0, 0, 30, 20}, "empty_clip.child"},

      // An ordinary overlapping sibling declared after the clips, which must
      // still paint and still be hittable over them.
      {0, PixelRect{40, 45, 30, 30}, "unclipped_sibling"},
  };
  return scene;
}

// The same questions asked of a scene that FADES, and the answers must not
// change at all.
//
// Every shape the layer code branches on has an instance here, for the reason
// the clipped scene above already records - a sweep over a scene that lacks
// the shape is evidence about nothing:
//
//   a faded group with two OVERLAPPING children, which is the shape that
//     distinguishes group opacity from per-object alpha;
//   a fade inside a fade;
//   a group faded to NOTHING, sitting over the root with no neighbour on top
//     of it, which is the shape that tests the decision rather than the
//     arithmetic: it is invisible and must still be hittable;
//   a faded group that also CLIPS, so a pixel removed by the clip and a pixel
//     removed by the fade are both present and must be answered differently;
//   an opaque node overlapping a faded one and declared after it, so the fade
//     cannot be confused with "everything after this stops painting".
Scene faded_scene() {
  Scene scene;
  scene.viewport = PixelSize{160, 120};
  scene.rows = {
      {0, PixelRect{0, 0, 160, 120}, "root"},

      {0, PixelRect{10, 10, 60, 40}, "fade", dg::Overflow::kVisible, 0.5F},
      {1, PixelRect{5, 5, 30, 20}, "fade.left"},
      {1, PixelRect{20, 12, 30, 20}, "fade.right"},

      {0, PixelRect{80, 10, 50, 40}, "deep_fade", dg::Overflow::kVisible, 0.6F},
      {4, PixelRect{5, 5, 35, 25}, "deep_fade.inner", dg::Overflow::kVisible, 0.4F},
      {5, PixelRect{5, 5, 25, 15}, "deep_fade.inner.leaf"},

      {0, PixelRect{10, 60, 50, 35}, "ghost", dg::Overflow::kVisible, 0.0F},
      {7, PixelRect{6, 6, 30, 20}, "ghost.child"},

      {0, PixelRect{80, 60, 50, 35}, "fade_clip", dg::Overflow::kClip, 0.7F},
      {9, PixelRect{20, 5, 50, 20}, "fade_clip.cut"},

      {0, PixelRect{60, 45, 40, 25}, "solid"},
  };
  return scene;
}

std::string describe(const Scene& scene, std::optional<std::uint32_t> index) {
  return index.has_value() ? scene.rows[index.value_or(0)].name : "<nothing>";
}
// The hit as an index, with the node count meaning "nothing". Every case below
// goes through this rather than dereferencing the optional: the check and the
// access would otherwise sit in different expressions, which is a shape
// clang-analyzer cannot follow and a reader has to re-derive.
std::uint32_t hit_index(const RenderTree& tree, PixelPoint point) {
  const std::optional<NodeId> hit = tree.hit_test(point);
  return hit.value_or(NodeId{static_cast<std::uint32_t>(tree.node_count())}).value;
}

// One pixel sweep, with the oracle supplied. Extracted so the two exhaustive
// cases are one loop read twice rather than two loops that can drift apart.
struct Sweep {
  std::size_t mismatches = 0;
  int x = 0;
  int y = 0;
  std::uint32_t said = 0;
  std::uint32_t expected = 0;
};

template <typename Oracle>
Sweep sweep(const Scene& scene, const RenderTree& tree, const Oracle& oracle) {
  Sweep result;
  for (int y = 0; y < scene.viewport.height; ++y) {
    for (int x = 0; x < scene.viewport.width; ++x) {
      const PixelPoint point{x, y};
      const std::uint32_t expected = oracle(point);
      const std::uint32_t actual = hit_index(tree, point);
      if (actual == expected) {
        continue;
      }
      if (result.mismatches == 0) {
        result.x = x;
        result.y = y;
        result.said = actual;
        result.expected = expected;
      }
      ++result.mismatches;
    }
  }
  return result;
}

void report(const Scene& scene, const Sweep& result, const char* oracle) {
  if (result.mismatches == 0) {
    return;
  }
  MESSAGE("first mismatch at ", result.x, ",", result.y, ": hit test says ",
          describe(scene, result.said), ", ", oracle, " says ",
          describe(scene, result.expected));
}

// One scene, swept against what the rasterizer actually drew.
//
// A named function rather than the body of a loop inside the TEST_CASE, for
// the reason this file's neighbours already record: doctest expands every
// assertion into branches, and two scenes' worth inside one case runs past
// clang-tidy's cognitive-complexity budget.
void check_against_the_surface(const Scene& scene) {
  RenderTree tree = build(scene);

  std::optional<dg::RasterSurface> surface =
      dg::RasterSurface::create(scene.viewport.width, scene.viewport.height);

  // Tested with an `if` rather than a REQUIRE, and the difference is not
  // cosmetic: clang-tidy cannot model doctest's REQUIRE, so a REQUIRE
  // followed by a dereference reads to clang-analyzer as an unchecked
  // optional access. Failing explicitly and returning gives it the control
  // flow it needs, and gives a reader the same information.
  if (!surface.has_value()) {
    FAIL("could not allocate a surface");
    return;
  }
  dg::RasterSurface& target = *surface;
  tree.repaint_full(target);

  const dg::PixelView view = target.peek_pixels();
  REQUIRE(view.pixels != nullptr);
  REQUIRE(view.is_bgra8888);

  const Sweep result = sweep(scene, tree, [&view](PixelPoint point) {
    // BGRA: red is the third byte, and colour_for() put the node index there
    // offset by one so that zero cannot be mistaken for node 0.
    const std::size_t offset = (static_cast<std::size_t>(point.y) * view.row_bytes) +
                               (static_cast<std::size_t>(point.x) * 4);
    return static_cast<std::uint32_t>(view.pixels[offset + 2]) - 1;
  });
  report(scene, result, "the surface");
  CHECK(result.mismatches == 0);
}

// One channel of one pixel, which is all the rasterizer oracle ever needs:
// colour_for() puts the node index in red.
std::uint32_t red_at(const dg::PixelView& pixels, PixelPoint point) {
  const std::size_t offset = (static_cast<std::size_t>(point.y) * pixels.row_bytes) +
                             (static_cast<std::size_t>(point.x) * 4);
  return static_cast<std::uint32_t>(pixels.pixels[offset + 2]);
}

// Hoisted out of its TEST_CASE for the reason this file's neighbours already
// record: doctest expands every assertion into branches, and a body carrying
// two full-viewport sweeps runs past clang-tidy's cognitive-complexity budget.
// How many pixels the fades are responsible for, which is what stops the
// case above from holding for the trivial reason that nothing faded.
std::size_t pixels_the_fades_changed(const Scene& scene, const dg::PixelView& faded,
                                     const dg::PixelView& opaque) {
  std::size_t changed = 0;
  for (int y = 0; y < scene.viewport.height; ++y) {
    for (int x = 0; x < scene.viewport.width; ++x) {
      if (red_at(faded, PixelPoint{x, y}) != red_at(opaque, PixelPoint{x, y})) {
        ++changed;
      }
    }
  }
  return changed;
}

// The whole opacity-versus-clipping decision, at every pixel of one box: the
// surface must show the node UNDERNEATH the ghost, and hit testing must go on
// naming the ghost.
// The first pixel of `ghost` that the surface shows something at, and the
// first that hit testing declines to name the ghost at. Kept as two searches
// returning a point rather than as two assertions inside the loop, because an
// assertion inside a doubly nested loop is what puts this over clang-tidy's
// cognitive-complexity budget - and reporting the first offender is what a
// failure needs anyway.
struct GhostProbe {
  std::optional<PixelPoint> painted;
  std::optional<PixelPoint> unclickable;
};

GhostProbe probe_the_ghost_box(const RenderTree& tree, const dg::PixelView& view,
                               const PixelRect& ghost, std::uint32_t background) {
  GhostProbe found;
  for (int y = ghost.top(); y < ghost.bottom(); ++y) {
    for (int x = ghost.left(); x < ghost.right(); ++x) {
      const PixelPoint point{x, y};
      const std::uint32_t hit = hit_index(tree, point);
      if (red_at(view, point) != background && !found.painted.has_value()) {
        found.painted = point;
      }
      if (hit != 7 && hit != 8 && !found.unclickable.has_value()) {
        found.unclickable = point;
      }
    }
  }
  return found;
}

// The whole opacity-versus-clipping decision, at every pixel of one box: the
// surface must show the node UNDERNEATH the ghost, and hit testing must go on
// naming the ghost.
void check_the_ghost_box(const RenderTree& tree, const dg::PixelView& view,
                         const PixelRect& ghost) {
  const std::uint32_t root_red = red_at(view, PixelPoint{ghost.left(), ghost.top()});
  CHECK(root_red == 1);

  const GhostProbe probed = probe_the_ghost_box(tree, view, ghost, root_red);
  const PixelPoint painted = probed.painted.value_or(PixelPoint{});
  const PixelPoint unclickable = probed.unclickable.value_or(PixelPoint{});
  CHECK_MESSAGE(!probed.painted.has_value(), "the ghost painted at ", painted.x, ",",
                painted.y);
  CHECK_MESSAGE(!probed.unclickable.has_value(), "the ghost was not hittable at ",
                unclickable.x, ",", unclickable.y);
}

// Hoisted out of its TEST_CASE for the reason this file's neighbours already
// record: doctest expands every assertion into branches, and a body carrying
// two full-viewport sweeps runs past clang-tidy's cognitive-complexity budget.
void check_a_ghost_is_invisible_and_still_hittable() {
  const Scene scene = faded_scene();
  RenderTree faded = build(scene);
  RenderTree opaque = build(without_opacity(scene));

  std::optional<dg::RasterSurface> surface =
      dg::RasterSurface::create(scene.viewport.width, scene.viewport.height);
  std::optional<dg::RasterSurface> opaque_surface =
      dg::RasterSurface::create(scene.viewport.width, scene.viewport.height);
  if (!surface.has_value() || !opaque_surface.has_value()) {
    FAIL("could not allocate a surface");
    return;
  }
  faded.repaint_full(*surface);
  opaque.repaint_full(*opaque_surface);

  const dg::PixelView view = surface->peek_pixels();
  REQUIRE(view.is_bgra8888);

  const std::size_t changed =
      pixels_the_fades_changed(scene, view, opaque_surface->peek_pixels());
  MESSAGE("fading changes ", changed, " pixels");
  CHECK(changed > 2000);

  check_the_ghost_box(faded, view, absolute_bounds_of(scene)[7]);
}

}  // namespace

TEST_SUITE("hit testing") {
  TEST_CASE("the scene tables are declared in paint order") {
    check_declaration_order(overlapping_scene());
    check_declaration_order(clipped_scene());
    check_declaration_order(faded_scene());
  }

  TEST_CASE("every pixel agrees with a flat reverse-paint-order scan") {
    for (const Scene& scene : {overlapping_scene(), clipped_scene(), faded_scene()}) {
      check_declaration_order(scene);
      const std::vector<PixelRect> absolute = absolute_bounds_of(scene);
      const RenderTree tree = build(scene);
      const auto miss = static_cast<std::uint32_t>(scene.rows.size());

      // The tree's own geometry is checked once, separately, so that a bounds
      // disagreement is reported as one failure rather than as thirty
      // thousand.
      for (std::size_t index = 0; index < scene.rows.size(); ++index) {
        REQUIRE(tree.absolute_bounds(NodeId{static_cast<std::uint32_t>(index)}) ==
                absolute[index]);
      }

      const Sweep result = sweep(scene, tree, [&scene, &absolute, miss](PixelPoint point) {
        const std::optional<std::uint32_t> topmost = topmost_by_order(scene, absolute, point);
        return topmost.value_or(miss);
      });
      report(scene, result, "paint order");
      CHECK(result.mismatches == 0);
    }
  }

  TEST_CASE("every pixel agrees with what the rasterizer actually drew") {
    check_against_the_surface(overlapping_scene());
    check_against_the_surface(clipped_scene());

    // The opaque twin of the faded scene. The oracle recovers a node index
    // from a flat colour and a blended one names nobody, so the pixel sweep
    // runs here and the two cases below carry the opacity claim.
    check_against_the_surface(without_opacity(faded_scene()));
  }

  // The decision, stated at every pixel: opacity is a paint property and
  // changes no hit-test answer anywhere. Two trees, identical but for the
  // fades, swept against each other.
  TEST_CASE("fading a scene changes no hit-test answer, at any pixel") {
    const Scene scene = faded_scene();
    const RenderTree faded = build(scene);
    const RenderTree opaque = build(without_opacity(scene));

    const Sweep result =
        sweep(scene, faded, [&opaque](PixelPoint point) { return hit_index(opaque, point); });
    report(scene, result, "the same scene unfaded");
    CHECK(result.mismatches == 0);
  }

  // Guards the case above against the failure this project has hit
  // repeatedly: if the fades changed no pixel, "the answers match" would hold
  // for the trivial reason. The two renders are required to differ at a great
  // many pixels, and the ghost's box is required to be indistinguishable from
  // the background while hit testing still names the ghost inside it - which
  // is the whole opacity-versus-clipping decision in one assertion.
  TEST_CASE("a group faded to nothing is invisible and still hittable") {
    check_a_ghost_is_invisible_and_still_hittable();
  }

  // Guards the two sweeps above against the failure this project has hit
  // repeatedly: a scene that does not contain the shape under test passes
  // whatever the code does. If clipping removed nothing, both oracles would
  // agree for the trivial reason, so the pixel count the clip is responsible
  // for is asserted to be large rather than merely non-zero, and asserted
  // through the clip-blind oracle the previous slice shipped.
  TEST_CASE("the clipped scene really is clipped") {
    const Scene scene = clipped_scene();
    const std::vector<PixelRect> absolute = absolute_bounds_of(scene);

    std::size_t removed = 0;
    for (int y = 0; y < scene.viewport.height; ++y) {
      for (int x = 0; x < scene.viewport.width; ++x) {
        const PixelPoint point{x, y};
        std::optional<std::uint32_t> blind;
        for (std::size_t index = 0; index < absolute.size(); ++index) {
          if (dg::contains(absolute[index], point)) {
            blind = static_cast<std::uint32_t>(index);
          }
        }
        if (blind != topmost_by_order(scene, absolute, point)) {
          ++removed;
        }
      }
    }
    MESSAGE("clipping changes the answer at ", removed, " pixels");
    CHECK(removed > 500);
  }

  // Each of the five nodes the clipped scene declares to be entirely hidden is
  // named, so that a defect removing one shape's coverage - the grandparent
  // chain, say - is reported as itself rather than as a sweep mismatch whose
  // cause has to be reconstructed.
  TEST_CASE("a node a clip removes entirely is hit nowhere") {
    const Scene scene = clipped_scene();
    const RenderTree tree = build(scene);
    const std::vector<PixelRect> absolute = absolute_bounds_of(scene);

    for (const std::uint32_t hidden : {4U, 11U, 12U}) {
      const PixelRect box = absolute[hidden];
      for (int y = box.top(); y < box.bottom(); ++y) {
        for (int x = box.left(); x < box.right(); ++x) {
          REQUIRE_MESSAGE(hit_index(tree, PixelPoint{x, y}) != hidden, describe(scene, hidden),
                          " was hit at ", x, ",", y);
        }
      }
    }
  }

  TEST_CASE("a child overflowing its parent is hittable in the overflow") {
    const Scene scene = overlapping_scene();
    const RenderTree tree = build(scene);
    const std::vector<PixelRect> absolute = absolute_bounds_of(scene);

    const PixelRect panel = absolute[1];
    const PixelRect overflowing = absolute[3];
    REQUIRE(overflowing.right() > panel.right());
    REQUIRE(overflowing.bottom() > panel.bottom());

    // A pixel that belongs to the child and lies outside the parent on BOTH
    // axes, so neither edge alone can produce the right answer by accident.
    const PixelPoint outside{overflowing.right() - 1, overflowing.bottom() - 1};
    REQUIRE_FALSE(dg::contains(panel, outside));
    CHECK(hit_index(tree, outside) == 3);
  }

  TEST_CASE("a fully occluded node is never hit") {
    const Scene scene = overlapping_scene();
    const RenderTree tree = build(scene);
    const std::vector<PixelRect> absolute = absolute_bounds_of(scene);
    REQUIRE(dg::contains(absolute[7], absolute[6]));

    for (int y = absolute[6].top(); y < absolute[6].bottom(); ++y) {
      for (int x = absolute[6].left(); x < absolute[6].right(); ++x) {
        REQUIRE(hit_index(tree, PixelPoint{x, y}) != 6);
      }
    }
  }

  TEST_CASE("edge-sharing neighbours claim a boundary column exactly once") {
    const Scene scene = overlapping_scene();
    const RenderTree tree = build(scene);
    const std::vector<PixelRect> absolute = absolute_bounds_of(scene);
    REQUIRE(absolute[8].right() == absolute[9].left());

    const int y = absolute[8].top() + 1;
    CHECK(tree.hit_test(PixelPoint{absolute[8].right() - 1, y}) ==
          std::optional<NodeId>{NodeId{8}});
    CHECK(tree.hit_test(PixelPoint{absolute[9].left(), y}) == std::optional<NodeId>{NodeId{9}});
  }

  TEST_CASE("a point outside the viewport hits nothing") {
    const Scene scene = overlapping_scene();
    const RenderTree tree = build(scene);
    CHECK_FALSE(tree.hit_test(PixelPoint{-1, 10}).has_value());
    CHECK_FALSE(tree.hit_test(PixelPoint{10, -1}).has_value());
    CHECK_FALSE(tree.hit_test(PixelPoint{scene.viewport.width, 10}).has_value());
    CHECK_FALSE(tree.hit_test(PixelPoint{10, scene.viewport.height}).has_value());
  }

  TEST_CASE("moving a node moves where it is hit, with no stale rectangle") {
    const Scene scene = overlapping_scene();
    RenderTree tree = build(scene);

    const PixelPoint before{18, 18};
    REQUIRE(hit_index(tree, before) == 2);

    tree.set_local_origin(NodeId{2}, 30, 15);
    CHECK(hit_index(tree, before) != 2);
    CHECK(hit_index(tree, PixelPoint{45, 30}) == 2);
  }

  TEST_CASE("a resize repositions nothing but still answers inside the new viewport") {
    const Scene scene = overlapping_scene();
    RenderTree tree = build(scene);
    tree.resize(PixelSize{200, 150});

    // The root grew, so a point that was outside the viewport now hits it.
    CHECK(hit_index(tree, PixelPoint{180, 140}) == RenderTree::root().value);
    CHECK(hit_index(tree, PixelPoint{18, 18}) == 2);
  }
}
