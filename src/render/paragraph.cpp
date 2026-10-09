#include "drawgui/render/paragraph.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "drawgui/base/utf8.h"
#include "drawgui/render/grapheme.h"

#include "modules/skparagraph/include/Paragraph.h"

#include "render/paragraph_build.h"

namespace dg {

namespace {

// Offset tables between the caller's UTF-8 bytes and the two units
// SkParagraph speaks: UTF-16 (hit testing, ranges, lines, words) and the
// UTF-8 of the text it was actually GIVEN (glyph clusters), which differs
// from the caller's bytes only where build_paragraph() replaced an invalid
// byte with a 3-byte U+FFFD. Bytes inside a codepoint map to its start.
struct OffsetMap {
  std::vector<int> utf16_of_byte;   // size = text bytes + 1
  std::vector<int> skia8_of_byte;   // size = text bytes + 1
  std::vector<int> byte_of_utf16;   // size = UTF-16 units + 1
};

OffsetMap build_offset_map(std::string_view text) {
  OffsetMap map;
  map.utf16_of_byte.resize(text.size() + 1);
  map.skia8_of_byte.resize(text.size() + 1);
  int utf16 = 0;
  int skia8 = 0;
  std::size_t byte = 0;
  while (byte < text.size()) {
    const Utf8Step step = utf8_decode(text, byte);
    for (std::size_t i = 0; i < step.length; ++i) {
      map.utf16_of_byte[byte + i] = utf16;
      map.skia8_of_byte[byte + i] = skia8;
    }
    const int units = (step.valid && step.codepoint > 0xFFFF) ? 2 : 1;
    for (int i = 0; i < units; ++i) {
      map.byte_of_utf16.push_back(static_cast<int>(byte));
    }
    utf16 += units;
    skia8 += step.valid ? static_cast<int>(step.length) : 3;
    byte += step.length;
  }
  map.utf16_of_byte[text.size()] = utf16;
  map.skia8_of_byte[text.size()] = skia8;
  map.byte_of_utf16.push_back(static_cast<int>(text.size()));
  return map;
}

// Grapheme boundaries for well-formed text; one boundary per decode step
// otherwise - dg::grapheme_boundaries() must never see malformed UTF-8
// (libgrapheme was measured to spin forever on a truncated lead byte such
// as "A\xE0"), and an editor never holds malformed text anyway.
std::vector<int> caret_boundaries(std::string_view text) {
  if (sanitize_utf8(text) == text) {
    return grapheme_boundaries(text);
  }
  std::vector<int> boundaries{0};
  std::size_t byte = 0;
  while (byte < text.size()) {
    byte += utf8_decode(text, byte).length;
    boundaries.push_back(static_cast<int>(byte));
  }
  return boundaries;
}

}  // namespace

struct Paragraph::Impl {
  std::unique_ptr<skia::textlayout::Paragraph> paragraph;
  std::string text;
  OffsetMap map;
  std::vector<int> boundaries;  // grapheme-cluster boundaries of `text`
  std::vector<LineInfo> lines;
  std::vector<float> line_left;  // per line: x of an empty line's caret

  [[nodiscard]] int size() const { return static_cast<int>(text.size()); }

  [[nodiscard]] int byte_of_utf16(std::size_t unit) const {
    const std::size_t last = map.byte_of_utf16.size() - 1;
    return map.byte_of_utf16[std::min(unit, last)];
  }

  [[nodiscard]] unsigned utf16_of(int byte) const {
    return static_cast<unsigned>(map.utf16_of_byte[static_cast<std::size_t>(byte)]);
  }

  // The offset tables, boundaries and lines are only built by the first
  // editing-geometry query - a paragraph built only for metrics()/caret_x()
  // (every TextField measurement) never pays for them.
  bool geometry_ready = false;
  void ensure_geometry();

  void build_lines();
  [[nodiscard]] float x_in_line(int line, int offset) const;
  [[nodiscard]] int line_at_y(float y) const;
};

void Paragraph::Impl::ensure_geometry() {
  if (geometry_ready) {
    return;
  }
  map = build_offset_map(text);
  boundaries = caret_boundaries(text);
  build_lines();
  geometry_ready = true;
}

void Paragraph::Impl::build_lines() {
  std::vector<skia::textlayout::LineMetrics> metrics;
  paragraph->getLineMetrics(metrics);
  const bool truncated = paragraph->didExceedMaxLines();
  int start = 0;
  for (std::size_t i = 0; i < metrics.size(); ++i) {
    const skia::textlayout::LineMetrics& m = metrics[i];
    LineInfo line;
    line.start = start;
    const bool last = i + 1 == metrics.size();
    line.end_incl = (last && !truncated)
                        ? size()
                        : std::clamp(byte_of_utf16(m.fEndIncludingNewline), start, size());
    line.end_excl_newline = line.end_incl;
    if (line.end_incl > line.start &&
        text[static_cast<std::size_t>(line.end_incl - 1)] == '\n') {
      line.end_excl_newline = line.end_incl - 1;
    }
    line.baseline = static_cast<float>(m.fBaseline);
    line.top = static_cast<float>(m.fBaseline - m.fAscent);
    line.height = static_cast<float>(m.fHeight);
    lines.push_back(line);
    line_left.push_back(static_cast<float>(m.fLeft));
    start = line.end_incl;
  }
  if (lines.empty()) {
    LineInfo whole;
    whole.end_excl_newline = whole.end_incl = size();
    whole.height = static_cast<float>(paragraph->getHeight());
    lines.push_back(whole);
    line_left.push_back(0.0F);
    return;
  }
  // A trailing '\n' owns an empty line after it. SkParagraph reports one in
  // this build, but if it ever does not, the caret still needs a line.
  if (!truncated && !lines.empty() && lines.back().end_excl_newline < lines.back().end_incl) {
    LineInfo tail = lines.back();
    tail.start = tail.end_excl_newline = tail.end_incl = size();
    tail.top += tail.height;
    tail.baseline += tail.height;
    lines.push_back(tail);
    line_left.push_back(line_left.back());
  }
}

float Paragraph::Impl::x_in_line(int line, int offset) const {
  const LineInfo& info = lines[static_cast<std::size_t>(line)];
  skia::textlayout::Paragraph::GlyphClusterInfo cluster;
  const auto skia8 = [&](int byte) {
    return static_cast<std::size_t>(map.skia8_of_byte[static_cast<std::size_t>(byte)]);
  };
  if (offset < info.end_excl_newline && paragraph->getGlyphClusterAt(skia8(offset), &cluster)) {
    return cluster.fBounds.left();
  }
  if (info.end_excl_newline > info.start &&
      paragraph->getGlyphClusterAt(skia8(info.end_excl_newline - 1), &cluster)) {
    return cluster.fBounds.right();
  }
  return line_left[static_cast<std::size_t>(line)];
}

int Paragraph::Impl::line_at_y(float y) const {
  for (std::size_t i = 0; i < lines.size(); ++i) {
    if (y < lines[i].top + lines[i].height) {
      return static_cast<int>(i);
    }
  }
  return static_cast<int>(lines.size()) - 1;
}

Expected<Paragraph, FontError> Paragraph::build(const FontCatalog& fonts, const TextStyle& text,
                                                float width) {
  std::unique_ptr<skia::textlayout::Paragraph> built =
      detail::build_paragraph(fonts, text, width);
  if (!built) {
    return Unexpected{
        FontError{"paragraph layout failed: invalid font, empty text, or "
                  "non-positive size"}};
  }
  auto impl = std::make_unique<Impl>();
  impl->paragraph = std::move(built);
  impl->text = text.text;
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

float Paragraph::caret_x(int offset) const {
  skia::textlayout::Paragraph::GlyphClusterInfo info;
  if (impl_->paragraph->getGlyphClusterAt(static_cast<std::size_t>(offset), &info)) {
    return info.fBounds.left();
  }
  // `offset` names no cluster of its own - the end of the text, or (should a
  // caller ever pass one) an out-of-range position past it. Sit right after
  // whatever the PRECEDING byte's cluster is, rather than at 0: an empty
  // string never reaches here (Paragraph::build() itself declines it), so
  // `offset > 0` here means there is a real preceding cluster to ask about.
  if (offset > 0 &&
      impl_->paragraph->getGlyphClusterAt(static_cast<std::size_t>(offset - 1), &info)) {
    return info.fBounds.right();
  }
  return 0.0F;
}

int Paragraph::offset_at(float x, float y) const {
  impl_->ensure_geometry();
  const int line = impl_->line_at_y(y);
  const LineInfo& info = impl_->lines[static_cast<std::size_t>(line)];
  const float line_y = info.top + (info.height * 0.5F);
  const skia::textlayout::PositionWithAffinity hit =
      impl_->paragraph->getGlyphPositionAtCoordinate(x, line_y);
  const int raw = impl_->byte_of_utf16(static_cast<std::size_t>(std::max(0, hit.position)));
  const int offset = std::clamp(raw, info.start, info.end_excl_newline);

  // Snap to a grapheme boundary: whichever neighbouring boundary's caret is
  // nearer to `x`.
  const std::vector<int>& bounds = impl_->boundaries;
  const auto it = std::lower_bound(bounds.begin(), bounds.end(), offset);
  if (it == bounds.end() || *it == offset || it == bounds.begin()) {
    return it == bounds.end() ? impl_->size() : *it;
  }
  const int after = std::min(*it, info.end_excl_newline);
  const int before = std::max(*(it - 1), info.start);
  const float before_x = impl_->x_in_line(line, before);
  const float after_x = impl_->x_in_line(line, after);
  return std::abs(x - before_x) <= std::abs(after_x - x) ? before : after;
}

CaretRect Paragraph::caret_rect(int offset, bool upstream) const {
  impl_->ensure_geometry();
  const int clamped = std::clamp(offset, 0, impl_->size());
  int line = line_of(clamped);
  if (upstream && line > 0) {
    const LineInfo& previous = impl_->lines[static_cast<std::size_t>(line - 1)];
    // Only a SOFT wrap has two caret positions for one offset; after a
    // '\n' the offset can only sit at the start of the next line.
    if (clamped == previous.end_incl && previous.end_excl_newline == previous.end_incl) {
      line -= 1;
    }
  }
  const LineInfo& info = impl_->lines[static_cast<std::size_t>(line)];
  return CaretRect{impl_->x_in_line(line, clamped), info.top, info.height, line};
}

std::vector<PixelRectF> Paragraph::range_rects(int start, int end) const {
  impl_->ensure_geometry();
  const int lo = std::clamp(std::min(start, end), 0, impl_->size());
  const int hi = std::clamp(std::max(start, end), 0, impl_->size());
  std::vector<PixelRectF> out;
  if (lo == hi) {
    return out;
  }
  const std::vector<skia::textlayout::TextBox> boxes = impl_->paragraph->getRectsForRange(
      impl_->utf16_of(lo), impl_->utf16_of(hi), skia::textlayout::RectHeightStyle::kMax,
      skia::textlayout::RectWidthStyle::kTight);
  out.reserve(boxes.size());
  for (const skia::textlayout::TextBox& box : boxes) {
    out.push_back(PixelRectF{box.rect.left(), box.rect.top(), box.rect.width(),
                             box.rect.height()});
  }
  return out;
}

std::vector<LineInfo> Paragraph::lines() const {
  impl_->ensure_geometry();
  return impl_->lines;
}

int Paragraph::line_of(int offset) const {
  impl_->ensure_geometry();
  const std::vector<LineInfo>& lines = impl_->lines;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    if (offset < lines[i].end_incl) {
      return static_cast<int>(i);
    }
  }
  return static_cast<int>(lines.size()) - 1;
}

std::pair<int, int> Paragraph::word_at(int offset) const {
  impl_->ensure_geometry();
  const int clamped = std::clamp(offset, 0, impl_->size());
  const skia::textlayout::SkRange<std::size_t> word =
      impl_->paragraph->getWordBoundary(impl_->utf16_of(clamped));
  const int start = impl_->byte_of_utf16(word.start);
  const int end = impl_->byte_of_utf16(word.end);
  return {std::min(start, end), std::max(start, end)};
}

}  // namespace dg
