// Damage-driven retained repaint: mark one node dirty, rasterize and present
// only that region.
//
// Step 2 measured a full-window repaint at 1080p as 3.91 ms of rasterization
// plus 7.71 ms of presentation, whose p95 pair already exceeds the 16.6 ms
// frame budget, against 0.12 ms for the same scene under a 260x72 clip. This
// demo is that difference made watchable: it animates a small retained render
// tree, alternates between repainting only what changed and repainting
// everything, and prints both sets of timings into the window as it runs.
//
//   (no arguments)        the demo, alternating between the two strategies
//   --mode damage|full|alternate
//   --verify-damage N     N frames offscreen, compared byte for byte
//   --bench               offscreen raster timings at four resolutions
//
// Build Release before quoting any number. A Debug figure would describe an
// unoptimized rasterizer and would misrepresent the decision it informs.

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "drawgui/render/render_tree.h"

#include "demo.h"
#include "selfcheck.h"

namespace {

enum class Mode : std::uint8_t {
  kWindow,
  kVerify,
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

bool parse_repaint_mode(const std::string& value, Options& options) {
  if (value == "alternate") {
    options.settings.alternate = true;
    return true;
  }
  options.settings.alternate = false;
  options.settings.damage_mode = value == "damage";
  return value == "damage" || value == "full";
}

bool parse_paint_mode(const std::string& value, Options& options) {
  options.settings.paint_mode =
      value == "picture" ? dg::PaintMode::kPicture : dg::PaintMode::kDirect;
  return value == "picture" || value == "direct";
}

bool apply_valued(const std::string& flag, const std::string& value, Options& options) {
  if (flag == "--size") {
    return parse_size(value, options);
  }
  if (flag == "--mode") {
    return parse_repaint_mode(value, options);
  }
  if (flag == "--paint") {
    return parse_paint_mode(value, options);
  }
  if (flag == "--font-dir") {
    options.settings.font_dir = value;
    return true;
  }

  const std::optional<int> number = parse_positive_int(value);
  if (!number.has_value()) {
    return false;
  }
  if (flag == "--verify-damage") {
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
  std::cerr << "usage: drawgui_damage_repaint [options]\n"
            << "  --mode damage|full|alternate  repaint strategy (default alternate)\n"
            << "  --verify-damage N             N frames offscreen, compared byte for byte\n"
            << "  --bench                       offscreen raster timings, four resolutions\n"
            << "  --size WxH                    window size (default 1280x800)\n"
            << "  --paint direct|picture        traverse the tree, or replay an SkPicture\n"
            << "  --max-damage-rects N          damage list cap; 1 is always-union\n"
            << "  --alternate-ms N              how long to stay in each mode (default 1500)\n"
            << "  --frames N                    timed frames per benchmark row\n"
            << "  --frame-budget-ms N           pace the loop to this budget (default 16)\n"
            << "  --run-ms N                    close the window after N ms\n"
            << "  --font-dir DIR                where to scan for fonts\n";
}

selfcheck::Config verify_config(const Options& options) {
  selfcheck::Config config;
  config.viewport = options.settings.size;
  config.max_damage_rects = options.settings.max_damage_rects;
  config.paint_mode = options.settings.paint_mode;
  config.frames = options.frames;
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
      return selfcheck::verify_damage(verify_config(options), std::cout) ? 0 : 1;
    case Mode::kBench:
      selfcheck::bench(options.frames, std::cout);
      return 0;
    case Mode::kWindow:
      return demo::run(options.settings, std::cout);
  }
  return 2;
}
