#include "theme_check.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <string>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/render_tree.h"

#include "theme_scene.h"

namespace theme_check {
namespace {

using dg::Color;
using dg::PixelRect;
using dg::RasterSurface;
using dg::Theme;
using dg::ThemeVariant;

std::uint32_t pixel_at(const dg::PixelView& view, int x, int y) {
  const std::size_t offset =
      (static_cast<std::size_t>(y) * view.row_bytes) + (static_cast<std::size_t>(x) * 4);
  return (static_cast<std::uint32_t>(view.pixels[offset + 3]) << 24U) |
         (static_cast<std::uint32_t>(view.pixels[offset + 2]) << 16U) |
         (static_cast<std::uint32_t>(view.pixels[offset + 1]) << 8U) |
         static_cast<std::uint32_t>(view.pixels[offset]);
}

bool check_pixel(const dg::PixelView& view, int x, int y, Color expected, const char* what,
                 std::ostream& out, bool& ok) {
  const std::uint32_t got = pixel_at(view, x, y);
  const std::uint32_t want = expected.argb();
  if (got != want) {
    out << "  FAIL " << what << " at " << x << "," << y << ": got " << std::hex << got
        << " expected " << want << std::dec << "\n";
    ok = false;
    return false;
  }
  return true;
}

// The value_or() fallback (opaque magenta, never a real theme colour) is
// only reached when the theme is missing a token this scene bound - which
// tools/check_consistency.py already keeps from happening for the shipped
// theme, so a check that then fails on a wrong-colour comparison is the
// right outcome rather than an unchecked-optional-access finding.
Color resolved_or_sentinel(const Theme& theme, dg_token_id token, ThemeVariant variant) {
  return theme.color_value(token, variant).value_or(Color::rgba(0xFF, 0x00, 0xFF));
}

bool check_variant(theme_scene::Scene& scene, std::ostream& out) {
  bool ok = true;
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

  const Color surface_color =
      resolved_or_sentinel(scene.theme, DG_TOKEN_COLOR_SURFACE, scene.variant);
  const Color primary_color =
      resolved_or_sentinel(scene.theme, DG_TOKEN_COLOR_PRIMARY, scene.variant);
  const Color primary_hover_color =
      resolved_or_sentinel(scene.theme, DG_TOKEN_COLOR_PRIMARY_HOVER, scene.variant);

  const PixelRect surface_box = scene.tree.bounds(scene.handles.surface_panel);
  check_pixel(view, surface_box.left() + (surface_box.width / 2),
              surface_box.top() + (surface_box.height / 2), surface_color, "surface_panel", out,
              ok);

  const PixelRect border_box = scene.tree.bounds(scene.handles.border_panel);
  check_pixel(view, border_box.left() + (border_box.width / 2),
              border_box.top() + (border_box.height / 2), primary_color, "border_panel", out,
              ok);

  const PixelRect primary_box = scene.tree.bounds(scene.handles.primary_panel);
  check_pixel(view, primary_box.left() + (primary_box.width / 2),
              primary_box.top() + (primary_box.height / 2), primary_hover_color,
              "primary_panel", out, ok);

  return ok;
}

}  // namespace

int run(std::ostream& out) {
  out << "THEME: $token live references resolved against the shipped theme.json, and the "
         "light/dark runtime switch (design.md section 5.7, P3's own acceptance bar).\n\n";

  dg::TreeSpec spec;
  spec.viewport = theme_scene::kDemoViewport;
  spec.background.fill = Color::from_argb(0xFF14171C);
  theme_scene::Scene scene = theme_scene::build(spec);

  if (!scene.tree.diagnostics().empty()) {
    out << "  FAIL: the demo scene produced a layout diagnostic:\n";
    for (const std::string& line : scene.tree.diagnostics()) {
      out << "    " << line << "\n";
    }
    return 1;
  }

  bool ok = true;

  ok = check_variant(scene, out) && ok;
  out << "  light variant oracle: " << (ok ? "PASS" : "FAIL") << "\n";

  const std::size_t unresolved_to_dark = theme_scene::switch_variant(scene);
  const dg::LayoutStats after_dark_switch = scene.tree.layout();
  const bool dark_ok = check_variant(scene, out);
  ok = dark_ok && ok;
  out << "  dark variant oracle (after switching): " << (dark_ok ? "PASS" : "FAIL") << "\n";
  out << "  switch light->dark: " << unresolved_to_dark << " unresolved binding(s)\n";
  out << "  LayoutStats after a colour-only variant switch: nodes_visited="
      << after_dark_switch.nodes_visited
      << " nodes_relaid_out=" << after_dark_switch.nodes_relaid_out << "\n";
  if (after_dark_switch.nodes_visited != 0 || after_dark_switch.nodes_relaid_out != 0) {
    out << "  FAIL: a colour-only theme switch was expected to cost zero relayout\n";
    ok = false;
  }

  const std::size_t unresolved_to_light = theme_scene::switch_variant(scene);
  const bool light_again_ok = check_variant(scene, out);
  ok = light_again_ok && ok;
  out << "  light variant oracle (switched back): " << (light_again_ok ? "PASS" : "FAIL")
      << "\n";
  out << "  switch dark->light: " << unresolved_to_light << " unresolved binding(s)\n";

  out << (ok ? "\nPASS\n" : "\nFAIL\n");
  return ok ? 0 : 1;
}

}  // namespace theme_check
