// The dropdown demo, on screen and headless.
//
//   (no arguments)            the demo window
//   --branch native|overlay   which branch the dropdown opens (default native)
//   --run-ms N                close after N milliseconds
//   --font-dir DIR            scan a different font directory
//   --verify-dropdown         the headless check. No display needed.
//   --dump-png FILE           rasterize one frame (third option selected) and write it

#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "dropdown_check.h"
#include "dropdown_window.h"

namespace {

enum class Mode : std::uint8_t {
  kWindow,
  kVerify,
  kDumpPng,
};

struct Options {
  Mode mode = Mode::kWindow;
  dropdown_window::Settings settings;
  std::string png_path;
};

std::optional<Options> parse_options(int argc, char** argv) {
  Options options;
  const std::vector<std::string> args{argv + 1, argv + argc};
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "--verify-dropdown") {
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
    if (args[i] == "--font-dir" && i + 1 < args.size()) {
      options.settings.font_dir = args[++i];
      continue;
    }
    if (args[i] == "--branch" && i + 1 < args.size()) {
      const std::string& branch = args[++i];
      if (branch == "native") {
        options.settings.force_overlay = false;
      } else if (branch == "overlay") {
        options.settings.force_overlay = true;
      } else {
        return std::nullopt;
      }
      continue;
    }
    return std::nullopt;
  }
  return options;
}

void usage() {
  std::cerr << "usage: drawgui_dropdown_menu [--branch native|overlay] [--run-ms N] "
               "[--font-dir DIR] [--dump-png FILE] [--verify-dropdown]\n";
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
      return dropdown_check::run(std::cout);
    case Mode::kDumpPng:
      return dropdown_window::dump_png(options->settings, options->png_path, std::cout);
    case Mode::kWindow:
      break;
  }
  return dropdown_window::run(options->settings, std::cout);
}
