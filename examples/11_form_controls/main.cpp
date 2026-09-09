// Form controls, on screen.
//
//   (no arguments)              the demo window - click the checkbox/radios,
//                               drag either slider's track
//   --size WxH                  open at a given size
//   --run-ms N                  close after N milliseconds
//   --preset-radio-a N          pre-select radio group A's option N
//                               (offscreen modes only)
//   --preset-volume V           pre-set the volume slider's value
//   --probe X,Y                 print the pixel AND the hit at one point,
//                               then exit
//   --verify-form-controls      the headless check. No display needed.
//   --dump-png FILE             rasterize one frame and write it out

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "drawgui/base/pixel_geometry.h"

#include "form_check.h"
#include "form_window.h"

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
  form_window::Settings settings;
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

std::optional<float> parse_float(const std::string& text) {
  float value = 0.0F;
  const char* end = text.data() + text.size();
  const std::from_chars_result parsed = std::from_chars(text.data(), end, value);
  if (parsed.ec != std::errc{} || parsed.ptr != end) {
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
  if (flag == "--preset-radio-a") {
    const std::optional<int> number = parse_int(value, 0);
    if (!number.has_value()) {
      return false;
    }
    options.settings.preset_checked_radio_a = *number;
    return true;
  }
  if (flag == "--preset-volume") {
    const std::optional<float> number = parse_float(value);
    if (!number.has_value()) {
      return false;
    }
    options.settings.preset_volume = *number;
    return true;
  }
  return false;
}

std::optional<Options> parse_options(int argc, char** argv) {
  Options options;
  const std::vector<std::string> args{argv + 1, argv + argc};
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "--verify-form-controls") {
      options.mode = Mode::kVerify;
      continue;
    }
    if (args[i] == "--script") {
      options.mode = Mode::kScript;
      continue;
    }
    if (args[i] == "--preset-checkbox") {
      options.settings.preset_checkbox = true;
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
  std::cerr << "usage: drawgui_form_controls [--size WxH] [--run-ms N]\n"
            << "                             [--preset-radio-a N] [--preset-volume V]\n"
            << "                             [--preset-checkbox] [--probe X,Y]\n"
            << "                             [--dump-png FILE] [--verify-form-controls]\n"
            << "                             [--script]\n";
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
      return form_check::run(std::cout);
    case Mode::kDumpPng:
      return form_window::dump_png(options->settings, options->png_path, std::cout);
    case Mode::kProbe:
      return form_window::probe(options->settings, options->probe, std::cout);
    case Mode::kScript:
      return form_window::script(options->settings, std::cout);
    case Mode::kWindow:
      break;
  }
  return form_window::run(options->settings, std::cout);
}
