// The data controls demo (ProgressBar, Tabs, TreeView, Table).
//
//   (no arguments)       the demo window: click/arrow keys/Tab, wheel to scroll,
//                        click a Name/Size header to sort
//   --run-ms N           close after N milliseconds
//   --font-dir DIR       scan a different font directory
//   --verify             the headless check. No display needed.
//   --dump-png FILE      rasterize one frame and write it

#include <chrono>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "data_scene.h"
#include "drawgui/anim/clock.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/window/window_manager.h"

namespace {

enum class Mode : std::uint8_t { kWindow, kVerify, kDumpPng };

struct Options {
  Mode mode = Mode::kWindow;
  std::string font_dir = "/usr/share/fonts";
  std::string png_path;
  int run_ms = 0;
};

std::optional<Options> parse(int argc, char** argv) {
  Options options;
  const std::vector<std::string> args{argv + 1, argv + argc};
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string& arg = args[i];
    const bool has_value = i + 1 < args.size();
    if (arg == "--verify") {
      options.mode = Mode::kVerify;
    } else if (arg == "--dump-png" && has_value) {
      options.mode = Mode::kDumpPng;
      options.png_path = args[++i];
    } else if (arg == "--font-dir" && has_value) {
      options.font_dir = args[++i];
    } else if (arg == "--run-ms" && has_value) {
      options.run_ms = std::stoi(args[++i]);
    } else {
      std::cerr << "unrecognized argument: " << arg << "\n";
      return std::nullopt;
    }
  }
  return options;
}

void present(dg::WindowManager& manager, dg::WindowId window, dg::RenderTree& tree,
             dg::RasterSurface& surface, bool full) {
  if (full) {
    tree.repaint_full(surface);
  } else if (tree.damage().is_empty()) {
    return;
  } else {
    tree.repaint(surface);
  }
  const dg::PixelView view = surface.peek_pixels();
  if (view.pixels == nullptr || !view.is_bgra8888) {
    return;
  }
  const dg::ImageView image{view.pixels, view.width, view.height, view.row_bytes,
                            dg::PixelFormat::kBgra8888};
  (void)manager.present(window, image, tree.painted().rects());
}

int run_window(const Options& options) {
  dg::Expected<dg::WindowManager, dg::WindowError> made = dg::WindowManager::create();
  if (!made) {
    std::cerr << "no window manager: " << made.error().message << "\n";
    return 1;
  }
  dg::WindowManager& manager = made.value();
  dg::WindowSpec spec;
  spec.title = "drawgui data widgets";
  spec.width = data_scene::kWidth;
  spec.height = data_scene::kHeight;
  const dg::Expected<dg::WindowId, dg::WindowError> opened = manager.open(spec);
  if (!opened) {
    std::cerr << "open() failed: " << opened.error().message << "\n";
    return 1;
  }
  const dg::WindowId window = opened.value();
  std::unique_ptr<data_scene::Scene> scene = data_scene::build(options.font_dir);
  std::optional<dg::RasterSurface> surface =
      dg::RasterSurface::create(data_scene::kWidth, data_scene::kHeight);
  if (!surface.has_value()) {
    return 1;
  }
  present(manager, window, scene->tree(), *surface, true);

  const auto started = std::chrono::steady_clock::now();
  while (manager.open_window_count() > 0) {
    const dg::PumpResult pumped = manager.pump(16);
    for (const dg::PointerEvent& event : pumped.pointer) {
      data_scene::handle_pointer(*scene, event);
    }
    for (const dg::KeyEvent& event : pumped.key) {
      data_scene::handle_key(*scene, event);
    }
    dg::ControlContext ctx = scene->ctx();
    scene->indeterminate.tick(ctx, dg::steady_anim_time());
    present(manager, window, scene->tree(), *surface, !pumped.needs_repaint.empty());
    if (!pumped.closed.empty()) {
      break;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    if (options.run_ms > 0 && elapsed.count() >= options.run_ms) {
      manager.request_close(window);
    }
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  const std::optional<Options> options = parse(argc, argv);
  if (!options.has_value()) {
    return 2;
  }
  switch (options->mode) {
    case Mode::kVerify:
      return data_scene::verify(options->font_dir, std::cout);
    case Mode::kDumpPng:
      return data_scene::dump_png(options->font_dir, options->png_path, std::cout);
    case Mode::kWindow:
      return run_window(*options);
  }
  return 1;
}
