#include "font_scene.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/graphics/types.h"
#include "drawgui/render/font_catalog.h"
#include "drawgui/render/render_tree.h"

namespace font_scene {
namespace {

using dg::Color;
using dg::FontId;
using dg::NodeId;
using dg::NodeStyle;
using dg::PixelRect;
using dg::RenderTree;
using dg::TextAlign;
using dg::TextStyle;

constexpr std::uint32_t kBackground = 0xFF14171C;
constexpr std::uint32_t kPanel = 0xFF1D2129;
constexpr std::uint32_t kHeading = 0xFFF6C445;
constexpr std::uint32_t kLabel = 0xFF8A93A0;
constexpr std::uint32_t kBody = 0xFFE7ECF3;
constexpr std::uint32_t kWarn = 0xFFE74C3C;

constexpr int kMargin = 24;
constexpr int kRowHeight = 34;
constexpr int kScriptColumn = 150;

// A codepoint chosen because NOTHING here covers it: U+10330 GOTHIC LETTER
// AHSA. It is in the scene on purpose - the missing-glyph decision is supposed
// to be visible, and a demo that only shows successes cannot show what failure
// looks like.
//
// The first choice was U+10000 LINEAR B SYLLABLE B008 A, and it was WRONG:
// WenQuanYi Zen Hei covers it, so the row rendered perfectly and the demo
// silently stopped demonstrating anything. font_check::verify() now REQUIRES
// this row to be missing, which is what turned that into a failure instead of
// a passing lie.
constexpr const char* kUncovered = "\xF0\x90\x8C\xB0";

TextStyle text_of(std::string content, FontId font, int size, std::uint32_t colour) {
  TextStyle text;
  text.text = std::move(content);
  text.font = font;
  text.size = static_cast<float>(size);
  text.color = Color::from_argb(colour);
  text.align = TextAlign::kLeft;
  text.inset = 8;
  return text;
}

NodeStyle label_node(const TextStyle& text) {
  NodeStyle style;
  style.text = text;
  return style;
}

NodeStyle panel_node(std::uint32_t fill) {
  NodeStyle style;
  style.fill = Color::from_argb(fill);
  return style;
}

std::vector<Sample> build_samples() {
  return {
      {"Latin", "The quick brown fox"},
      {"Greek",
       "\xCE\x91\xCE\xB8\xCE\xAE\xCE\xBD\xCE\xB1 \xCE\xBA\xCE\xB1\xCE\xB9 "
       "\xCE\xA3\xCF\x80\xCE\xAC\xCF\x81\xCF\x84\xCE\xB7"},
      {"Cyrillic", "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82 \xD0\xBC\xD0\xB8\xD1\x80"},
      {"Hebrew", "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D \xD7\xA2\xD7\x95\xD7\x9C\xD7\x9D"},
      {"Arabic",
       "\xD8\xA7\xD9\x84\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85 "
       "\xD8\xB9\xD9\x84\xD9\x8A\xD9\x83\xD9\x85"},
      {"Han", "\xE4\xB8\xAD\xE6\x96\x87\xE6\xB8\xB2\xE6\x9F\x93\xE6\xB5\x8B\xE8\xAF\x95"},
      {"Kana",
       "\xE3\x81\xB2\xE3\x82\x89\xE3\x81\x8C\xE3\x81\xAA "
       "\xE3\x82\xAB\xE3\x82\xBF\xE3\x82\xAB\xE3\x83\x8A"},
      {"Hangul", "\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4 \xED\x85\x8C\xEC\x8A\xA4\xED\x8A\xB8"},
      {"Georgian",
       "\xE1\x83\x92\xE1\x83\x90\xE1\x83\x9B\xE1\x83\x90\xE1\x83\xA0\xE1\x83\xAF\xE1\x83\x9D"
       "\xE1\x83\x91\xE1\x83\x90"},
      {"Armenian", "\xD4\xB2\xD5\xA1\xD6\x80\xD5\xA5\xD6\x82 \xD5\xB1\xD5\xA5\xD5\xA6"},
      {"Symbols", "\xE2\x86\x92 \xE2\x88\x91 \xE2\x94\x82 \xE2\xA0\x8B \xE2\x99\xA0"},
      // Mixed in one string, which is the case that needs run splitting rather
      // than a single lookup per label.
      {"Mixed",
       "Hello \xE4\xB8\x96\xE7\x95\x8C \xF0\x9F\x8C\x8D \xD0\x9C\xD0\xB8\xD1\x80 "
       "\xE2\x86\x92 OK"},
      {"Emoji", "\xF0\x9F\x98\x80 \xF0\x9F\x91\x8D \xF0\x9F\x8E\xA8 \xF0\x9F\x9A\x80"},
      {"No font", std::string{kUncovered} + kUncovered + " (Gothic - expect boxes)"},
  };
}

// The families that can draw Han, in the pool's own order. Derived rather than
// hardcoded so the demo works on a machine with a different font set - and so
// that "this machine only has one" is a state the scene can report instead of
// a state that makes it silently draw the same thing twice.
std::vector<std::string> han_capable(dg::FontCatalog& catalog) {
  std::vector<std::string> found;
  for (const std::string& family : catalog.fallback_families()) {
    const dg::Expected<FontId, dg::FontError> id = catalog.add(family, false);
    if (!id) {
      continue;
    }
    const dg::FontResolution direct = catalog.resolve(id.value(), "", 0x4E2D);
    if (direct.from_primary && direct.glyph != 0) {
      found.push_back(family);
    }
  }
  return found;
}

// Renders the Han sample in one family, so that "these two faces look
// different" can be measured rather than assumed from their names.
std::vector<std::uint8_t> render_han(dg::FontCatalog& catalog, const std::string& family) {
  constexpr int kProbeWidth = 260;
  constexpr int kProbeHeight = 72;
  std::vector<std::uint8_t> pixels;

  const dg::Expected<FontId, dg::FontError> id = catalog.add(family, false);
  std::optional<dg::RasterSurface> surface =
      dg::RasterSurface::create(kProbeWidth, kProbeHeight);
  if (!id || !surface.has_value()) {
    return pixels;
  }

  dg::TreeSpec spec;
  spec.viewport = dg::PixelSize{kProbeWidth, kProbeHeight};
  spec.background.fill = Color::from_argb(kBackground);
  spec.fonts = catalog;
  RenderTree tree{spec};
  TextStyle text = text_of(han_sample(), id.value(), 48, kBody);
  text.align = TextAlign::kCenter;
  tree.add_child(RenderTree::root(), PixelRect{0, 0, kProbeWidth, kProbeHeight},
                 label_node(text));
  tree.repaint_full(*surface);

  const dg::PixelView view = surface->peek_pixels();
  const auto row = static_cast<std::size_t>(view.width) * 4;
  pixels.resize(row * static_cast<std::size_t>(view.height));
  for (int y = 0; y < view.height; ++y) {
    std::memcpy(pixels.data() + (static_cast<std::size_t>(y) * row),
                view.pixels + (static_cast<std::size_t>(y) * view.row_bytes), row);
  }
  return pixels;
}

std::size_t difference(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
  if (a.size() != b.size() || a.empty()) {
    return 0;
  }
  std::size_t count = 0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    count += a[i] != b[i] ? std::size_t{1} : std::size_t{0};
  }
  return count;
}

// The two Han-capable families whose rendering of the sample differs MOST.
//
// Picked by measurement rather than by name, and the reason is a measurement
// too: Droid Sans Fallback and WenQuanYi Micro Hei share a design lineage and
// differ in only 215 pixels of the demo panel, while WenQuanYi Zen Hei is a
// visibly different face. A screenshot has to show the difference to be worth
// taking, and "which of these fonts look different" is not answerable from
// their names.
std::pair<std::string, std::string> most_distinct_pair(dg::FontCatalog& catalog,
                                                       const std::vector<std::string>& han) {
  if (han.empty()) {
    return {};
  }
  if (han.size() == 1) {
    return {han.front(), han.front()};
  }
  std::vector<std::vector<std::uint8_t>> rendered;
  rendered.reserve(han.size());
  for (const std::string& family : han) {
    rendered.push_back(render_han(catalog, family));
  }

  std::size_t best_left = 0;
  std::size_t best_right = 1;
  std::size_t best = 0;
  for (std::size_t left = 0; left < han.size(); ++left) {
    for (std::size_t right = left + 1; right < han.size(); ++right) {
      const std::size_t score = difference(rendered[left], rendered[right]);
      if (score > best) {
        best = score;
        best_left = left;
        best_right = right;
      }
    }
  }
  return {han[best_left], han[best_right]};
}

// Routes zh-Hans and ja to two DIFFERENT families when this machine has two.
//
// On a machine with a real Japanese font the default table already does the
// right thing. This one does not have one - every CJK font here is Chinese -
// so the demo names two Chinese faces with visibly different outlines and says
// on screen that it is demonstrating the ROUTING, not correct Japanese
// typography. Faking the second half would be worse than admitting it.
void route_languages(dg::FontCatalog& catalog, const std::vector<std::string>& han,
                     Scene& scene) {
  if (han.empty()) {
    return;
  }
  const std::pair<std::string, std::string> pair = most_distinct_pair(catalog, han);
  scene.hans_family = pair.first;
  scene.ja_family = pair.second;

  std::vector<dg::FontFallbackRule> rules = {
      dg::FontFallbackRule{"zh-Hans", {scene.hans_family}},
      dg::FontFallbackRule{"ja", {scene.ja_family}},
  };
  for (dg::FontFallbackRule& rule : dg::FontCatalog::default_fallback_rules()) {
    rules.push_back(std::move(rule));
  }
  catalog.set_fallback_rules(std::move(rules));
}

int add_rows(Scene& scene, FontId label_font, int y) {
  for (const Sample& sample : scene.samples) {
    const NodeId row = scene.tree.add_child(
        RenderTree::root(),
        PixelRect{kMargin, y, scene.tree.viewport().width - (2 * kMargin), kRowHeight},
        panel_node(kPanel));
    scene.tree.add_child(row, PixelRect{0, 0, kScriptColumn, kRowHeight},
                         label_node(text_of(sample.script, label_font, 14, kLabel)));
    const std::uint32_t colour = sample.script == "No font" ? kWarn : kBody;
    scene.tree.add_child(
        row,
        PixelRect{kScriptColumn, 0, scene.tree.viewport().width - (2 * kMargin) - kScriptColumn,
                  kRowHeight},
        label_node(text_of(sample.text, scene.primary, 20, colour)));
    y += kRowHeight + 4;
  }
  return y;
}

void add_han_panels(Scene& scene, FontId label_font, int y) {
  const int width = scene.tree.viewport().width - (2 * kMargin);
  const int half = (width - 12) / 2;

  scene.tree.add_child(
      RenderTree::root(), PixelRect{kMargin, y, width, 26},
      label_node(text_of("Han unification: the same four codepoints, two language tags",
                         label_font, 15, kHeading)));
  y += 30;

  const auto panel = [&](int x, const std::string& tag, const std::string& family) {
    const NodeId host = scene.tree.add_child(RenderTree::root(), PixelRect{x, y, half, 128},
                                             panel_node(kPanel));
    scene.tree.add_child(host, PixelRect{0, 4, half, 22},
                         label_node(text_of("language = \"" + tag + "\"  ->  " + family,
                                            label_font, 13, kLabel)));
    TextStyle han = text_of(han_sample(), scene.primary, 56, kBody);
    han.language = tag;
    han.align = TextAlign::kCenter;
    const NodeId text_node =
        scene.tree.add_child(host, PixelRect{0, 28, half, 96}, label_node(han));
    return text_node;
  };

  scene.hans_panel = panel(kMargin, "zh-Hans", scene.hans_family);
  scene.ja_panel = panel(kMargin + half + 12, "ja", scene.ja_family);
  y += 136;

  // Two short lines rather than one long one: there is no line breaking in
  // this slice (design.md section 5.13.6 puts it behind ICU), so a string that
  // does not fit is a string that is cut off.
  const bool distinct = scene.hans_family != scene.ja_family;
  const std::vector<std::string> note =
      distinct
          ? std::vector<std::string>{
                "Two faces, chosen only by the language tag - nothing else differs.",
                "Both are CHINESE faces: this machine has no Japanese font, so this proves "
                "the routing, not the typography."}
          : std::vector<std::string>{
                "This machine offers only one Han-capable family, so both panels are "
                "identical.",
                "The routing still ran; there was nothing to route to."};
  for (const std::string& line : note) {
    scene.tree.add_child(RenderTree::root(), PixelRect{kMargin, y, width, 22},
                         label_node(text_of(line, label_font, 13, kWarn)));
    y += 22;
  }
}

}  // namespace

std::string han_sample() {
  return "\xE9\xAA\xA8\xE7\x9B\xB4\xE6\xAC\xA1\xE6\xB5\xB7";
}

dg::Expected<Scene, dg::FontError> build(const Options& options) {
  dg::Expected<dg::FontCatalog, dg::FontError> scanned =
      dg::FontCatalog::scan(options.font_dir);
  if (!scanned) {
    return dg::Unexpected{scanned.error()};
  }
  dg::FontCatalog catalog = std::move(scanned).value();

  const dg::Expected<FontId, dg::FontError> primary =
      catalog.add(options.primary_family, false);
  if (!primary) {
    return dg::Unexpected{primary.error()};
  }
  const dg::Expected<FontId, dg::FontError> label = catalog.add(options.primary_family, true);
  if (!label) {
    return dg::Unexpected{label.error()};
  }

  dg::TreeSpec spec;
  spec.viewport = options.viewport;
  spec.background.fill = Color::from_argb(kBackground);
  spec.fonts = catalog;

  Scene scene{
      RenderTree{spec}, catalog, primary.value(), build_samples(), NodeId{}, NodeId{}, {}, {}};

  route_languages(scene.catalog, han_capable(scene.catalog), scene);
  // The tree holds its own copy of the catalog handle, and the handle is
  // shared, so the rules just set are the rules the paint path reads.

  scene.tree.add_child(RenderTree::root(),
                       PixelRect{kMargin, kMargin, options.viewport.width - (2 * kMargin), 28},
                       label_node(text_of("One named family - " + options.primary_family +
                                              " - drawing every script below",
                                          label.value(), 18, kHeading)));

  const int after_rows = add_rows(scene, label.value(), kMargin + 40);
  add_han_panels(scene, label.value(), after_rows + 12);
  return scene;
}

}  // namespace font_scene
