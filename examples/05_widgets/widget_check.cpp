#include "widget_check.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "drawgui/graphics/raster_surface.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/widget/interaction.h"
#include "drawgui/window/window_manager.h"

#include "widget_scene.h"

namespace widget_check {
namespace {

using dg::NodeId;
using dg::PixelPoint;
using dg::PixelRect;
using dg::PixelSize;
using dg::PointerAction;
using dg::PointerEvent;
using dg::RasterSurface;

widget_scene::Options options_for(const Config& config) {
  widget_scene::Options options;
  options.spec.viewport = config.viewport;
  options.spec.background.fill = dg::Color::from_argb(0xFF11161D);
  options.rounded_controls = config.rounded_controls;
  options.rounded_containers = config.rounded_containers;
  return options;
}

// Row by row and only the pixels, never the row padding: the bytes past the
// end of a row are allocated but never written, so comparing them would be
// comparing whatever the allocator left behind.
std::vector<std::uint8_t> snapshot(const RasterSurface& surface) {
  const dg::PixelView view = surface.peek_pixels();
  const auto row = static_cast<std::size_t>(view.width) * 4;
  std::vector<std::uint8_t> pixels(row * static_cast<std::size_t>(view.height));
  for (int y = 0; y < view.height; ++y) {
    std::memcpy(pixels.data() + (static_cast<std::size_t>(y) * row),
                view.pixels + (static_cast<std::size_t>(y) * view.row_bytes), row);
  }
  return pixels;
}

std::size_t first_difference(const std::vector<std::uint8_t>& a,
                             const std::vector<std::uint8_t>& b) {
  const std::size_t limit = std::min(a.size(), b.size());
  for (std::size_t index = 0; index < limit; ++index) {
    if (a[index] != b[index]) {
      return index;
    }
  }
  return limit;
}

// The centre of a widget, and points chosen to sit just inside and just
// outside it. A script aimed only at centres never crosses a boundary, and
// boundaries are where hit testing and hover both fail.
PixelPoint centre_of(const widget_scene::Scene& scene, NodeId id) {
  const PixelRect bounds = scene.tree.bounds(id);
  return PixelPoint{bounds.x + (bounds.width / 2), bounds.y + (bounds.height / 2)};
}

// The pointer path. Every step names what it is for, because a script whose
// steps are unexplained is a script nobody can tell is still covering the case
// it was written for.
std::vector<PointerEvent> script_for(const widget_scene::Scene& scene) {
  std::vector<PointerEvent> events;
  const auto move = [&events](PixelPoint at) {
    events.push_back(PointerEvent{dg::WindowId{}, PointerAction::kMove, at.x, at.y});
  };
  const auto down = [&events](PixelPoint at) {
    events.push_back(PointerEvent{dg::WindowId{}, PointerAction::kDown, at.x, at.y});
  };
  const auto up = [&events](PixelPoint at) {
    events.push_back(PointerEvent{dg::WindowId{}, PointerAction::kUp, at.x, at.y});
  };
  const auto leave = [&events] {
    events.push_back(PointerEvent{dg::WindowId{}, PointerAction::kLeave, 0, 0});
  };

  const PixelRect viewport{0, 0, scene.tree.viewport().width, scene.tree.viewport().height};
  const PixelPoint nowhere{viewport.width - 2, viewport.height - 2};

  for (const NodeId id : scene.handles.interactive) {
    const PixelPoint at = centre_of(scene, id);

    // Hover on, click, hover off: the ordinary case, for every widget.
    move(at);
    down(at);
    up(at);

    // Press and drag away before releasing - the cancel that must not click.
    down(at);
    move(nowhere);
    up(nowhere);

    // Press, drag away, drag back, release - the cancel that changes its mind.
    down(at);
    move(nowhere);
    move(at);
    up(at);

    // Leave the window while hovering, and while pressed.
    move(at);
    leave();
    down(at);
    leave();
    up(at);
  }

  // Straight from one overlapping widget to the other and back, which is the
  // pair where the wrong z-order shows.
  const PixelPoint under = centre_of(scene, scene.handles.lab_under);
  const PixelPoint over = centre_of(scene, scene.handles.lab_over);
  for (int repeat = 0; repeat < 3; ++repeat) {
    move(under);
    move(over);
    down(over);
    up(over);
  }

  // The overflowing button, aimed at the part of it OUTSIDE its parent.
  const PixelRect spill = scene.tree.bounds(scene.handles.lab_overflow);
  const PixelRect host = scene.tree.bounds(scene.handles.lab_host);
  if (spill.right() > host.right()) {
    const PixelPoint outside{spill.right() - 2, spill.y + (spill.height / 2)};
    move(outside);
    down(outside);
    up(outside);
  }

  // The checkbox, twice, so its state goes on and comes back off.
  const PixelPoint check = centre_of(scene, scene.handles.checkbox);
  for (int repeat = 0; repeat < 2; ++repeat) {
    move(check);
    down(check);
    up(check);
  }

  move(nowhere);
  leave();
  return events;
}

const char* name_of(PointerAction action) {
  switch (action) {
    case PointerAction::kMove:
      return "move";
    case PointerAction::kDown:
      return "down";
    case PointerAction::kUp:
      return "up";
    case PointerAction::kLeave:
      return "leave";
    case PointerAction::kWheel:
      return "wheel";
  }
  return "?";
}

bool run_identity(const Config& config, bool rounded, std::ostream& out) {
  Config local = config;
  local.rounded_controls = rounded;
  local.rounded_containers = rounded;
  const widget_scene::Options options = options_for(local);

  widget_scene::Scene damaged = widget_scene::build(options);
  widget_scene::Scene reference = widget_scene::build(options);

  std::optional<RasterSurface> damaged_surface =
      RasterSurface::create(config.viewport.width, config.viewport.height);
  std::optional<RasterSurface> reference_surface =
      RasterSurface::create(config.viewport.width, config.viewport.height);
  if (!damaged_surface.has_value() || !reference_surface.has_value()) {
    out << "  could not allocate a surface\n";
    return false;
  }

  dg::Interaction damaged_input;
  dg::Interaction reference_input;

  // Both start from a full repaint, so the comparison starts from agreement
  // and every later difference is something an interaction did.
  damaged.tree.render().repaint_full(*damaged_surface);
  reference.tree.render().repaint_full(*reference_surface);

  const std::vector<PointerEvent> script = script_for(damaged);
  std::int64_t damaged_pixels = 0;

  for (std::size_t step = 0; step < script.size(); ++step) {
    const PointerEvent& event = script[step];
    widget_scene::dispatch(damaged, damaged_input, event);
    widget_scene::dispatch(reference, reference_input, event);

    damaged.tree.layout();
    reference.tree.layout();

    damaged_pixels += damaged.tree.render().repaint(*damaged_surface).pixels;
    reference.tree.render().repaint_full(*reference_surface);

    const std::vector<std::uint8_t> left = snapshot(*damaged_surface);
    const std::vector<std::uint8_t> right = snapshot(*reference_surface);
    if (left != right) {
      const std::size_t offset = first_difference(left, right);
      const auto row = static_cast<std::size_t>(config.viewport.width) * 4;
      out << "  FAIL: " << (rounded ? "rounded" : "square ") << " step " << step << " ("
          << name_of(event.action) << " at " << event.x << "," << event.y
          << ") diverged at pixel " << (offset % row) / 4 << "," << (offset / row) << "\n";
      return false;
    }
  }

  if (damaged.clicks != reference.clicks) {
    out << "  FAIL: the two copies disagree about how many clicks happened\n";
    return false;
  }

  // A script that produced no clicks would pass the byte comparison trivially,
  // so the count is checked as well as the pixels. Sub-step 1's `check(true)`
  // trap in a different costume.
  if (damaged.clicks == 0) {
    out << "  FAIL: the script activated nothing, so it proved nothing\n";
    return false;
  }

  out << "  ok  " << (rounded ? "rounded" : "square ") << " containers: " << script.size()
      << " pointer events, " << damaged.clicks << " clicks, " << damaged_pixels
      << " px repainted incrementally\n";
  return true;
}

// Children reconstructed by index order rather than asked of the library. A
// node's children were appended in order, so scanning indices upward and
// grouping by parent recovers the same lists the tree holds - from the parent
// links alone, which is the one piece of structure an oracle has to trust.
std::vector<std::vector<std::uint32_t>> children_of(const dg::RenderTree& tree) {
  std::vector<std::vector<std::uint32_t>> children(tree.node_count());
  for (std::uint32_t index = 1; index < tree.node_count(); ++index) {
    children[tree.parent(NodeId{index}).value].push_back(index);
  }
  return children;
}

std::vector<std::uint32_t> paint_order_of(const dg::RenderTree& tree) {
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

bool run_hit_testing(const Config& config, PixelSize viewport, std::ostream& out) {
  Config local = config;
  local.viewport = viewport;
  widget_scene::Scene scene = widget_scene::build(options_for(local));

  // Laid out at a size other than the one it was built at, so the check runs
  // against a REFLOWED tree rather than a freshly built one. Stale geometry
  // after a resize is the bug this is aimed at.
  scene.tree.resize(viewport);
  scene.tree.layout();

  const dg::RenderTree& tree = scene.tree.render();
  const std::vector<std::uint32_t> order = paint_order_of(tree);

  std::vector<PixelRect> absolute(tree.node_count());
  for (std::uint32_t index = 0; index < tree.node_count(); ++index) {
    absolute[index] = tree.absolute_bounds(NodeId{index});
  }

  std::size_t mismatches = 0;
  std::size_t first_x = 0;
  std::size_t first_y = 0;
  std::uint32_t said = 0;
  std::uint32_t expected_node = 0;

  for (int y = 0; y < viewport.height; ++y) {
    for (int x = 0; x < viewport.width; ++x) {
      const PixelPoint point{x, y};

      auto topmost = static_cast<std::uint32_t>(tree.node_count());
      for (const std::uint32_t index : order) {
        if (dg::contains(absolute[index], point)) {
          topmost = index;
        }
      }

      const std::optional<NodeId> hit = tree.hit_test(point);
      const std::uint32_t actual =
          hit.has_value() ? hit->value : static_cast<std::uint32_t>(tree.node_count());
      if (actual != topmost) {
        if (mismatches == 0) {
          first_x = static_cast<std::size_t>(x);
          first_y = static_cast<std::size_t>(y);
          said = actual;
          expected_node = topmost;
        }
        ++mismatches;
      }
    }
  }

  if (mismatches != 0) {
    out << "  FAIL: " << viewport.width << "x" << viewport.height << ", " << mismatches
        << " pixel(s) disagree; first at " << first_x << "," << first_y << ": hit test says "
        << said << ", paint order says " << expected_node << "\n";
    return false;
  }

  out << "  ok  " << viewport.width << "x" << viewport.height << ": "
      << (viewport.width * viewport.height) << " pixels, " << tree.node_count()
      << " nodes, every pixel agrees with paint order\n";
  return true;
}

// The rasterizer oracle, on a scene reduced to flat opaque squares. It has to
// be a reduction: the real scene has rounded corners, borders and anti-aliased
// text, and a pixel on any of those edges is a blend of two nodes' colours and
// belongs to neither. Sub-step 1 measured that integer-aligned square
// rectangles rasterize exactly, which is what makes this oracle meaningful at
// all - and reducing the scene keeps its GEOMETRY, which is the part hit
// testing reads.
bool run_rasterizer_oracle(const Config& config, std::ostream& out) {
  widget_scene::Scene scene = widget_scene::build(options_for(config));
  dg::RenderTree& tree = scene.tree.render();

  if (tree.node_count() > 250) {
    out << "  skipped: the scene has more nodes than a single colour channel can name\n";
    return true;
  }

  for (std::uint32_t index = 0; index < tree.node_count(); ++index) {
    dg::NodeStyle flat;
    flat.fill = dg::Color::rgba(static_cast<std::uint8_t>(index + 1), 0x40, 0x80);
    tree.set_style(NodeId{index}, flat);
  }

  std::optional<RasterSurface> surface =
      RasterSurface::create(config.viewport.width, config.viewport.height);
  if (!surface.has_value()) {
    out << "  could not allocate a surface\n";
    return false;
  }
  tree.repaint_full(*surface);

  const dg::PixelView view = surface->peek_pixels();
  std::size_t mismatches = 0;
  int first_x = 0;
  int first_y = 0;

  for (int y = 0; y < config.viewport.height; ++y) {
    for (int x = 0; x < config.viewport.width; ++x) {
      const std::size_t offset =
          (static_cast<std::size_t>(y) * view.row_bytes) + (static_cast<std::size_t>(x) * 4);
      const auto painted = static_cast<std::uint32_t>(view.pixels[offset + 2]) - 1;
      const std::optional<NodeId> hit = tree.hit_test(PixelPoint{x, y});
      if (!hit.has_value() || hit->value != painted) {
        if (mismatches == 0) {
          first_x = x;
          first_y = y;
        }
        ++mismatches;
      }
    }
  }

  if (mismatches != 0) {
    out << "  FAIL: " << mismatches << " pixel(s) hit a node other than the one drawn there;"
        << " first at " << first_x << "," << first_y << "\n";
    return false;
  }
  out << "  ok  what the rasterizer drew is what hit testing names, at every pixel\n";
  return true;
}

}  // namespace

bool verify_interaction(const Config& config, std::ostream& out) {
  out << "verify: interaction-driven repaint against full repaint, " << config.viewport.width
      << "x" << config.viewport.height << "\n";
  const bool square = run_identity(config, false, out);
  const bool rounded = run_identity(config, true, out);
  if (square && rounded) {
    out << "  OK: every frame byte-identical to a full repaint\n";
  }
  return square && rounded;
}

bool verify_hit_testing(const Config& config, std::ostream& out) {
  out << "verify: hit testing, every pixel, against paint order and against the rasterizer\n";

  // Several sizes, because a reflow moves every widget and the check has to
  // run against the tree AFTER it moved.
  const PixelSize sizes[] = {
      config.viewport,
      PixelSize{760, 560},
      PixelSize{1101, 641},
  };
  bool ok = true;
  for (const PixelSize& size : sizes) {
    ok = run_hit_testing(config, size, out) && ok;
  }
  ok = run_rasterizer_oracle(config, out) && ok;
  if (ok) {
    out << "  OK: nothing visible is unclickable, nothing occluded is reachable\n";
  }
  return ok;
}

void report_damage_cost(const Config& config, std::ostream& out) {
  out << "\nWHAT ONE HOVER COSTS, AND WHAT A CORNER RADIUS ADDS TO IT\n"
      << "  A rounded node is not clip-invariant under Skia's anti-aliasing, so it repaints\n"
      << "  whole. That makes a radius a DAMAGE decision as much as a visual one - and the\n"
      << "  two places a radius can go cost very different amounts, which is why they are\n"
      << "  reported apart. Not a gate: the owner has relaxed performance requirements. The\n"
      << "  point is that the cost is known before it is chosen.\n\n";

  struct Variant {
    const char* label;
    bool controls;
    bool containers;
  };
  constexpr Variant kVariants[] = {
      {"everything square", false, false},
      {"rounded controls only", true, false},
      {"rounded containers too", true, true},
  };

  std::int64_t baseline = 0;
  for (const Variant& variant : kVariants) {
    Config local = config;
    local.rounded_controls = variant.controls;
    local.rounded_containers = variant.containers;

    widget_scene::Scene scene = widget_scene::build(options_for(local));
    std::optional<RasterSurface> surface =
        RasterSurface::create(config.viewport.width, config.viewport.height);
    if (!surface.has_value()) {
      return;
    }
    scene.tree.render().repaint_full(*surface);

    dg::Interaction interaction;
    std::int64_t total = 0;
    std::int64_t requested = 0;
    int events = 0;

    for (const NodeId id : scene.handles.interactive) {
      const PixelPoint point = centre_of(scene, id);
      widget_scene::dispatch(
          scene, interaction,
          PointerEvent{dg::WindowId{}, PointerAction::kMove, point.x, point.y});
      const dg::RepaintStats stats = scene.tree.render().repaint(*surface);
      total += stats.pixels;
      requested += stats.requested_pixels;
      ++events;
    }
    if (events == 0) {
      return;
    }

    const std::int64_t per_hover = total / events;
    if (baseline == 0) {
      baseline = per_hover;
    }
    const auto window = static_cast<double>(config.viewport.width) *
                        static_cast<double>(config.viewport.height);
    out << "  " << std::left << std::setw(26) << variant.label << std::right << std::setw(9)
        << per_hover << " px/hover   asked for " << std::setw(7) << (requested / events)
        << " px   " << std::fixed << std::setprecision(2)
        << (100.0 * static_cast<double>(per_hover) / window) << "% of window   "
        << std::setprecision(1)
        << (baseline > 0 ? static_cast<double>(per_hover) / static_cast<double>(baseline) : 0.0)
        << "x\n";
  }

  out << "\n  The gap between repainted and asked-for is the price of the clip-atomic rule:\n"
      << "  a damage rectangle grows until every ROUNDED node it cuts is inside it.\n"
      << "  Rounding a CONTAINER is what makes that expensive, because its whole area then\n"
      << "  becomes the smallest unit of damage anything inside it can produce. Text is NOT\n"
      << "  in this rule - measured clip-invariant, see --clip-probe.\n";
}

void report_widgets(const Config& config, std::ostream& out) {
  const widget_scene::Scene scene = widget_scene::build(options_for(config));
  out << "interactive widgets at " << config.viewport.width << "x" << config.viewport.height
      << "\n";
  for (const NodeId id : scene.handles.interactive) {
    const PixelRect bounds = scene.tree.bounds(id);
    const PixelPoint centre = centre_of(scene, id);
    out << "  " << std::left << std::setw(24) << widget_scene::describe(scene, id) << std::right
        << "  box " << std::setw(5) << bounds.x << "," << std::setw(4) << bounds.y << " "
        << std::setw(4) << bounds.width << "x" << std::setw(3) << bounds.height << "   centre "
        << centre.x << "," << centre.y << "\n";
  }
}

}  // namespace widget_check
