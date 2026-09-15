// Internal access to an ImageCatalog's decoded bitmaps.
//
// The public header names no Skia type, and the paint path needs one. This is
// the seam, mirroring src/render/font_access.h exactly: it lives under src/,
// nothing outside the library can include it, and it exists so that
// skia_paint.cpp can turn an ImageId into an SkImage without ImageCatalog
// growing a public accessor that leaks Skia into include/.

#pragma once

#include "drawgui/render/image_catalog.h"

#include "include/core/SkImage.h"
#include "include/core/SkRefCnt.h"

#include "render/image_catalog_impl.h"

namespace dg {

struct ImageAccess {
  // Null when `id` names nothing in `catalog`, including the zero id. The
  // caller draws the placeholder (or nothing) rather than substituting an
  // image, matching FontAccess::typeface()'s "no id, no font" contract.
  [[nodiscard]] static sk_sp<SkImage> image(const ImageCatalog& catalog, ImageId id) {
    if (!catalog.holds(id)) {
      return nullptr;
    }
    return catalog.impl_->images[id.value - 1];
  }
};

}  // namespace dg
