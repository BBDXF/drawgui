#include "theme_package_check.h"

#include <unistd.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/theme/theme_package.h"

#include "theme_package_scene.h"

#ifndef DRAWGUI_THEME_PACKAGE_FIXTURE_DIR
#error "DRAWGUI_THEME_PACKAGE_FIXTURE_DIR must be provided by CMakeLists.txt"
#endif

namespace theme_package_check {
namespace {

namespace fs = std::filesystem;

using dg::Color;
using dg::PixelRect;
using dg::RasterSurface;

std::uint32_t pixel_at(const dg::PixelView& view, int x, int y) {
  const std::size_t offset =
      (static_cast<std::size_t>(y) * view.row_bytes) + (static_cast<std::size_t>(x) * 4);
  return (static_cast<std::uint32_t>(view.pixels[offset + 3]) << 24U) |
         (static_cast<std::uint32_t>(view.pixels[offset + 2]) << 16U) |
         (static_cast<std::uint32_t>(view.pixels[offset + 1]) << 8U) |
         static_cast<std::uint32_t>(view.pixels[offset]);
}

bool check_panel_color(theme_package_scene::Scene& scene, dg::NodeId panel, Color expected,
                       const char* what, std::ostream& out) {
  std::optional<RasterSurface> surface =
      RasterSurface::create(scene.tree.viewport().width, scene.tree.viewport().height);
  if (!surface.has_value()) {
    out << "  could not allocate a surface\n";
    return false;
  }
  scene.tree.render().repaint_full(*surface);
  const dg::PixelView view = surface->peek_pixels();
  if (view.pixels == nullptr || !view.is_bgra8888) {
    out << "  the surface is not the format this check reads\n";
    return false;
  }
  const PixelRect box = scene.tree.bounds(panel);
  const std::uint32_t got =
      pixel_at(view, box.left() + (box.width / 2), box.top() + (box.height / 2));
  const std::uint32_t want = expected.argb();
  if (got != want) {
    out << "  FAIL " << what << ": got 0x" << std::hex << got << " expected 0x" << want
        << std::dec << "\n";
    return false;
  }
  return true;
}

void replace_first(std::string& text, const std::string& needle,
                   const std::string& replacement) {
  const std::size_t pos = text.find(needle);
  if (pos != std::string::npos) {
    text.replace(pos, needle.size(), replacement);
  }
}

void edit_theme_json(const fs::path& json_path, const std::string& needle,
                     const std::string& replacement) {
  std::string text;
  {
    std::ifstream in(json_path, std::ios::binary);
    text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }
  replace_first(text, needle, replacement);
  std::ofstream out_file(json_path, std::ios::binary);
  out_file << text;
}

// Half 1 of the hot-reload measurement (see this file's own header
// comment): a scene with NO bound int property, so its reload can isolate
// the colour-only case doc/theme.md's own "every binding unconditionally"
// finding would otherwise contaminate.
bool check_color_only_reload(const dg::TreeSpec& spec, const fs::path& work_dir,
                             std::ostream& out) {
  std::string error;
  std::optional<theme_package_scene::Scene> scene =
      theme_package_scene::build(spec, work_dir.string(), error, /*bind_gap_to_token=*/false);
  if (!scene.has_value()) {
    out << "  FAIL: could not load the package: " << error << "\n";
    return false;
  }
  out << "  package loaded from " << work_dir.string() << "\n";

  bool ok = check_panel_color(
      *scene, scene->handles.surface_panel, Color::from_argb(0xFFF5F0E6),
      "surface_panel (initial, light, from fixtures/mytheme/theme.json)", out);
  out << "  initial surface colour: " << (ok ? "PASS" : "FAIL") << "\n";

  edit_theme_json(work_dir / "theme.json", "\"color.surface\": \"#F5F0E6FF\"",
                  "\"color.surface\": \"#00FF00FF\"");
  std::string reload_error;
  const bool reloaded = theme_package_scene::reload(*scene, reload_error);
  ok = reloaded && ok;
  out << "  reload after colour-only edit: " << (reloaded ? "PASS" : ("FAIL: " + reload_error))
      << "\n";

  const dg::LayoutStats stats = scene->tree.layout();
  out << "  LayoutStats after colour-only reload: nodes_visited=" << stats.nodes_visited
      << " nodes_relaid_out=" << stats.nodes_relaid_out << "\n";
  if (stats.nodes_visited != 0 || stats.nodes_relaid_out != 0) {
    out << "  FAIL: a colour-only reload was expected to cost zero relayout\n";
    ok = false;
  }

  const bool green_ok =
      check_panel_color(*scene, scene->handles.surface_panel, Color::from_argb(0xFF00FF00),
                        "surface_panel (after colour-only reload)", out);
  ok = green_ok && ok;
  out << "  surface colour after reload: " << (green_ok ? "PASS" : "FAIL") << "\n";

  const dg::Expected<std::vector<std::uint8_t>, dg::ThemeLoadError> legit =
      scene->package.read_resource("icons/readme.txt");
  ok = legit.has_value() && ok;
  out << "  read_resource('icons/readme.txt'): " << (legit.has_value() ? "PASS" : "FAIL")
      << "\n";

  const dg::Expected<std::vector<std::uint8_t>, dg::ThemeLoadError> hostile =
      scene->package.read_resource("../../../../../../etc/passwd");
  const bool hostile_rejected =
      !hostile.has_value() && hostile.error().status == dg::ThemeLoadStatus::kPathTraversal;
  ok = hostile_rejected && ok;
  out << "  read_resource('../../../../../../etc/passwd'): "
      << (hostile_rejected ? "PASS (rejected, DG_THEME_ERR_PATH_TRAVERSAL-shaped)" : "FAIL")
      << "\n";

  return ok;
}

// Half 2: a SEPARATE package copy/scene with `gap` genuinely bound to
// space.md, so this half measures a real value CHANGE reaching a bound
// BoxStyle field, not merely "any reload with a bound int property costs
// one" (which half 1 above already isolated away from).
bool check_int_token_reload(const dg::TreeSpec& spec, const fs::path& work_dir,
                            std::ostream& out) {
  std::string error;
  std::optional<theme_package_scene::Scene> scene =
      theme_package_scene::build(spec, work_dir.string(), error, /*bind_gap_to_token=*/true);
  if (!scene.has_value()) {
    out << "  FAIL: could not load the second package copy: " << error << "\n";
    return false;
  }

  const PixelRect primary_before = scene->tree.bounds(scene->handles.primary_panel);
  edit_theme_json(work_dir / "theme.json", "\"space.md\": 18", "\"space.md\": 96");

  std::string reload_error;
  bool ok = theme_package_scene::reload(*scene, reload_error);
  out << "  reload after int-token (space.md) edit: "
      << (ok ? "PASS" : ("FAIL: " + reload_error)) << "\n";

  const dg::LayoutStats stats = scene->tree.layout();
  out << "  LayoutStats after int-token reload: nodes_visited=" << stats.nodes_visited
      << " nodes_relaid_out=" << stats.nodes_relaid_out << "\n";
  if (stats.nodes_visited == 0 || stats.nodes_relaid_out == 0) {
    out << "  FAIL: an int-token (space.md) reload was expected to relayout the bound row\n";
    ok = false;
  }

  const PixelRect primary_after = scene->tree.bounds(scene->handles.primary_panel);
  const bool moved = primary_after.left() != primary_before.left();
  ok = moved && ok;
  out << "  primary_panel actually moved (gap widened): " << (moved ? "PASS" : "FAIL")
      << " (before x=" << primary_before.left() << ", after x=" << primary_after.left()
      << ")\n";
  return ok;
}

}  // namespace

int run(std::ostream& out) {
  out << "THEME PACKAGE: an EXTERNAL, untrusted theme.json directory loaded through "
         "dg::ThemePackage (P7 slice 7-6), with $token bindings that hot-reload without "
         "rebuilding the widget tree (design.md section 5.7.4/5.7.5/5.7.6).\n\n";

  dg::TreeSpec spec;
  spec.viewport = theme_package_scene::kDemoViewport;
  spec.background.fill = Color::from_argb(0xFF14171C);

  // A writable copy, so this run never mutates the checked-in fixture -
  // the SAME reasoning tests/unit/test_theme_package.cpp's TempDir already
  // uses, applied here to a real example rather than a unit test. Two
  // SEPARATE copies, one per half of the measurement - see
  // check_color_only_reload()/check_int_token_reload()'s own comments.
  const fs::path work_dir = fs::temp_directory_path() /
                            ("drawgui_theme_package_example_" + std::to_string(::getpid()));
  const fs::path work_dir2 = fs::temp_directory_path() /
                             ("drawgui_theme_package_example2_" + std::to_string(::getpid()));
  fs::remove_all(work_dir);
  fs::remove_all(work_dir2);
  fs::copy(DRAWGUI_THEME_PACKAGE_FIXTURE_DIR, work_dir, fs::copy_options::recursive);
  fs::copy(DRAWGUI_THEME_PACKAGE_FIXTURE_DIR, work_dir2, fs::copy_options::recursive);

  const bool color_ok = check_color_only_reload(spec, work_dir, out);
  const bool int_ok = check_int_token_reload(spec, work_dir2, out);
  const bool ok = color_ok && int_ok;

  fs::remove_all(work_dir);
  fs::remove_all(work_dir2);

  out << (ok ? "\nPASS\n" : "\nFAIL\n");
  return ok ? 0 : 1;
}

}  // namespace theme_package_check
