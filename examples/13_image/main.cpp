// The image demo, on screen and headless.
//
//   (no arguments)     the demo window - resize it
//   --run-ms N         close after N milliseconds
//   --verify-image     the headless check. No display needed.
//   --dump-png FILE    rasterize one frame and write it out

#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "image_check.h"
#include "image_window.h"

namespace {

enum class Mode : std::uint8_t {
  kWindow,
  kVerify,
  kDumpPng,
};

struct Options {
  Mode mode = Mode::kWindow;
  image_window::Settings settings;
  std::string png_path;
};

std::optional<Options> parse_options(int argc, char** argv) {
  Options options;
  const std::vector<std::string> args{argv + 1, argv + argc};
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "--verify-image") {
      options.mode = Mode::kVerify;
      continue;
    }
    if (args[i] == "--dump-png" && i + 1 < args.size()) {
      options.mode = Mode::kDumpPng;
      options.png_path = args[++i];
      continue;
    }
    if (args[i] == "--run-ms" && i + 1 < args.size()) {
      options.settings.run_ms = std::stoi(args[++i]);
      continue;
    }
    return std::nullopt;
  }
  return options;
}

void usage() {
  std::cerr << "usage: drawgui_image [--run-ms N] [--dump-png FILE] [--verify-image]\n";
}

}  // namespace

int main(int argc, char** argv) {
  const std::optional<Options> options = parse_options(argc, argv);
  if (!options.has_value()) {
    usage();
    return 2;
  }

  switch (options->mode) {
    case Mode::kVerify:
      return image_check::run(std::cout);
    case Mode::kDumpPng:
      return image_window::dump_png(options->settings, options->png_path, std::cout);
    case Mode::kWindow:
      break;
  }
  return image_window::run(options->settings, std::cout);
}
