// What `overflow` MEANS, pinned to numbers derived by hand.
//
// The byte-identity gate next door cannot answer this and it is important to
// say why: it drives two copies of the same code - an incremental repaint and
// a full one - so a clip that cut one pixel too many would corrupt both
// equally and the comparison would still hold. It verifies that the pixels
// skipped really did not change; it cannot verify that the pixels drawn are
// the right ones. Slice 3 recorded the same structural limit for widget
// semantics and it applies unchanged here.
//
// So every expectation below is a coordinate worked out from the scene's own
// geometry and written into the test, not read back from the engine. The
// boundary cases are stated as the LAST pixel kept and the FIRST pixel
// removed, because an off-by-one in a clip is the defect this is for and a
// test that only probes the middle of a region cannot see one.
//
// Both readers are asked at every point: what the surface shows, and what
// hit_test answers. A clip that painting honoured and hit testing did not
// would pass a paint-only test.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/graphics/types.h"
#include "drawgui/render/render_tree.h"

#include "render/clip_shape.h"

namespace {

using dg::Color;
using dg::NodeId;
using dg::Overflow;
using dg::PixelPoint;
using dg::PixelRect;
using dg::PixelSize;
using dg::RenderTree;

constexpr Color kBack = Color::rgba(0x10, 0x20, 0x30);
constexpr Color kParent = Color::rgba(0x40, 0x80, 0xC0);
constexpr Color kChild = Color::rgba(0xE0, 0xC0, 0x40);

dg::NodeStyle filled(Color fill, Overflow overflow = Overflow::kVisible) {
  dg::NodeStyle style;
  style.fill = fill;
  style.overflow = overflow;
  return style;
}

// The painted surface, kept beside the tree so the two answers are read from
// one arrangement rather than from two that could drift.
class Painted {
 public:
  explicit Painted(RenderTree& tree)
      : surface_(dg::RasterSurface::create(tree.viewport().width, tree.viewport().height)) {
    if (surface_.has_value()) {
      tree.repaint_full(*surface_);
      view_ = surface_->peek_pixels();
    }
  }

  [[nodiscard]] bool ok() const { return surface_.has_value() && view_.pixels != nullptr; }

  // Opaque BGRA, so a pixel is one node's flat fill unless anti-aliasing blended
  // two - which only a rounded shape can do.
  [[nodiscard]] Color at(PixelPoint point) const {
    const std::size_t offset = (static_cast<std::size_t>(point.y) * view_.row_bytes) +
                               (static_cast<std::size_t>(point.x) * 4);
    return Color::rgba(view_.pixels[offset + 2], view_.pixels[offset + 1],
                       view_.pixels[offset]);
  }

 private:
  std::optional<dg::RasterSurface> surface_;
  dg::PixelView view_{};
};

// The rounded clip under test, at namespace scope so the corner predicate can
// name it without a capture.
constexpr PixelRect kRoundBox{20, 20, 80, 60};
constexpr int kRoundRadius = 16;

// The four squares the corner arcs live in. Anti-aliasing may only appear
// here: along a straight edge the clip lands on a pixel boundary and there is
// no fraction to blend.
bool in_round_corner(PixelPoint point) {
  const bool horizontal =
      point.x < kRoundBox.left() + kRoundRadius || point.x >= kRoundBox.right() - kRoundRadius;
  const bool vertical =
      point.y < kRoundBox.top() + kRoundRadius || point.y >= kRoundBox.bottom() - kRoundRadius;
  return horizontal && vertical;
}

// The first place a node the clip removed reached the screen or the hit test,
// or an empty string when none did.
//
// Hoisted out of the case that uses it because doctest expands each assertion
// into branches and a nested loop of them runs past clang-tidy's
// cognitive-complexity budget - the same reason test_text_damage.cpp and
// test_font_coverage.cpp hoist theirs.
std::string find_visible(const RenderTree& tree, const Painted& painted,
                         const std::vector<NodeId>& hidden);

std::uint32_t hit_index(const RenderTree& tree, PixelPoint point) {
  const std::optional<NodeId> hit = tree.hit_test(point);
  return hit.value_or(NodeId{static_cast<std::uint32_t>(tree.node_count())}).index;
}

// Every pixel of a rounded clip, split three ways. Fully covered and fully
// uncovered are decidable and must agree with hit testing; the anti-aliased
// band between them is not, and is required to exist and to stay inside the
// corner squares.
struct CurveTally {
  std::size_t covered = 0;
  std::size_t blended = 0;
  std::size_t wrong = 0;
  std::string first;
};

// Counted rather than asserted per pixel, and reported once by the caller.
// doctest expands every assertion into branches, so a REQUIRE inside a nested
// loop runs past clang-tidy's cognitive-complexity budget - and a sweep that
// stopped at the first bad pixel would say nothing about how many there were.
// What is wrong with one pixel of a rounded clip, or nullptr when nothing is.
//
// Fully covered must be hittable and fully uncovered must not; a pixel that
// is neither is on the anti-aliased curve, where "was this painted" has no
// yes-or-no answer, and all that can be required of it is that it is in a
// corner.
const char* classify(Color colour, bool hit, bool in_corner) {
  if (colour == kChild) {
    return hit ? nullptr : "painted but not hittable";
  }
  if (colour == kBack) {
    return hit ? "clipped away but hittable" : nullptr;
  }
  return in_corner ? nullptr : "the curve bled outside a corner";
}

CurveTally sweep_curve(const RenderTree& tree, const Painted& painted, NodeId child) {
  CurveTally tally;
  const PixelSize viewport = tree.viewport();
  for (int y = 0; y < viewport.height; ++y) {
    for (int x = 0; x < viewport.width; ++x) {
      const PixelPoint point{x, y};
      const Color colour = painted.at(point);
      const bool hit = hit_index(tree, point) == child.index;
      tally.covered += colour == kChild ? 1U : 0U;
      tally.blended += (colour != kChild && colour != kBack) ? 1U : 0U;
      const char* wrong = classify(colour, hit, in_round_corner(point));
      if (wrong != nullptr) {
        ++tally.wrong;
        if (tally.first.empty()) {
          tally.first =
              std::string{wrong} + " at " + std::to_string(x) + "," + std::to_string(y);
        }
      }
    }
  }
  return tally;
}

std::string find_visible(const RenderTree& tree, const Painted& painted,
                         const std::vector<NodeId>& hidden) {
  const PixelSize viewport = tree.viewport();
  for (int y = 0; y < viewport.height; ++y) {
    for (int x = 0; x < viewport.width; ++x) {
      const PixelPoint point{x, y};
      const std::string where = std::to_string(x) + "," + std::to_string(y);
      if (painted.at(point) == kChild) {
        return "a hidden child painted at " + where;
      }
      const std::uint32_t hit = hit_index(tree, point);
      const bool reachable =
          std::ranges::any_of(hidden, [hit](NodeId id) { return hit == id.index; });
      if (reachable) {
        return "a hidden node was hit at " + where;
      }
    }
  }
  return {};
}

// One parent holding one child that overruns it to the right and below.
//
//   parent  absolute 20,20 .. 60,50   (40 x 30)
//   child   absolute 40,30 .. 80,60   (40 x 30)
//
// So the child is inside on 40..60 x 30..50 and outside on both axes past
// that, which is what makes a one-sided defect visible.
struct Overrun {
  RenderTree tree;
  NodeId parent;
  NodeId child;

  explicit Overrun(Overflow overflow)
      : tree(spec()),
        parent(tree.add_child(RenderTree::root(), PixelRect{20, 20, 40, 30},
                              filled(kParent, overflow))),
        child(tree.add_child(parent, PixelRect{20, 10, 40, 30}, filled(kChild))) {}

  static dg::TreeSpec spec() {
    dg::TreeSpec built;
    built.viewport = PixelSize{120, 80};
    built.background = filled(kBack);
    return built;
  }
};

}  // namespace

TEST_SUITE("clipping") {
  TEST_CASE("overflow=visible leaves an overrunning child painted and hittable outside") {
    Overrun scene{Overflow::kVisible};
    const Painted painted{scene.tree};
    REQUIRE(painted.ok());

    REQUIRE(scene.tree.absolute_bounds(scene.parent) == PixelRect{20, 20, 40, 30});
    REQUIRE(scene.tree.absolute_bounds(scene.child) == PixelRect{40, 30, 40, 30});

    // Beyond the parent on BOTH axes, so neither edge alone can produce the
    // right answer by accident.
    const PixelPoint beyond{70, 55};
    CHECK(painted.at(beyond) == kChild);
    CHECK(hit_index(scene.tree, beyond) == scene.child.index);

    // The far corner of the child, three pixels outside the parent's own.
    CHECK(painted.at(PixelPoint{79, 59}) == kChild);
    CHECK(hit_index(scene.tree, PixelPoint{79, 59}) == scene.child.index);
  }

  TEST_CASE("overflow=clip cuts the child at the parent's edge, to the pixel") {
    Overrun scene{Overflow::kClip};
    const Painted painted{scene.tree};
    REQUIRE(painted.ok());

    const PixelRect parent = scene.tree.absolute_bounds(scene.parent);
    const auto root = RenderTree::root().index;

    // Horizontally: the parent ends at x = 60, so column 59 is the last one
    // the child keeps and column 60 is the first it loses. Both rows are
    // inside the child vertically, so only the horizontal edge is under test.
    const int y = 40;
    CHECK(parent.right() == 60);
    CHECK(painted.at(PixelPoint{59, y}) == kChild);
    CHECK(hit_index(scene.tree, PixelPoint{59, y}) == scene.child.index);
    CHECK(painted.at(PixelPoint{60, y}) == kBack);
    CHECK(hit_index(scene.tree, PixelPoint{60, y}) == root);

    // Vertically: the parent ends at y = 50.
    const int x = 50;
    CHECK(parent.bottom() == 50);
    CHECK(painted.at(PixelPoint{x, 49}) == kChild);
    CHECK(hit_index(scene.tree, PixelPoint{x, 49}) == scene.child.index);
    CHECK(painted.at(PixelPoint{x, 50}) == kBack);
    CHECK(hit_index(scene.tree, PixelPoint{x, 50}) == root);

    // The corner the previous test found the child in is now background, and
    // the clip did not eat the parent itself: 30,25 is parent-only territory.
    CHECK(painted.at(PixelPoint{70, 55}) == kBack);
    CHECK(hit_index(scene.tree, PixelPoint{70, 55}) == root);
    CHECK(painted.at(PixelPoint{30, 25}) == kParent);
    CHECK(hit_index(scene.tree, PixelPoint{30, 25}) == scene.parent.index);
  }

  // A clip inside a clip is the INTERSECTION of both. The two failure modes
  // are "only the innermost is applied" and "only the outermost is", and the
  // geometry is chosen so each one shows at a different edge:
  //
  //   outer  10,10 .. 70,50      inner  40,15 .. 100,55
  //   intersection                      40,15 ..  70,50
  //
  // The inner box reaches past the outer on the right and below; the outer
  // reaches past the inner on the left and above. The grandchild covers all
  // of it and more.
  TEST_CASE("nested clips intersect rather than the innermost winning") {
    dg::TreeSpec spec;
    spec.viewport = PixelSize{120, 80};
    spec.background = filled(kBack);
    RenderTree tree{spec};
    const NodeId outer = tree.add_child(RenderTree::root(), PixelRect{10, 10, 60, 40},
                                        filled(kParent, Overflow::kClip));
    const NodeId inner =
        tree.add_child(outer, PixelRect{30, 5, 60, 40}, filled(kParent, Overflow::kClip));
    const NodeId grandchild = tree.add_child(inner, PixelRect{-20, -5, 90, 60}, filled(kChild));

    REQUIRE(tree.absolute_bounds(outer) == PixelRect{10, 10, 60, 40});
    REQUIRE(tree.absolute_bounds(inner) == PixelRect{40, 15, 60, 40});
    REQUIRE(tree.absolute_bounds(grandchild) == PixelRect{20, 10, 90, 60});

    const Painted painted{tree};
    REQUIRE(painted.ok());
    const auto root = RenderTree::root().index;

    // Inside the intersection.
    CHECK(painted.at(PixelPoint{40, 15}) == kChild);
    CHECK(hit_index(tree, PixelPoint{40, 15}) == grandchild.index);
    CHECK(painted.at(PixelPoint{69, 49}) == kChild);
    CHECK(hit_index(tree, PixelPoint{69, 49}) == grandchild.index);

    // The INNER edges: one column left of the inner box and one row above it,
    // both still well inside the outer one. Only the inner clip can remove
    // these, so "the outermost wins" fails here.
    CHECK(painted.at(PixelPoint{39, 20}) == kParent);
    CHECK(hit_index(tree, PixelPoint{39, 20}) == outer.index);
    CHECK(painted.at(PixelPoint{45, 14}) == kParent);
    CHECK(hit_index(tree, PixelPoint{45, 14}) == outer.index);

    // The OUTER edges: the inner box and the grandchild both reach past x=70
    // and y=50, so only the outer clip can remove these. "The innermost wins"
    // fails here.
    CHECK(painted.at(PixelPoint{70, 20}) == kBack);
    CHECK(hit_index(tree, PixelPoint{70, 20}) == root);
    CHECK(painted.at(PixelPoint{50, 50}) == kBack);
    CHECK(hit_index(tree, PixelPoint{50, 50}) == root);
  }

  // The shape that distinguishes "honours ancestor clips" from "honours the
  // immediate parent": the clip is on the GRANDPARENT and the parent in
  // between does not clip at all.
  TEST_CASE("a clip on a grandparent removes a grandchild its own parent admits") {
    dg::TreeSpec spec;
    spec.viewport = PixelSize{120, 80};
    spec.background = filled(kBack);
    RenderTree tree{spec};
    const NodeId grandparent = tree.add_child(RenderTree::root(), PixelRect{10, 10, 40, 40},
                                              filled(kParent, Overflow::kClip));
    const NodeId parent = tree.add_child(grandparent, PixelRect{5, 5, 30, 30}, filled(kParent));
    const NodeId child = tree.add_child(parent, PixelRect{10, 5, 60, 20}, filled(kChild));

    REQUIRE(tree.absolute_bounds(grandparent) == PixelRect{10, 10, 40, 40});
    REQUIRE(tree.absolute_bounds(parent) == PixelRect{15, 15, 30, 30});
    REQUIRE(tree.absolute_bounds(child) == PixelRect{25, 20, 60, 20});

    const Painted painted{tree};
    REQUIRE(painted.ok());

    // x = 46 is outside the immediate parent (which ends at 45) and inside
    // the grandparent (which ends at 50), so it must still be the child.
    CHECK(painted.at(PixelPoint{46, 25}) == kChild);
    CHECK(hit_index(tree, PixelPoint{46, 25}) == child.index);

    // x = 50 is inside the child's own bounds and outside the grandparent's.
    // Only an ancestor walk removes it.
    CHECK(painted.at(PixelPoint{50, 25}) == kBack);
    CHECK(hit_index(tree, PixelPoint{50, 25}) == RenderTree::root().index);
    CHECK(painted.at(PixelPoint{84, 25}) == kBack);
    CHECK(hit_index(tree, PixelPoint{84, 25}) == RenderTree::root().index);
  }

  // A clip of zero area removes everything, is hittable nowhere, and above all
  // reaches neither Skia nor the rounded-rectangle constructor with a
  // degenerate box. Both degeneracies are covered: zero width, and a
  // subtree clipped down to nothing by an ancestor further up.
  TEST_CASE("a clip with no area paints nothing and is hit nowhere") {
    dg::TreeSpec spec;
    spec.viewport = PixelSize{120, 80};
    spec.background = filled(kBack);
    RenderTree tree{spec};

    dg::NodeStyle rounded = filled(kParent, Overflow::kClip);
    rounded.radii = dg::Radii::all(8.0F);
    const NodeId flat = tree.add_child(RenderTree::root(), PixelRect{20, 20, 0, 30}, rounded);
    const NodeId under_flat = tree.add_child(flat, PixelRect{0, 0, 40, 30}, filled(kChild));

    // A clip whose own box is fine but which an ancestor has already reduced
    // to nothing.
    const NodeId near = tree.add_child(RenderTree::root(), PixelRect{60, 20, 20, 20},
                                       filled(kParent, Overflow::kClip));
    const NodeId far =
        tree.add_child(near, PixelRect{40, 0, 20, 20}, filled(kParent, Overflow::kClip));
    const NodeId under_far = tree.add_child(far, PixelRect{0, 0, 20, 20}, filled(kChild));

    const Painted painted{tree};
    REQUIRE(painted.ok());
    CHECK(find_visible(tree, painted, {under_flat, under_far, flat, far}).empty());

    // The clip that has area is still there and still works, so the sweep
    // above is not passing because the whole scene is empty.
    CHECK(painted.at(PixelPoint{65, 25}) == kParent);
    CHECK(hit_index(tree, PixelPoint{65, 25}) == near.index);
  }

  // The rounded clip, checked against the rasterizer's own coverage.
  //
  // This is where the painting-equals-hit-testing equivalence has to be
  // stated carefully, because an anti-aliased curve makes "was this pixel
  // painted" a question with three answers. Fully covered and fully uncovered
  // are decidable and are required to agree with hit testing at EVERY pixel.
  // The band between them is not, and rather than exclude it quietly the test
  // requires it to exist - a zero-width band would mean the clip is not
  // anti-aliased at all - and requires every pixel in it to sit in one of the
  // four corner squares, which is the machine-checkable form of "it follows
  // the curve and nothing bleeds along the straight edges".
  //
  // The clipper's own fill is transparent so that only two colours can reach
  // the surface, which is what makes "fully covered" readable as an exact
  // byte comparison rather than as a threshold.
  TEST_CASE("a rounded clip follows its curve, and hit testing follows the same curve") {
    dg::TreeSpec spec;
    spec.viewport = PixelSize{120, 100};
    spec.background = filled(kBack);
    RenderTree tree{spec};

    dg::NodeStyle clipper = filled(Color::rgba(0, 0, 0, 0), Overflow::kClip);
    clipper.radii = dg::Radii::all(kRoundRadius);
    const NodeId shape = tree.add_child(RenderTree::root(), kRoundBox, clipper);
    const NodeId child = tree.add_child(shape, PixelRect{-10, -10, 100, 80}, filled(kChild));
    REQUIRE(tree.absolute_bounds(child) == PixelRect{10, 10, 100, 80});

    const Painted painted{tree};
    REQUIRE(painted.ok());

    const CurveTally tally = sweep_curve(tree, painted, child);
    MESSAGE("rounded clip: ", tally.covered, " fully covered, ", tally.blended,
            " on the curve");
    CHECK_MESSAGE(tally.wrong == 0, tally.wrong, " disagreements, first: ", tally.first);
    CHECK(tally.covered > 4000);
    CHECK(tally.blended > 0);

    // Hand-derived, in the top-left corner square, so this says "the curve"
    // rather than "the bounding box". The corner circle is centred at
    // (20+16, 20+16) = (36,36) with radius 16.
    //   pixel 22,22 -> centre 22.5,22.5 -> 13.5^2 + 13.5^2 = 364.5 > 256 : out
    //   pixel 26,26 -> centre 26.5,26.5 ->  9.5^2 +  9.5^2 = 180.5 < 256 : in
    // Both are inside the bounding box, so a clip that used it would keep
    // both and a clip that used the curve keeps exactly one.
    CHECK(painted.at(PixelPoint{22, 22}) == kBack);
    CHECK(hit_index(tree, PixelPoint{22, 22}) != child.index);
    CHECK(painted.at(PixelPoint{26, 26}) == kChild);
    CHECK(hit_index(tree, PixelPoint{26, 26}) == child.index);
  }

  // Radii wider than the box they round. Skia scales all four by one factor
  // until they fit, and the hit test has to scale identically or it answers
  // for a shape that was never drawn. 40 wide with radii of 100 scales by
  // 40/200 = 0.2, giving four radii of 20 - a circle inscribed in the box.
  TEST_CASE("oversized radii are scaled to fit by painting and by hit testing alike") {
    dg::TreeSpec spec;
    spec.viewport = PixelSize{60, 60};
    spec.background = filled(kBack);
    RenderTree tree{spec};

    dg::NodeStyle clipper = filled(Color::rgba(0, 0, 0, 0), Overflow::kClip);
    clipper.radii = dg::Radii::all(100.0F);
    const NodeId shape = tree.add_child(RenderTree::root(), PixelRect{0, 0, 40, 40}, clipper);
    const NodeId child = tree.add_child(shape, PixelRect{0, 0, 40, 40}, filled(kChild));

    const Painted painted{tree};
    REQUIRE(painted.ok());

    // Every probe is worked out against the SCALED circle, centre (20,20)
    // radius 20, and every one is a whole pixel clear of it so that no
    // anti-aliased fraction is being compared:
    //
    //   0,0  farthest corner 1,1  -> 26.9 from the centre : wholly outside
    //   4,4  nearest  corner 5,5  -> 21.2                 : wholly outside
    //   6,6  farthest corner 7,7  -> 19.8                 : wholly inside
    //   20,3 farthest corner 21,3 -> 17.0                 : wholly inside
    //
    // 4,4 and 6,6 are two pixels apart inside the same corner, which is what
    // makes this a statement about the radius rather than about the box. A
    // hit test that skipped the scaling would be asking about a circle of
    // radius 100 centred at (100,100) and would refuse both.
    CHECK(painted.at(PixelPoint{0, 0}) == kBack);
    CHECK(hit_index(tree, PixelPoint{0, 0}) != child.index);
    CHECK(painted.at(PixelPoint{4, 4}) == kBack);
    CHECK(hit_index(tree, PixelPoint{4, 4}) != child.index);
    CHECK(painted.at(PixelPoint{6, 6}) == kChild);
    CHECK(hit_index(tree, PixelPoint{6, 6}) == child.index);
    CHECK(painted.at(PixelPoint{20, 3}) == kChild);
    CHECK(hit_index(tree, PixelPoint{20, 3}) == child.index);
  }

  // A negative radius is not a smaller corner, it is a value with no meaning,
  // and letting one through would put the corner circle's centre outside the
  // box. Clamped to zero, so the clip is the plain rectangle - which also
  // routes it through clipRect rather than clipRRect.
  TEST_CASE("a negative radius clamps to a square corner rather than bulging") {
    dg::TreeSpec spec;
    spec.viewport = PixelSize{60, 60};
    spec.background = filled(kBack);
    RenderTree tree{spec};

    dg::NodeStyle clipper = filled(Color::rgba(0, 0, 0, 0), Overflow::kClip);
    clipper.radii = dg::Radii::all(-9.0F);
    const NodeId shape = tree.add_child(RenderTree::root(), PixelRect{10, 10, 30, 30}, clipper);
    const NodeId child = tree.add_child(shape, PixelRect{-5, -5, 50, 50}, filled(kChild));

    const Painted painted{tree};
    REQUIRE(painted.ok());

    CHECK(painted.at(PixelPoint{10, 10}) == kChild);
    CHECK(hit_index(tree, PixelPoint{10, 10}) == child.index);
    CHECK(painted.at(PixelPoint{39, 39}) == kChild);
    CHECK(hit_index(tree, PixelPoint{39, 39}) == child.index);
    CHECK(painted.at(PixelPoint{9, 9}) == kBack);
    CHECK(painted.at(PixelPoint{40, 40}) == kBack);
  }
}

// The clip geometry on its own, without a rasterizer or a hit test in front
// of it.
//
// These exist because two defect injections survived everything else. Both
// times the reason was the same and it is worth stating: `fit_radii` has two
// consumers that INDEPENDENTLY tolerate a bad answer - Skia clamps a negative
// radius itself, and clip_contains' quadrant tests are simply never entered
// when a radius is negative - so a defect in the arithmetic was masked twice
// over. A function whose every caller repairs it is a function with no test.
TEST_SUITE("clip geometry") {
  using dg::Radii;
  using dg::detail::fit_radii;

  TEST_CASE("radii that fit are returned unchanged") {
    const Radii radii{6.0F, 8.0F, 10.0F, 4.0F};
    CHECK(fit_radii(PixelRect{0, 0, 100, 100}, radii) == radii);
  }

  TEST_CASE("a pair sharing an edge is scaled until it fits, and all four with it") {
    // Top edge is 40 wide and asks for 30 + 50 = 80, so the factor is 0.5 and
    // every radius halves - including the two on the bottom edge, which fit
    // perfectly well on their own. Scaling only the offending pair would
    // change the curvature halfway down each side.
    const Radii fitted = fit_radii(PixelRect{0, 0, 40, 400}, Radii{30.0F, 50.0F, 8.0F, 4.0F});
    CHECK(fitted == Radii{15.0F, 25.0F, 4.0F, 2.0F});
  }

  // THE SHAPE THE FIRST NEGATIVE-RADIUS TEST LACKED. With all four radii
  // negative the clamp is invisible: nothing is scaled either way and both
  // consumers fall back to a square corner. A negative radius BESIDE a large
  // positive one on the same edge is different, because the negative one
  // makes their sum smaller and so hides the fact that the positive one does
  // not fit:
  //
  //   clamped   top edge asks 0 + 60 = 60 against 40 -> scale 2/3 -> tr = 40
  //   unclamped top edge asks -30 + 60 = 30 against 40 -> no scale -> tr = 60
  //
  // Skia clamps and rescales on its own, so it would draw the 40 while a hit
  // test built on the unclamped answer would ask about the 60 - the two
  // readers this slice exists to keep together, pulled apart by one max().
  TEST_CASE("a negative radius beside a large one does not hide that the large one overruns") {
    const Radii fitted = fit_radii(PixelRect{0, 0, 40, 400}, Radii{-30.0F, 60.0F, 0.0F, 0.0F});
    CHECK(fitted.top_left == 0.0F);
    CHECK(static_cast<double>(fitted.top_right) == doctest::Approx(40.0));
  }

  TEST_CASE("an empty rectangle has no corners to round") {
    // This is what actually keeps a degenerate box away from SkRRect, and it
    // is why apply_clip() reaches clipRect rather than clipRRect for one.
    CHECK(fit_radii(PixelRect{0, 0, 0, 30}, Radii::all(8.0F)).is_zero());
    CHECK(fit_radii(PixelRect{}, Radii::all(8.0F)).is_zero());
  }
}
