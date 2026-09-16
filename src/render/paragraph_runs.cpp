#include "render/paragraph_runs.h"

#include "drawgui/base/utf8.h"

namespace dg::detail {

std::vector<ParagraphRun> paragraph_runs(const FontCatalog& fonts, const TextStyle& text) {
  std::vector<ParagraphRun> out;
  if (text.text.empty() || !fonts.holds(text.font)) {
    return out;
  }
  const std::string primary_family = fonts.family_name(text.font);

  std::size_t offset = 0;
  while (offset < text.text.size()) {
    const Utf8Step step = utf8_decode(text.text, offset);
    std::string family = primary_family;
    if (step.valid) {
      const FontResolution resolution = fonts.resolve(text.font, text.language, step.codepoint);
      if (!resolution.family.empty()) {
        family = resolution.family;
      }
    }

    if (!out.empty() && out.back().family == family && out.back().invalid == !step.valid) {
      out.back().end = offset + step.length;
    } else {
      ParagraphRun run{family, offset, offset + step.length, !step.valid};
      out.push_back(run);
    }
    offset += step.length;
  }
  return out;
}

}  // namespace dg::detail
