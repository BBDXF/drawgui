#include "cprops_check.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <string>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/props/node_props.h"
#include "drawgui/render/render_tree.h"

#include "cprops_scene.h"

namespace cprops_check {
namespace {

using dg::Color;
using dg::PixelRect;
using dg::PropStatus;
using dg::RasterSurface;

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

// The SAME formula gradient_shader() (src/render/skia_paint.cpp) computes -
// recomputed independently here rather than read off a previous run, the
// discipline test_opacity.cpp and examples/13_image's image_check.cpp both
// already use. Channel-wise linear interpolation in the destination's own
// (sRGB, non-linearized) space, rounding to nearest - design.md section
// 5.11.3 rule 2 is what makes "no linearization" the right formula rather
// than a simplification.
Color lerp_stop(Color start, Color end, float local_t) {
  const auto channel = [local_t](std::uint8_t a, std::uint8_t b) {
    const float value =
        static_cast<float>(a) + (local_t * (static_cast<float>(b) - static_cast<float>(a)));
    return static_cast<std::uint8_t>(std::lround(value));
  };
  return Color::rgba(channel(start.red(), end.red()), channel(start.green(), end.green()),
                     channel(start.blue(), end.blue()));
}

// The gradient AXIS this scene built: angle 0, so the line spans exactly
// [box.left(), box.right()) in x, at t=0 and t=1 respectively - the
// "gradient line length" formula in src/render/skia_paint.cpp's
// gradient_shader() collapses to exactly the box width when angle_deg is 0
// (cos(0)=1, sin(0)=0). Pixel x samples at continuous position x+0.5, which
// is what makes the very first column NOT read as the pure start colour -
// measured against a real render before this formula was written down,
// exactly as this project's own convention requires.
Color expected_gradient_pixel(const PixelRect& box, int x) {
  const float t = (static_cast<float>(x) + 0.5F - static_cast<float>(box.left())) /
                  static_cast<float>(box.width);
  if (t <= 0.5F) {
    return lerp_stop(cprops_scene::kGradientStart, cprops_scene::kGradientMid, t / 0.5F);
  }
  return lerp_stop(cprops_scene::kGradientMid, cprops_scene::kGradientEnd, (t - 0.5F) / 0.5F);
}

}  // namespace

int run(std::ostream& out) {
  out << "COMPLEX PROPERTIES: the dedicated-setter channel (design.md section 5.9.5),\n"
         "  a hand-derived gradient oracle, a shadow-outside-bounds oracle, the\n"
         "  channel's image prototype, and transform's recorded decline.\n\n";

  dg::TreeSpec spec;
  spec.viewport = cprops_scene::kDemoViewport;
  spec.background.fill = Color::from_argb(0xFF14171C);
  cprops_scene::Scene scene = cprops_scene::build(spec);

  if (!scene.tree.diagnostics().empty()) {
    out << "  FAIL: the demo scene produced a layout diagnostic:\n";
    for (const std::string& line : scene.tree.diagnostics()) {
      out << "    " << line << "\n";
    }
    return 1;
  }

  std::optional<RasterSurface> surface =
      RasterSurface::create(spec.viewport.width, spec.viewport.height);
  if (!surface.has_value()) {
    out << "  could not allocate a surface\n";
    return 1;
  }
  scene.tree.render().repaint_full(*surface);
  const dg::PixelView view = surface->peek_pixels();
  if (view.pixels == nullptr || !view.is_bgra8888) {
    out << "  the surface is not the format this check reads\n";
    return 1;
  }

  bool ok = true;

  // 1. gradient - four sample columns, each computed independently.
  {
    const PixelRect box = scene.tree.bounds(scene.handles.gradient_panel);
    const int cy = box.top() + (box.height / 2);
    for (const int dx : {2, box.width / 4, box.width / 2, box.width - 3}) {
      const int x = box.left() + dx;
      check_pixel(view, x, cy, expected_gradient_pixel(box, x), "gradient", out, ok);
    }
  }
  out << "  gradient oracle: " << (ok ? "PASS" : "FAIL") << "\n";

  // 2. shadow - the sliver IS the shadow colour, and everywhere the
  // unblurred, unspread, offset rectangle does not reach IS exactly the
  // background - both facts asserted, not only the first.
  {
    const bool before = ok;
    const PixelRect box = scene.tree.bounds(scene.handles.shadow_panel);
    const int mid_y = box.top() + (box.height / 2);

    // Well inside the shifted-but-unblurred shadow rectangle, outside the
    // panel's own box: exactly the shadow colour.
    check_pixel(view, box.right() + 5, mid_y, cprops_scene::kShadowColor,
                "shadow sliver (right)", out, ok);

    // Above the panel entirely - the shadow's offset is (+x, +y), so nothing
    // reaches here, and the row itself paints nothing (NodeStyle{}): exactly
    // the root's own background.
    check_pixel(view, box.left() + 10, box.top() - 3, spec.background.fill,
                "above the shadowed panel (background)", out, ok);

    // Just past where the shifted rectangle ends, before the next panel's
    // gap closes: exactly background again.
    check_pixel(view, box.right() + cprops_scene::kShadowOffsetX + 3, mid_y,
                spec.background.fill, "past the shadow's reach (background)", out, ok);

    out << "  shadow-outside-bounds oracle: " << (before && ok ? "PASS" : "FAIL") << "\n";
  }

  // 3. image - the channel's prototype.
  {
    const PixelRect box = scene.tree.bounds(scene.handles.image_panel);
    check_pixel(view, box.left() + 10, box.top() + 10, cprops_scene::kImageTop, "image top",
                out, ok);
    check_pixel(view, box.left() + 10, box.bottom() - 10, cprops_scene::kImageBottom,
                "image bottom", out, ok);
  }
  out << "  image-via-channel oracle: " << (ok ? "PASS" : "FAIL") << "\n";

  // 4. transform - no pixels; the decline itself is the result.
  const bool transform_ok = scene.transform_result.status == PropStatus::kUnsupported;
  out << "  dg::set_transform() status: "
      << (transform_ok ? "kUnsupported (declined, as recorded)" : "UNEXPECTED") << "\n"
      << "    " << scene.transform_result.message << "\n";
  ok = ok && transform_ok;

  out << (ok ? "\nPASS\n" : "\nFAIL\n");
  return ok ? 0 : 1;
}

}  // namespace cprops_check
