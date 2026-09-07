// Font fallback: one named family, every script, and the language tag that
// picks between Han faces.
//
//   (no arguments)      the demo window
//   --verify-fallback   every codepoint in the scene checked for a real glyph,
//                       and the two Han panels compared. No display needed.
//   --dump-png PATH     render the scene offscreen and write it, for evidence
//   --size WxH          viewport size
//   --font-dir DIR      the directory to scan
//   --run-ms N          close the window after N milliseconds
//
// Build Release before quoting any number.

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/window/window_manager.h"

#include "font_check.h"
#include "font_scene.h"

namespace {

enum class Mode : std::uint8_t {
  kWindow,
  kVerify,
  kDumpPng,
};

struct Options {
  Mode mode = Mode::kWindow;
  font_scene::Options scene;
  std::string png_path;
  int run_ms = 0;
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
  options.scene.viewport = dg::PixelSize{*width, *height};
  return true;
}

bool apply_valued(const std::string& flag, const std::string& value, Options& options) {
  if (flag == "--size") {
    return parse_size(value, options);
  }
  if (flag == "--font-dir") {
    options.scene.font_dir = value;
    return true;
  }
  if (flag == "--family") {
    options.scene.primary_family = value;
    return true;
  }
  if (flag == "--dump-png") {
    options.mode = Mode::kDumpPng;
    options.png_path = value;
    return true;
  }
  if (flag == "--run-ms") {
    const std::optional<int> number = parse_positive_int(value);
    if (!number.has_value()) {
      return false;
    }
    options.run_ms = *number;
    return true;
  }
  return false;
}

std::optional<Options> parse_options(int argc, char** argv) {
  Options options;
  const std::vector<std::string> args{argv + 1, argv + argc};
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "--verify-fallback") {
      options.mode = Mode::kVerify;
      continue;
    }
    if (i + 1 >= args.size() || !apply_valued(args[i], args[i + 1], options)) {
      std::cerr << "unrecognized option: " << args[i] << "\n";
      return std::nullopt;
    }
    ++i;
  }
  return options;
}

int write_png(const dg::RasterSurface& surface, const std::string& path) {
  const std::vector<std::uint8_t> png = surface.encode_png();
  if (png.empty()) {
    std::cerr << "failed to encode PNG\n";
    return 2;
  }
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(png.data()),
            static_cast<std::streamsize>(png.size()));
  if (!out) {
    std::cerr << "failed to write " << path << "\n";
    return 3;
  }
  std::cout << "wrote " << path << " (" << png.size() << " bytes)\n";
  return 0;
}

int render_offscreen(font_scene::Scene& scene, const std::string& path) {
  std::optional<dg::RasterSurface> surface =
      dg::RasterSurface::create(scene.tree.viewport().width, scene.tree.viewport().height);
  if (!surface) {
    std::cerr << "failed to create a raster surface\n";
    return 1;
  }
  scene.tree.repaint_full(*surface);
  return write_png(*surface, path);
}

int run_window(font_scene::Scene& scene, const Options& options) {
  dg::Expected<dg::WindowManager, dg::WindowError> manager = dg::WindowManager::create();
  if (!manager) {
    std::cerr << "window manager: " << manager.error().message << "\n";
    return 1;
  }
  dg::WindowSpec spec;
  spec.title = "drawgui font fallback";
  spec.width = scene.tree.viewport().width;
  spec.height = scene.tree.viewport().height;
  const dg::Expected<dg::WindowId, dg::WindowError> window = manager.value().open(spec);
  if (!window) {
    std::cerr << "open: " << window.error().message << "\n";
    return 1;
  }

  std::optional<dg::RasterSurface> surface =
      dg::RasterSurface::create(scene.tree.viewport().width, scene.tree.viewport().height);
  if (!surface) {
    std::cerr << "failed to create a raster surface\n";
    return 1;
  }

  int elapsed = 0;
  bool first = true;
  while (manager.value().open_window_count() > 0) {
    const dg::PumpResult pumped = manager.value().pump(16);
    if (first || !pumped.needs_repaint.empty()) {
      const dg::Expected<dg::PixelSize, dg::WindowError> size =
          manager.value().drawable_size(window.value());
      if (size && (size.value().width != scene.tree.viewport().width ||
                   size.value().height != scene.tree.viewport().height)) {
        scene.tree.resize(size.value());
        surface = dg::RasterSurface::create(size.value().width, size.value().height);
        if (!surface) {
          return 1;
        }
      }
      scene.tree.repaint_full(*surface);
      const dg::PixelView view = surface->peek_pixels();
      const dg::ImageView image{view.pixels, view.width, view.height, view.row_bytes,
                                dg::PixelFormat::kBgra8888};
      const dg::Expected<void, dg::WindowError> presented = manager.value().present(
          window.value(), image, dg::PixelRect{0, 0, view.width, view.height});
      if (!presented) {
        std::cerr << "present: " << presented.error().message << "\n";
        return 1;
      }
      first = false;
    }
    if (options.run_ms > 0) {
      elapsed += 16;
      if (elapsed >= options.run_ms) {
        manager.value().request_close(window.value());
      }
    }
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  const std::optional<Options> options = parse_options(argc, argv);
  if (!options.has_value()) {
    return 2;
  }

  dg::Expected<font_scene::Scene, dg::FontError> built = font_scene::build(options->scene);
  if (!built) {
    std::cerr << "scene: " << built.error().message << "\n";
    return 1;
  }
  font_scene::Scene scene = std::move(built).value();

  switch (options->mode) {
    case Mode::kVerify: {
      const font_check::Result result = font_check::verify(scene);
      font_check::print(scene, result, std::cout);
      return result.passed ? 0 : 1;
    }
    case Mode::kDumpPng:
      return render_offscreen(scene, options->png_path);
    case Mode::kWindow:
      break;
  }
  return run_window(scene, *options);
}
