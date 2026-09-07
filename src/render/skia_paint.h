// Painting one node, and the one geometry conversion the render layer needs.
//
// Split out of render_tree.cpp rather than inlined there because it is the
// only part of the render layer that names Skia types, and because both this
// file and the tree's picture recorder need the rectangle conversion. It is
// an implementation header under src/, not an interface: nothing outside the
// library can include it, and it exists because two translation units share
// code today rather than because a second implementation is anticipated.

#pragma once

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/render/render_tree.h"

#include "include/core/SkRect.h"

class SkCanvas;

namespace dg::detail {

[[nodiscard]] SkRect to_sk_rect(const PixelRect& rect);

// Draws `style` at `bounds`, touching no pixel outside `bounds`. That
// containment is the contract the whole damage model rests on: a node whose
// paint escapes its declared rectangle leaves pixels that nothing will ever
// invalidate.
//
// `fonts` may be null, and is null for every tree built before sub-step 3. A
// node asking for text without a catalog to resolve its family draws no text,
// which is the same visible outcome as naming a family the machine does not
// have - deliberately, because with no fallback chain those two failures have
// the same cause and the same fix.
void paint_node(SkCanvas& canvas, const PixelRect& bounds, const NodeStyle& style,
                const FontCatalog* fonts);

// Confines everything drawn until the matching restore() to `bounds` rounded
// by `radii`.
//
// ANTI-ALIASED FOR A ROUNDED SHAPE, ALIASED FOR A SQUARE ONE, and the two
// halves are decided by different arguments. A square clip lands on integer
// pixel boundaries, so anti-aliasing it could only blend an edge that has no
// fraction to blend - and the damage clip in paint_region() is aliased for
// exactly that reason, so an aliased square clip is also the one that cannot
// disagree with it. A rounded clip has a real curve, and the whole point of
// the feature is that content follows the curve rather than its bounding box.
//
// The radii handed to SkRRect are fit_radii()'s, not the caller's, so that
// hit testing - which asks clip_contains() about the same numbers - cannot
// answer for a shape the rasterizer never drew.
void apply_clip(SkCanvas& canvas, const PixelRect& bounds, const Radii& radii);

}  // namespace dg::detail
