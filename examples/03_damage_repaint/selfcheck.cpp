#include "selfcheck.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"

#include "scene.h"

namespace selfcheck {
namespace {

using Clock = std::chrono::steady_clock;

constexpr int kWarmupFrames = 12;

struct Ladder {
  const char* label;
  int width;
  int height;
};

constexpr Ladder kSizes[] = {
    {"800x600", 800, 600},
    {"1280x720", 1280, 720},
    {"1920x1080", 1920, 1080},
    {"2560x1440", 2560, 1440},
};

dg::TreeSpec spec_from(const Config& config) {
  dg::TreeSpec spec;
  spec.viewport = config.viewport;
  spec.background.fill = dg::Color::from_argb(0xFF14171C);
  spec.max_damage_rects = config.max_damage_rects;
  spec.paint_mode = config.paint_mode;
  return spec;
}

std::vector<std::uint8_t> snapshot(const dg::RasterSurface& surface) {
  const dg::PixelView view = surface.peek_pixels();
  const auto row = static_cast<std::size_t>(view.width) * 4;
  std::vector<std::uint8_t> pixels(row * static_cast<std::size_t>(view.height));
  for (int y = 0; y < view.height; ++y) {
    std::memcpy(pixels.data() + (static_cast<std::size_t>(y) * row),
                view.pixels + (static_cast<std::size_t>(y) * view.row_bytes), row);
  }
  return pixels;
}

void report_first_difference(const std::vector<std::uint8_t>& damaged,
                             const std::vector<std::uint8_t>& reference, int width,
                             std::ostream& out) {
  std::size_t differing = 0;
  std::size_t first = 0;
  for (std::size_t i = 0; i < damaged.size(); i += 4) {
    if (std::memcmp(&damaged[i], &reference[i], 4) != 0) {
      if (differing == 0) {
        first = i / 4;
      }
      ++differing;
    }
  }
  out << "    " << differing << " pixel(s) differ; first at "
      << (first % static_cast<std::size_t>(width)) << ","
      << (first / static_cast<std::size_t>(width)) << "\n";
}

struct Stats {
  double median_ms = 0.0;
  double p95_ms = 0.0;
  double worst_ms = 0.0;
};

Stats summarize(std::vector<double> samples) {
  Stats stats;
  if (samples.empty()) {
    return stats;
  }
  std::sort(samples.begin(), samples.end());
  stats.median_ms = samples[samples.size() / 2];
  stats.p95_ms =
      samples[static_cast<std::size_t>(0.95 * static_cast<double>(samples.size() - 1))];
  stats.worst_ms = samples.back();
  return stats;
}

template <typename Body>
Stats time_frames(int frames, const Body& body) {
  for (int i = 0; i < kWarmupFrames; ++i) {
    body(i);
  }
  std::vector<double> samples;
  samples.reserve(static_cast<std::size_t>(frames));
  for (int i = 0; i < frames; ++i) {
    const Clock::time_point started = Clock::now();
    body(kWarmupFrames + i);
    samples.push_back(
        std::chrono::duration<double, std::milli>(Clock::now() - started).count());
  }
  return summarize(std::move(samples));
}

void write_row(std::ostream& out, const std::string& label, const Stats& stats,
               std::int64_t pixels) {
  out << "  " << std::left << std::setw(30) << label << std::right << std::fixed
      << std::setprecision(3) << std::setw(10) << stats.median_ms << std::setw(10)
      << stats.p95_ms << std::setw(10) << stats.worst_ms << std::setw(14) << pixels << "\n";
}

struct Row {
  const char* label;
  bool damage_driven;
  dg::PaintMode paint_mode;
  std::size_t cap;
};

constexpr Row kRows[] = {
    {"full repaint, direct", false, dg::PaintMode::kDirect, 8},
    {"full repaint, picture", false, dg::PaintMode::kPicture, 8},
    {"damage, direct, 8 rects", true, dg::PaintMode::kDirect, 8},
    {"damage, picture, 8 rects", true, dg::PaintMode::kPicture, 8},
    {"damage, direct, 1 rect", true, dg::PaintMode::kDirect, 1},
};

}  // namespace

bool verify_damage(const Config& config, std::ostream& out) {
  const dg::TreeSpec spec = spec_from(config);
  std::optional<dg::RasterSurface> damaged_surface =
      dg::RasterSurface::create(spec.viewport.width, spec.viewport.height);
  std::optional<dg::RasterSurface> reference_surface =
      dg::RasterSurface::create(spec.viewport.width, spec.viewport.height);
  if (!damaged_surface.has_value() || !reference_surface.has_value()) {
    out << "verify: could not allocate a " << spec.viewport.width << "x" << spec.viewport.height
        << " surface\n";
    return false;
  }

  scene::Scene damaged = scene::build(spec);
  scene::Scene reference = scene::build(spec);

  out << "verify: " << config.frames << " frames at " << spec.viewport.width << "x"
      << spec.viewport.height << ", cap " << config.max_damage_rects << " rect(s), "
      << (config.paint_mode == dg::PaintMode::kPicture ? "picture" : "direct") << " painting\n";

  for (int frame = 0; frame < config.frames; ++frame) {
    scene::apply_frame(damaged.tree, damaged.handles, frame);
    scene::apply_frame(reference.tree, reference.handles, frame);
    damaged.tree.repaint(*damaged_surface);
    reference.tree.repaint_full(*reference_surface);

    const std::vector<std::uint8_t> left = snapshot(*damaged_surface);
    const std::vector<std::uint8_t> right = snapshot(*reference_surface);
    if (left != right) {
      out << "  FAIL at frame " << frame << "\n";
      report_first_difference(left, right, spec.viewport.width, out);
      return false;
    }
  }

  out << "  OK: every frame identical byte for byte to a full repaint\n";
  return true;
}

void bench(int frames, std::ostream& out) {
  out << "\nOFFSCREEN RASTER - render tree, no window, no presentation.\n"
      << "  " << frames << " timed frames per row after " << kWarmupFrames
      << " warmup frames.\n";

  for (const Ladder& size : kSizes) {
    out << "\n  " << std::left << std::setw(30) << size.label << std::right << std::setw(10)
        << "median" << std::setw(10) << "p95" << std::setw(10) << "worst" << std::setw(14)
        << "px/frame" << "\n  " << std::string(74, '-') << "\n";

    for (const Row& row : kRows) {
      Config config;
      config.viewport = dg::PixelSize{size.width, size.height};
      config.max_damage_rects = row.cap;
      config.paint_mode = row.paint_mode;

      std::optional<dg::RasterSurface> surface =
          dg::RasterSurface::create(size.width, size.height);
      if (!surface.has_value()) {
        out << "  " << row.label << ": could not allocate a surface\n";
        continue;
      }
      scene::Scene built = scene::build(spec_from(config));
      std::int64_t pixels = 0;

      const Stats stats = time_frames(frames, [&](int frame) {
        scene::apply_frame(built.tree, built.handles, frame);
        const dg::RepaintStats painted = row.damage_driven ? built.tree.repaint(*surface)
                                                           : built.tree.repaint_full(*surface);
        pixels = painted.pixels;
      });
      write_row(out, row.label, stats, pixels);
    }
  }
}

}  // namespace selfcheck
