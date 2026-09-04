// The golden-image test runner.
//
// Each scene renders on CPU raster (design.md section 5.3.2) and is compared
// against a committed PNG baseline. On failure the actual and diff images are
// written next to each other so the regression can be looked at rather than
// guessed at.
//
// Baselines are regenerated with --update. That flag is deliberately explicit:
// a runner that silently refreshes its own expectations is not a test.

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "drawgui/graphics/canvas.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/graphics/types.h"
#include "golden_image.h"

namespace {

using dg::Canvas;
using dg::Color;
using dg::Radii;
using dg::Rect;

struct Scene {
  std::string_view name;
  int width;
  int height;
  void (*render)(Canvas&);
};

void scene_solid_background(Canvas& canvas) {
  canvas.clear(Color::rgba(0xF2, 0xF4, 0xF8));
}

void scene_rounded_rect(Canvas& canvas) {
  canvas.clear(Color::rgba(0xF2, 0xF4, 0xF8));
  canvas.fill_rrect(Rect::from_xywh(40, 40, 200, 120), Radii::all(24),
                    Color::rgba(0x2E, 0x86, 0xDE));
}

void scene_stroked_shapes(Canvas& canvas) {
  canvas.clear(Color::rgba(0xFF, 0xFF, 0xFF));
  canvas.stroke_rect(Rect::from_xywh(30, 30, 140, 100), Color::rgba(0xE7, 0x4C, 0x3C), 6);
  canvas.stroke_rrect(Rect::from_xywh(210, 30, 140, 100), Radii::all(20),
                      Color::rgba(0x27, 0xAE, 0x60), 6);
  // Asymmetric corners: catches a corner-ordering mistake in the Radii to
  // SkRRect conversion, which symmetric radii would hide entirely.
  canvas.fill_rrect(Rect::from_xywh(30, 160, 320, 110), Radii{40, 4, 40, 4},
                    Color::rgba(0x8E, 0x44, 0xAD));
}

void scene_alpha_blend(Canvas& canvas) {
  // design.md section 5.11.1: color alpha blends per draw, so the overlap of
  // two translucent fills is doubly blended and therefore darker. Node-level
  // opacity would not behave this way. This baseline pins that distinction.
  canvas.clear(Color::rgba(0x20, 0x24, 0x2C));
  canvas.fill_rect(Rect::from_xywh(40, 40, 180, 180), Color::rgba(0xE7, 0x4C, 0x3C, 0x80));
  canvas.fill_rect(Rect::from_xywh(140, 100, 180, 180), Color::rgba(0x2E, 0x86, 0xDE, 0x80));
}

constexpr Scene kScenes[] = {
    {"solid_background", 320, 200, scene_solid_background},
    {"rounded_rect", 400, 300, scene_rounded_rect},
    {"stroked_shapes", 400, 300, scene_stroked_shapes},
    {"alpha_blend", 360, 320, scene_alpha_blend},
};

const Scene* find_scene(std::string_view name) {
  for (const Scene& scene : kScenes) {
    if (scene.name == name) {
      return &scene;
    }
  }
  return nullptr;
}

std::optional<std::vector<std::uint8_t>> render_scene(const Scene& scene) {
  std::optional<dg::RasterSurface> surface =
      dg::RasterSurface::create(scene.width, scene.height);
  if (!surface) {
    return std::nullopt;
  }
  Canvas canvas = surface->canvas();
  scene.render(canvas);

  std::vector<std::uint8_t> png = surface->encode_png();
  if (png.empty()) {
    return std::nullopt;
  }
  return png;
}

void print_usage() {
  std::cerr << "usage: golden_test --baseline-dir DIR [--output-dir DIR] [--tolerance N]\n"
               "                   (--list | --all | --update | SCENE)\n";
}

enum class Mode : std::uint8_t { kCompare, kUpdate, kList, kInvalid };

struct Options {
  Mode mode = Mode::kCompare;
  std::filesystem::path baseline_dir;
  std::filesystem::path output_dir;
  int tolerance = 0;
  bool all = false;
  std::string scene_name;
};

Options parse_args(int argc, char** argv) {
  Options options;

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    const bool has_value = (i + 1) < argc;

    if (arg == "--baseline-dir" && has_value) {
      options.baseline_dir = argv[++i];
    } else if (arg == "--output-dir" && has_value) {
      options.output_dir = argv[++i];
    } else if (arg == "--tolerance" && has_value) {
      options.tolerance = std::stoi(argv[++i]);
    } else if (arg == "--update") {
      options.mode = Mode::kUpdate;
    } else if (arg == "--list") {
      options.mode = Mode::kList;
    } else if (arg == "--all") {
      options.all = true;
    } else if (!arg.empty() && arg.front() != '-') {
      options.scene_name = arg;
    } else {
      std::cerr << "golden_test: unrecognised argument '" << arg << "'\n";
      options.mode = Mode::kInvalid;
      return options;
    }
  }
  return options;
}

std::optional<std::vector<const Scene*>> select_scenes(const Options& options) {
  std::vector<const Scene*> selected;

  if (!options.scene_name.empty()) {
    const Scene* scene = find_scene(options.scene_name);
    if (scene == nullptr) {
      std::cerr << "golden_test: no such scene '" << options.scene_name << "'\n";
      return std::nullopt;
    }
    selected.push_back(scene);
    return selected;
  }

  if (!options.all && options.mode != Mode::kUpdate) {
    return std::nullopt;
  }
  for (const Scene& scene : kScenes) {
    selected.push_back(&scene);
  }
  return selected;
}

bool update_baseline(const Scene& scene, const std::vector<std::uint8_t>& png,
                     const std::filesystem::path& baseline_path) {
  if (!dg::testing::write_file(baseline_path, png)) {
    std::cerr << "FAIL " << scene.name << ": could not write baseline " << baseline_path
              << "\n";
    return false;
  }
  std::cout << "updated " << baseline_path.string() << "\n";
  return true;
}

void report_failure(const Scene& scene, const dg::testing::Comparison& comparison,
                    const std::vector<std::uint8_t>& actual_png,
                    const std::filesystem::path& output_dir) {
  std::cerr << "FAIL " << scene.name << ": " << comparison.failure_reason << "\n";

  const std::string name{scene.name};
  const std::filesystem::path actual_path = output_dir / (name + ".actual.png");
  if (dg::testing::write_file(actual_path, actual_png)) {
    std::cerr << "  actual: " << actual_path.string() << "\n";
  }
  if (!comparison.diff.empty()) {
    const std::filesystem::path diff_path = output_dir / (name + ".diff.png");
    if (dg::testing::write_file(diff_path, dg::testing::encode_png(comparison.diff))) {
      std::cerr << "  diff:   " << diff_path.string() << "\n";
    }
  }
}

bool compare_against_baseline(const Scene& scene, const std::vector<std::uint8_t>& actual_png,
                              const Options& options,
                              const std::filesystem::path& baseline_path) {
  const std::optional<std::vector<std::uint8_t>> baseline_png =
      dg::testing::read_file(baseline_path);
  if (!baseline_png) {
    std::cerr << "FAIL " << scene.name << ": baseline " << baseline_path
              << " is missing. Create it with --update, and review the image "
                 "before committing it.\n";
    return false;
  }

  // Both sides are decoded through the same path, so the comparison also
  // exercises the encoder rather than trusting it.
  const std::optional<dg::testing::Image> baseline = dg::testing::decode_png(*baseline_png);
  const std::optional<dg::testing::Image> actual = dg::testing::decode_png(actual_png);
  if (!baseline || !actual) {
    std::cerr << "FAIL " << scene.name << ": PNG decode failed\n";
    return false;
  }

  const dg::testing::Comparison comparison =
      dg::testing::compare(*baseline, *actual, options.tolerance);
  if (comparison.matched) {
    std::cout << "PASS " << scene.name << "\n";
    return true;
  }

  report_failure(scene, comparison, actual_png, options.output_dir);
  return false;
}

bool run_scene(const Scene& scene, const Options& options) {
  const std::optional<std::vector<std::uint8_t>> actual_png = render_scene(scene);
  if (!actual_png) {
    std::cerr << "FAIL " << scene.name << ": rendering produced no PNG\n";
    return false;
  }

  const std::filesystem::path baseline_path =
      options.baseline_dir / (std::string{scene.name} + ".png");

  if (options.mode == Mode::kUpdate) {
    return update_baseline(scene, *actual_png, baseline_path);
  }
  return compare_against_baseline(scene, *actual_png, options, baseline_path);
}

}  // namespace

int main(int argc, char** argv) {
  Options options = parse_args(argc, argv);

  if (options.mode == Mode::kInvalid) {
    print_usage();
    return 2;
  }
  if (options.mode == Mode::kList) {
    for (const Scene& scene : kScenes) {
      std::cout << scene.name << "\n";
    }
    return 0;
  }

  if (options.baseline_dir.empty()) {
    std::cerr << "golden_test: --baseline-dir is required\n";
    print_usage();
    return 2;
  }
  if (options.output_dir.empty()) {
    options.output_dir = std::filesystem::current_path();
  }

  const std::optional<std::vector<const Scene*>> selected = select_scenes(options);
  if (!selected) {
    print_usage();
    return 2;
  }

  int failures = 0;
  for (const Scene* scene : *selected) {
    if (!run_scene(*scene, options)) {
      ++failures;
    }
  }
  return failures == 0 ? 0 : 1;
}
