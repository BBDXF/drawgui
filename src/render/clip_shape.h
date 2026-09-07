// The shape a clipping node clips to, defined once for the two readers.
//
// This slice's whole point is that painting and hit testing honour ONE rule.
// The rule has two halves - which rectangle, and which corners - and the
// corners are where two implementations would silently drift: Skia scales a
// rounded rectangle's radii down when a pair of them overrun the edge they
// share, so a hit test that tested the radii the caller wrote would answer for
// a shape the rasterizer never drew. Both halves therefore live here, the
// painter hands `fit_radii`'s output to SkRRect rather than the raw radii, and
// the hit test asks `clip_contains` about the same numbers.
//
// It lives under src/ because it is not an interface: the clip is a property
// on NodeStyle, and this is the arithmetic two translation units of the same
// library share.

#pragma once

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/types.h"

namespace dg::detail {

// The radii Skia will actually use for `rect`, which are the caller's unless a
// pair sharing an edge is wider than that edge, in which case ALL FOUR are
// scaled by one factor.
//
// Scaling uniformly rather than clamping each pair independently is Skia's
// rule, not a choice made here (SkRRect::scaleRadii), and it matters: clamping
// per pair would round the two corners of a short edge while leaving the other
// two, producing a shape whose curvature changes halfway along a side.
//
// Negative radii are clamped to zero first. A negative radius is not a smaller
// corner, it is a value with no meaning, and letting one through would give
// the corner circle a negative centre offset - a shape that bulges outward.
[[nodiscard]] Radii fit_radii(const PixelRect& rect, const Radii& radii);

// Whether the pixel at `point` is inside the rounded rectangle.
//
// TESTED AT THE PIXEL CENTRE, which is the only definition that can be exact.
// A clip is a binary question and an anti-aliased curve is not: at the
// boundary Skia produces partial coverage, so there is a band one pixel wide
// where "was this painted" has no yes-or-no answer. Every pixel OUTSIDE that
// band does, and that is where the painting-equals-hit-testing equivalence is
// checkable - tests/unit/test_clip.cpp sweeps a rounded clip and requires
// agreement at every fully-covered and every fully-uncovered pixel, which
// pins this predicate against the rasterizer rather than against itself.
[[nodiscard]] bool clip_contains(const PixelRect& rect, const Radii& radii, PixelPoint point);

}  // namespace dg::detail
