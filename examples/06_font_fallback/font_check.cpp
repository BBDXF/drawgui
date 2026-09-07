#include "font_check.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/base/utf8.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/render/font_catalog.h"
#include "drawgui/render/render_tree.h"

#include "font_scene.h"

namespace font_check {
namespace {

// U+10330 GOTHIC LETTER AHSA. The scene carries it on purpose; every other
// codepoint in the scene must resolve. Must agree with font_scene.cpp.
constexpr char32_t kDeliberatelyUncovered = 0x10330;

bool contains_uncovered(const std::string& text) {
  for (std::size_t offset = 0; offset < text.size();) {
    const dg::Utf8Step step = dg::utf8_decode(text, offset);
    offset += step.length;
    if (step.valid && step.codepoint == kDeliberatelyUncovered) {
      return true;
    }
  }
  return false;
}

std::uint32_t pixel_at(const dg::PixelView& view, int x, int y) {
  std::uint32_t value = 0;
  std::memcpy(&value,
              view.pixels + (static_cast<std::size_t>(y) * view.row_bytes) +
                  (static_cast<std::size_t>(x) * 4),
              4);
  return value;
}

// Compares the two panels cell by cell relative to their own origins. They are
// the same size by construction, so this is a like-for-like comparison rather
// than a hash of two different regions.
std::size_t compare_panels(const dg::PixelView& view, const dg::PixelRect& left,
                           const dg::PixelRect& right) {
  std::size_t differences = 0;
  const int height = left.height < right.height ? left.height : right.height;
  const int width = left.width < right.width ? left.width : right.width;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if (pixel_at(view, left.x + x, left.y + y) != pixel_at(view, right.x + x, right.y + y)) {
        ++differences;
      }
    }
  }
  return differences;
}

}  // namespace

Result verify(font_scene::Scene& scene) {
  Result result;
  result.han_families_distinct = scene.hans_family != scene.ja_family;

  for (std::uint32_t index = 0; index < scene.tree.node_count(); ++index) {
    const dg::NodeId id{index};
    const dg::TextStyle& text = scene.tree.style(id).text;
    if (text.text.empty()) {
      continue;
    }
    const std::vector<dg::FontResolution> resolved =
        scene.catalog.resolve_text(text.font, text.language, text.text);
    result.codepoints += resolved.size();

    std::size_t missing = 0;
    for (const dg::FontResolution& item : resolved) {
      missing += item.glyph == 0 ? 1 : 0;
    }
    if (missing == 0) {
      continue;
    }
    result.missing += missing;
    if (contains_uncovered(text.text)) {
      result.deliberate_missing += missing;
    } else {
      result.nodes_with_missing.push_back(text.text);
    }
  }

  std::optional<dg::RasterSurface> surface =
      dg::RasterSurface::create(scene.tree.viewport().width, scene.tree.viewport().height);
  if (!surface.has_value()) {
    return result;
  }
  scene.tree.repaint_full(*surface);
  const dg::PixelView view = surface->peek_pixels();
  result.han_panel_differences =
      compare_panels(view, scene.tree.absolute_bounds(scene.hans_panel),
                     scene.tree.absolute_bounds(scene.ja_panel));

  // Passing means three things, and the third is the one that was learned the
  // hard way. The row that exists to show a missing glyph MUST actually be
  // missing: the first codepoint chosen for it turned out to be covered by
  // WenQuanYi Zen Hei, so the row rendered fine and this check reported a
  // clean pass over a demo that had stopped demonstrating its own point.
  const bool coverage_ok = result.nodes_with_missing.empty();
  const bool demonstration_ok = result.deliberate_missing > 0;
  const bool routing_ok = !result.han_families_distinct || result.han_panel_differences > 0;
  result.passed = coverage_ok && routing_ok && demonstration_ok;
  return result;
}

void print(const font_scene::Scene& scene, const Result& result, std::ostream& out) {
  out << "font fallback check\n";
  out << "  primary family      " << scene.catalog.size() << " catalog entries\n";
  out << "  codepoints in scene " << result.codepoints << "\n";
  out << "  without a glyph     " << result.missing << ", of which deliberate "
      << result.deliberate_missing << "\n";
  if (result.deliberate_missing == 0) {
    out << "  ERROR: the row that exists to show a missing glyph has none - some font "
           "here covers it, so the demonstration is vacuous\n";
  }
  out << "  zh-Hans face        " << scene.hans_family << "\n";
  out << "  ja face             " << scene.ja_family << "\n";
  out << "  Han panels differ   " << result.han_panel_differences << " pixels\n";
  for (const std::string& node : result.nodes_with_missing) {
    out << "  UNEXPECTED missing glyph in: " << node << "\n";
  }
  if (!result.han_families_distinct) {
    out << "  note: only one Han-capable family here, so the two panels are the same\n";
  }
  out << (result.passed ? "PASS\n" : "FAIL\n");
}

}  // namespace font_check
