#include "image_check.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "drawgui/base/expected.h"
#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/image_catalog.h"
#include "drawgui/render/render_tree.h"

#include "image_scene.h"

namespace image_check {
namespace {

using dg::Color;
using dg::LayoutStats;
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

std::uint32_t solid(Color color) {
  return color.argb();
}

bool check_pixel(const dg::PixelView& view, int x, int y, Color expected, const char* what,
                 std::ostream& out, bool& ok) {
  const std::uint32_t got = pixel_at(view, x, y);
  const std::uint32_t want = solid(expected);
  if (got != want) {
    out << "  FAIL " << what << " at " << x << "," << y << ": got " << std::hex << got
        << " expected " << want << std::dec << "\n";
    ok = false;
    return false;
  }
  return true;
}

}  // namespace

int run(std::ostream& out) {
  out << "IMAGE: fit-mode pixels against a hand-derived oracle, and the swap-does-not-\n"
         "  relayout property, on the exact scene the demo window draws.\n\n";

  dg::TreeSpec spec;
  spec.viewport = image_scene::kDemoViewport;
  spec.background.fill = Color::from_argb(0xFF14171C);
  image_scene::Scene scene = image_scene::build(spec);

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

  // fill: 192x96 against a 64x64 source, stretched non-uniformly (3x, 1.5x).
  // Every one of the four quadrants lands exactly on the corresponding
  // quarter of the panel, so a point 10px in from each edge samples it.
  {
    const PixelRect box = scene.tree.bounds(scene.handles.fill_panel);
    check_pixel(view, box.left() + 10, box.top() + 10, image_scene::kTopLeft, "fill TL", out,
                ok);
    check_pixel(view, box.right() - 10, box.top() + 10, image_scene::kTopRight, "fill TR", out,
                ok);
    check_pixel(view, box.left() + 10, box.bottom() - 10, image_scene::kBottomLeft, "fill BL",
                out, ok);
    check_pixel(view, box.right() - 10, box.bottom() - 10, image_scene::kBottomRight, "fill BR",
                out, ok);
  }

  // contain: 96x192 (tall). scale = min(96/64, 192/64) = 1.5, so the scaled
  // image is 96x96, letterboxed with 48px of frame fill above and below.
  {
    const PixelRect box = scene.tree.bounds(scene.handles.contain_panel);
    check_pixel(view, box.left() + 10, box.top() + 15, image_scene::kFrameFill,
                "contain top letterbox", out, ok);
    check_pixel(view, box.left() + 15, box.top() + 70, image_scene::kTopLeft, "contain TL", out,
                ok);
    check_pixel(view, box.right() - 15, box.top() + 120, image_scene::kBottomRight,
                "contain BR", out, ok);
    check_pixel(view, box.left() + 10, box.top() + 177, image_scene::kFrameFill,
                "contain bottom letterbox", out, ok);
  }

  // cover: 192x96 (wide). scale = max(192/64, 96/64) = 3, so the scaled image
  // is 192x192, cropped 48px off the top and bottom - both quadrant rows
  // survive, just not their outer 48px, so the same 10px-in samples still
  // land in the quadrant the geometry predicts.
  {
    const PixelRect box = scene.tree.bounds(scene.handles.cover_panel);
    check_pixel(view, box.left() + 10, box.top() + 10, image_scene::kTopLeft, "cover TL", out,
                ok);
    check_pixel(view, box.right() - 10, box.top() + 10, image_scene::kTopRight, "cover TR", out,
                ok);
    check_pixel(view, box.left() + 10, box.bottom() - 10, image_scene::kBottomLeft, "cover BL",
                out, ok);
    check_pixel(view, box.right() - 10, box.bottom() - 10, image_scene::kBottomRight,
                "cover BR", out, ok);
  }

  // none: 160x160, the 64x64 source unscaled and centred - a 48px margin of
  // frame fill on every side, the source itself in [48, 112) on both axes.
  {
    const PixelRect box = scene.tree.bounds(scene.handles.none_panel);
    check_pixel(view, box.left() + 10, box.top() + 10, image_scene::kFrameFill, "none margin",
                out, ok);
    check_pixel(view, box.left() + 60, box.top() + 60, image_scene::kTopLeft, "none TL", out,
                ok);
    check_pixel(view, box.left() + 90, box.top() + 90, image_scene::kBottomRight, "none BR",
                out, ok);
  }

  // placeholder: no source at all, the whole 128x128 panel painted the
  // configured placeholder colour, never a hole.
  {
    const PixelRect box = scene.tree.bounds(scene.handles.placeholder_panel);
    check_pixel(view, box.left() + (box.width / 2), box.top() + (box.height / 2),
                image_scene::kPlaceholderColor, "placeholder", out, ok);
  }

  out << "  fit-mode pixel oracle: " << (ok ? "PASS" : "FAIL") << "\n";

  // THE central claim, against this exact scene rather than a scene built
  // only for a unit test: swap the decoded source for one of a wildly
  // different pixel size and show LayoutStats reports zero layout work.
  const std::vector<std::uint8_t> larger_png = image_scene::quadrants(512);
  const dg::Expected<dg::ImageId, dg::ImageError> larger =
      scene.images.decode(larger_png.data(), larger_png.size());
  if (!larger.has_value()) {
    out << "  FAIL: could not decode the swap-in source: " << larger.error().message << "\n";
    return 1;
  }

  const PixelRect before = scene.tree.bounds(scene.handles.fill_panel);
  dg::NodeStyle swapped = scene.tree.render().style(scene.handles.fill_panel);
  swapped.image.source = larger.value();
  scene.tree.render().set_image(scene.handles.fill_panel, swapped.image);

  const LayoutStats after_swap = scene.tree.layout();
  out << "  after swapping the source from " << image_scene::kSourceSize << "x"
      << image_scene::kSourceSize << " to 512x512: nodes_visited=" << after_swap.nodes_visited
      << " nodes_relaid_out=" << after_swap.nodes_relaid_out
      << " dirty_roots=" << after_swap.dirty_roots << "\n";

  const bool swap_ok = after_swap.nodes_visited == 0 && after_swap.nodes_relaid_out == 0 &&
                       after_swap.dirty_roots == 0 &&
                       scene.tree.bounds(scene.handles.fill_panel) == before;
  out << "  swap-does-not-relayout: " << (swap_ok ? "PASS" : "FAIL") << "\n";
  ok = ok && swap_ok;

  out << (ok ? "\nPASS\n" : "\nFAIL\n");
  return ok ? 0 : 1;
}

}  // namespace image_check
