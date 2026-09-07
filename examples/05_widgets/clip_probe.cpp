// Is Skia's anti-aliased TEXT clip-invariant?
//
// Sub-step 1 asked this of rounded rectangles, found that it is not, and made
// a rounded node repaint whole because of it. Text is the other thing Skia
// anti-aliases, so a label raises the same question and the answer decides
// whether a label can be clipped in half by a damage rectangle.
//
// IT CANNOT BE MEASURED THROUGH THE RENDER TREE, and that is the interesting
// part. clips_atomically() already returns true for a node carrying text, so
// the tree grows every damage rectangle to swallow such a node whole before
// any clip reaches it. A probe written at that level reports zero differing
// pixels no matter what Skia does - it measures the rule rather than the fact
// the rule exists for. The first version of this probe did exactly that and
// had to be thrown away.
//
// So this asks Skia directly, through RasterSurface::sk_canvas(), which is the
// documented escape hatch for exactly this: draw the same run twice, once
// unclipped and once under a clip that cuts it, and compare the pixels INSIDE
// the clip - where a clip-invariant rasterizer must produce identical output.
//
// It lives in its own translation unit because it is the only part of the
// widget example that names a Skia type outside the readout, and because it
// must not be compiled into the headless test binaries, which do not link
// Skia.

#include "clip_probe.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/render/font_catalog.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkFontStyle.h"
#include "include/core/SkFontTypes.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"
#include "include/ports/SkFontMgr_directory.h"

namespace clip_probe {
namespace {

constexpr int kWidth = 360;
constexpr int kHeight = 140;
constexpr int kTrials = 600;

// The shape under test, in the middle of the surface so a random clip has room
// to cut it from any side.
constexpr dg::PixelRect kBox{60, 40, 240, 56};

SkRect to_sk(const dg::PixelRect& rect) {
  return SkRect::MakeXYWH(static_cast<float>(rect.x), static_cast<float>(rect.y),
                          static_cast<float>(rect.width), static_cast<float>(rect.height));
}

std::vector<std::uint8_t> snapshot(const dg::RasterSurface& surface) {
  const dg::PixelView view = surface.peek_pixels();
  const auto row = static_cast<std::size_t>(view.width) * 4;
  std::vector<std::uint8_t> pixels(row * static_cast<std::size_t>(view.height));
  for (int y = 0; y < view.height; ++y) {
    std::copy_n(
        view.pixels + (static_cast<std::size_t>(y) * view.row_bytes), row,
        pixels.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y) * row));
  }
  return pixels;
}

// What each subject draws. Both are painted the way the render tree paints
// them, so the answer is about the real draw calls rather than about a
// simplified stand-in.
enum class Subject : std::uint8_t {
  kSquare,
  kRounded,
  kText,
};

void draw_subject(SkCanvas& canvas, Subject subject, const SkFont& font) {
  const SkRect rect = to_sk(kBox);

  SkPaint fill;
  fill.setAntiAlias(true);
  fill.setColor(SkColor{0xFF2C3644});

  switch (subject) {
    case Subject::kSquare:
      canvas.drawRect(rect, fill);
      return;
    case Subject::kRounded: {
      SkRRect rounded;
      rounded.setRectXY(rect, 6.0F, 6.0F);
      canvas.drawRRect(rounded, fill);
      return;
    }
    case Subject::kText:
      break;
  }

  canvas.drawRect(rect, fill);
  SkPaint glyphs;
  glyphs.setAntiAlias(true);
  glyphs.setColor(SkColor{0xFFE8EDF4});
  const char* run = "Handgloves 123";
  canvas.save();
  canvas.clipRect(rect, false);
  canvas.drawSimpleText(run, std::char_traits<char>::length(run), SkTextEncoding::kUTF8,
                        static_cast<float>(kBox.x) + 12.0F, static_cast<float>(kBox.y) + 36.0F,
                        font, glyphs);
  canvas.restore();
}

const char* name_of(Subject subject) {
  switch (subject) {
    case Subject::kSquare:
      return "square rect";
    case Subject::kRounded:
      return "rounded rect";
    case Subject::kText:
      return "text";
  }
  return "?";
}

std::size_t differing_pixels(Subject subject, const SkFont& font, int slack,
                             dg::RasterSurface& whole, dg::RasterSurface& cut) {
  SkCanvas* unclipped = whole.sk_canvas();
  unclipped->clear(SkColor{0xFF14171C});
  draw_subject(*unclipped, subject, font);
  const std::vector<std::uint8_t> reference = snapshot(whole);

  std::mt19937 random{20260907};
  std::uniform_int_distribution<int> x_at{0, kWidth - 1};
  std::uniform_int_distribution<int> y_at{0, kHeight - 1};

  const auto row = static_cast<std::size_t>(kWidth) * 4;
  std::size_t differing = 0;

  for (int trial = 0; trial < kTrials; ++trial) {
    int left = x_at(random);
    int right = x_at(random);
    int top = y_at(random);
    int bottom = y_at(random);
    if (left > right) {
      std::swap(left, right);
    }
    if (top > bottom) {
      std::swap(top, bottom);
    }
    dg::PixelRect clip = dg::PixelRect::from_edges(left, top, right + 1, bottom + 1);

    // Only clips that actually CUT the shape are interesting. One that misses
    // it entirely, or already contains it, cannot answer the question and
    // would dilute the count with trivially equal trials.
    const dg::PixelRect halo = kBox.inflated_by(slack);
    if (!dg::intersects(clip, kBox) || dg::contains(clip, halo)) {
      continue;
    }
    if (slack > 0) {
      clip = dg::join(clip, halo);
      if (dg::contains(clip, dg::PixelRect{0, 0, kWidth, kHeight})) {
        continue;
      }
    }

    SkCanvas* clipped = cut.sk_canvas();
    clipped->clear(SkColor{0xFF14171C});
    clipped->save();
    clipped->clipRect(to_sk(clip), false);
    draw_subject(*clipped, subject, font);
    clipped->restore();

    const std::vector<std::uint8_t> under_clip = snapshot(cut);
    for (int y = clip.top(); y < clip.bottom(); ++y) {
      for (int x = clip.left(); x < clip.right(); ++x) {
        const std::size_t offset =
            (static_cast<std::size_t>(y) * row) + (static_cast<std::size_t>(x) * 4);
        if (under_clip[offset] != reference[offset] ||
            under_clip[offset + 1] != reference[offset + 1] ||
            under_clip[offset + 2] != reference[offset + 2]) {
          ++differing;
        }
      }
    }
  }
  return differing;
}

}  // namespace

void report(std::ostream& out, const std::string& font_dir) {
  out << "\nIS SKIA'S ANTI-ALIASED OUTPUT CLIP-INVARIANT?\n"
      << "  The same shape is drawn twice - once unclipped, once under a random clip that\n"
      << "  CUTS it - and the pixels inside the clip are compared. A clip-invariant\n"
      << "  rasterizer must produce identical bytes there. `slack` is how far the clip is\n"
      << "  grown around the shape before drawing, which is the rule the render tree\n"
      << "  applies to a clip-atomic node.\n\n"
      << "  The square row is the CONTROL: sub-step 1 measured it at zero, so a non-zero\n"
      << "  square row would mean this probe is broken rather than that Skia changed.\n\n";

  sk_sp<SkFontMgr> manager = SkFontMgr_New_Custom_Directory(font_dir.c_str());
  sk_sp<SkTypeface> typeface =
      manager ? manager->matchFamilyStyle("DejaVu Sans", SkFontStyle::Normal()) : nullptr;
  if (!typeface) {
    out << "  no 'DejaVu Sans' under " << font_dir << ", so text cannot be measured\n";
    return;
  }
  SkFont font{std::move(typeface), 17.0F};
  font.setEdging(SkFont::Edging::kAntiAlias);
  font.setSubpixel(false);

  std::optional<dg::RasterSurface> whole = dg::RasterSurface::create(kWidth, kHeight);
  std::optional<dg::RasterSurface> cut = dg::RasterSurface::create(kWidth, kHeight);
  if (!whole.has_value() || !cut.has_value()) {
    out << "  could not allocate a surface\n";
    return;
  }

  out << "  shape           slack 0     slack 1     slack 2\n"
      << "  " << std::string(50, '-') << "\n";

  for (const Subject subject : {Subject::kSquare, Subject::kRounded, Subject::kText}) {
    out << "  " << name_of(subject);
    for (int pad = static_cast<int>(std::string{name_of(subject)}.size()); pad < 16; ++pad) {
      out << ' ';
    }
    for (int slack = 0; slack <= 2; ++slack) {
      const std::size_t differing = differing_pixels(subject, font, slack, *whole, *cut);
      out << differing;
      for (int pad = static_cast<int>(std::to_string(differing).size()); pad < 12; ++pad) {
        out << ' ';
      }
    }
    out << "\n";
  }

  out << "\n  A non-zero cell means that shape cannot be cut by a damage rectangle at that\n"
      << "  slack, and must therefore be repainted whole. That is what clips_atomically()\n"
      << "  encodes, and this table is its justification.\n";
}

}  // namespace clip_probe
