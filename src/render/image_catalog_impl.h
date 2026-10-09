// The state behind ImageCatalog.
//
// One translation unit needs it today (image_catalog.cpp); split into its own
// header anyway, mirroring font_catalog_impl.h, because src/render/image_access.h
// - the seam skia_paint.cpp uses - needs the same struct without pulling in
// the decode machinery.

#pragma once

#include <optional>
#include <vector>

#include "drawgui/render/image_catalog.h"

#include "include/core/SkImage.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkSize.h"

#include "modules/svg/include/SkSVGDOM.h"

namespace dg {

// Exactly one of `bitmap` / `svg` is set. For an SVG, `intrinsic` is the
// document's own size and its container size is already set to it, so the
// paint path only scales - it never mutates the shared DOM.
struct ImageEntry {
  sk_sp<SkImage> bitmap;
  sk_sp<SkSVGDOM> svg;
  SkSize intrinsic = SkSize::MakeEmpty();
  std::optional<Color> tint;
};

struct ImageCatalog::Impl {
  // One-based: index 0 of this vector backs ImageId{1}, matching
  // FontCatalog::Impl::typefaces and leaving ImageId{0} meaning "no image".
  std::vector<ImageEntry> images;
};

}  // namespace dg
