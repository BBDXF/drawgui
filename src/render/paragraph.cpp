#include "drawgui/render/paragraph.h"

#include <cmath>
#include <utility>

#include "modules/skparagraph/include/Paragraph.h"

#include "render/paragraph_build.h"

namespace dg {

struct Paragraph::Impl {
  std::unique_ptr<skia::textlayout::Paragraph> paragraph;
};

Expected<Paragraph, FontError> Paragraph::build(const FontCatalog& fonts, const TextStyle& text,
                                                float width) {
  std::unique_ptr<skia::textlayout::Paragraph> built = detail::build_paragraph(fonts, text, width);
  if (!built) {
    return Unexpected{FontError{"paragraph layout failed: invalid font, empty text, or "
                                "non-positive size"}};
  }
  auto impl = std::make_unique<Impl>();
  impl->paragraph = std::move(built);
  return Paragraph{std::move(impl)};
}

Paragraph::Paragraph(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Paragraph::Paragraph(Paragraph&&) noexcept = default;
Paragraph& Paragraph::operator=(Paragraph&&) noexcept = default;
Paragraph::~Paragraph() = default;

ParagraphMetrics Paragraph::metrics() const {
  ParagraphMetrics out;
  out.height = static_cast<int>(std::ceil(impl_->paragraph->getHeight()));
  out.line_count = static_cast<int>(impl_->paragraph->lineNumber());
  out.exceeded_max_lines = impl_->paragraph->didExceedMaxLines();
  return out;
}

}  // namespace dg
