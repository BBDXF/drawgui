#include "drawgui/render/grapheme.h"

#include <algorithm>
#include <string>

#include "drawgui/base/utf8.h"

#include "include/private/SkTArray.h"
#include "modules/skunicode/include/SkUnicode.h"
#include "modules/skunicode/include/SkUnicode_libgrapheme.h"

namespace dg {

namespace {

// Appends the boundaries of one WELL-FORMED run text[begin, end) (offset by
// `begin`), excluding `end` itself. libgrapheme must only ever see
// well-formed UTF-8: it was measured to spin forever on a truncated lead
// byte such as "A\xE0" (its break iterator makes no progress), so
// grapheme_boundaries() below feeds it the valid runs between malformed
// bytes and never a malformed byte itself.
bool append_run(SkUnicode& unicode, std::string_view text, std::size_t begin, std::size_t end,
                std::vector<int>& out) {
  // computeCodeUnitFlags takes `char utf8[]` (non-const); a copy is the
  // correct way to get a mutable pointer without a const_cast.
  std::string run{text.substr(begin, end - begin)};
  skia_private::TArray<SkUnicode::CodeUnitFlags, true> flags;
  if (!unicode.computeCodeUnitFlags(run.data(), static_cast<int>(run.size()), false, &flags)) {
    return false;
  }
  // `flags` also carries an entry for the end position; only offsets
  // strictly inside the run are boundaries it owns.
  const int count = std::min(flags.size(), static_cast<int>(run.size()));
  for (int i = 0; i < count; ++i) {
    if ((flags[i] & SkUnicode::kGraphemeStart) != 0 || i == 0) {
      out.push_back(static_cast<int>(begin) + i);
    }
  }
  return true;
}

}  // namespace

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

  // Malformed bytes (dg::utf8_decode()'s invalid steps) are each a cluster
  // of their own - the unit dg::sanitize_utf8() would replace with one
  // U+FFFD - and split the text into well-formed runs segmented separately.
  std::vector<int> boundaries;
  boundaries.reserve(utf8.size() + 1);
  std::size_t run_start = 0;
  std::size_t byte = 0;
  const auto flush = [&](std::size_t run_end) {
    if (run_end > run_start && !append_run(*unicode, utf8, run_start, run_end, boundaries)) {
      boundaries.push_back(static_cast<int>(run_start));
    }
  };
  while (byte < utf8.size()) {
    const Utf8Step step = utf8_decode(utf8, byte);
    if (!step.valid) {
      flush(byte);
      boundaries.push_back(static_cast<int>(byte));
      run_start = byte + step.length;
    }
    byte += step.length;
  }
  flush(utf8.size());
  boundaries.push_back(static_cast<int>(utf8.size()));
  return boundaries;
}

}  // namespace dg
