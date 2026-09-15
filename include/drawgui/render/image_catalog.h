// The decoded bitmaps a render tree is allowed to paint, named once and
// referred to by number afterwards - the same shape FontCatalog already has
// for typefaces, one layer over.
//
// This exists because design.md section 5.10.1 makes ImageSource a shared
// loading/caching path, and slice 4-10's completeness audit found nothing
// here at all: no image field on NodeStyle, no decode call anywhere the
// engine's own render tree could reach. This is that path's bitmap-only
// entry, decoding synchronously (design.md section 5.10.3's thread pool is
// explicitly out of this slice - doc/image.md records why the call sites
// below are still shaped so a future async decode does not have to move).
//
// SCOPE: bitmap sources only, decoded from encoded bytes already in memory.
// design.md section 5.10.1's other two ImageSource rows - vector (SVG) and
// nine-patch - are declined by name; doc/image.md records why. The default
// prebuilt Skia codec set decodes BMP/GIF/ICO/JPEG/PNG/WBMP (design.md
// section 5.10.2); nothing here restricts a caller to PNG specifically, but
// PNG is the only one this slice's own tests exercise.
//
// No Skia type appears below, matching FontCatalog and RasterSurface: the
// decoded images live behind the pimpl, and src/render/image_access.h is the
// seam that lets the paint path reach them without this header naming
// SkImage.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "drawgui/base/expected.h"
#include "drawgui/base/pixel_geometry.h"

namespace dg {

// Why a decode failed, as text. The same reasoning as FontError and
// WindowError: nothing here branches on the reason, and the reason (which
// codec rejected which byte, or that no codec was registered for the format)
// is the whole of what makes it fixable.
struct ImageError {
  std::string message;
};

// Names one entry of an ImageCatalog.
//
// Zero is not an image, matching FontId's zero-is-invalid contract: a
// default-constructed ImageStyle therefore carries no image, so "I forgot to
// assign one" and "decode has not produced one yet" are the same visible
// state - the placeholder colour paints, never a hole and never a stale
// frame's pixels (design.md section 5.10.3).
struct ImageId {
  std::uint16_t value = 0;

  [[nodiscard]] constexpr bool is_valid() const { return value != 0; }

  friend bool operator==(ImageId, ImageId) = default;
};

// A shared, append-only table of decoded bitmaps.
//
// Copyable, and a copy names the same table - a handle, not a container that
// can fork into two that disagree, exactly like FontCatalog. A render tree
// holds one through TreeSpec::images; every node whose NodeStyle::image names
// a source holds an ImageId into it.
class ImageCatalog {
 public:
  ImageCatalog();

  // Decodes `encoded` (a complete file's bytes: PNG/BMP/GIF/ICO/JPEG/WBMP,
  // design.md section 5.10.2) and returns the id that names it from now on.
  //
  // SYNCHRONOUS, on purpose and only for this slice: the thread pool design.md
  // section 5.10.3 asks for is explicitly out of scope. Nothing about this
  // signature would need to change for a future async version to call it from
  // a worker thread and hand the resulting ImageId back to the main one -
  // doc/image.md records exactly what that migration would and would not
  // touch.
  //
  // Fails rather than substituting: a corrupt file or an unregistered codec
  // is reported, never silently skipped or replaced with a placeholder here -
  // the placeholder is a PAINT-time decision (NodeStyle::image.placeholder),
  // not a decode-time one, so a caller that wants one still has to ask for it
  // explicitly.
  [[nodiscard]] Expected<ImageId, ImageError> decode(const std::uint8_t* encoded,
                                                     std::size_t size);

  [[nodiscard]] std::size_t size() const;

  // True when `id` names an entry of THIS table. A node carrying an id from a
  // different catalog is a bug the paint path must not dereference, matching
  // FontCatalog::holds().
  [[nodiscard]] bool holds(ImageId id) const;

  // The decoded pixel dimensions, or nothing when `id` names nothing here.
  //
  // PAINT-TIME ONLY. This is what fit-mode arithmetic (fill/contain/cover/
  // none) scales against, and nothing else may read it: design.md section
  // 5.10.3 forbids a node's LAYOUT size from depending on decoded content, and
  // the only caller of this accessor is src/render/skia_paint.cpp, after
  // layout has already run. LayoutTree never calls this - see
  // doc/image.md's central finding.
  [[nodiscard]] std::optional<PixelSize> pixel_size(ImageId id) const;

 private:
  struct Impl;

  std::shared_ptr<Impl> impl_;

  friend struct ImageAccess;
};

}  // namespace dg
