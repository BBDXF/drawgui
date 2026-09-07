#include "layout_check.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/layout/box.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/render_tree.h"

#include "layout_scene.h"

namespace layout_check {
namespace {

using Clock = std::chrono::steady_clock;
using dg::LayoutStats;
using dg::LayoutTree;
using dg::NodeId;
using dg::PixelRect;

constexpr int kWarmupFrames = 12;

struct Ladder {
  const char* label;
  int width;
  int height;
};

constexpr Ladder kSizes[] = {
    {"800x600", 800, 600},
    {"1280x720", 1280, 720},
    {"1920x1080", 1920, 1080},
    {"2560x1440", 2560, 1440},
};

layout_scene::Options options_from(const Config& config) {
  layout_scene::Options options;
  options.spec.viewport = config.viewport;
  options.spec.background.fill = dg::Color::from_argb(0xFF14171C);
  options.rounded_containers = config.rounded_containers;
  return options;
}

std::vector<PixelRect> bounds_of(const LayoutTree& tree) {
  std::vector<PixelRect> bounds;
  bounds.reserve(tree.node_count());
  for (std::uint32_t index = 0; index < tree.node_count(); ++index) {
    bounds.push_back(tree.bounds(NodeId{index}));
  }
  return bounds;
}

void report_first_difference(const std::vector<PixelRect>& incremental,
                             const std::vector<PixelRect>& full, std::ostream& out) {
  if (incremental.size() != full.size()) {
    out << "    node counts differ: " << incremental.size() << " vs " << full.size() << "\n";
    return;
  }
  std::size_t differing = 0;
  std::size_t first = incremental.size();
  for (std::size_t index = 0; index < incremental.size(); ++index) {
    if (incremental[index] != full[index]) {
      if (differing == 0) {
        first = index;
      }
      ++differing;
    }
  }
  out << "    " << differing << " node(s) differ; first is node " << first << "\n";
  if (first < incremental.size()) {
    const PixelRect& a = incremental[first];
    const PixelRect& b = full[first];
    out << "      incremental " << a.x << "," << a.y << " " << a.width << "x" << a.height
        << "\n      full        " << b.x << "," << b.y << " " << b.width << "x" << b.height
        << "\n";
  }
}

double percent(std::size_t part, std::size_t whole) {
  return whole == 0 ? 0.0 : 100.0 * static_cast<double>(part) / static_cast<double>(whole);
}

struct Stats {
  double median_ms = 0.0;
  double p95_ms = 0.0;
  double worst_ms = 0.0;
};

Stats summarize(std::vector<double> samples) {
  Stats stats;
  if (samples.empty()) {
    return stats;
  }
  std::sort(samples.begin(), samples.end());
  stats.median_ms = samples[samples.size() / 2];
  stats.p95_ms =
      samples[static_cast<std::size_t>(0.95 * static_cast<double>(samples.size() - 1))];
  stats.worst_ms = samples.back();
  return stats;
}

template <typename Body>
Stats time_frames(int frames, const Body& body) {
  for (int i = 0; i < kWarmupFrames; ++i) {
    body(i);
  }
  std::vector<double> samples;
  samples.reserve(static_cast<std::size_t>(frames));
  for (int i = 0; i < frames; ++i) {
    const Clock::time_point started = Clock::now();
    body(kWarmupFrames + i);
    samples.push_back(
        std::chrono::duration<double, std::milli>(Clock::now() - started).count());
  }
  return summarize(std::move(samples));
}

}  // namespace

bool verify_layout(const Config& config, std::ostream& out) {
  const layout_scene::Options options = options_from(config);
  layout_scene::Scene incremental = layout_scene::build(options);
  layout_scene::Scene full = layout_scene::build(options);

  out << "verify: " << config.frames << " frames at " << config.viewport.width << "x"
      << config.viewport.height << ", " << incremental.tree.node_count() << " nodes, "
      << (config.rounded_containers ? "rounded" : "square") << " containers\n";

  int count = 0;
  const layout_scene::Mutation* mutations = layout_scene::all_mutations(count);

  for (int which = 0; which <= count; ++which) {
    // The last pass is the composite script, where several mutations land in
    // the same frame - the case a dirty list that assumes one root gets
    // wrong.
    const bool composite = which == count;
    const char* label =
        composite ? "every mutation together" : layout_scene::name_of(mutations[which]);

    for (int frame = 0; frame < config.frames; ++frame) {
      if (composite) {
        layout_scene::apply_frame(incremental.tree, incremental.handles, frame);
        layout_scene::apply_frame(full.tree, full.handles, frame);
      } else {
        layout_scene::apply(incremental.tree, incremental.handles, mutations[which], frame);
        layout_scene::apply(full.tree, full.handles, mutations[which], frame);
      }
      incremental.tree.layout();
      full.tree.layout_full();

      if (bounds_of(incremental.tree) != bounds_of(full.tree)) {
        out << "  FAIL: " << label << ", frame " << frame << "\n";
        report_first_difference(bounds_of(incremental.tree), bounds_of(full.tree), out);
        return false;
      }
    }
    out << "  ok  " << label << "\n";
  }

  out << "  OK: every node's bounds identical to a full layout, every frame\n";
  return true;
}

namespace {

// One line of the scope table, for one class of change under one container
// style. Painted as well as laid out, because the pixels a repaint touches
// are not the pixels layout damaged: a damage rectangle grows until every
// clip-atomic node it cuts is inside it, and a rounded node is clip-atomic.
struct ScopeRow {
  dg::LayoutStats layout;
  std::int64_t repainted = 0;
};

ScopeRow worst_over(layout_scene::Scene& scene, dg::RasterSurface& surface,
                    const layout_scene::Mutation* mutation, int first, int last) {
  ScopeRow worst;
  for (int frame = first; frame < last; ++frame) {
    if (mutation == nullptr) {
      layout_scene::apply_frame(scene.tree, scene.handles, frame);
    } else {
      layout_scene::apply(scene.tree, scene.handles, *mutation, frame);
    }
    const dg::LayoutStats stats = scene.tree.layout();
    const dg::RepaintStats painted = scene.tree.render().repaint(surface);
    if (stats.nodes_relaid_out > worst.layout.nodes_relaid_out ||
        painted.pixels > worst.repainted) {
      worst.layout = stats;
      worst.repainted = std::max(worst.repainted, painted.pixels);
    }
  }
  return worst;
}

void write_scope_row(std::ostream& out, const char* label, const ScopeRow& row,
                     std::size_t total) {
  out << "  " << std::left << std::setw(38) << label << std::right << std::setw(9)
      << row.layout.nodes_visited << std::setw(12) << row.layout.nodes_relaid_out
      << std::setw(8) << row.layout.nodes_moved << std::setw(9) << std::fixed
      << std::setprecision(1) << percent(row.layout.nodes_relaid_out, total) << "%"
      << std::setw(12) << row.layout.damage_area << std::setw(12) << row.repainted << "\n";
}

void scope_table(const Config& config, bool rounded, std::ostream& out) {
  Config local = config;
  local.rounded_containers = rounded;
  layout_scene::Scene scene = layout_scene::build(options_from(local));
  std::optional<dg::RasterSurface> surface =
      dg::RasterSurface::create(config.viewport.width, config.viewport.height);
  if (!surface.has_value()) {
    out << "  could not allocate a surface\n";
    return;
  }
  const std::size_t total = scene.tree.node_count();

  out << "\n  " << (rounded ? "ROUNDED" : "SQUARE ") << " containers, " << total << " nodes\n"
      << "  " << std::left << std::setw(38) << "change" << std::right << std::setw(9)
      << "entered" << std::setw(12) << "recomputed" << std::setw(8) << "moved" << std::setw(10)
      << "% of all" << std::setw(12) << "damage px" << std::setw(12) << "repaint px"
      << "\n  " << std::string(99, '-') << "\n";

  // Warmed up so the numbers describe steady state rather than the first
  // frame, where nothing has been laid out yet and everything is a miss.
  worst_over(scene, *surface, nullptr, 0, 16);

  int count = 0;
  const layout_scene::Mutation* mutations = layout_scene::all_mutations(count);
  for (int which = 0; which < count; ++which) {
    write_scope_row(out, layout_scene::name_of(mutations[which]),
                    worst_over(scene, *surface, &mutations[which], 100, 140), total);
  }
  write_scope_row(out, "every mutation together",
                  worst_over(scene, *surface, nullptr, 100, 140), total);

  ScopeRow full;
  layout_scene::apply_frame(scene.tree, scene.handles, 141);
  full.layout = scene.tree.layout_full();
  full.repainted = scene.tree.render().repaint_full(*surface).pixels;
  write_scope_row(out, "full layout + full repaint", full, total);

  for (const std::string& diagnostic : scene.tree.diagnostics()) {
    out << "\n  " << diagnostic << "\n";
  }
}

// A column of rows of leaves, which is the shape a list or a table has. Each
// row is a fixed height and stretches across, so it is constrained tightly on
// both axes and is therefore a relayout boundary - which means changing one
// leaf costs one row however many rows there are. That is the property the
// ladder below is measuring.
struct Synthetic {
  dg::LayoutTree tree;
  dg::NodeId leaf;
};

Synthetic build_synthetic(int rows, int columns) {
  dg::TreeSpec spec;
  spec.viewport = dg::PixelSize{1400, (rows * 20) + 40};
  dg::LayoutTree tree{spec};

  dg::BoxStyle root_box;
  root_box.kind = dg::LayoutKind::kColumn;
  root_box.cross_align = dg::CrossAlign::kStretch;
  tree.set_box(dg::LayoutTree::root(), root_box);

  dg::NodeId tracked{0};
  for (int row = 0; row < rows; ++row) {
    dg::BoxStyle row_box;
    row_box.kind = dg::LayoutKind::kRow;
    row_box.height = 18;
    row_box.gap = 2;
    row_box.cross_align = dg::CrossAlign::kStretch;
    const dg::NodeId host = tree.add_child(dg::LayoutTree::root(), row_box, dg::NodeStyle{});
    for (int column = 0; column < columns; ++column) {
      dg::BoxStyle cell;
      cell.width = 12;
      const dg::NodeId leaf = tree.add_child(host, cell, dg::NodeStyle{});
      if (row == rows / 2 && column == columns / 2) {
        tracked = leaf;
      }
    }
  }
  tree.layout_full();
  return Synthetic{std::move(tree), tracked};
}

struct TreeLadder {
  int rows;
  int columns;
};

constexpr TreeLadder kTreeSizes[] = {
    {12, 8},
    {40, 24},
    {125, 79},
    {250, 159},
};

}  // namespace

void report_scope(const Config& config, std::ostream& out) {
  out << "\nRE-LAYOUT SCOPE at " << config.viewport.width << "x" << config.viewport.height
      << "\n  Entered is every node layout() walked into; recomputed is the ones whose size "
         "was\n"
      << "  actually worked out again - the gap is what constraint equality buys. Repaint px "
         "is\n"
      << "  what the damage cost once every rounded node it touched had been swallowed "
         "whole.\n";

  scope_table(config, false, out);
  scope_table(config, true, out);
}

void bench(int frames, std::ostream& out) {
  out << "\nLAYOUT ONLY - no painting, no window. " << frames << " timed frames per row after "
      << kWarmupFrames << " warmup frames.\n"
      << "  A tight loop understates a paced frame (sub-step 1 measured 0.28 vs 0.76 ms for\n"
      << "  the same work), so read these as a lower bound and the demo's on-screen figure\n"
      << "  as the one a user experiences.\n";

  out << "\n  THE DEMO SCENE at four viewport sizes. 92 nodes, so a full layout is already\n"
      << "  cheap here and the ratio understates what incremental layout is for.\n";

  for (const Ladder& size : kSizes) {
    Config config;
    config.viewport = dg::PixelSize{size.width, size.height};
    layout_scene::Scene scene = layout_scene::build(options_from(config));

    std::size_t incremental_nodes = 0;
    const Stats incremental = time_frames(frames, [&](int frame) {
      layout_scene::apply_frame(scene.tree, scene.handles, frame);
      incremental_nodes = scene.tree.layout().nodes_relaid_out;
    });
    std::size_t full_nodes = 0;
    const Stats full = time_frames(frames, [&](int frame) {
      layout_scene::apply_frame(scene.tree, scene.handles, frame);
      full_nodes = scene.tree.layout_full().nodes_relaid_out;
    });

    out << "  " << std::left << std::setw(14) << size.label << std::right << std::fixed
        << std::setprecision(4) << std::setw(12) << incremental.median_ms << " ms incremental ("
        << incremental_nodes << " nodes)" << std::setw(12) << full.median_ms << " ms full ("
        << full_nodes << " nodes)   " << std::setprecision(1)
        << (incremental.median_ms > 0.0 ? full.median_ms / incremental.median_ms : 0.0)
        << "x\n";
  }

  out << "\n  A COLUMN OF ROWS at four tree sizes, one leaf changed per frame. This is the\n"
      << "  ladder that answers the question: a full layout is O(nodes), an incremental one\n"
      << "  is O(the boundary that contains the change), so the two curves diverge.\n\n"
      << "  " << std::left << std::setw(10) << "nodes" << std::right << std::setw(14)
      << "incremental" << std::setw(12) << "recomputed" << std::setw(14) << "full"
      << std::setw(12) << "recomputed" << std::setw(10) << "speedup"
      << "\n  " << std::string(72, '-') << "\n";

  for (const TreeLadder& shape : kTreeSizes) {
    Synthetic synthetic = build_synthetic(shape.rows, shape.columns);
    const std::size_t total = synthetic.tree.node_count();

    std::size_t incremental_nodes = 0;
    const Stats incremental = time_frames(frames, [&](int frame) {
      dg::BoxStyle box = synthetic.tree.box(synthetic.leaf);
      box.width = 8 + (frame % 9);
      synthetic.tree.set_box(synthetic.leaf, box);
      incremental_nodes = synthetic.tree.layout().nodes_relaid_out;
    });
    std::size_t full_nodes = 0;
    const Stats full = time_frames(frames, [&](int frame) {
      dg::BoxStyle box = synthetic.tree.box(synthetic.leaf);
      box.width = 8 + (frame % 9);
      synthetic.tree.set_box(synthetic.leaf, box);
      full_nodes = synthetic.tree.layout_full().nodes_relaid_out;
    });

    out << "  " << std::left << std::setw(10) << total << std::right << std::fixed
        << std::setprecision(4) << std::setw(11) << incremental.median_ms << " ms"
        << std::setw(12) << incremental_nodes << std::setw(11) << full.median_ms << " ms"
        << std::setw(12) << full_nodes << std::setw(9) << std::setprecision(0)
        << (incremental.median_ms > 0.0 ? full.median_ms / incremental.median_ms : 0.0)
        << "x\n";
  }
}

}  // namespace layout_check
