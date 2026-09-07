// Skia on the CPU, in a real window, with the numbers that say whether that
// is good enough.
//
// This is step 2 of the restarted plan: decide how Skia attaches to the
// window layer, and do it the simplest way that can be measured. Simplest
// means CPU raster into a plain buffer and a copy onto the window surface.
// There is no GL context in this process at all - `ldd` shows no libGL - so
// the numbers below are the rasterizer's, not a driver's.
//
//   (no arguments)      the gallery in a window; resize it, it re-renders
//   --bench             offscreen raster timings at four resolutions
//   --bench-present     raster time and presentation time, separated
//   --dump-png PATH     one deterministic frame, offscreen, as a PNG
//   --stress-surfaces N recreate the surface at many sizes (run under ASan)
//
// Build Release before quoting any timing. A Debug number would be an
// unoptimized rasterizer's and would misrepresent the decision it informs.

#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "drawgui/graphics/raster_surface.h"
#include "drawgui/graphics/types.h"
#include "drawgui/window/window_manager.h"

#include "include/core/SkSize.h"

#include "bench.h"
#include "gallery.h"
#include "panel_support.h"

namespace {

constexpr int kPumpTimeoutMs = 16;

enum class Mode : std::uint8_t {
  kInteractive,
  kBench,
  kBenchPresent,
  kDumpPng,
  kStressSurfaces,
};

struct Options {
  Mode mode = Mode::kInteractive;
  int width = 1280;
  int height = 840;
  int frames = 200;
  int cycles = 20;
  int run_ms = 0;
  std::string font_dir = "/usr/share/fonts";
  std::string png_path;
};

std::optional<int> parse_positive_int(const std::string& text) {
  int value = 0;
  const char* end = text.data() + text.size();
  const std::from_chars_result parsed = std::from_chars(text.data(), end, value);
  if (parsed.ec != std::errc{} || parsed.ptr != end || value <= 0) {
    return std::nullopt;
  }
  return value;
}

bool parse_size(const std::string& text, Options& options) {
  const std::size_t separator = text.find('x');
  if (separator == std::string::npos) {
    return false;
  }
  const std::optional<int> width = parse_positive_int(text.substr(0, separator));
  const std::optional<int> height = parse_positive_int(text.substr(separator + 1));
  if (!width.has_value() || !height.has_value()) {
    return false;
  }
  options.width = *width;
  options.height = *height;
  return true;
}

bool apply_switch(const std::string& flag, Options& options) {
  if (flag == "--bench") {
    options.mode = Mode::kBench;
    return true;
  }
  if (flag == "--bench-present") {
    options.mode = Mode::kBenchPresent;
    return true;
  }
  return false;
}

bool apply_valued(const std::string& flag, const std::string& value, Options& options) {
  if (flag == "--dump-png") {
    options.mode = Mode::kDumpPng;
    options.png_path = value;
    return true;
  }
  if (flag == "--font-dir") {
    options.font_dir = value;
    return true;
  }
  if (flag == "--size") {
    return parse_size(value, options);
  }

  const std::optional<int> number = parse_positive_int(value);
  if (!number.has_value()) {
    return false;
  }
  if (flag == "--stress-surfaces") {
    options.mode = Mode::kStressSurfaces;
    options.cycles = *number;
    return true;
  }
  if (flag == "--frames") {
    options.frames = *number;
    return true;
  }
  if (flag == "--run-ms") {
    options.run_ms = *number;
    return true;
  }
  return false;
}

std::optional<Options> parse_options(int argc, char** argv) {
  Options options;
  const std::vector<std::string> args{argv + 1, argv + argc};

  for (std::size_t i = 0; i < args.size(); ++i) {
    if (apply_switch(args[i], options)) {
      continue;
    }
    if (i + 1 >= args.size()) {
      return std::nullopt;
    }
    const std::string& flag = args[i];
    if (!apply_valued(flag, args[++i], options)) {
      return std::nullopt;
    }
  }
  return options;
}

void usage() {
  std::cerr << "usage: drawgui_skia_cpu_gallery [options]\n"
            << "  --bench                 offscreen raster timings at four resolutions\n"
            << "  --bench-present         raster time and presentation time, separated\n"
            << "  --dump-png PATH         write one deterministic frame and exit\n"
            << "  --stress-surfaces N     recreate the surface at many sizes, N times\n"
            << "  --size WxH              window / render size (default 1280x840)\n"
            << "  --frames N              timed frames per benchmark row (default 200)\n"
            << "  --run-ms N              close the window after N ms\n"
            << "  --font-dir DIR          where to scan for fonts (default /usr/share/fonts)\n";
}

// Redraws into a surface that always matches the window, recreating it when
// the window has changed size. Keeping a stale surface and blitting it is the
// classic resize bug; recreating it every frame instead is the classic resize
// leak.
class WindowPainter {
 public:
  WindowPainter(dg::WindowManager& manager, dg::WindowId window,
                const gallery::Resources& resources)
      : manager_(&manager), window_(window), resources_(&resources) {}

  [[nodiscard]] bool paint() {
    const dg::Expected<dg::PixelSize, dg::WindowError> size = manager_->drawable_size(window_);
    if (!size) {
      std::cerr << "drawable_size: " << size.error().message << "\n";
      return false;
    }
    const dg::PixelSize pixels = size.value();
    if (pixels.width <= 0 || pixels.height <= 0) {
      return true;
    }

    if (!surface_.has_value() || surface_->width() != pixels.width ||
        surface_->height() != pixels.height) {
      surface_ = dg::RasterSurface::create(pixels.width, pixels.height);
      if (!surface_.has_value()) {
        std::cerr << "could not allocate a " << pixels.width << "x" << pixels.height
                  << " surface\n";
        return false;
      }
      std::cout << "surface now " << pixels.width << "x" << pixels.height << "\n";
    }

    gallery::draw_frame(*surface_->sk_canvas(), *resources_,
                        SkISize::Make(pixels.width, pixels.height), gallery::Selection{});

    const dg::PixelView view = surface_->peek_pixels();
    if (view.pixels == nullptr || !view.is_bgra8888) {
      std::cerr << "the raster surface is not in the format the window expects\n";
      return false;
    }

    const dg::Expected<void, dg::WindowError> presented =
        manager_->present(window_,
                          dg::ImageView{view.pixels, view.width, view.height, view.row_bytes,
                                        dg::PixelFormat::kBgra8888},
                          dg::PixelRect{0, 0, view.width, view.height});
    if (!presented) {
      std::cerr << "present: " << presented.error().message << "\n";
      return false;
    }
    return true;
  }

 private:
  dg::WindowManager* manager_;
  dg::WindowId window_;
  const gallery::Resources* resources_;
  std::optional<dg::RasterSurface> surface_;
};

int run_offscreen_render(const gallery::Resources& resources, const Options& options) {
  std::optional<dg::RasterSurface> surface =
      dg::RasterSurface::create(options.width, options.height);
  if (!surface) {
    std::cerr << "could not allocate a raster surface\n";
    return 1;
  }
  gallery::draw_frame(*surface->sk_canvas(), resources,
                      SkISize::Make(options.width, options.height), gallery::Selection{});

  const std::vector<std::uint8_t> png = surface->encode_png();
  if (png.empty()) {
    std::cerr << "PNG encoding failed\n";
    return 1;
  }
  std::ofstream out(options.png_path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(png.data()),
            static_cast<std::streamsize>(png.size()));
  if (!out) {
    std::cerr << "could not write " << options.png_path << "\n";
    return 1;
  }
  std::cout << "wrote " << options.png_path << " (" << png.size() << " bytes, " << options.width
            << "x" << options.height << ")\n";
  return 0;
}

int run_window(const gallery::Resources& resources, const Options& options) {
  dg::Expected<dg::WindowManager, dg::WindowError> created = dg::WindowManager::create();
  if (!created) {
    std::cerr << "cannot start the window manager: " << created.error().message << "\n";
    return 1;
  }
  dg::WindowManager manager = std::move(created).value();

  dg::Expected<dg::WindowId, dg::WindowError> opened =
      manager.open(dg::WindowSpec{"drawgui - Skia CPU gallery", options.width, options.height,
                                  dg::Color::from_argb(gallery::kBackground)});
  if (!opened) {
    std::cerr << "cannot open a window: " << opened.error().message << "\n";
    return 1;
  }
  const dg::WindowId window = opened.value();

  // Said out loud rather than assumed. A blue/red swap is almost invisible on
  // a dark grey gallery, so the format is reported at startup and confirmed
  // against a screenshot afterwards.
  const dg::Expected<dg::PixelFormat, dg::WindowError> format = manager.surface_format(window);
  if (!format) {
    std::cerr << "unusable window surface: " << format.error().message << "\n";
    return 1;
  }
  std::cout << "window surface format: BGRA8888 (blue in the lowest-addressed byte)\n";

  WindowPainter painter{manager, window, resources};
  if (!painter.paint()) {
    return 1;
  }

  if (options.mode == Mode::kBenchPresent) {
    bench::run_windowed(manager, window, resources, options.frames, std::cout);
    return 0;
  }

  const auto started = std::chrono::steady_clock::now();
  bool asked_to_close = false;

  while (manager.open_window_count() > 0) {
    const dg::PumpResult pumped = manager.pump(kPumpTimeoutMs);
    if (!pumped.needs_repaint.empty() && !painter.paint()) {
      return 1;
    }
    if (options.run_ms > 0 && !asked_to_close) {
      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - started);
      if (elapsed.count() >= options.run_ms) {
        manager.request_close(window);
        asked_to_close = true;
      }
    }
  }

  std::cout << "window closed; exiting cleanly\n";
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  const std::optional<Options> parsed = parse_options(argc, argv);
  if (!parsed.has_value()) {
    usage();
    return 2;
  }
  const Options& options = *parsed;

  const gallery::Resources resources = gallery::load_resources(options.font_dir);
  for (const std::string& note : resources.notes) {
    std::cout << "  " << note << "\n";
  }
  if (!resources.complete()) {
    std::cout << "  (some resources are missing; the gallery draws what it can)\n";
  }

  switch (options.mode) {
    case Mode::kDumpPng:
      return run_offscreen_render(resources, options);
    case Mode::kBench:
      bench::run_offscreen(resources, options.frames, std::cout);
      return 0;
    case Mode::kStressSurfaces:
      return bench::stress_surfaces(resources, options.cycles, std::cout) ? 0 : 1;
    case Mode::kInteractive:
    case Mode::kBenchPresent:
      return run_window(resources, options);
  }
  return 2;
}
