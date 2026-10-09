// LOW-LEVEL TEACHING EXAMPLE. This opens an overlay popup through
// PopupHost directly and routes its dismissal (click outside, Escape) by
// hand. dg::App does that for you - Window::open_menu()/open_dialog() and
// Window::set_tooltip() open, route and close their own popups and report
// MenuEvent/DialogResultEvent. Start with doc/guide/app.md; read this to
// see what the runtime does underneath.
//
// The popup demo, on screen and headless.
//
//   (no arguments)       the demo window - click the "menu" button to open
//                        an overlay popup; click outside or press Escape to
//                        dismiss it
//   --run-ms N           close after N milliseconds
//   --verify-popup       the headless check. No display needed.
//   --dump-png FILE      rasterize one frame (host window only) and write it

#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "popup_check.h"
#include "popup_window.h"

namespace {

enum class Mode : std::uint8_t {
  kWindow,
  kVerify,
  kDumpPng,
};

struct Options {
  Mode mode = Mode::kWindow;
  popup_window::Settings settings;
  std::string png_path;
};

std::optional<Options> parse_options(int argc, char** argv) {
  Options options;
  const std::vector<std::string> args{argv + 1, argv + argc};
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "--verify-popup") {
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
  std::cerr << "usage: drawgui_popup [--run-ms N] [--dump-png FILE] [--verify-popup]\n";
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
      return popup_check::run(std::cout);
    case Mode::kDumpPng:
      return popup_window::dump_png(options->settings, options->png_path, std::cout);
    case Mode::kWindow:
      break;
  }
  return popup_window::run(options->settings, std::cout);
}
