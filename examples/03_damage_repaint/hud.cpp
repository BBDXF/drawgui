#include "hud.h"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <vector>

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

std::string cell(double milliseconds) {
  return pad_left(fixed(milliseconds, 2) + " ms", 11);
}

// A lane with no frames in it has no number, and printing 0.00 ms for it
// would read as "free" rather than as "not measured" - which is exactly what
// --mode damage produces, since it never takes a full-repaint sample.
std::string cell_or_dash(double milliseconds, std::size_t frames) {
  return frames == 0 ? pad_left("-", 11) : cell(milliseconds);
}

std::string ratio(double slow, double fast, bool measured) {
  if (!measured || fast <= 0.0 || slow <= 0.0) {
    return pad_left("-", 11);
  }
  return pad_left(fixed(slow / fast, 1) + "x", 11);
}

std::string lane_row(const char* label, const Lane& lane) {
  const double total = lane.raster.median_ms + lane.present.median_ms;
  return pad_left(label, 10) + cell_or_dash(lane.raster.median_ms, lane.frames) +
         cell_or_dash(lane.present.median_ms, lane.frames) + cell_or_dash(total, lane.frames) +
         pad_left(std::to_string(lane.frames), 9);
}

std::string tail_row(const Lane& damage, const Lane& full) {
  return pad_left("p95", 10) +
         cell_or_dash(damage.raster.p95_ms + damage.present.p95_ms, damage.frames) +
         pad_left("vs", 6) +
         cell_or_dash(full.raster.p95_ms + full.present.p95_ms, full.frames) +
         pad_left("   worst", 10) +
         cell_or_dash(damage.raster.worst_ms + damage.present.worst_ms, damage.frames) +
         pad_left("vs", 6) +
         cell_or_dash(full.raster.worst_ms + full.present.worst_ms, full.frames);
}

std::string speedup_row(const Lane& damage, const Lane& full) {
  const bool measured = damage.frames > 0 && full.frames > 0;
  return pad_left("speedup", 10) +
         ratio(full.raster.median_ms, damage.raster.median_ms, measured) +
         ratio(full.present.median_ms, damage.present.median_ms, measured) +
         ratio(full.raster.median_ms + full.present.median_ms,
               damage.raster.median_ms + damage.present.median_ms, measured) +
         pad_left("", 9);
}

std::string title_line(const Readout& readout) {
  const double megapixels = static_cast<double>(readout.viewport.width) *
                            static_cast<double>(readout.viewport.height) / 1'000'000.0;
  return "drawgui  damage-driven retained repaint      " +
         std::to_string(readout.viewport.width) + "x" +
         std::to_string(readout.viewport.height) + "  (" + fixed(megapixels, 2) + " Mpx)";
}

std::string mode_line(const Readout& readout) {
  return std::string{"mode: "} + (readout.damage_mode ? "DAMAGE" : "FULL  ") + "    paint: " +
         (readout.paint_mode == dg::PaintMode::kPicture ? "picture" : "direct ") +
         "    nodes: " + std::to_string(readout.nodes) +
         "    damage cap: " + std::to_string(readout.damage_cap) + " rects";
}

std::string frame_line(const Readout& readout) {
  const auto window = static_cast<double>(readout.viewport.width) *
                      static_cast<double>(readout.viewport.height);
  const double share =
      window > 0.0 ? 100.0 * static_cast<double>(readout.last.pixels) / window : 0.0;
  return "last damage frame: " + std::to_string(readout.last.rects) + " rect(s), " +
         grouped(readout.last.pixels) + " px = " + fixed(share, 2) + "% of the window,  " +
         std::to_string(readout.last.nodes_drawn) + " of " +
         std::to_string(readout.last.nodes_total) + " nodes drawn";
}

// Said out loud rather than quietly done. Repainting this panel is the
// measuring instrument, not the scene, and it is a twelfth of the window -
// leaving those frames in would put the cost of the readout into the number
// the readout exists to report.
std::string caveat_line(const Readout& readout) {
  return "dirty nodes asked for " + grouped(readout.last.requested_pixels) +
         " px;  timings exclude the ~5 frames a second that repaint this panel";
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
  // Nine baselines including the two blank-line gaps, so the strip has to be
  // divided by rather more than nine or the last row falls off the bottom.
  const float size = std::clamp(static_cast<float>(bounds.height) / 16.0F, 8.0F, 15.0F);
  const float line = size * 1.45F;

  const SkFont heading{sans_ ? sans_ : mono_, size + 1.0F};
  const SkFont mono{mono_, size};

  float y = top + line;
  canvas.drawString(title_line(readout).c_str(), left, y, heading, ink(kInk));
  y += line;
  canvas.drawString(mode_line(readout).c_str(), left, y, mono, ink(kAccent));

  y += line * 1.4F;
  canvas.drawString((pad_left("", 10) + pad_left("raster", 11) + pad_left("present", 11) +
                     pad_left("total", 11) + pad_left("frames", 9))
                        .c_str(),
                    left, y, mono, ink(kInkDim));
  y += line;
  canvas.drawString(lane_row("damage", readout.damage).c_str(), left, y, mono, ink(kFast));
  y += line;
  canvas.drawString(lane_row("full", readout.full).c_str(), left, y, mono, ink(kSlow));
  y += line;
  canvas.drawString(speedup_row(readout.damage, readout.full).c_str(), left, y, mono,
                    ink(kInk));

  y += line * 1.4F;
  canvas.drawString(tail_row(readout.damage, readout.full).c_str(), left, y, mono,
                    ink(kInkDim));

  y += line * 1.4F;
  canvas.drawString(frame_line(readout).c_str(), left, y, mono, ink(kInk));
  y += line;
  canvas.drawString(caveat_line(readout).c_str(), left, y, mono, ink(kInkDim));
}

}  // namespace hud
