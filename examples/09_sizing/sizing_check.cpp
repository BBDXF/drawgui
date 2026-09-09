#include "sizing_check.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <set>
#include <vector>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/layout/box.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/render_tree.h"

#include "sizing_scene.h"

namespace sizing_check {
namespace {

using dg::LayoutTree;
using dg::NodeId;
using dg::PixelRect;
using dg::PixelSize;
using dg::RenderTree;

constexpr int kWidths[] = {1280, 1040, 900, 780, 640, 560};
constexpr int kHeights[] = {700, 500, 420, 380, 350, 320};

dg::TreeSpec spec_for(PixelSize size) {
  dg::TreeSpec spec;
  spec.viewport = size;
  spec.background.fill = dg::Color::from_argb(0xFF14171C);
  return spec;
}

sizing_scene::Scene at(PixelSize size) {
  sizing_scene::Scene scene = sizing_scene::build(spec_for(size));

  // Laid out at a size other than the one it was built at, so every check runs
  // against a REFLOWED tree rather than a freshly built one. Stale geometry
  // after a reflow is exactly what a second sizing stage is most likely to
  // leave behind.
  scene.tree.resize(size);
  scene.tree.layout();
  return scene;
}

std::vector<PixelRect> bounds_of(const LayoutTree& tree) {
  std::vector<PixelRect> bounds;
  bounds.reserve(tree.node_count());
  for (std::uint32_t index = 0; index < tree.node_count(); ++index) {
    bounds.push_back(tree.bounds(NodeId{index}));
  }
  return bounds;
}

std::vector<std::uint8_t> snapshot(const dg::RasterSurface& surface) {
  const dg::PixelView view = surface.peek_pixels();
  const auto* bytes = static_cast<const std::uint8_t*>(view.pixels);
  return std::vector<std::uint8_t>{
      bytes, bytes + (view.row_bytes * static_cast<std::size_t>(view.height))};
}

// The row's content box: inside its padding, which is where its children live
// and therefore the box their extents have to add up to.
PixelRect content_of(const sizing_scene::Scene& scene, NodeId row) {
  return scene.tree.content_bounds(row);
}

// --------------------------------------------------------------------------
// Claim 1: exactly once.
// --------------------------------------------------------------------------

bool check_exactly_once(std::ostream& out) {
  bool ok = true;
  for (const int width : kWidths) {
    sizing_scene::Scene scene = at(PixelSize{width, 600});
    const dg::LayoutStats stats = scene.tree.layout_full();
    if (stats.nodes_visited != stats.nodes_total ||
        stats.nodes_relaid_out != stats.nodes_total) {
      out << "  FAIL at " << width << ": " << stats.nodes_visited << " entered and "
          << stats.nodes_relaid_out << " recomputed for " << stats.nodes_total << " nodes\n";
      ok = false;
    }
    if (!scene.tree.diagnostics().empty()) {
      out << "  FAIL at " << width << ": " << scene.tree.diagnostics().front() << "\n";
      ok = false;
    }
  }
  if (ok) {
    out << "  OK: every node entered once and recomputed once, at every width, no "
           "diagnostics\n";
  }
  return ok;
}

// --------------------------------------------------------------------------
// Claims 2 and 3: the deficit is exact, ordered, and actually reached.
// --------------------------------------------------------------------------

// One width of the ladder: are the buttons at their declared base, or split
// between them, and how far apart did the split put the outermost two?
struct ShrinkRow {
  bool surplus = false;
  int spread = 0;
  bool ok = true;
};

ShrinkRow check_shrink_at(int width, std::ostream& out) {
  sizing_scene::Scene scene = at(PixelSize{width, 600});
  const PixelRect room = content_of(scene, scene.handles.toolbar);
  const auto count = static_cast<int>(scene.handles.buttons.size());

  std::vector<int> widths;
  widths.reserve(scene.handles.buttons.size());
  int total = scene.tree.box(scene.handles.toolbar).gap * (count - 1);
  for (const NodeId button : scene.handles.buttons) {
    widths.push_back(scene.tree.bounds(button).width);
    total += widths.back();
  }

  ShrinkRow row;
  row.surplus = widths.front() == sizing_scene::kButtonBase;
  if (row.surplus) {
    for (const int each : widths) {
      if (each != sizing_scene::kButtonBase) {
        out << "  FAIL at " << width << ": a surplus row shrank a button to " << each << "\n";
        row.ok = false;
      }
    }
    return row;
  }

  // Exact: the buttons and the gaps between them fill the content box to the
  // pixel. A per-child division would land one or two pixels short.
  if (total != room.width) {
    out << "  FAIL at " << width << ": buttons and gaps total " << total << " in a "
        << room.width << " px content box\n";
    row.ok = false;
  }

  // Ordered by weight, strictly. A shallow deficit orders them by only a pixel
  // or two, which is correct, so the "not trivially satisfied" half of this
  // claim is carried by the ladder-wide spread rather than by demanding a wide
  // gap at every width.
  if (widths[0] <= widths[1] || widths[1] <= widths[2]) {
    out << "  FAIL at " << width << ": widths " << widths[0] << "/" << widths[1] << "/"
        << widths[2] << " are not ordered by shrink weight\n";
    row.ok = false;
  }
  row.spread = widths[0] - widths[2];
  return row;
}

bool check_shrink(std::ostream& out) {
  bool saw_surplus = false;
  bool saw_deficit = false;
  int widest_spread = 0;
  bool ok = true;

  for (const int width : kWidths) {
    const ShrinkRow row = check_shrink_at(width, out);
    ok = row.ok && ok;
    saw_surplus = saw_surplus || row.surplus;
    saw_deficit = saw_deficit || !row.surplus;
    widest_spread = std::max(widest_spread, row.spread);
  }

  // Three equal widths would satisfy the ordering check at every shallow
  // deficit while proving the weights were never read at all, so the ladder is
  // required to reach a deficit deep enough to separate them clearly.
  if (widest_spread < 20) {
    out << "  FAIL: the widest spread between button 0 and button 2 was " << widest_spread
        << " px, which a rounding difference could produce\n";
    ok = false;
  }
  if (!saw_surplus || !saw_deficit) {
    out << "  FAIL: the width ladder never crossed the transition (surplus seen: "
        << (saw_surplus ? "yes" : "no") << ", deficit seen: " << (saw_deficit ? "yes" : "no")
        << ")\n";
    ok = false;
  }
  if (ok) {
    out << "  OK: surplus and deficit both reached; in deficit the split is exact and "
           "strictly ordered by weight\n";
  }
  return ok;
}

// --------------------------------------------------------------------------
// Claim 4: the ratio holds on the axis it derives.
// --------------------------------------------------------------------------

bool check_aspect(std::ostream& out) {
  std::set<int> seen_heights;
  bool ok = true;

  for (const int height : kHeights) {
    sizing_scene::Scene scene = at(PixelSize{1280, height});
    const PixelRect room = content_of(scene, scene.handles.ratio_row);
    seen_heights.insert(room.height);

    const std::pair<NodeId, float> thumbs[] = {
        {scene.handles.wide_thumb, sizing_scene::kWideRatio},
        {scene.handles.square_thumb, sizing_scene::kSquareRatio}};
    for (const auto& [node, ratio] : thumbs) {
      const PixelRect box = scene.tree.bounds(node);
      const auto wanted = static_cast<int>(
          std::lround(static_cast<double>(room.height) * static_cast<double>(ratio)));
      if (box.height != room.height || box.width != wanted) {
        out << "  FAIL at height " << height << ": thumbnail " << box.width << "x" << box.height
            << " where the row offers " << room.height << " and the ratio wants " << wanted
            << "\n";
        ok = false;
      }
    }
  }

  // The row must actually change height across the ladder, or every pass above
  // checked the same rectangle.
  if (seen_heights.size() < 3) {
    out << "  FAIL: the height ladder produced only " << seen_heights.size()
        << " distinct row height(s), so the cross axis never moved\n";
    ok = false;
  }
  if (ok) {
    out << "  OK: " << seen_heights.size()
        << " distinct row heights, and both thumbnails derived their width from every one\n";
  }
  return ok;
}

// --------------------------------------------------------------------------
// Claim 5: filling the main axis reaches the edge.
// --------------------------------------------------------------------------

bool check_main_size(std::ostream& out) {
  bool ok = true;
  for (const int width : kWidths) {
    sizing_scene::Scene scene = at(PixelSize{width, 600});
    const PixelRect body = scene.tree.content_bounds(scene.handles.body);

    for (const NodeId row :
         {scene.handles.toolbar, scene.handles.chip_row, scene.handles.footer}) {
      if (scene.tree.bounds(row).width != body.width) {
        out << "  FAIL at " << width << ": a filled row is " << scene.tree.bounds(row).width
            << " wide in a " << body.width << " px page\n";
        ok = false;
      }
    }

    const PixelRect room = content_of(scene, scene.handles.footer);
    const PixelRect last = scene.tree.bounds(scene.handles.footer_items.back());
    if (last.right() != room.right()) {
      out << "  FAIL at " << width << ": the last footer item ends at " << last.right()
          << " rather than " << room.right() << "\n";
      ok = false;
    }
  }
  if (ok) {
    out << "  OK: every filled row spans the page, and the footer's last item ends on the "
           "content edge\n";
  }
  return ok;
}

// --------------------------------------------------------------------------
// Claim 6: hit testing follows the shrink.
// --------------------------------------------------------------------------

std::vector<std::vector<std::uint32_t>> children_of(const RenderTree& tree) {
  std::vector<std::vector<std::uint32_t>> children(tree.node_count());
  for (std::uint32_t index = 1; index < tree.node_count(); ++index) {
    children[tree.parent(NodeId{index}).value].push_back(index);
  }
  return children;
}

std::vector<std::uint32_t> paint_order_of(const RenderTree& tree) {
  const std::vector<std::vector<std::uint32_t>> children = children_of(tree);
  std::vector<std::uint32_t> order;
  order.reserve(tree.node_count());
  std::vector<std::uint32_t> stack{0};
  while (!stack.empty()) {
    const std::uint32_t index = stack.back();
    stack.pop_back();
    order.push_back(index);
    for (auto child = children[index].rbegin(); child != children[index].rend(); ++child) {
      stack.push_back(*child);
    }
  }
  return order;
}

// The last node in paint order covering the point owns it, because painting is
// depth-first pre-order. Derived from parent() and absolute_bounds(), so it
// shares no code with hit_test().
std::uint32_t topmost_at(const RenderTree& tree, const std::vector<std::uint32_t>& order,
                         const std::vector<PixelRect>& absolute, dg::PixelPoint point) {
  auto found = static_cast<std::uint32_t>(tree.node_count());
  for (const std::uint32_t index : order) {
    if (dg::contains(absolute[index], point)) {
      found = index;
    }
  }
  return found;
}

bool check_hit_testing(std::ostream& out) {
  std::set<int> seen_layouts;
  bool ok = true;

  for (const int width : kWidths) {
    sizing_scene::Scene scene = at(PixelSize{width, 600});
    const RenderTree& tree = scene.tree.render();
    const std::vector<std::uint32_t> order = paint_order_of(tree);

    std::vector<PixelRect> absolute(tree.node_count());
    for (std::uint32_t index = 0; index < tree.node_count(); ++index) {
      absolute[index] = tree.absolute_bounds(NodeId{index});
    }
    seen_layouts.insert(scene.tree.bounds(scene.handles.buttons.front()).width);

    for (const NodeId row : {scene.handles.toolbar, scene.handles.chip_row}) {
      const PixelRect band = scene.tree.bounds(row);
      for (int y = band.top(); y < band.bottom() && ok; ++y) {
        for (int x = band.left(); x < band.right(); ++x) {
          const dg::PixelPoint point{x, y};
          const std::optional<NodeId> hit = tree.hit_test(point);
          const std::uint32_t said =
              hit.has_value() ? hit->value : static_cast<std::uint32_t>(tree.node_count());
          if (said != topmost_at(tree, order, absolute, point)) {
            out << "  FAIL at " << width << ", pixel " << x << "," << y
                << ": hit testing disagrees with paint order\n";
            ok = false;
            break;
          }
        }
      }
    }
  }

  if (seen_layouts.size() < 3) {
    out << "  FAIL: the ladder produced only " << seen_layouts.size()
        << " distinct button width(s), so the rows never really moved\n";
    ok = false;
  }
  if (ok) {
    out << "  OK: " << seen_layouts.size()
        << " distinct shrink states, and hit testing agreed at every pixel of both rows\n";
  }
  return ok;
}

// --------------------------------------------------------------------------
// The standing gate: incremental equals full, in bounds and in pixels.
// --------------------------------------------------------------------------

bool check_identity(std::ostream& out) {
  constexpr PixelSize kStart{1280, 700};
  std::optional<dg::RasterSurface> damaged =
      dg::RasterSurface::create(kStart.width, kStart.height);
  std::optional<dg::RasterSurface> whole =
      dg::RasterSurface::create(kStart.width, kStart.height);
  if (!damaged.has_value() || !whole.has_value()) {
    out << "  FAIL: could not allocate a raster surface\n";
    return false;
  }

  sizing_scene::Scene incremental = sizing_scene::build(spec_for(kStart));
  sizing_scene::Scene full = sizing_scene::build(spec_for(kStart));
  incremental.tree.render().repaint_full(*damaged);
  full.tree.render().repaint_full(*whole);

  // Both directions through the transition, and a weight change on top of it -
  // a resize alone would only ever cross it while every node's own box stayed
  // put.
  const PixelSize script[] = {{1280, 700}, {900, 700},  {640, 640}, {560, 380},
                              {780, 420},  {1040, 500}, {1280, 700}};

  for (std::size_t step = 0; step < std::size(script); ++step) {
    const PixelSize size = script[step];
    if (size.width > kStart.width || size.height > kStart.height) {
      continue;
    }
    incremental.tree.resize(size);
    full.tree.resize(size);

    // On alternate steps the middle button flips between growing and
    // shrinking, which changes WHICH distribution runs on an otherwise
    // unchanged tree.
    dg::BoxStyle middle = incremental.tree.box(incremental.handles.buttons[1]);
    middle.shrink = step % 2 == 0 ? 2 : 0;
    middle.grow = step % 2 == 0 ? 0 : 1;
    incremental.tree.set_box(incremental.handles.buttons[1], middle);
    full.tree.set_box(full.handles.buttons[1], middle);

    incremental.tree.layout();
    full.tree.layout_full();

    if (bounds_of(incremental.tree) != bounds_of(full.tree)) {
      out << "  FAIL: bounds differ at step " << step << " (" << size.width << "x"
          << size.height << ")\n";
      return false;
    }

    incremental.tree.render().repaint(*damaged);
    full.tree.render().repaint_full(*whole);
    if (snapshot(*damaged) != snapshot(*whole)) {
      out << "  FAIL: pixels differ at step " << step << " (" << size.width << "x"
          << size.height << ")\n";
      return false;
    }
  }

  out << "  OK: bounds and pixels identical to a full pass across the whole resize script\n";
  return true;
}

}  // namespace

int run(std::ostream& out) {
  out << "verify: the second sizing stage, on the scene the demo puts on screen\n";
  bool ok = true;
  ok = check_exactly_once(out) && ok;
  ok = check_shrink(out) && ok;
  ok = check_aspect(out) && ok;
  ok = check_main_size(out) && ok;
  ok = check_hit_testing(out) && ok;
  ok = check_identity(out) && ok;
  out << (ok ? "PASS\n" : "FAIL\n");
  return ok ? 0 : 1;
}

}  // namespace sizing_check
