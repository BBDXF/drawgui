// The decoded bitmaps a render tree is allowed to paint, named once and
// referred to by number afterwards - the same shape FontCatalog already has
// for typefaces, one layer over.
//
// This exists because design.md section 5.10.1 makes ImageSource a shared
// loading/caching path, and slice 4-10's completeness audit found nothing
// here at all: no image field on NodeStyle, no decode call anywhere the
// engine's own render tree could reach. This is that path's bitmap-only
// entry, decoding synchronously (design.md section 5.10.3's thread pool is
// explicitly out of this slice - the call sites
// below are still shaped so a future async decode does not have to move).
//
// SCOPE: bitmaps (PNG, JPEG, WebP - the three codecs registered and tested)
// and SVG vector icons, from encoded bytes already in memory or from a file.
// design.md section 5.10.1's nine-patch row is still declined by name.
//
// SVG entries are kept as a parsed vector document, NOT rasterized at decode
// time: the paint path renders them as vectors at the node's own size every
// paint, so an icon is crisp at any node size or DPI scale and a size change
// needs no re-rasterization or cache invalidation (the trade: an SVG costs a
// vector draw per paint rather than one bitmap blit - fine for icons, the
// intended use). An SVG's pixel_size() is its intrinsic size (width/height
// attributes, else the viewBox), which is what fit-mode arithmetic scales.
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
#include "drawgui/graphics/types.h"

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

// Options for an SVG entry. `tint`, when set, recolours every painted pixel
// of the icon to that colour while keeping the icon's own coverage/alpha
// (a SrcIn colour filter) - the usual treatment for monochrome icon sets. It
// is fixed per catalog entry; the same SVG wanted in two tints is decoded
// twice (cheap: a parsed DOM, not a bitmap).
struct SvgOptions {
  std::optional<Color> tint;
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

  // Decodes `encoded` (a complete RASTER file's bytes: PNG, JPEG or WebP,
  // sniffed by Skia's codecs) and returns the id that names it from now on.
  // Never parses SVG, on purpose: theme_package.h hands untrusted resource
  // bytes here and its security decision declines an SVG parser by name. Use
  // decode_any() / decode_svg() for SVG.
  //
  // SYNCHRONOUS, on purpose and only for this slice: the thread pool design.md
  // section 5.10.3 asks for is explicitly out of scope. Nothing about this
  // signature would need to change for a future async version to call it from
  // a worker thread and hand the resulting ImageId back to the main one -
  // that migration would leave the decode call sites otherwise untouched.
  //
  // Fails rather than substituting: a corrupt file or an unregistered codec
  // is reported, never silently skipped or replaced with a placeholder here -
  // the placeholder is a PAINT-time decision (NodeStyle::image.placeholder),
  // not a decode-time one, so a caller that wants one still has to ask for it
  // explicitly.
  [[nodiscard]] Expected<ImageId, ImageError> decode(const std::uint8_t* encoded,
                                                     std::size_t size);

  // FORMAT-AGNOSTIC: bytes that start (after an optional UTF-8 BOM and
  // whitespace) with '<' go to decode_svg() with `options`, everything else
  // to decode(). For trusted, application-supplied assets.
  [[nodiscard]] Expected<ImageId, ImageError> decode_any(const std::uint8_t* encoded,
                                                         std::size_t size,
                                                         const SvgOptions& options = {});

  // Parses `encoded` as SVG explicitly (no sniffing), with `options`.
  // Fails on bytes that are not a well-formed SVG document with a positive
  // intrinsic size (width/height or viewBox).
  [[nodiscard]] Expected<ImageId, ImageError> decode_svg(const std::uint8_t* encoded,
                                                         std::size_t size,
                                                         const SvgOptions& options = {});

  // Reads `path` and hands its bytes to decode_any(). `options` is ignored
  // for bitmaps.
  [[nodiscard]] Expected<ImageId, ImageError> decode_file(const std::string& path,
                                                          const SvgOptions& options = {});

  [[nodiscard]] std::size_t size() const;

  // True when `id` names an SVG (vector) entry of this table.
  [[nodiscard]] bool is_vector(ImageId id) const;

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
  // layout has already run. LayoutTree never calls this.
  [[nodiscard]] std::optional<PixelSize> pixel_size(ImageId id) const;

 private:
  struct Impl;

  std::shared_ptr<Impl> impl_;

  friend struct ImageAccess;
};

}  // namespace dg
