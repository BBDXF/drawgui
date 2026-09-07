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

}  // namespace dg::detail
