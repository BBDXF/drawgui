#include "theme_package_check.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <unistd.h>

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

void replace_first(std::string& text, const std::string& needle, const std::string& replacement) {
  const std::size_t pos = text.find(needle);
  if (pos != std::string::npos) {
    text.replace(pos, needle.size(), replacement);
  }
}

}  // namespace

int run(std::ostream& out) {
  out << "THEME PACKAGE: an EXTERNAL, untrusted theme.json directory loaded through "
         "dg::ThemePackage (P7 slice 7-6), with $token bindings that hot-reload without "
         "rebuilding the widget tree (design.md section 5.7.4/5.7.5/5.7.6).\n\n";

  bool ok = true;

  // A writable copy, so this run never mutates the checked-in fixture -
  // the SAME reasoning tests/unit/test_theme_package.cpp's TempDir already
  // uses, applied here to a real example rather than a unit test.
  const fs::path work_dir = fs::temp_directory_path() /
                            ("drawgui_theme_package_example_" + std::to_string(::getpid()));
  fs::remove_all(work_dir);
  fs::copy(DRAWGUI_THEME_PACKAGE_FIXTURE_DIR, work_dir, fs::copy_options::recursive);

  dg::TreeSpec spec;
  spec.viewport = theme_package_scene::kDemoViewport;
  spec.background.fill = Color::from_argb(0xFF14171C);

  std::string error;
  std::optional<theme_package_scene::Scene> scene =
      theme_package_scene::build(spec, work_dir.string(), error, /*bind_gap_to_token=*/false);
  if (!scene.has_value()) {
    out << "  FAIL: could not load the package: " << error << "\n";
    fs::remove_all(work_dir);
    return 1;
  }
  out << "  package loaded from " << work_dir.string() << "\n";

  const bool initial_ok =
      check_panel_color(*scene, scene->handles.surface_panel, Color::from_argb(0xFFF5F0E6),
                        "surface_panel (initial, light, from fixtures/mytheme/theme.json)", out);
  ok = initial_ok && ok;
  out << "  initial surface colour: " << (initial_ok ? "PASS" : "FAIL") << "\n";

  // --- Hot reload, half 1: a COLOUR-only edit costs zero relayout. -------
  //
  // This scene deliberately does NOT bind `gap` to a token (bind_gap_to_
  // token=false, matching examples/18_theme's own scene) - doc/theme.md's
  // own recorded finding is that ThemeBindings::apply() re-resolves and
  // re-writes EVERY recorded binding unconditionally, so a scene with even
  // ONE bound int property would cost a relayout on ANY reload, colour-only
  // or not. Isolating the colour-only claim needs a scene with no bound int
  // property at all - see theme_package_scene.h's own header comment.
  const fs::path json_path = work_dir / "theme.json";
  {
    std::string text;
    {
      std::ifstream in(json_path, std::ios::binary);
      text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    replace_first(text, "\"color.surface\": \"#F5F0E6FF\"", "\"color.surface\": \"#00FF00FF\"");
    std::ofstream out_file(json_path, std::ios::binary);
    out_file << text;
  }
  std::string reload_error;
  const bool reload1_ok = theme_package_scene::reload(*scene, reload_error);
  ok = reload1_ok && ok;
  out << "  reload after colour-only edit: " << (reload1_ok ? "PASS" : ("FAIL: " + reload_error))
      << "\n";
  const dg::LayoutStats after_color_edit = scene->tree.layout();
  out << "  LayoutStats after colour-only reload: nodes_visited="
      << after_color_edit.nodes_visited
      << " nodes_relaid_out=" << after_color_edit.nodes_relaid_out << "\n";
  if (after_color_edit.nodes_visited != 0 || after_color_edit.nodes_relaid_out != 0) {
    out << "  FAIL: a colour-only reload was expected to cost zero relayout\n";
    ok = false;
  }
  const bool green_ok = check_panel_color(*scene, scene->handles.surface_panel,
                                          Color::from_argb(0xFF00FF00),
                                          "surface_panel (after colour-only reload)", out);
  ok = green_ok && ok;
  out << "  surface colour after reload: " << (green_ok ? "PASS" : "FAIL") << "\n";

  // --- Hot reload, half 2: an INT-token edit DOES relayout. --------------
  //
  // A SEPARATE package copy and scene, built with bind_gap_to_token=true -
  // this scene's own `gap` genuinely is a $token binding, so this half
  // measures the real thing design.md section 12 asks for: a value CHANGE
  // reaching a bound BoxStyle field through dg::set_prop()'s existing
  // relayout rule, not merely "any reload with a bound int property costs
  // one" (which half 1 above already isolated away from).
  const fs::path work_dir2 = fs::temp_directory_path() /
                             ("drawgui_theme_package_example2_" + std::to_string(::getpid()));
  fs::remove_all(work_dir2);
  fs::copy(DRAWGUI_THEME_PACKAGE_FIXTURE_DIR, work_dir2, fs::copy_options::recursive);
  std::optional<theme_package_scene::Scene> scene2 =
      theme_package_scene::build(spec, work_dir2.string(), error, /*bind_gap_to_token=*/true);
  if (!scene2.has_value()) {
    out << "  FAIL: could not load the second package copy: " << error << "\n";
    ok = false;
  } else {
    const PixelRect primary_before = scene2->tree.bounds(scene2->handles.primary_panel);
    const fs::path json_path2 = work_dir2 / "theme.json";
    {
      std::string text;
      {
        std::ifstream in(json_path2, std::ios::binary);
        text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
      }
      replace_first(text, "\"space.md\": 18", "\"space.md\": 96");
      std::ofstream out_file(json_path2, std::ios::binary);
      out_file << text;
    }
    const bool reload2_ok = theme_package_scene::reload(*scene2, reload_error);
    ok = reload2_ok && ok;
    out << "  reload after int-token (space.md) edit: "
        << (reload2_ok ? "PASS" : ("FAIL: " + reload_error)) << "\n";
    const dg::LayoutStats after_int_edit = scene2->tree.layout();
    out << "  LayoutStats after int-token reload: nodes_visited=" << after_int_edit.nodes_visited
        << " nodes_relaid_out=" << after_int_edit.nodes_relaid_out << "\n";
    if (after_int_edit.nodes_visited == 0 || after_int_edit.nodes_relaid_out == 0) {
      out << "  FAIL: an int-token (space.md) reload was expected to relayout the bound row\n";
      ok = false;
    }
    const PixelRect primary_after = scene2->tree.bounds(scene2->handles.primary_panel);
    const bool moved = primary_after.left() != primary_before.left();
    ok = moved && ok;
    out << "  primary_panel actually moved (gap widened): " << (moved ? "PASS" : "FAIL")
        << " (before x=" << primary_before.left() << ", after x=" << primary_after.left()
        << ")\n";
  }
  fs::remove_all(work_dir2);

  // --- Security: a legitimate resource read succeeds, a traversal one does
  // not - design.md section 5.7.5, exercised through the SAME package this
  // whole demo just proved works for its legitimate purpose. ---------------
  const dg::Expected<std::vector<std::uint8_t>, dg::ThemeLoadError> legit =
      scene->package.read_resource("icons/readme.txt");
  const bool legit_ok = legit.has_value();
  ok = legit_ok && ok;
  out << "  read_resource('icons/readme.txt'): " << (legit_ok ? "PASS" : "FAIL") << "\n";

  const dg::Expected<std::vector<std::uint8_t>, dg::ThemeLoadError> hostile =
      scene->package.read_resource("../../../../../../etc/passwd");
  const bool hostile_rejected =
      !hostile.has_value() && hostile.error().status == dg::ThemeLoadStatus::kPathTraversal;
  ok = hostile_rejected && ok;
  out << "  read_resource('../../../../../../etc/passwd'): "
      << (hostile_rejected ? "PASS (rejected, DG_THEME_ERR_PATH_TRAVERSAL-shaped)" : "FAIL")
      << "\n";

  fs::remove_all(work_dir);

  out << (ok ? "\nPASS\n" : "\nFAIL\n");
  return ok ? 0 : 1;
}

}  // namespace theme_package_check
