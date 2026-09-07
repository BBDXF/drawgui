#include "gallery.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "include/core/SkColor.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkScalar.h"
#include "include/core/SkTextBlob.h"

#include "panel_support.h"

namespace gallery {
namespace {

constexpr int kColumns = 4;
constexpr int kRows = 4;
constexpr float kOuterMargin = 18.0F;
constexpr float kHeaderHeight = 46.0F;
constexpr float kGutter = 10.0F;
constexpr float kCardRadius = 8.0F;
constexpr float kTitleHeight = 24.0F;
constexpr float kBodyInset = 10.0F;

SkRect cell_bounds(int index) {
  const float grid_top = kOuterMargin + kHeaderHeight;
  const float grid_width = kDesignWidth - (2.0F * kOuterMargin);
  const float grid_height = kDesignHeight - grid_top - kOuterMargin;
  const float cell_width = grid_width / static_cast<float>(kColumns);
  const float cell_height = grid_height / static_cast<float>(kRows);

  const int column_index = index % kColumns;
  const int row_index = (index - column_index) / kColumns;
  const auto column = static_cast<float>(column_index);
  const auto row = static_cast<float>(row_index);
  return SkRect::MakeXYWH(kOuterMargin + (column * cell_width) + (kGutter * 0.5F),
                          grid_top + (row * cell_height) + (kGutter * 0.5F),
                          cell_width - kGutter, cell_height - kGutter);
}

void draw_header(SkCanvas& canvas, const Resources& resources, const Selection& selection) {
  if (!resources.sans_bold) {
    return;
  }
  const SkFont title = text_font(resources.sans_bold, 21.0F);
  canvas.drawString("drawgui - Skia CPU raster capability gallery", kOuterMargin,
                    kOuterMargin + 21.0F, title, fill_paint(kInk));

  if (!resources.sans) {
    return;
  }
  const SkFont note = text_font(resources.sans, 13.0F);
  const char* subtitle =
      "every panel below is rasterized on the CPU and blitted to the window "
      "surface - no GL context exists in this process";
  canvas.drawString(subtitle, kOuterMargin, kOuterMargin + 39.0F, note, fill_paint(kInkDim));

  // Only shown when something has been switched off, because a benchmark
  // screenshot that does not say which groups it excluded is unreadable
  // evidence.
  if (selection.geometry && selection.text && selection.effects) {
    return;
  }
  std::string excluded = "excluded:";
  if (!selection.geometry) {
    excluded += " geometry";
  }
  if (!selection.text) {
    excluded += " text";
  }
  if (!selection.effects) {
    excluded += " effects";
  }
  canvas.drawString(excluded.c_str(), kDesignWidth - kOuterMargin - 240.0F,
                    kOuterMargin + 39.0F, note, fill_paint(kAmber));
}

void draw_card(SkCanvas& canvas, const Resources& resources, const Panel& panel,
               const SkRect& bounds) {
  const SkRRect card = SkRRect::MakeRectXY(bounds, kCardRadius, kCardRadius);
  canvas.drawRRect(card, fill_paint(kCard));
  canvas.drawRRect(card, stroke_paint(kCardEdge, 1.0F));

  // Clipped to its own card, title included. A title that is one character
  // too long should be truncated at the card edge rather than written across
  // the neighbouring panel.
  canvas.save();
  canvas.clipRRect(card, true);

  if (resources.sans_bold) {
    const SkFont title = text_font(resources.sans_bold, 11.0F);
    canvas.drawString(panel.title, bounds.left() + kBodyInset, bounds.top() + 16.0F, title,
                      fill_paint(kInkDim));
  }

  const SkRect body = SkRect::MakeXYWH(bounds.left() + kBodyInset, bounds.top() + kTitleHeight,
                                       bounds.width() - (2.0F * kBodyInset),
                                       bounds.height() - kTitleHeight - kBodyInset);
  if (body.isEmpty()) {
    canvas.restore();
    return;
  }

  // Translated so every panel draws from its own origin, and clipped so a
  // panel that overruns is a visible bug inside its own card rather than
  // graffiti across its neighbour.
  canvas.save();
  canvas.translate(body.left(), body.top());
  canvas.clipRect(SkRect::MakeWH(body.width(), body.height()), true);
  panel.draw(canvas, resources, SkRect::MakeWH(body.width(), body.height()));
  canvas.restore();

  canvas.restore();
}

}  // namespace

bool Selection::includes(Category category) const {
  switch (category) {
    case Category::kGeometry:
      return geometry;
    case Category::kText:
      return text;
    case Category::kEffects:
      return effects;
  }
  return false;
}

std::vector<Panel> all_panels() {
  std::vector<Panel> panels = geometry_panels();
  for (const Panel& panel : text_panels()) {
    panels.push_back(panel);
  }
  for (const Panel& panel : effect_panels()) {
    panels.push_back(panel);
  }
  return panels;
}

void draw_frame(SkCanvas& canvas, const Resources& resources, const SkISize& target,
                const Selection& selection) {
  canvas.save();

  // Uniform scale and centred, so the gallery never distorts and the same
  // scene is being timed at every resolution.
  const float scale = std::min(static_cast<float>(target.width()) / kDesignWidth,
                               static_cast<float>(target.height()) / kDesignHeight);
  canvas.translate((static_cast<float>(target.width()) - (kDesignWidth * scale)) * 0.5F,
                   (static_cast<float>(target.height()) - (kDesignHeight * scale)) * 0.5F);
  canvas.scale(scale, scale);

  // Respects the caller's clip, which is what makes a dirty-rect repaint
  // genuinely cheaper rather than merely presenting less.
  canvas.clear(kBackground);

  draw_header(canvas, resources, selection);

  // Panels keep their slot when a group is switched off, so the benchmark
  // isolates a cost without also changing where everything is.
  const std::vector<Panel> panels = all_panels();
  for (std::size_t index = 0; index < panels.size(); ++index) {
    if (!selection.includes(panels[index].category)) {
      continue;
    }
    draw_card(canvas, resources, panels[index], cell_bounds(static_cast<int>(index)));
  }

  canvas.restore();
}

}  // namespace gallery
