#include "demo.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <thread>
#include <utility>
#include <vector>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/window/window_manager.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkRect.h"

#include "hud.h"
#include "layout_scene.h"

namespace demo {
namespace {

using Clock = std::chrono::steady_clock;

constexpr std::size_t kRollingFrames = 240;
constexpr int kReadoutRefreshMs = 200;

double ms_since(Clock::time_point started) {
  return std::chrono::duration<double, std::milli>(Clock::now() - started).count();
}

// A fixed-size ring, so a long run does not grow without bound and the
// numbers on screen describe the last few seconds rather than the whole
// session - which matters, because the first frames after a resize are
// nothing like the steady state.
class Samples {
 public:
  void add(double layout_ms, double raster_ms, double present_ms) {
    if (layout_.size() < kRollingFrames) {
      layout_.push_back(layout_ms);
      raster_.push_back(raster_ms);
      present_.push_back(present_ms);
    } else {
      layout_[next_] = layout_ms;
      raster_[next_] = raster_ms;
      present_[next_] = present_ms;
    }
    next_ = (next_ + 1) % kRollingFrames;
    ++total_;
  }

  [[nodiscard]] hud::Lane summarize() const {
    hud::Lane lane;
    lane.layout = quantiles(layout_);
    lane.raster = quantiles(raster_);
    lane.present = quantiles(present_);
    lane.frames = total_;
    return lane;
  }

 private:
  static hud::Timing quantiles(std::vector<double> samples) {
    hud::Timing timing;
    if (samples.empty()) {
      return timing;
    }
    std::sort(samples.begin(), samples.end());
    timing.median_ms = samples[samples.size() / 2];
    timing.p95_ms =
        samples[static_cast<std::size_t>(0.95 * static_cast<double>(samples.size() - 1))];
    timing.worst_ms = samples.back();
    return timing;
  }

  std::vector<double> layout_;
  std::vector<double> raster_;
  std::vector<double> present_;
  std::size_t next_ = 0;
  std::size_t total_ = 0;
};

SkRect to_sk(const dg::PixelRect& rect) {
  return SkRect::MakeXYWH(static_cast<float>(rect.x), static_cast<float>(rect.y),
                          static_cast<float>(rect.width), static_cast<float>(rect.height));
}

// A rolling median of the layout damage seen under one container style, so
// the two figures on screen are comparable steady-state numbers rather than
// whatever the last frame happened to be.
class DamageTrack {
 public:
  void add(std::int64_t pixels) {
    if (samples_.size() < kRollingFrames) {
      samples_.push_back(pixels);
    } else {
      samples_[next_] = pixels;
    }
    next_ = (next_ + 1) % kRollingFrames;
  }

  [[nodiscard]] std::int64_t median() const {
    if (samples_.empty()) {
      return 0;
    }
    std::vector<std::int64_t> sorted = samples_;
    std::sort(sorted.begin(), sorted.end());
    return sorted[sorted.size() / 2];
  }

 private:
  std::vector<std::int64_t> samples_;
  std::size_t next_ = 0;
};

class Runner {
 public:
  Runner(dg::WindowManager& manager, dg::WindowId window, const Settings& settings)
      : manager_(&manager),
        window_(window),
        settings_(settings),
        hud_(hud::Hud::load(settings.font_dir)),
        started_(Clock::now()),
        readout_refreshed_(started_) {}

  [[nodiscard]] bool sync_with_window();
  [[nodiscard]] bool frame();
  void write_summary(std::ostream& out) const;

 private:
  [[nodiscard]] bool incremental() const {
    if (!settings_.alternate) {
      return settings_.incremental;
    }
    return (static_cast<long long>(ms_since(started_)) / settings_.alternate_ms) % 2 == 0;
  }

  [[nodiscard]] bool rounded() const {
    if (!settings_.rounded_alternate) {
      return settings_.rounded;
    }
    return (static_cast<long long>(ms_since(started_)) / settings_.rounded_ms) % 2 == 1;
  }

  void rebuild(dg::PixelSize pixels);
  void refresh_readout();
  void draw_readout(const layout_scene::Scene& built, dg::RasterSurface& surface);
  [[nodiscard]] bool present_painted(const layout_scene::Scene& built,
                                     const dg::RasterSurface& surface);

  dg::WindowManager* manager_;
  dg::WindowId window_;
  Settings settings_;
  hud::Hud hud_;
  Clock::time_point started_;
  Clock::time_point readout_refreshed_;

  std::optional<dg::RasterSurface> surface_;
  std::optional<layout_scene::Scene> scene_;
  bool built_rounded_ = false;
  Samples incremental_samples_;
  Samples full_samples_;
  DamageTrack square_repaint_;
  DamageTrack rounded_repaint_;
  hud::Readout readout_;
  int frame_index_ = 0;
};

void Runner::rebuild(dg::PixelSize pixels) {
  layout_scene::Options options;
  options.spec.viewport = pixels;
  options.spec.background.fill = dg::Color::from_argb(0xFF14171C);
  options.spec.max_damage_rects = settings_.max_damage_rects;
  options.rounded_containers = rounded();
  scene_.emplace(layout_scene::build(options));
  built_rounded_ = options.rounded_containers;

  readout_.viewport = pixels;
  readout_.nodes = scene_->tree.node_count();
  readout_.damage_cap = settings_.max_damage_rects;
}

bool Runner::sync_with_window() {
  const dg::Expected<dg::PixelSize, dg::WindowError> size = manager_->drawable_size(window_);
  if (!size) {
    return false;
  }
  const dg::PixelSize pixels = size.value();
  if (pixels.width <= 0 || pixels.height <= 0) {
    return true;
  }

  const bool resized = !surface_.has_value() || surface_->width() != pixels.width ||
                       surface_->height() != pixels.height;
  if (resized) {
    surface_ = dg::RasterSurface::create(pixels.width, pixels.height);
    if (!surface_.has_value()) {
      return false;
    }
  }

  // A resize now RE-LAYS-OUT rather than rebuilding, which is the thing
  // examples/03 could not do: its scene computed its geometry once at
  // construction, so every resize threw the tree away. Here the tree survives
  // and only the boxes move.
  if (!scene_.has_value() || built_rounded_ != rounded()) {
    rebuild(pixels);
    return true;
  }
  if (resized) {
    scene_->tree.resize(pixels);
    scene_->tree.layout();
    readout_.viewport = pixels;
  }
  return true;
}

void Runner::refresh_readout() {
  readout_.incremental_lane = incremental_samples_.summarize();
  readout_.full_lane = full_samples_.summarize();
  readout_.square_repaint = square_repaint_.median();
  readout_.rounded_repaint = rounded_repaint_.median();
  readout_refreshed_ = Clock::now();
}

void Runner::draw_readout(const layout_scene::Scene& built, dg::RasterSurface& surface) {
  if (!hud_.ready()) {
    return;
  }
  const dg::PixelRect bounds = built.tree.bounds(built.handles.readout);
  SkCanvas* canvas = surface.sk_canvas();
  for (const dg::PixelRect& painted : built.tree.render().painted().rects()) {
    const dg::PixelRect region = dg::intersect(painted, bounds);
    if (region.is_empty()) {
      continue;
    }
    canvas->save();
    canvas->clipRect(to_sk(region), false);
    hud_.draw(*canvas, bounds, readout_);
    canvas->restore();
  }
}

// Takes the scene rather than reading the member the caller has already
// checked. clang-tidy cannot see that `scene_` was tested in frame(), and
// silencing it would be silencing the one check that would catch the day the
// test moves.
bool Runner::present_painted(const layout_scene::Scene& built,
                             const dg::RasterSurface& surface) {
  const dg::PixelView view = surface.peek_pixels();
  if (view.pixels == nullptr || !view.is_bgra8888) {
    return false;
  }
  const dg::ImageView image{view.pixels, view.width, view.height, view.row_bytes,
                            dg::PixelFormat::kBgra8888};
  return manager_->present(window_, image, built.tree.render().painted().rects()).has_value();
}

bool Runner::frame() {
  if (!scene_.has_value() || !surface_.has_value()) {
    return false;
  }
  layout_scene::Scene& built = *scene_;
  dg::RasterSurface& surface = *surface_;

  const Clock::time_point frame_started = Clock::now();
  const bool incremental_frame = incremental();

  layout_scene::apply_frame(built.tree, built.handles, frame_index_++);

  const Clock::time_point layout_started = Clock::now();
  const dg::LayoutStats layout_stats =
      incremental_frame ? built.tree.layout() : built.tree.layout_full();
  const double layout_ms = ms_since(layout_started);

  // A frame that redraws the readout is measuring the instrument: the panel
  // is a fifth of the window, so those frames are excluded from the lanes and
  // from the per-frame line, and the readout says so on screen.
  const bool repaints_readout = ms_since(readout_refreshed_) >= kReadoutRefreshMs;
  if (repaints_readout) {
    refresh_readout();
    built.tree.render().damage_rect(built.tree.bounds(built.handles.readout));
  }

  const Clock::time_point raster_started = Clock::now();
  const dg::RepaintStats paint_stats = built.tree.render().repaint(surface);
  if (!repaints_readout) {
    readout_.last_layout = layout_stats;
    readout_.last_paint = paint_stats;
    (built_rounded_ ? rounded_repaint_ : square_repaint_).add(paint_stats.pixels);
  }
  readout_.incremental = incremental_frame;
  readout_.rounded_containers = built_rounded_;
  readout_.diagnostic =
      built.tree.diagnostics().empty() ? std::string{} : built.tree.diagnostics().front();

  draw_readout(built, surface);
  const double raster_ms = ms_since(raster_started);

  const Clock::time_point present_started = Clock::now();
  if (!present_painted(built, surface)) {
    return false;
  }
  const double present_ms = ms_since(present_started);

  if (!repaints_readout) {
    (incremental_frame ? incremental_samples_ : full_samples_)
        .add(layout_ms, raster_ms, present_ms);
  }

  // Paced rather than free-running. The work per frame is what is being
  // measured; spinning as fast as the machine allows would only measure how
  // fast the machine allows, and sub-step 1 measured a tight loop understating
  // a paced frame by 2.7x.
  const double remaining =
      static_cast<double>(settings_.frame_budget_ms) - ms_since(frame_started);
  if (remaining > 0.0) {
    std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(remaining));
  }
  return true;
}

void Runner::write_summary(std::ostream& out) const {
  const hud::Lane incremental_lane = incremental_samples_.summarize();
  const hud::Lane full_lane = full_samples_.summarize();
  const auto line = [&out](const char* label, const hud::Lane& lane) {
    if (lane.frames == 0) {
      out << "  " << label << "  not measured in this mode\n";
      return;
    }
    out << "  " << label << "  layout " << lane.layout.median_ms << " ms (p95 "
        << lane.layout.p95_ms << ")  raster " << lane.raster.median_ms << " ms  present "
        << lane.present.median_ms << " ms  over " << lane.frames << " frames\n";
  };

  out << "\nfinal medians at " << readout_.viewport.width << "x" << readout_.viewport.height
      << ", " << readout_.nodes << " nodes, cap " << readout_.damage_cap << " rect(s)\n";
  line("incremental", incremental_lane);
  line("full       ", full_lane);

  if (incremental_lane.frames > 0 && full_lane.frames > 0 &&
      incremental_lane.layout.median_ms > 0.0) {
    out << "  layout speedup  "
        << (full_lane.layout.median_ms / incremental_lane.layout.median_ms) << "x\n";
  }

  const std::int64_t square = square_repaint_.median();
  const std::int64_t round = rounded_repaint_.median();
  out << "  repainted pixels per frame: square containers " << square << " px, rounded "
      << round << " px";
  if (square > 0 && round > 0) {
    out << "  (" << (static_cast<double>(round) / static_cast<double>(square)) << "x)";
  }
  out << "\n";
}

}  // namespace

int run(const Settings& settings, std::ostream& out) {
  dg::Expected<dg::WindowManager, dg::WindowError> created = dg::WindowManager::create();
  if (!created) {
    out << "cannot start the window manager: " << created.error().message << "\n";
    return 1;
  }
  dg::WindowManager manager = std::move(created).value();

  const dg::Expected<dg::WindowId, dg::WindowError> opened =
      manager.open(dg::WindowSpec{"drawgui - incremental layout", settings.size.width,
                                  settings.size.height, dg::Color::from_argb(0xFF14171C)});
  if (!opened) {
    out << "cannot open a window: " << opened.error().message << "\n";
    return 1;
  }
  const dg::WindowId window = opened.value();

  Runner runner{manager, window, settings};
  const Clock::time_point started = Clock::now();
  bool asked_to_close = false;

  while (manager.open_window_count() > 0) {
    const dg::PumpResult pumped = manager.pump(0);
    if (!pumped.closed.empty()) {
      break;
    }
    if (!runner.sync_with_window() || !runner.frame()) {
      out << "the demo could not draw a frame\n";
      return 1;
    }
    if (settings.run_ms > 0 && !asked_to_close && ms_since(started) >= settings.run_ms) {
      manager.request_close(window);
      asked_to_close = true;
    }
  }

  runner.write_summary(out);
  return 0;
}

}  // namespace demo
