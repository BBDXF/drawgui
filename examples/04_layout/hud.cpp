#include "hud.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkFontStyle.h"
#include "include/core/SkPaint.h"
#include "include/ports/SkFontMgr_directory.h"

namespace hud {
namespace {

constexpr SkColor kInk = 0xFFE8EDF4;
constexpr SkColor kInkDim = 0xFF8794A6;
constexpr SkColor kFast = 0xFF5BD98A;
constexpr SkColor kSlow = 0xFFE9825F;
constexpr SkColor kAccent = 0xFF3FA9F5;
constexpr SkColor kWarn = 0xFFF6C445;

SkPaint ink(SkColor color) {
  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setColor(color);
  return paint;
}

std::string fixed(double value, int places) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(places) << value;
  return out.str();
}

// Thousands separators, because "38214 px" and "382140 px" are hard to tell
// apart at a glance and telling them apart is the whole point of the line.
std::string grouped(std::int64_t value) {
  std::string digits = std::to_string(value);
  for (auto position = static_cast<int>(digits.size()) - 3; position > 0; position -= 3) {
    digits.insert(static_cast<std::size_t>(position), ",");
  }
  return digits;
}

std::string pad_left(const std::string& text, std::size_t width) {
  return text.size() >= width ? text : std::string(width - text.size(), ' ') + text;
}

std::string cell(double milliseconds, int places) {
  return pad_left(fixed(milliseconds, places) + " ms", 11);
}

// A lane with no frames in it has no number, and printing 0.000 ms for it
// would read as "free" rather than as "not measured".
std::string cell_or_dash(double milliseconds, std::size_t frames, int places) {
  return frames == 0 ? pad_left("-", 11) : cell(milliseconds, places);
}

std::string ratio(double slow, double fast, bool measured) {
  if (!measured || fast <= 0.0 || slow <= 0.0) {
    return pad_left("-", 11);
  }
  return pad_left(fixed(slow / fast, 1) + "x", 11);
}

std::string lane_row(const char* label, const Lane& lane) {
  const double total = lane.layout.median_ms + lane.raster.median_ms + lane.present.median_ms;
  return pad_left(label, 13) + cell_or_dash(lane.layout.median_ms, lane.frames, 3) +
         cell_or_dash(lane.raster.median_ms, lane.frames, 2) +
         cell_or_dash(lane.present.median_ms, lane.frames, 2) +
         cell_or_dash(total, lane.frames, 2) + pad_left(std::to_string(lane.frames), 9);
}

std::string speedup_row(const Lane& incremental, const Lane& full) {
  const bool measured = incremental.frames > 0 && full.frames > 0;
  const auto total = [](const Lane& lane) {
    return lane.layout.median_ms + lane.raster.median_ms + lane.present.median_ms;
  };
  return pad_left("speedup", 13) +
         ratio(full.layout.median_ms, incremental.layout.median_ms, measured) +
         ratio(full.raster.median_ms, incremental.raster.median_ms, measured) +
         ratio(full.present.median_ms, incremental.present.median_ms, measured) +
         ratio(total(full), total(incremental), measured) + pad_left("", 9);
}

std::string title_line(const Readout& readout) {
  const double megapixels = static_cast<double>(readout.viewport.width) *
                            static_cast<double>(readout.viewport.height) / 1'000'000.0;
  return "drawgui  incremental layout                 " +
         std::to_string(readout.viewport.width) + "x" +
         std::to_string(readout.viewport.height) + "  (" + fixed(megapixels, 2) + " Mpx)";
}

std::string mode_line(const Readout& readout) {
  return std::string{"layout: "} + (readout.incremental ? "INCREMENTAL" : "FULL       ") +
         "   containers: " + (readout.rounded_containers ? "ROUNDED" : "SQUARE ") +
         "   nodes: " + std::to_string(readout.nodes) +
         "   damage cap: " + std::to_string(readout.damage_cap);
}

std::string scope_line(const Readout& readout) {
  const dg::LayoutStats& stats = readout.last_layout;
  const double share = stats.nodes_total == 0
                           ? 0.0
                           : 100.0 * static_cast<double>(stats.nodes_relaid_out) /
                                 static_cast<double>(stats.nodes_total);
  return "last layout: entered " + std::to_string(stats.nodes_visited) + ", RECOMPUTED " +
         std::to_string(stats.nodes_relaid_out) + " of " + std::to_string(stats.nodes_total) +
         " (" + fixed(share, 1) + "%),  moved " + std::to_string(stats.nodes_moved) +
         ",  from " + std::to_string(stats.dirty_roots) + " boundary/ies";
}

std::string damage_line(const Readout& readout) {
  const auto window = static_cast<double>(readout.viewport.width) *
                      static_cast<double>(readout.viewport.height);
  const double share =
      window > 0.0 ? 100.0 * static_cast<double>(readout.last_layout.damage_area) / window
                   : 0.0;
  return "layout damage: " + grouped(readout.last_layout.damage_area) +
         " px = " + fixed(share, 3) + "% of the window in " +
         std::to_string(readout.last_layout.damage_rects) + " rect(s);  repaint drew " +
         std::to_string(readout.last_paint.nodes_drawn) + " of " +
         std::to_string(readout.last_paint.nodes_total) + " nodes";
}

// The number sub-step 3 has to design against. A rounded node is not
// clip-invariant under Skia's anti-aliasing, so it must be repainted whole,
// and a rounded CONTAINER therefore makes its entire area the smallest unit
// of damage anything inside it can produce.
std::string corner_line(const Readout& readout) {
  const bool measured = readout.square_repaint > 0 && readout.rounded_repaint > 0;
  const std::string factor = measured ? fixed(static_cast<double>(readout.rounded_repaint) /
                                                  static_cast<double>(readout.square_repaint),
                                              1) +
                                            "x"
                                      : "-";
  return "corner radius costs: square containers repaint " + grouped(readout.square_repaint) +
         " px/frame vs rounded " + grouped(readout.rounded_repaint) + " px  =  " + factor;
}

}  // namespace

Hud Hud::load(const std::string& font_dir) {
  Hud hud;
  sk_sp<SkFontMgr> manager = SkFontMgr_New_Custom_Directory(font_dir.c_str());
  if (!manager) {
    return hud;
  }
  hud.mono_ = manager->matchFamilyStyle("DejaVu Sans Mono", SkFontStyle::Normal());
  hud.sans_ = manager->matchFamilyStyle("DejaVu Sans", SkFontStyle::Bold());
  return hud;
}

void Hud::draw(SkCanvas& canvas, const dg::PixelRect& bounds, const Readout& readout) const {
  if (!mono_) {
    return;
  }
  const auto left = static_cast<float>(bounds.x) + 18.0F;
  const auto top = static_cast<float>(bounds.y);
  const float size = std::clamp(static_cast<float>(bounds.height) / 17.0F, 8.0F, 15.0F);
  const float line = size * 1.45F;

  const SkFont heading{sans_ ? sans_ : mono_, size + 1.0F};
  const SkFont mono{mono_, size};

  float y = top + line;
  canvas.drawString(title_line(readout).c_str(), left, y, heading, ink(kInk));
  y += line;
  canvas.drawString(mode_line(readout).c_str(), left, y, mono, ink(kAccent));

  y += line * 1.4F;
  canvas.drawString((pad_left("", 13) + pad_left("layout", 11) + pad_left("raster", 11) +
                     pad_left("present", 11) + pad_left("total", 11) + pad_left("frames", 9))
                        .c_str(),
                    left, y, mono, ink(kInkDim));
  y += line;
  canvas.drawString(lane_row("incremental", readout.incremental_lane).c_str(), left, y, mono,
                    ink(kFast));
  y += line;
  canvas.drawString(lane_row("full", readout.full_lane).c_str(), left, y, mono, ink(kSlow));
  y += line;
  canvas.drawString(speedup_row(readout.incremental_lane, readout.full_lane).c_str(), left, y,
                    mono, ink(kInk));

  y += line * 1.4F;
  canvas.drawString(scope_line(readout).c_str(), left, y, mono, ink(kInk));
  y += line;
  canvas.drawString(damage_line(readout).c_str(), left, y, mono, ink(kInk));
  y += line;
  canvas.drawString(corner_line(readout).c_str(), left, y, mono, ink(kWarn));

  if (!readout.diagnostic.empty()) {
    y += line;
    canvas.drawString(readout.diagnostic.c_str(), left, y, mono, ink(kSlow));
  }
}

}  // namespace hud
