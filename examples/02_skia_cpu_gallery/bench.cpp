#include "bench.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iomanip>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "drawgui/graphics/raster_surface.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkRect.h"
#include "include/core/SkSize.h"

namespace bench {
namespace {

using Clock = std::chrono::steady_clock;

struct Size {
  const char* label;
  int width;
  int height;
};

// 1080p is the design.md section 5.15.7 baseline; 1440p is past the 2-megapixel
// mark at which section 5.15.2 says partial presentation must switch itself on,
// so it is the size that tests whether that rule is right.
constexpr Size kSizes[] = {
    {"800x600", 800, 600},
    {"1280x720", 1280, 720},
    {"1920x1080", 1920, 1080},
    {"2560x1440", 2560, 1440},
};

// A hover highlight or a blinking caret is about this big. Chosen once and
// used at every resolution so the partial-repaint numbers are comparable.
constexpr int kDirtyWidth = 260;
constexpr int kDirtyHeight = 72;

constexpr int kWarmupFrames = 12;

double elapsed_ms(Clock::time_point started) {
  return std::chrono::duration<double, std::milli>(Clock::now() - started).count();
}

template <typename Body>
Stats time_frames(int frames, const Body& body) {
  for (int i = 0; i < kWarmupFrames; ++i) {
    body();
  }
  std::vector<double> samples;
  samples.reserve(static_cast<std::size_t>(frames));
  for (int i = 0; i < frames; ++i) {
    const Clock::time_point started = Clock::now();
    body();
    samples.push_back(elapsed_ms(started));
  }
  return summarize(std::move(samples));
}

void write_header(std::ostream& out, const char* first_column) {
  out << "\n  " << std::left << std::setw(34) << first_column << std::right << std::setw(9)
      << "median" << std::setw(9) << "p95" << std::setw(9) << "worst" << std::setw(10) << "fps"
      << "\n  " << std::string(71, '-') << "\n";
}

void write_row(std::ostream& out, const std::string& label, const Stats& stats) {
  out << "  " << std::left << std::setw(34) << label << std::right << std::fixed
      << std::setprecision(2) << std::setw(9) << stats.median_ms << std::setw(9) << stats.p95_ms
      << std::setw(9) << stats.max_ms << std::setprecision(1) << std::setw(10)
      << stats.median_fps() << "\n";
}

gallery::Selection only(gallery::Category category) {
  return gallery::Selection{category == gallery::Category::kGeometry,
                            category == gallery::Category::kText,
                            category == gallery::Category::kEffects};
}

}  // namespace

double Stats::median_fps() const {
  return median_ms > 0.0 ? 1000.0 / median_ms : 0.0;
}

Stats summarize(std::vector<double> samples_ms) {
  Stats stats;
  if (samples_ms.empty()) {
    return stats;
  }
  std::sort(samples_ms.begin(), samples_ms.end());
  stats.samples = samples_ms.size();
  stats.min_ms = samples_ms.front();
  stats.max_ms = samples_ms.back();
  stats.median_ms = samples_ms[samples_ms.size() / 2];
  const auto p95_index =
      static_cast<std::size_t>(0.95 * static_cast<double>(samples_ms.size() - 1));
  stats.p95_ms = samples_ms[p95_index];
  return stats;
}

void run_offscreen(const gallery::Resources& resources, int frames, std::ostream& out) {
  out << "\nOFFSCREEN RASTER - Skia CPU time only, no window, no presentation.\n"
      << "  " << frames << " timed frames per row after " << kWarmupFrames
      << " warmup frames.\n";

  for (const Size& size : kSizes) {
    std::optional<dg::RasterSurface> surface =
        dg::RasterSurface::create(size.width, size.height);
    if (!surface) {
      out << "  " << size.label << ": could not allocate a surface\n";
      continue;
    }
    SkCanvas* canvas = surface->sk_canvas();
    const SkISize target = SkISize::Make(size.width, size.height);

    write_header(out, std::string{"raster @ "}.append(size.label).c_str());

    const auto draw = [&](const gallery::Selection& selection) {
      gallery::draw_frame(*canvas, resources, target, selection);
    };

    write_row(out, "full frame (everything)",
              time_frames(frames, [&] { draw(gallery::Selection{}); }));
    write_row(out, "without effects (no blur/shadow)",
              time_frames(frames, [&] { draw(gallery::Selection{true, true, false}); }));
    write_row(out, "without text",
              time_frames(frames, [&] { draw(gallery::Selection{true, false, true}); }));
    write_row(out, "geometry only",
              time_frames(frames, [&] { draw(only(gallery::Category::kGeometry)); }));
    write_row(out, "text only",
              time_frames(frames, [&] { draw(only(gallery::Category::kText)); }));
    write_row(out, "effects only",
              time_frames(frames, [&] { draw(only(gallery::Category::kEffects)); }));

    // The same scene, drawn under a small clip. Skia rejects everything
    // outside it, so this is what a GUI actually pays for a hover or a caret
    // blink - the number that decides whether CPU raster scales with the
    // screen or with the change.
    const SkRect dirty =
        SkRect::MakeXYWH(static_cast<float>(size.width) * 0.25F,
                         static_cast<float>(size.height) * 0.4F, kDirtyWidth, kDirtyHeight);
    write_row(out, "dirty rect 260x72 only", time_frames(frames, [&] {
                canvas->save();
                canvas->clipRect(dirty, false);
                gallery::draw_frame(*canvas, resources, target, gallery::Selection{});
                canvas->restore();
              }));
  }
}

void run_windowed(dg::WindowManager& manager, dg::WindowId window,
                  const gallery::Resources& resources, int frames, std::ostream& out) {
  const dg::Expected<dg::PixelSize, dg::WindowError> size = manager.drawable_size(window);
  if (!size) {
    out << "cannot measure: " << size.error().message << "\n";
    return;
  }
  const dg::PixelSize pixels = size.value();

  std::optional<dg::RasterSurface> surface =
      dg::RasterSurface::create(pixels.width, pixels.height);
  if (!surface) {
    out << "cannot measure: no surface at " << pixels.width << "x" << pixels.height << "\n";
    return;
  }
  SkCanvas* canvas = surface->sk_canvas();
  const SkISize target = SkISize::Make(pixels.width, pixels.height);
  gallery::draw_frame(*canvas, resources, target, gallery::Selection{});

  const dg::PixelView view = surface->peek_pixels();
  const dg::ImageView image{view.pixels, view.width, view.height, view.row_bytes,
                            dg::PixelFormat::kBgra8888};
  const dg::PixelRect whole{0, 0, pixels.width, pixels.height};
  const dg::PixelRect dirty{pixels.width / 4,
                            static_cast<int>(static_cast<float>(pixels.height) * 0.4F),
                            kDirtyWidth, kDirtyHeight};

  out << "\nON SCREEN @ " << pixels.width << "x" << pixels.height
      << " - raster and presentation separated.\n";
  write_header(out, "stage");

  write_row(out, "raster: full frame", time_frames(frames, [&] {
              gallery::draw_frame(*canvas, resources, target, gallery::Selection{});
            }));
  write_row(out, "raster: dirty rect only", time_frames(frames, [&] {
              canvas->save();
              canvas->clipRect(
                  SkRect::MakeXYWH(static_cast<float>(dirty.x), static_cast<float>(dirty.y),
                                   kDirtyWidth, kDirtyHeight),
                  false);
              gallery::draw_frame(*canvas, resources, target, gallery::Selection{});
              canvas->restore();
            }));
  write_row(out, "present: whole window",
            time_frames(frames, [&] { (void)manager.present(window, image, whole); }));
  write_row(out, "present: dirty rect only",
            time_frames(frames, [&] { (void)manager.present(window, image, dirty); }));
}

bool stress_surfaces(const gallery::Resources& resources, int cycles, std::ostream& out) {
  // Deliberately awkward sizes: odd widths make row padding differ from
  // width * 4, which is where an assumption about stride would show up.
  constexpr Size kLadder[] = {
      {"a", 640, 401},   {"b", 1024, 641}, {"c", 1280, 800},
      {"d", 1921, 1081}, {"e", 333, 222},  {"f", 1600, 901},
  };

  for (int cycle = 0; cycle < cycles; ++cycle) {
    for (const Size& size : kLadder) {
      std::optional<dg::RasterSurface> surface =
          dg::RasterSurface::create(size.width, size.height);
      if (!surface) {
        out << "surface allocation failed at " << size.width << "x" << size.height << "\n";
        return false;
      }
      gallery::draw_frame(*surface->sk_canvas(), resources,
                          SkISize::Make(size.width, size.height), gallery::Selection{});
      const dg::PixelView view = surface->peek_pixels();
      if (view.pixels == nullptr || !view.is_bgra8888 ||
          view.row_bytes < static_cast<std::size_t>(size.width) * 4) {
        out << "surface at " << size.width << "x" << size.height << " reported bad pixels\n";
        return false;
      }
    }
  }
  out << "resized a surface " << (cycles * 6) << " times, drawing a full frame into each\n";
  return true;
}

}  // namespace bench
