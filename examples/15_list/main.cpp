// A virtualized, 1000-item list, on screen.
//
//   (no arguments)      the demo window - wheel or click-drag the panel
//   --size WxH          open at a given size
//   --run-ms N          close after N milliseconds
//   --jump N            pre-scroll to item N (offscreen modes only)
//   --verify-list       the headless check. No display needed.
//   --dump-png FILE     rasterize one frame and write it out
//   --bench [N]         measure against the pre-virtualization baseline at
//                       N items (default 1000). No display needed.

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "drawgui/base/pixel_geometry.h"

#include "list_check.h"
#include "list_window.h"

namespace {

enum class Mode : std::uint8_t {
  kWindow,
  kVerify,
  kDumpPng,
  kBench,
};

struct Options {
  Mode mode = Mode::kWindow;
  list_window::Settings settings;
  std::string png_path;
  int bench_items = 1000;
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
  if (flag == "--jump") {
    const std::optional<int> number = parse_int(value, -1000000);
    if (!number.has_value()) {
      return false;
    }
    options.settings.preset_jump_items = *number;
    return true;
  }
  return false;
}

// `--bench` takes an OPTIONAL trailing item count, unlike every other flag
// here, so it gets its own branch rather than folding into apply_valued()'s
// "flag always consumes the next argument" shape.
bool apply_bench(const std::vector<std::string>& args, std::size_t& index, Options& options) {
  options.mode = Mode::kBench;
  if (index + 1 < args.size()) {
    if (const std::optional<int> n = parse_int(args[index + 1], 1)) {
      options.bench_items = *n;
      ++index;
    }
  }
  return true;
}

std::optional<Options> parse_options(int argc, char** argv) {
  Options options;
  const std::vector<std::string> args{argv + 1, argv + argc};
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "--verify-list") {
      options.mode = Mode::kVerify;
      continue;
    }
    if (args[i] == "--bench") {
      apply_bench(args, i, options);
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
  std::cerr << "usage: drawgui_list [--size WxH] [--run-ms N] [--jump N]\n"
            << "                    [--dump-png FILE] [--verify-list] [--bench [N]]\n";
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
      return list_check::run(std::cout);
    case Mode::kDumpPng:
      return list_window::dump_png(options->settings, options->png_path, std::cout);
    case Mode::kBench:
      return list_window::bench(options->bench_items, std::cout);
    case Mode::kWindow:
      break;
  }
  return list_window::run(options->settings, std::cout);
}
