#include "list_window.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/window/window_manager.h"

#include "list_scene.h"

namespace list_window {
namespace {

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point started) {
  return std::chrono::duration<double, std::milli>(Clock::now() - started).count();
}

dg::TreeSpec spec_for(dg::PixelSize size) {
  dg::TreeSpec spec;
  spec.viewport = size;
  spec.background.fill = dg::Color::from_argb(0xFF14171C);
  return spec;
}

void legend(std::ostream& out) {
  out << "  wheel or click-drag inside the panel scrolls a virtualized 1000-item list.\n"
      << "  only " << list_scene::kPoolSize << " real nodes exist at any time - the rest is "
      << "recycled.\n";
}

void apply_presets(list_scene::Scene& scene, const Settings& settings) {
  if (settings.preset_jump_items == 0) {
    return;
  }
  const std::vector<dg::ListSlot> slots = scene.widgets.list_scroll_by(
      scene.tree.render(), scene.handles.list, list_scene::kViewportHeight, 0,
      settings.preset_jump_items * list_scene::kItemHeight);
  list_scene::refresh(scene, slots);
}

class Runner {
 public:
  Runner(dg::WindowManager& manager, dg::WindowId window, const Settings& settings,
         std::ostream& out)
      : settings_(settings), out_(&out), manager_(&manager), window_(window) {}

  int run();

 private:
  bool resize_if_needed();
  void draw(list_scene::Scene& scene, dg::RasterSurface& surface);
  static void handle_wheel(list_scene::Scene& scene, const dg::PointerEvent& event);
  void handle_down(list_scene::Scene& scene, dg::PixelPoint at);
  void handle_move(list_scene::Scene& scene, dg::PixelPoint at);
  void handle_up();

  Settings settings_;
  std::ostream* out_;
  dg::WindowManager* manager_;
  dg::WindowId window_;
  std::optional<list_scene::Scene> scene_;
  std::optional<dg::RasterSurface> surface_;

  std::optional<dg::NodeId> dragging_;
  dg::PixelPoint drag_last_;
};

bool Runner::resize_if_needed() {
  const dg::Expected<dg::PixelSize, dg::WindowError> size = manager_->drawable_size(window_);
  if (!size) {
    return false;
  }
  const dg::PixelSize pixels = size.value();
  if (scene_.has_value()) {
    if (surface_.has_value() && scene_->tree.viewport() == pixels) {
      return true;
    }
    scene_->tree.resize(pixels);
    scene_->tree.layout();
  } else {
    list_scene::Options options;
    options.spec = spec_for(pixels);
    scene_ = list_scene::build(options);
    apply_presets(*scene_, settings_);
  }
  surface_ = dg::RasterSurface::create(pixels.width, pixels.height);
  return surface_.has_value() && scene_.has_value();
}

void Runner::draw(list_scene::Scene& scene, dg::RasterSurface& surface) {
  scene.tree.layout();
  if (scene.tree.render().damage().is_empty()) {
    return;
  }
  scene.tree.render().repaint(surface);
  const dg::PixelView view = surface.peek_pixels();
  if (view.pixels == nullptr || !view.is_bgra8888) {
    return;
  }
  const dg::ImageView image{view.pixels, view.width, view.height, view.row_bytes,
                            dg::PixelFormat::kBgra8888};
  (void)manager_->present(window_, image, scene.tree.render().painted().rects());
}

void Runner::handle_wheel(list_scene::Scene& scene, const dg::PointerEvent& event) {
  const dg::PixelPoint at{event.x, event.y};
  const std::optional<dg::NodeId> hit = scene.tree.render().hit_test(at);
  if (!hit.has_value()) {
    return;
  }
  const std::optional<dg::NodeId> target =
      scene.widgets.list_owner_of(scene.tree.render(), *hit);
  if (!target.has_value()) {
    return;
  }
  constexpr int kPixelsPerNotch = 48;
  const int dy = static_cast<int>(event.wheel_y * static_cast<float>(kPixelsPerNotch));
  const std::vector<dg::ListSlot> slots = scene.widgets.list_scroll_by(
      scene.tree.render(), *target, list_scene::kViewportHeight, 0, -dy);
  list_scene::refresh(scene, slots);
}

void Runner::handle_down(list_scene::Scene& scene, dg::PixelPoint at) {
  const std::optional<dg::NodeId> hit = scene.tree.render().hit_test(at);
  if (!hit.has_value()) {
    return;
  }
  dragging_ = scene.widgets.list_owner_of(scene.tree.render(), *hit);
  drag_last_ = at;
}

void Runner::handle_move(list_scene::Scene& scene, dg::PixelPoint at) {
  if (!dragging_.has_value()) {
    return;
  }
  const int dy = at.y - drag_last_.y;
  drag_last_ = at;
  if (dy == 0) {
    return;
  }
  const std::vector<dg::ListSlot> slots = scene.widgets.list_scroll_by(
      scene.tree.render(), *dragging_, list_scene::kViewportHeight, 0, -dy);
  list_scene::refresh(scene, slots);
}

void Runner::handle_up() {
  dragging_.reset();
}

int Runner::run() {
  const Clock::time_point started = Clock::now();
  *out_ << "list demo: wheel or click-drag the panel.\n";
  legend(*out_);

  while (manager_->open_window_count() > 0) {
    if (settings_.run_ms > 0 && ms_since(started) >= settings_.run_ms) {
      manager_->request_close(window_);
    }
    const dg::PumpResult pumped = manager_->pump(40);
    const bool stale = !pumped.needs_repaint.empty() || !surface_.has_value();
    if (stale && !resize_if_needed()) {
      return 1;
    }
    if (!scene_.has_value() || !surface_.has_value()) {
      return 1;
    }
    list_scene::Scene& scene = *scene_;
    dg::RasterSurface& surface = *surface_;

    for (const dg::PointerEvent& event : pumped.pointer) {
      switch (event.action) {
        case dg::PointerAction::kWheel:
          handle_wheel(scene, event);
          break;
        case dg::PointerAction::kDown:
          handle_down(scene, dg::PixelPoint{event.x, event.y});
          break;
        case dg::PointerAction::kMove:
          handle_move(scene, dg::PixelPoint{event.x, event.y});
          break;
        case dg::PointerAction::kUp:
        case dg::PointerAction::kLeave:
          handle_up();
          break;
      }
    }
    draw(scene, surface);
  }
  return 0;
}

}  // namespace

int run(const Settings& settings, std::ostream& out) {
  dg::Expected<dg::WindowManager, dg::WindowError> made = dg::WindowManager::create();
  if (!made) {
    out << "could not start the window system: " << made.error().message << "\n";
    return 1;
  }
  dg::WindowManager manager = std::move(made.value());

  dg::WindowSpec spec;
  spec.title = "drawgui list";
  spec.width = settings.size.width;
  spec.height = settings.size.height;
  spec.fill = dg::Color::from_argb(0xFF14171C);
  dg::Expected<dg::WindowId, dg::WindowError> opened = manager.open(spec);
  if (!opened) {
    out << "could not open a window: " << opened.error().message << "\n";
    return 1;
  }

  Runner runner{manager, opened.value(), settings, out};
  return runner.run();
}

int dump_png(const Settings& settings, const std::string& path, std::ostream& out) {
  std::optional<dg::RasterSurface> surface =
      dg::RasterSurface::create(settings.size.width, settings.size.height);
  if (!surface.has_value()) {
    out << "could not allocate a surface\n";
    return 1;
  }
  list_scene::Options options;
  options.spec = spec_for(settings.size);
  list_scene::Scene scene = list_scene::build(options);
  apply_presets(scene, settings);
  scene.tree.layout();
  scene.tree.render().repaint_full(*surface);

  const std::vector<std::uint8_t> png = surface->encode_png();
  if (png.empty()) {
    out << "could not encode the frame\n";
    return 2;
  }
  std::ofstream file(path, std::ios::binary);
  file.write(reinterpret_cast<const char*>(png.data()),
             static_cast<std::streamsize>(png.size()));
  if (!file) {
    out << "could not write " << path << "\n";
    return 3;
  }
  out << "wrote " << path << " (" << png.size() << " bytes), top item "
      << (scene.tree.render().scroll_offset(scene.handles.list).y / list_scene::kItemHeight)
      << "\n";
  return 0;
}

int bench(int item_count, std::ostream& out) {
  constexpr dg::PixelSize kBenchSize{500, 620};

  list_scene::Options options;
  options.spec = spec_for(kBenchSize);
  options.item_count = item_count;

  const Clock::time_point build_started = Clock::now();
  list_scene::Scene virtualized = list_scene::build(options);
  const double virtualized_build_ms = ms_since(build_started);

  const Clock::time_point baseline_started = Clock::now();
  list_scene::Scene baseline = list_scene::build_baseline(options);
  const double baseline_build_ms = ms_since(baseline_started);

  out << "item_count=" << item_count << "\n"
      << "virtualized: node_count=" << virtualized.tree.node_count()
      << " build+layout_full=" << virtualized_build_ms << " ms\n"
      << "baseline (real nodes): node_count=" << baseline.tree.node_count()
      << " build+layout_full=" << baseline_build_ms << " ms\n";

  std::optional<dg::RasterSurface> surface =
      dg::RasterSurface::create(kBenchSize.width, kBenchSize.height);
  if (!surface.has_value()) {
    out << "could not allocate a bench surface\n";
    return 1;
  }
  virtualized.tree.render().repaint_full(*surface);
  baseline.tree.render().repaint_full(*surface);

  // A FIXED number of steps, not one per item: `item_count` items scrolled
  // one at a time is the honest end-to-end measurement at 1000 (matching
  // design.md's own number), but the baseline's per-call repaint() cost is
  // O(node_count) (doc/damage-repaint.md's own recorded limit - no spatial
  // index), so looping `item_count` times at, say, 100000 items would be
  // O(item_count^2) and never finish. `kSteps` bounds the LOOP; `item_count`
  // still governs how many real nodes the baseline allocates and how large
  // `list_item_count` is for the virtualized scene, so the node-count
  // comparison stays honest at any scale even where the timing loop is
  // capped.
  const int steps = std::min(item_count, 300);
  const Clock::time_point virt_scroll_started = Clock::now();
  for (int i = 0; i < steps; ++i) {
    const std::vector<dg::ListSlot> slots = virtualized.widgets.list_scroll_by(
        virtualized.tree.render(), virtualized.handles.list, list_scene::kViewportHeight, 0,
        list_scene::kItemHeight);
    list_scene::refresh(virtualized, slots);
    virtualized.tree.render().repaint(*surface);
  }
  const double virt_scroll_ms = ms_since(virt_scroll_started);
  const dg::LayoutStats virt_stats = virtualized.tree.layout();

  const Clock::time_point base_scroll_started = Clock::now();
  const dg::PixelRect content_box = baseline.tree.content_bounds(baseline.handles.list);
  for (int i = 0; i < steps; ++i) {
    baseline.widgets.scroll_by(baseline.tree.render(), baseline.handles.list, content_box, 0,
                               list_scene::kItemHeight);
    baseline.tree.render().repaint(*surface);
  }
  const double base_scroll_ms = ms_since(base_scroll_started);
  const dg::LayoutStats base_stats = baseline.tree.layout();

  out << "virtualized: " << steps << " scroll+repaint steps in " << virt_scroll_ms << " ms ("
      << (virt_scroll_ms / steps) << " ms/step); after the loop, layout() "
      << "visited " << virt_stats.nodes_visited << " and relaid out "
      << virt_stats.nodes_relaid_out << " nodes\n"
      << "baseline:    " << steps << " scroll+repaint steps in " << base_scroll_ms << " ms ("
      << (base_scroll_ms / steps) << " ms/step); after the loop, layout() "
      << "visited " << base_stats.nodes_visited << " and relaid out "
      << base_stats.nodes_relaid_out << " nodes\n";

  constexpr double kFrameBudgetMs = 1000.0 / 60.0;
  out << "60fps frame budget: " << kFrameBudgetMs << " ms/frame\n"
      << "virtualized average: " << (virt_scroll_ms / steps) << " ms/step -> "
      << ((virt_scroll_ms / steps) < kFrameBudgetMs ? "within" : "OVER") << " budget\n"
      << "baseline average:    " << (base_scroll_ms / steps) << " ms/step -> "
      << ((base_scroll_ms / steps) < kFrameBudgetMs ? "within" : "OVER") << " budget\n";
  return 0;
}

}  // namespace list_window
