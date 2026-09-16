// Text input and editing, on screen.
//
//   (no arguments)         the demo window - click a field, type, select
//   --size WxH             open at a given size
//   --run-ms N             close after N milliseconds
//   --preset-field-b TEXT  pre-fill field b (offscreen modes only)
//   --preset-focus-a       open with field a focused
//   --probe X,Y            print the pixel AND the hit at one point, then exit
//   --verify-text-input    the headless check. No display needed.
//   --dump-png FILE        rasterize one frame and write it out
//   --script               opens a real window and drives a real click, a
//                          real committed-text event and a real key event
//                          through the platform's own queue - manual
//                          verification, not a CTest entry

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "drawgui/base/pixel_geometry.h"

#include "text_field_check.h"
#include "text_field_window.h"

namespace {

enum class Mode : std::uint8_t {
  kWindow,
  kVerify,
  kDumpPng,
  kProbe,
  kScript,
};

struct Options {
  Mode mode = Mode::kWindow;
  text_field_window::Settings settings;
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
    return parse_pair(value, ',', -100000, options.probe.x, options.probe.y);
  }
  if (flag == "--dump-png") {
    options.mode = Mode::kDumpPng;
    options.png_path = value;
    return true;
  }
  if (flag == "--run-ms") {
    const std::optional<int> number = parse_int(value, 0);
    if (!number.has_value()) {
      return false;
    }
    options.settings.run_ms = *number;
    return true;
  }
  if (flag == "--preset-field-b") {
    options.settings.preset_field_b_text = value;
    return true;
  }
  return false;
}

std::optional<Options> parse_options(int argc, char** argv) {
  Options options;
  const std::vector<std::string> args{argv + 1, argv + argc};
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "--verify-text-input") {
      options.mode = Mode::kVerify;
      continue;
    }
    if (args[i] == "--script") {
      options.mode = Mode::kScript;
      continue;
    }
    if (args[i] == "--preset-focus-a") {
      options.settings.preset_focus_field_a = true;
      continue;
    }
    if (args[i] == "--preset-select-a") {
      options.settings.preset_select_field_a = true;
      continue;
    }
    if (args[i] == "--preset-compose-b") {
      options.settings.preset_compose_field_b = true;
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
  std::cerr << "usage: drawgui_text_input [--size WxH] [--run-ms N]\n"
            << "                          [--preset-field-b TEXT] [--preset-focus-a]\n"
            << "                          [--preset-select-a] [--preset-compose-b]\n"
            << "                          [--probe X,Y] [--dump-png FILE]\n"
            << "                          [--verify-text-input] [--script]\n";
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
      return text_field_check::run(std::cout);
    case Mode::kDumpPng:
      return text_field_window::dump_png(options->settings, options->png_path, std::cout);
    case Mode::kProbe:
      return text_field_window::probe(options->settings, options->probe, std::cout);
    case Mode::kScript:
      return text_field_window::script(options->settings, std::cout);
    case Mode::kWindow:
      break;
  }
  return text_field_window::run(options->settings, std::cout);
}
