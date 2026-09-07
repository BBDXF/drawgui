// The fonts a render tree is allowed to draw with, named once and referred to
// by number afterwards.
//
// This exists because sub-step 3 needs a label, a label needs text, and text
// is the first thing a node can paint that is not a rectangle. It is
// deliberately the smallest table that supports the one constraint step 2
// measured and could not remove:
//
//   SkFontMgr_New_Custom_Directory has NO fallback chain.
//   matchFamilyStyleCharacter() returns null on it, so a family that lacks a
//   glyph does not quietly borrow one from another family - it draws nothing.
//
// A design that let a node carry a family NAME would therefore be a design in
// which "the text disappeared" is diagnosed at paint time, per node, forever.
// Naming the families up front turns that into one failure at startup, at the
// call that names a family the machine does not have. doc/cpu-raster-findings.md
// records the measurement; sub-step 4 is where the fallback chain gets fixed,
// and this table is what it will fix.
//
// No Skia type appears below. The typefaces live behind the pimpl, exactly as
// they do for RasterSurface.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "drawgui/base/expected.h"

namespace dg {

// Why a font operation failed, as text. The same reasoning as WindowError: no
// caller branches on the reason, and the reason ("no family named 'Inter' in
// /usr/share/fonts") is the whole of what makes it fixable.
struct FontError {
  std::string message;
};

// Names one entry of a FontCatalog.
//
// Zero is not a font. A default-constructed NodeStyle therefore carries no
// font, which is what makes "I forgot to set the font" and "I asked for a font
// that is not there" the same visible outcome - nothing drawn - rather than a
// silent fall back onto whichever family happened to be first.
struct FontId {
  std::uint16_t value = 0;

  [[nodiscard]] constexpr bool is_valid() const { return value != 0; }

  friend bool operator==(FontId, FontId) = default;
};

// A shared, append-only table of typefaces.
//
// Copyable, and a copy names the same fonts: this is a handle to one table,
// not a container that can be duplicated into two that disagree. A render tree
// holds one, every node that draws text holds an index into it, and the
// typefaces outlive both because the table is reference counted.
class FontCatalog {
 public:
  // Scans `directory` recursively for font files. Nothing is resolved yet -
  // this only builds the manager that add() will ask.
  [[nodiscard]] static Expected<FontCatalog, FontError> scan(const std::string& directory);

  // Resolves one family and returns the id that names it from now on.
  //
  // Fails rather than substituting. A GUI that silently swaps DejaVu Sans for
  // whatever was nearest looks fine on the developer's machine and wrong
  // everywhere else, and with no fallback chain the substitution would not
  // even be a near miss.
  [[nodiscard]] Expected<FontId, FontError> add(const std::string& family, bool bold);

  [[nodiscard]] std::size_t size() const;

  // The families this machine actually offers, for a diagnostic that can say
  // what was available instead of only what was missing.
  [[nodiscard]] std::string available_families() const;

  // True when `id` names an entry of THIS table. A node carrying an id from a
  // different catalog is a bug the paint path must not dereference.
  [[nodiscard]] bool holds(FontId id) const;

 private:
  struct Impl;

  explicit FontCatalog(std::shared_ptr<Impl> impl);

  std::shared_ptr<Impl> impl_;

  friend struct FontAccess;
};

}  // namespace dg
