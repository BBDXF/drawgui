// The state behind ImageCatalog.
//
// One translation unit needs it today (image_catalog.cpp); split into its own
// header anyway, mirroring font_catalog_impl.h, because src/render/image_access.h
// - the seam skia_paint.cpp uses - needs the same struct without pulling in
// the decode machinery.

#pragma once

#include <vector>

#include "drawgui/render/image_catalog.h"

#include "include/core/SkImage.h"
#include "include/core/SkRefCnt.h"

namespace dg {

struct ImageCatalog::Impl {
  // One-based: index 0 of this vector backs ImageId{1}, matching
  // FontCatalog::Impl::typefaces and leaving ImageId{0} meaning "no image".
  std::vector<sk_sp<SkImage>> images;
};

}  // namespace dg
