// Text and the damage system: is a label allowed to be cut in half?
//
// This file exists because of a wrong assumption that was caught by measuring.
//
// Sub-step 1 established that Skia's anti-aliased ROUNDED rectangles are not
// clip-invariant, so a rounded node repaints whole. When text arrived in
// sub-step 3 the obvious move was to assume the same of glyphs - they are
// anti-aliased too - and clips_atomically() was written to return true for any
// node carrying a string.
//
// The measurement says otherwise. examples/05_widgets --clip-probe draws the
// same run under 600 random clips that cut it: a rounded rectangle differs by
// 582 pixels at zero slack, and TEXT DIFFERS BY ZERO. Glyphs are rasterized
// into masks and blitted, and a clip masks the blit rather than changing the
// coverage, which is a different mechanism from the analytic anti-aliasing a
// path fill goes through.
//
// So the rule was removed, and a label is now cut freely. That is worth 1.57x
// of the demo's damage (2,912,220 px against 1,860,708 px over the demo's
// scripted pointer path).
//
// THE CATCH, AND WHY THIS FILE IS NOT THE INTERACTION TEST: the byte-identity
// check over the demo scene passed both with the rule and without it - because
// a hover damages a whole button subtree, so its label is always fully
// contained and never actually cut. Sub-step 2 recorded exactly this trap: an
// injection that nothing catches is either a missing shape or an unobservable
// term, and answering the first as if it were the second wastes a day. Here it
// was a MISSING SHAPE, so the shape is built by hand below: a damage rectangle
// deliberately laid across the middle of a run of text.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/render/font_catalog.h"
#include "drawgui/render/render_tree.h"

namespace {

using dg::NodeId;
using dg::PixelRect;
using dg::RasterSurface;
using dg::RenderTree;

constexpr int kWidth = 361;
constexpr int kHeight = 121;
constexpr std::uint32_t kBackground = 0xFF14171C;

// The label under test, wide enough that a clip through its middle cuts
// several glyphs rather than landing between two words.
constexpr PixelRect kLabel{40, 40, 280, 40};

std::optional<dg::FontCatalog> catalog_with_ui_font(dg::FontId& font) {
  dg::Expected<dg::FontCatalog, dg::FontError> scanned =
      dg::FontCatalog::scan("/usr/share/fonts");
  if (!scanned) {
    return std::nullopt;
  }
  dg::FontCatalog catalog = std::move(scanned).value();
  const dg::Expected<dg::FontId, dg::FontError> id = catalog.add("DejaVu Sans", false);
  if (!id) {
    return std::nullopt;
  }
  font = id.value();
  return catalog;
}

std::vector<std::uint8_t> snapshot(const RasterSurface& surface) {
  const dg::PixelView view = surface.peek_pixels();
  const auto row = static_cast<std::size_t>(view.width) * 4;
  std::vector<std::uint8_t> pixels(row * static_cast<std::size_t>(view.height));
  for (int y = 0; y < view.height; ++y) {
    std::memcpy(pixels.data() + (static_cast<std::size_t>(y) * row),
                view.pixels + (static_cast<std::size_t>(y) * view.row_bytes), row);
  }
  return pixels;
}

std::uint32_t pixel_at(const std::vector<std::uint8_t>& pixels, int x, int y) {
  const auto row = static_cast<std::size_t>(kWidth) * 4;
  const std::size_t offset =
      (static_cast<std::size_t>(y) * row) + (static_cast<std::size_t>(x) * 4);
  return (static_cast<std::uint32_t>(pixels[offset + 3]) << 24) |
         (static_cast<std::uint32_t>(pixels[offset + 2]) << 16) |
         (static_cast<std::uint32_t>(pixels[offset + 1]) << 8) |
         static_cast<std::uint32_t>(pixels[offset]);
}

struct Fixture {
  RenderTree tree;
  NodeId label;
};

// A pair of surfaces, or nothing. Returned together because every case that
// wants one wants both, and because doctest's assertion macros expand into
// enough control flow that inlining this pushed the cases past clang-tidy's
// cognitive-complexity threshold.
struct Surfaces {
  RasterSurface damaged;
  RasterSurface reference;
};

bool have_font() {
  dg::FontId font;
  return catalog_with_ui_font(font).has_value();
}

std::optional<Surfaces> make_surfaces() {
  std::optional<RasterSurface> damaged = RasterSurface::create(kWidth, kHeight);
  std::optional<RasterSurface> reference = RasterSurface::create(kWidth, kHeight);
  if (!damaged.has_value() || !reference.has_value()) {
    return std::nullopt;
  }
  return Surfaces{std::move(*damaged), std::move(*reference)};
}

// Pixels outside `keep` that are not the background - glyphs that escaped the
// box their node declared.
std::size_t escaped_pixels(const std::vector<std::uint8_t>& pixels, const PixelRect& keep) {
  std::size_t escaped = 0;
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      if (dg::contains(keep, dg::PixelPoint{x, y})) {
        continue;
      }
      escaped += pixel_at(pixels, x, y) != kBackground ? std::size_t{1} : std::size_t{0};
    }
  }
  return escaped;
}

// Sweeps one-pixel-wide vertical damage slices across `across`, repainting
// after each. Each slice cuts whatever glyph it lands on.
void sweep_slices(Fixture& fixture, RasterSurface& surface, const PixelRect& across) {
  for (int x = across.left() - 2; x < across.right() + 2; ++x) {
    fixture.tree.damage_rect(PixelRect{x, 0, 1, kHeight});
    fixture.tree.repaint(surface);
  }
}

std::optional<Fixture> build(const std::string& content, int size) {
  dg::FontId font;
  std::optional<dg::FontCatalog> catalog = catalog_with_ui_font(font);
  if (!catalog.has_value()) {
    return std::nullopt;
  }

  dg::TreeSpec spec;
  spec.viewport = dg::PixelSize{kWidth, kHeight};
  spec.background.fill = dg::Color::from_argb(kBackground);
  spec.fonts = std::move(catalog);
  RenderTree tree{spec};

  dg::NodeStyle style;
  style.fill = dg::Color::from_argb(0xFF2C3644);
  style.text.text = content;
  style.text.font = font;
  style.text.size = static_cast<float>(size);
  style.text.color = dg::Color::from_argb(0xFFE8EDF4);
  style.text.align = dg::TextAlign::kLeft;
  style.text.inset = 8;

  const NodeId label = tree.add_child(RenderTree::root(), kLabel, style);
  return Fixture{std::move(tree), label};
}

// Two identical scenes and a surface each: one repainted from damage, one from
// scratch. Assembled in one helper because doctest's assertion macros expand
// into enough control flow that checking three optionals inline pushed the
// case past clang-tidy's cognitive-complexity threshold - and a test that has
// to be read twice is not doing its job either.
struct Bench {
  Fixture damaged;
  Fixture reference;
  RasterSurface left;
  RasterSurface right;
};

std::optional<Bench> make_bench(const std::string& content, int size) {
  std::optional<Fixture> damaged = build(content, size);
  std::optional<Fixture> reference = build(content, size);
  std::optional<Surfaces> surfaces = make_surfaces();
  if (!damaged.has_value() || !reference.has_value() || !surfaces.has_value()) {
    return std::nullopt;
  }
  return Bench{std::move(*damaged), std::move(*reference), std::move(surfaces->damaged),
               std::move(surfaces->reference)};
}

}  // namespace

TEST_SUITE("text damage") {
  TEST_CASE("a damage rectangle laid across a label produces a full repaint's pixels") {
    std::optional<Bench> bench =
        have_font() ? make_bench("Handgloves 123 quick brown", 15) : std::nullopt;
    if (!bench.has_value()) {
      MESSAGE("no usable font on this machine; nothing to measure");
      return;
    }
    Bench& pair = *bench;

    pair.damaged.tree.repaint_full(pair.left);
    pair.reference.tree.repaint_full(pair.right);
    REQUIRE(snapshot(pair.left) == snapshot(pair.right));

    sweep_slices(pair.damaged, pair.left, kLabel);
    CHECK(snapshot(pair.left) == snapshot(pair.right));

    // And a horizontal band through the middle of the run, which cuts every
    // glyph at once rather than one at a time.
    pair.damaged.tree.damage_rect(PixelRect{0, kLabel.top() + (kLabel.height / 2), kWidth, 4});
    pair.damaged.tree.repaint(pair.left);
    CHECK(snapshot(pair.left) == snapshot(pair.right));
  }

  TEST_CASE("a label is not clip-atomic, so a small damage rectangle stays small") {
    std::optional<Fixture> fixture = build("Handgloves 123 quick brown", 15);
    if (!fixture.has_value()) {
      MESSAGE("no usable font on this machine; nothing to measure");
      return;
    }
    std::optional<RasterSurface> surface = RasterSurface::create(kWidth, kHeight);
    if (!surface.has_value()) {
      FAIL("could not allocate a surface");
      return;
    }
    Fixture& under_test = *fixture;
    under_test.tree.repaint_full(*surface);

    // The claim the previous case licenses: text does not force the damage to
    // swallow the whole node. If clips_atomically() ever starts returning true
    // for text again, this fails - and the case above is what says whether
    // that was necessary.
    const PixelRect sliver{kLabel.x + 10, kLabel.y + 10, 6, 6};
    under_test.tree.damage_rect(sliver);
    const dg::RepaintStats stats = under_test.tree.repaint(*surface);
    CHECK(stats.pixels == sliver.area());
  }

  TEST_CASE("text never escapes the box its node declared") {
    // A string far too long for its box. Whatever it does inside is the
    // renderer's business; outside is the damage system's, and a glyph landing
    // there is a pixel nothing will ever invalidate.
    std::optional<Fixture> fixture =
        build("this caption is far too long to fit inside the box it was given", 15);
    std::optional<RasterSurface> surface = RasterSurface::create(kWidth, kHeight);
    if (!fixture.has_value()) {
      MESSAGE("no usable font on this machine; nothing to measure");
      return;
    }
    if (!surface.has_value()) {
      FAIL("could not allocate a surface");
      return;
    }
    fixture->tree.repaint_full(*surface);
    CHECK(escaped_pixels(snapshot(*surface), kLabel) == 0);
  }

  TEST_CASE("a rounded node IS still clip-atomic") {
    // The control. Sub-step 1's rule has not been relaxed, only text has been
    // taken out of it, and a change that removed both would pass every case
    // above.
    dg::TreeSpec spec;
    spec.viewport = dg::PixelSize{kWidth, kHeight};
    spec.background.fill = dg::Color::from_argb(kBackground);
    RenderTree tree{spec};

    dg::NodeStyle style;
    style.fill = dg::Color::from_argb(0xFF2C3644);
    style.radii = dg::Radii::all(6.0F);
    tree.add_child(RenderTree::root(), kLabel, style);

    std::optional<RasterSurface> surface = RasterSurface::create(kWidth, kHeight);
    if (!surface.has_value()) {
      FAIL("could not allocate a surface");
      return;
    }
    tree.repaint_full(*surface);

    const PixelRect sliver{kLabel.x + 10, kLabel.y + 10, 6, 6};
    tree.damage_rect(sliver);
    const dg::RepaintStats stats = tree.repaint(*surface);
    CHECK(stats.pixels > sliver.area());
    CHECK(stats.pixels >= kLabel.inflated_by(1).area());
  }
}
