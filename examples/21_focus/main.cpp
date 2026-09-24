// The focus demo, on screen and headless.
//
//   (no arguments)       the demo window - Tab/Shift-Tab moves focus, click
//                        a widget to focus it, click "open popup" for a
//                        popup crossing Tab into it
//   --branch native|overlay   which branch "open popup" opens (default native)
//   --run-ms N           close after N milliseconds
//   --font-dir DIR       scan a different font directory for the text field
//   --verify-focus       the headless check. No display needed.
//   --dump-png FILE      rasterize one frame (first tab stop focused) and write it

#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "focus_check.h"
#include "focus_window.h"

namespace {

enum class Mode : std::uint8_t {
  kWindow,
  kVerify,
  kDumpPng,
};

struct Options {
  Mode mode = Mode::kWindow;
  focus_window::Settings settings;
  std::string png_path;
  std::string branch;  // empty means not passed
};

std::optional<Options> parse_options(int argc, char** argv) {
  Options options;
  const std::vector<std::string> args{argv + 1, argv + argc};
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "--verify-focus") {
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
      options.branch = args[++i];
      continue;
    }
    return std::nullopt;
  }
  return options;
}

void usage() {
  std::cerr << "usage: drawgui_focus [--branch overlay] [--run-ms N] "
               "[--font-dir DIR] [--dump-png FILE] [--verify-focus]\n";
}

}  // namespace

int main(int argc, char** argv) {
  const std::optional<Options> options = parse_options(argc, argv);
  if (!options.has_value()) {
    usage();
    return 2;
  }
  if (!options->branch.empty() && options->branch != "overlay") {
    std::cerr << "--branch native is no longer supported: native popup windows were removed; "
                 "popups are overlay-only now.\n";
    return 2;
  }

  switch (options->mode) {
    case Mode::kVerify:
      return focus_check::run(std::cout);
    case Mode::kDumpPng:
      return focus_window::dump_png(options->settings, options->png_path, std::cout);
    case Mode::kWindow:
      break;
  }
  return focus_window::run(options->settings, std::cout);
}
