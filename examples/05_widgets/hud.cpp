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
constexpr SkColor kGood = 0xFF5BD98A;
constexpr SkColor kAccent = 0xFF3FA9F5;
constexpr SkColor kWarn = 0xFFF6C445;
constexpr SkColor kBad = 0xFFE9825F;

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

// Thousands separators, because 38214 and 382140 are hard to tell apart at a
// glance and telling them apart is the whole point of the line.
std::string grouped(std::int64_t value) {
  std::string digits = std::to_string(value);
  for (auto position = static_cast<int>(digits.size()) - 3; position > 0; position -= 3) {
    digits.insert(static_cast<std::size_t>(position), ",");
  }
  return digits;
}

std::string pad_right(const std::string& text, std::size_t width) {
  return text.size() >= width ? text : text + std::string(width - text.size(), ' ');
}

std::string title_line(const Readout& readout) {
  return "drawgui  widgets and pointer interaction        " +
         std::to_string(readout.viewport.width) + "x" +
         std::to_string(readout.viewport.height) + "   " + std::to_string(readout.nodes) +
         " nodes, " + std::to_string(readout.widgets) + " widgets";
}

std::string pointer_line(const Readout& readout) {
  if (!readout.pointer_inside) {
    return "pointer:  outside the window";
  }
  return "pointer:  " +
         pad_right(std::to_string(readout.pointer.x) + "," + std::to_string(readout.pointer.y),
                   12) +
         "hover: " + pad_right(readout.hovered, 26) + "press: " + readout.pressed;
}

std::string click_line(const Readout& readout) {
  return "clicks:   " + pad_right(std::to_string(readout.clicks), 12) +
         "last: " + readout.last_click;
}

// Enters and leaves must track each other. They are printed together so that a
// drift - the symptom of a boundary crossing that reported one side and not
// the other - is visible while it is happening rather than inferred afterwards.
std::string balance_line(const Readout& readout) {
  const int outstanding = readout.enters - readout.leaves;
  return "enter/leave: " + std::to_string(readout.enters) + " / " +
         std::to_string(readout.leaves) + "   outstanding " + std::to_string(outstanding) +
         (outstanding == 0 || outstanding == 1 ? "  (balanced)" : "  <-- DRIFTING");
}

std::string damage_line(const Readout& readout) {
  const auto window = static_cast<double>(readout.viewport.width) *
                      static_cast<double>(readout.viewport.height);
  const double share =
      window > 0.0 ? 100.0 * static_cast<double>(readout.last_paint.pixels) / window : 0.0;
  return "last repaint: " + grouped(readout.last_paint.pixels) + " px = " + fixed(share, 3) +
         "% of the window in " + std::to_string(readout.last_paint.rects) + " rect(s), " +
         std::to_string(readout.last_paint.nodes_drawn) + " of " +
         std::to_string(readout.last_paint.nodes_total) + " nodes";
}

std::string corner_line(const Readout& readout) {
  const bool measured = readout.square_repaint > 0 && readout.rounded_repaint > 0;
  const std::string factor = measured ? fixed(static_cast<double>(readout.rounded_repaint) /
                                                  static_cast<double>(readout.square_repaint),
                                              1) +
                                            "x"
                                      : "-";
  return "corner radius costs: square controls repaint " + grouped(readout.square_repaint) +
         " px/interaction vs rounded " + grouped(readout.rounded_repaint) + "  =  " + factor;
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
  const float size = std::clamp(static_cast<float>(bounds.height) / 13.0F, 8.0F, 14.0F);
  const float line = size * 1.5F;

  const SkFont heading{sans_ ? sans_ : mono_, size + 1.0F};
  const SkFont mono{mono_, size};

  float y = top + line;
  canvas.drawString(title_line(readout).c_str(), left, y, heading, ink(kInk));

  y += line * 1.3F;
  canvas.drawString(pointer_line(readout).c_str(), left, y, mono, ink(kAccent));
  y += line;
  canvas.drawString(click_line(readout).c_str(), left, y, mono, ink(kGood));
  y += line;
  canvas.drawString(balance_line(readout).c_str(), left, y, mono,
                    ink(readout.enters - readout.leaves > 1 ? kBad : kInkDim));

  y += line * 1.3F;
  canvas.drawString(damage_line(readout).c_str(), left, y, mono, ink(kInk));
  y += line;
  canvas.drawString(corner_line(readout).c_str(), left, y, mono, ink(kWarn));

  if (!readout.note.empty()) {
    y += line;
    canvas.drawString(readout.note.c_str(), left, y, mono, ink(kInkDim));
  }
  if (!readout.diagnostic.empty()) {
    y += line;
    canvas.drawString(readout.diagnostic.c_str(), left, y, mono, ink(kWarn));
  }
}

}  // namespace hud
