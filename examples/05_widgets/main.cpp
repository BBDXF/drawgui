// Basic widgets and pointer interaction: hover, press, click, and only the
// pixels that changed.
//
//   (no arguments)      the demo window - move the pointer, click things
//   --script            walk the pointer over every widget and click each one,
//                       through the platform's own event queue
//   --verify-widgets    interaction repaint against a full repaint, byte for
//                       byte, plus every pixel of the scene against a
//                       brute-force hit-testing oracle. No display needed.
//   --clip-probe        is Skia's anti-aliased output clip-invariant? The
//                       measurement that decides which nodes repaint whole.
//   --damage-cost       what one hover costs, square controls versus rounded
//
// Build Release before quoting any number.

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "drawgui/base/pixel_geometry.h"

#include "clip_probe.h"
#include "demo.h"
#include "widget_check.h"

namespace {

enum class Mode : std::uint8_t {
  kWindow,
  kVerify,
  kClipProbe,
  kDamageCost,
  kListWidgets,
};

struct Options {
  Mode mode = Mode::kWindow;
  demo::Settings settings;
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

bool apply_valued(const std::string& flag, const std::string& value, Options& options) {
  if (flag == "--size") {
    return parse_size(value, options);
  }
  if (flag == "--font-dir") {
    options.settings.font_dir = value;
    return true;
  }
  if (flag == "--corners") {
    options.settings.rounded_controls = value == "rounded";
    return value == "rounded" || value == "square";
  }
  if (flag == "--container-corners") {
    options.settings.rounded_containers = value == "rounded";
    return value == "rounded" || value == "square";
  }

  const std::optional<int> number = parse_positive_int(value);
  if (!number.has_value()) {
    return false;
  }
  if (flag == "--max-damage-rects") {
    options.settings.max_damage_rects = static_cast<std::size_t>(*number);
    return true;
  }
  if (flag == "--script-step-ms") {
    options.settings.script_step_ms = *number;
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
    if (args[i] == "--script") {
      options.settings.script = true;
      continue;
    }
    if (args[i] == "--verify-widgets") {
      options.mode = Mode::kVerify;
      continue;
    }
    if (args[i] == "--clip-probe") {
      options.mode = Mode::kClipProbe;
      continue;
    }
    if (args[i] == "--damage-cost") {
      options.mode = Mode::kDamageCost;
      continue;
    }
    if (args[i] == "--list-widgets") {
      options.mode = Mode::kListWidgets;
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
  std::cerr << "usage: drawgui_widgets [options]\n"
            << "  --script                  drive the pointer over every widget\n"
            << "  --verify-widgets          repaint identity + exhaustive hit testing\n"
            << "  --clip-probe              is anti-aliased text clip-invariant?\n"
            << "  --damage-cost             pixels per hover, square vs rounded\n"
            << "  --list-widgets            every interactive widget and where it is\n"
            << "  --corners square|rounded  control corner radius (default square)\n"
            << "  --container-corners square|rounded   panel corner radius\n"
            << "  --size WxH                window size (default 1120x800)\n"
            << "  --max-damage-rects N      damage list cap; 1 is always-union\n"
            << "  --script-step-ms N        dwell per scripted step (default 90)\n"
            << "  --run-ms N                close the window after N ms\n"
            << "  --font-dir DIR            where to scan for fonts\n";
}

widget_check::Config check_config(const Options& options) {
  widget_check::Config config;
  config.viewport = options.settings.size;
  config.rounded_controls = options.settings.rounded_controls;
  config.rounded_containers = options.settings.rounded_containers;
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
    case Mode::kVerify: {
      const bool identity = widget_check::verify_interaction(check_config(options), std::cout);
      const bool hits = widget_check::verify_hit_testing(check_config(options), std::cout);
      return identity && hits ? 0 : 1;
    }
    case Mode::kClipProbe:
      clip_probe::report(std::cout, options.settings.font_dir);
      return 0;
    case Mode::kDamageCost:
      widget_check::report_damage_cost(check_config(options), std::cout);
      return 0;
    case Mode::kListWidgets:
      widget_check::report_widgets(check_config(options), std::cout);
      return 0;
    case Mode::kWindow:
      return demo::run(options.settings, std::cout);
  }
  return 2;
}
