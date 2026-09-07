// Clipping, on screen.
//
//   (no arguments)       the demo window - resize it, and hover the panels
//   --size WxH           open at a given size
//   --toggle-ms N        flip every panel between clip and visible every N ms,
//                        so the difference is visible without touching anything
//   --run-ms N           close after N milliseconds
//   --probe X,Y          print what hit testing answers at one point and exit,
//                        after laying the scene out at --size
//   --verify-clipping    the headless check: every pixel, every width, against
//                        two oracles. No display needed.
//   --dump-png FILE      rasterize one frame and write it out
//
// The five panels are the five shapes clipping branches on, and the two on the
// left differ only in `overflow` - same children, same sizes - so what is
// between them on screen is the property and nothing else.

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "drawgui/base/pixel_geometry.h"

#include "clip_check.h"
#include "clip_window.h"

namespace {

enum class Mode : std::uint8_t {
  kWindow,
  kVerify,
  kDumpPng,
  kProbe,
};

struct Options {
  Mode mode = Mode::kWindow;
  clip_window::Settings settings;
  std::string png_path;
  dg::PixelPoint probe;
};

std::optional<int> parse_int(const std::string& text, int minimum) {
  int value = 0;
  const char* end = text.data() + text.size();
  const std::from_chars_result parsed = std::from_chars(text.data(), end, value);
  if (parsed.ec != std::errc{} || parsed.ptr != end || value < minimum) {
    return std::nullopt;
  }
  return value;
}

bool parse_pair(const std::string& text, char separator, int minimum, int& first, int& second) {
  const std::size_t at = text.find(separator);
  if (at == std::string::npos) {
    return false;
  }
  const std::optional<int> left = parse_int(text.substr(0, at), minimum);
  const std::optional<int> right = parse_int(text.substr(at + 1), minimum);
  if (!left.has_value() || !right.has_value()) {
    return false;
  }
  first = *left;
  second = *right;
  return true;
}

bool apply_valued(const std::string& flag, const std::string& value, Options& options) {
  if (flag == "--size") {
    return parse_pair(value, 'x', 1, options.settings.size.width, options.settings.size.height);
  }
  if (flag == "--probe") {
    options.mode = Mode::kProbe;
    return parse_pair(value, ',', 0, options.probe.x, options.probe.y);
  }
  if (flag == "--dump-png") {
    options.mode = Mode::kDumpPng;
    options.png_path = value;
    return true;
  }
  const std::optional<int> number = parse_int(value, 0);
  if (!number.has_value()) {
    return false;
  }
  if (flag == "--toggle-ms") {
    options.settings.toggle_ms = *number;
    return true;
  }
  if (flag == "--run-ms") {
    options.settings.run_ms = *number;
    return true;
  }
  return false;
}

std::optional<Options> parse_options(int argc, char** argv) {
  Options options;
  const std::vector<std::string> args{argv + 1, argv + argc};
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "--verify-clipping") {
      options.mode = Mode::kVerify;
      continue;
    }
    if (i + 1 >= args.size() || !apply_valued(args[i], args[i + 1], options)) {
      return std::nullopt;
    }
    ++i;
  }
  return options;
}

void usage() {
  std::cerr << "usage: drawgui_clipping [--size WxH] [--toggle-ms N] [--run-ms N]\n"
            << "                        [--probe X,Y] [--dump-png FILE]\n"
            << "                        [--verify-clipping]\n";
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
      return clip_check::run(std::cout);
    case Mode::kDumpPng:
      return clip_window::dump_png(options->settings, options->png_path, std::cout);
    case Mode::kProbe:
      return clip_window::probe(options->settings, options->probe, std::cout);
    case Mode::kWindow:
      break;
  }
  return clip_window::run(options->settings, std::cout);
}
