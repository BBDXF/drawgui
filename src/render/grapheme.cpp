#include "drawgui/render/grapheme.h"

#include <string>

#include "include/private/SkTArray.h"
#include "modules/skunicode/include/SkUnicode.h"
#include "modules/skunicode/include/SkUnicode_libgrapheme.h"

namespace dg {

std::vector<int> grapheme_boundaries(std::string_view utf8) {
  if (utf8.empty()) {
    return {0};
  }

  const sk_sp<SkUnicode> unicode = SkUnicodes::Libgrapheme::Make();
  if (!unicode) {
    // Degrade to "the whole string is one unit" rather than crash - the
    // same null-unicode early return paragraph_build.cpp's build_paragraph()
    // already has for the identical failure.
    return {0, static_cast<int>(utf8.size())};
  }

  // computeCodeUnitFlags takes `char utf8[]` (non-const); a non-const
  // std::string's own `.data()` already returns `char*` in C++17, so a copy
  // is the correct way to get a mutable pointer without a const_cast.
  std::string mutable_copy{utf8};
  skia_private::TArray<SkUnicode::CodeUnitFlags, true> flags;
  const bool ok = unicode->computeCodeUnitFlags(
      mutable_copy.data(), static_cast<int>(mutable_copy.size()), false, &flags);
  if (!ok) {
    return {0, static_cast<int>(utf8.size())};
  }

  std::vector<int> boundaries;
  boundaries.reserve(static_cast<std::size_t>(flags.size()));
  for (int i = 0; i < flags.size(); ++i) {
    if ((flags[i] & SkUnicode::kGraphemeStart) != 0) {
      boundaries.push_back(i);
    }
  }
  if (boundaries.empty() || boundaries.back() != static_cast<int>(utf8.size())) {
    boundaries.push_back(static_cast<int>(utf8.size()));
  }
  return boundaries;
}

}  // namespace dg
