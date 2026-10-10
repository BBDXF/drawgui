#pragma once

// The caret's vertical extent, shared by TextField (widget_set.cpp) and
// TextArea (text_area.cpp) so both editors draw the same caret: as tall as
// the text line's own metrics (ascent + descent), capped at 1.2x the font
// size so a tall fallback font (CJK, emoji) cannot stretch it, and
// vertically centred on the line box it sits in.

#include <algorithm>
#include <cmath>

namespace dg::caret_metrics {

inline constexpr float kMaxHeightFactor = 1.2F;

struct Span {
  int top = 0;
  int height = 0;
};

// `line_top` / `line_height` are the line box the caret belongs to (a
// paragraph line, or a whole single-line field); `metric_height` is the
// font's line height (<= 0 when unknown); `font_size` is the text size.
inline Span caret_span(float line_top, float line_height, float metric_height,
                       float font_size) {
  float height = font_size * kMaxHeightFactor;
  if (metric_height > 0.0F) {
    height = std::min(height, metric_height);
  }
  height = std::max(1.0F, std::min(height, std::max(1.0F, line_height)));
  const int h = static_cast<int>(std::lround(static_cast<double>(height)));
  const float top = line_top + ((line_height - static_cast<float>(h)) * 0.5F);
  return Span{static_cast<int>(std::lround(static_cast<double>(top))), h};
}

}  // namespace dg::caret_metrics
