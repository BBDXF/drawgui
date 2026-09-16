// The theme package demo, on screen and headless.
//
//   --package-dir DIR         load an external theme package (default:
//                             the fixture at fixtures/mytheme)
//   --run-ms N                close after N milliseconds
//   --verify-theme-package    the headless check. No display needed.
//   --dump-png FILE           rasterize one frame and write it out
//   --idle-probe-ms N         measure idle CPU with the package loaded

#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "theme_package_check.h"
#include "theme_package_window.h"

#ifndef DRAWGUI_THEME_PACKAGE_FIXTURE_DIR
#error "DRAWGUI_THEME_PACKAGE_FIXTURE_DIR must be provided by CMakeLists.txt"
#endif

namespace {

enum class Mode : std::uint8_t {
  kWindow,
  kVerify,
  kDumpPng,
  kIdleProbe,
};

struct Options {
  Mode mode = Mode::kWindow;
  theme_package_window::Settings settings;
  std::string png_path;
  int idle_probe_ms = 0;
};

std::optional<Options> parse_options(int argc, char** argv) {
  Options options;
  options.settings.package_dir = DRAWGUI_THEME_PACKAGE_FIXTURE_DIR;
  const std::vector<std::string> args{argv + 1, argv + argc};
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "--verify-theme-package") {
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
    if (args[i] == "--package-dir" && i + 1 < args.size()) {
      options.settings.package_dir = args[++i];
      continue;
    }
    if (args[i] == "--idle-probe-ms" && i + 1 < args.size()) {
      options.mode = Mode::kIdleProbe;
      options.idle_probe_ms = std::stoi(args[++i]);
      continue;
    }
    return std::nullopt;
  }
  return options;
}

void usage() {
  std::cerr
      << "usage: drawgui_theme_package [--package-dir DIR] [--run-ms N] [--dump-png FILE] "
         "[--idle-probe-ms N] [--verify-theme-package]\n";
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
      return theme_package_check::run(std::cout);
    case Mode::kDumpPng:
      return theme_package_window::dump_png(options->settings, options->png_path, std::cout);
    case Mode::kIdleProbe:
      return theme_package_window::idle_probe(options->settings, options->idle_probe_ms,
                                              std::cout);
    case Mode::kWindow:
      break;
  }
  return theme_package_window::run(options->settings, std::cout);
}
