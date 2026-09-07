// Internal access to a FontCatalog's typefaces.
//
// The public header names no Skia type, and the paint path needs one. This
// header is the seam between those two facts: it lives under src/, nothing
// outside the library can include it, and it exists so that skia_paint.cpp can
// turn a FontId into an SkTypeface without FontCatalog growing a public
// accessor that leaks Skia into include/.

#pragma once

#include "drawgui/render/font_catalog.h"

#include "include/core/SkRefCnt.h"
#include "include/core/SkTypeface.h"

namespace dg {

struct FontAccess {
  // Null when `id` names nothing in `catalog`, including the zero id. The
  // caller draws no text rather than substituting a family.
  [[nodiscard]] static sk_sp<SkTypeface> typeface(const FontCatalog& catalog, FontId id);
};

}  // namespace dg
