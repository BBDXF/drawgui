// The menu/tooltip/dialog demo, on screen and headless.
//
//   (no arguments)              the demo window
//   --branch native|overlay     which branch context menu/tooltip/dialog use (default native)
//   --run-ms N                  close after N milliseconds
//   --font-dir DIR              scan a different font directory
//   --tooltip-delay-ms N        hover delay before a tooltip shows (default 400)
//   --verify-menus              the headless check. No display needed.
//   --idle-probe-ms N           measure idle CPU with the hover timer present but idle
//   --dump-png FILE             rasterize one frame and write it

#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "menu_check.h"
#include "menu_window.h"

namespace {

enum class Mode : std::uint8_t {
  kWindow,
  kVerify,
  kDumpPng,
  kIdleProbe,
};

struct Options {
  Mode mode = Mode::kWindow;
  menu_window::Settings settings;
  std::string png_path;
  int idle_probe_ms = 0;
};

// Whether `arg` was one of the recognised flags whose VALUE is another
// argument entirely (`--run-ms 300`, not `--verify-menus`) - factored out
// of parse_options() to keep its own cognitive complexity within this
// project's clang-tidy budget, the identical split 4-9's own
// dispatch_keyboard()/7-5's own focused_display_projection() already made.
bool parse_value_flag(const std::string& arg, const std::vector<std::string>& args,
                      std::size_t& i, Options& options) {
  if (arg == "--dump-png" && i + 1 < args.size()) {
    options.mode = Mode::kDumpPng;
    options.png_path = args[++i];
    return true;
  }
  if (arg == "--idle-probe-ms" && i + 1 < args.size()) {
    options.mode = Mode::kIdleProbe;
    options.idle_probe_ms = std::stoi(args[++i]);
    return true;
  }
  if (arg == "--run-ms" && i + 1 < args.size()) {
    options.settings.run_ms = std::stoi(args[++i]);
    return true;
  }
  if (arg == "--font-dir" && i + 1 < args.size()) {
    options.settings.font_dir = args[++i];
    return true;
  }
  if (arg == "--tooltip-delay-ms" && i + 1 < args.size()) {
    options.settings.tooltip_delay_ms = std::stoi(args[++i]);
    return true;
  }
  return false;
}

// std::nullopt means "--branch was present but named neither branch" - the
// caller's cue to abort rather than silently keeping the default.
std::optional<bool> parse_branch_value(const std::string& branch) {
  if (branch == "native") {
    return false;
  }
  if (branch == "overlay") {
    return true;
  }
  return std::nullopt;
}

std::optional<Options> parse_options(int argc, char** argv) {
  Options options;
  const std::vector<std::string> args{argv + 1, argv + argc};
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "--verify-menus") {
      options.mode = Mode::kVerify;
      continue;
    }
    if (parse_value_flag(args[i], args, i, options)) {
      continue;
    }
    if (args[i] == "--branch" && i + 1 < args.size()) {
      const std::optional<bool> force_overlay = parse_branch_value(args[++i]);
      if (!force_overlay.has_value()) {
        std::cerr << "unknown --branch value: " << args[i] << "\n";
        return std::nullopt;
      }
      options.settings.force_overlay = *force_overlay;
      continue;
    }
    std::cerr << "unrecognized argument: " << args[i] << "\n";
    return std::nullopt;
  }
  return options;
}

}  // namespace

int main(int argc, char** argv) {
  const std::optional<Options> options = parse_options(argc, argv);
  if (!options.has_value()) {
    return 2;
  }
  switch (options->mode) {
    case Mode::kVerify:
      return menu_check::run(std::cout);
    case Mode::kDumpPng:
      return menu_window::dump_png(options->settings, options->png_path, std::cout);
    case Mode::kIdleProbe:
      return menu_window::idle_probe(options->settings, options->idle_probe_ms, std::cout);
    case Mode::kWindow:
      return menu_window::run(options->settings, std::cout);
  }
  return 1;
}
