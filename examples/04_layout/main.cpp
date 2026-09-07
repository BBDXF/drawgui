// Incremental layout: change one leaf, lay out only what that change can
// reach, and feed the movement into the damage system.
//
// Sub-step 1 measured damage-driven repaint at 27x overall, which makes
// partial repaint a precondition rather than an optimisation - and a layout
// engine that recomputed the whole tree on every change would hand that
// system a full-window damage rectangle and undo it. This demo is that
// argument made watchable: it mutates one leaf per frame, alternates between
// laying out only the affected subtree and laying out everything, and prints
// how many nodes each one actually recomputed into the window as it runs.
//
// It also flips the container corner radius on a slower timer, because
// sub-step 1 measured a 30x difference in damaged pixels from one radius and
// layout is what decides node bounds.
//
//   (no arguments)        the demo, alternating between the two strategies
//   --mode incremental|full|alternate
//   --verify-layout N     N frames offscreen, bounds compared node for node
//   --scope               how much of the tree each class of change touches
//   --bench               offscreen layout timings at four viewport sizes
//
// Build Release before quoting any number, and read the demo's paced figures
// rather than --bench's tight-loop ones.

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "drawgui/base/pixel_geometry.h"

#include "demo.h"
#include "layout_check.h"

namespace {

enum class Mode : std::uint8_t {
  kWindow,
  kVerify,
  kScope,
  kBench,
};

struct Options {
  Mode mode = Mode::kWindow;
  demo::Settings settings;
  int frames = 240;
};

std::optional<int> parse_positive_int(const std::string& text) {
  int value = 0;
  const char* end = text.data() + text.size();
  const std::from_chars_result parsed = std::from_chars(text.data(), end, value);
  if (parsed.ec != std::errc{} || parsed.ptr != end || value <= 0) {
    return std::nullopt;
  }
  return value;
}

bool parse_size(const std::string& text, Options& options) {
  const std::size_t separator = text.find('x');
  if (separator == std::string::npos) {
    return false;
  }
  const std::optional<int> width = parse_positive_int(text.substr(0, separator));
  const std::optional<int> height = parse_positive_int(text.substr(separator + 1));
  if (!width.has_value() || !height.has_value()) {
    return false;
  }
  options.settings.size = dg::PixelSize{*width, *height};
  return true;
}

bool parse_layout_mode(const std::string& value, Options& options) {
  if (value == "alternate") {
    options.settings.alternate = true;
    return true;
  }
  options.settings.alternate = false;
  options.settings.incremental = value == "incremental";
  return value == "incremental" || value == "full";
}

bool parse_corners(const std::string& value, Options& options) {
  if (value == "alternate") {
    options.settings.rounded_alternate = true;
    return true;
  }
  options.settings.rounded_alternate = false;
  options.settings.rounded = value == "rounded";
  return value == "rounded" || value == "square";
}

bool apply_valued(const std::string& flag, const std::string& value, Options& options) {
  if (flag == "--size") {
    return parse_size(value, options);
  }
  if (flag == "--mode") {
    return parse_layout_mode(value, options);
  }
  if (flag == "--corners") {
    return parse_corners(value, options);
  }
  if (flag == "--font-dir") {
    options.settings.font_dir = value;
    return true;
  }

  const std::optional<int> number = parse_positive_int(value);
  if (!number.has_value()) {
    return false;
  }
  if (flag == "--verify-layout") {
    options.mode = Mode::kVerify;
    options.frames = *number;
    return true;
  }
  if (flag == "--frames") {
    options.frames = *number;
    return true;
  }
  if (flag == "--max-damage-rects") {
    options.settings.max_damage_rects = static_cast<std::size_t>(*number);
    return true;
  }
  if (flag == "--alternate-ms") {
    options.settings.alternate_ms = *number;
    return true;
  }
  if (flag == "--corners-ms") {
    options.settings.rounded_ms = *number;
    return true;
  }
  if (flag == "--frame-budget-ms") {
    options.settings.frame_budget_ms = *number;
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
  options.settings.size = dg::PixelSize{1280, 800};
  const std::vector<std::string> args{argv + 1, argv + argc};

  for (std::size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "--bench") {
      options.mode = Mode::kBench;
      continue;
    }
    if (args[i] == "--scope") {
      options.mode = Mode::kScope;
      continue;
    }
    if (i + 1 >= args.size()) {
      return std::nullopt;
    }
    const std::string& flag = args[i];
    if (!apply_valued(flag, args[++i], options)) {
      return std::nullopt;
    }
  }
  return options;
}

void usage() {
  std::cerr
      << "usage: drawgui_layout [options]\n"
      << "  --mode incremental|full|alternate  layout strategy (default alternate)\n"
      << "  --corners square|rounded|alternate container radius (default alternate)\n"
      << "  --verify-layout N             N frames offscreen, bounds compared node "
         "for node\n"
      << "  --scope                       how much of the tree each change touches\n"
      << "  --bench                       offscreen layout timings, four sizes\n"
      << "  --size WxH                    window size (default 1280x800)\n"
      << "  --max-damage-rects N          damage list cap; 1 is always-union\n"
      << "  --alternate-ms N              how long to stay in each mode (default 1500)\n"
      << "  --corners-ms N                how long to stay on each radius (default 5000)\n"
      << "  --frames N                    timed frames per benchmark row\n"
      << "  --frame-budget-ms N           pace the loop to this budget (default 16)\n"
      << "  --run-ms N                    close the window after N ms\n"
      << "  --font-dir DIR                where to scan for fonts\n";
}

layout_check::Config check_config(const Options& options) {
  layout_check::Config config;
  config.viewport = options.settings.size;
  config.frames = options.frames;
  config.rounded_containers = options.settings.rounded;
  return config;
}

}  // namespace

int main(int argc, char** argv) {
  const std::optional<Options> parsed = parse_options(argc, argv);
  if (!parsed.has_value()) {
    usage();
    return 2;
  }
  const Options& options = *parsed;

  switch (options.mode) {
    case Mode::kVerify:
      return layout_check::verify_layout(check_config(options), std::cout) ? 0 : 1;
    case Mode::kScope:
      layout_check::report_scope(check_config(options), std::cout);
      return 0;
    case Mode::kBench:
      layout_check::bench(options.frames, std::cout);
      return 0;
    case Mode::kWindow:
      return demo::run(options.settings, std::cout);
  }
  return 2;
}
