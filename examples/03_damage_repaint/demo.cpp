#include "demo.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <optional>
#include <ostream>
#include <thread>
#include <utility>
#include <vector>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/window/window_manager.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkRect.h"

#include "hud.h"
#include "scene.h"

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
  void add(double raster_ms, double present_ms) {
    if (raster_.size() < kRollingFrames) {
      raster_.push_back(raster_ms);
      present_.push_back(present_ms);
    } else {
      raster_[next_] = raster_ms;
      present_[next_] = present_ms;
    }
    next_ = (next_ + 1) % kRollingFrames;
    ++total_;
  }

  [[nodiscard]] hud::Lane summarize() const {
    hud::Lane lane;
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

  std::vector<double> raster_;
  std::vector<double> present_;
  std::size_t next_ = 0;
  std::size_t total_ = 0;
};

SkRect to_sk(const dg::PixelRect& rect) {
  return SkRect::MakeXYWH(static_cast<float>(rect.x), static_cast<float>(rect.y),
                          static_cast<float>(rect.width), static_cast<float>(rect.height));
}

class Runner {
 public:
  Runner(dg::WindowManager& manager, dg::WindowId window, const Settings& settings)
      : manager_(&manager),
        window_(window),
        settings_(settings),
        hud_(hud::Hud::load(settings.font_dir)),
        started_(Clock::now()),
        readout_refreshed_(started_) {}

  [[nodiscard]] bool rebuild_if_resized();
  [[nodiscard]] bool frame();
  void write_summary(std::ostream& out) const;

 private:
  [[nodiscard]] bool damage_driven() const {
    if (!settings_.alternate) {
      return settings_.damage_mode;
    }
    const auto elapsed = static_cast<long long>(ms_since(started_));
    return (elapsed / settings_.alternate_ms) % 2 == 0;
  }

  void refresh_readout();
  void draw_readout(const scene::Scene& built, dg::RasterSurface& surface);
  [[nodiscard]] bool present_painted(const scene::Scene& built,
                                     const dg::RasterSurface& surface);

  dg::WindowManager* manager_;
  dg::WindowId window_;
  Settings settings_;
  hud::Hud hud_;
  Clock::time_point started_;
  Clock::time_point readout_refreshed_;

  std::optional<dg::RasterSurface> surface_;
  std::optional<scene::Scene> scene_;
  Samples damage_samples_;
  Samples full_samples_;
  hud::Readout readout_;
  int frame_index_ = 0;
};

bool Runner::rebuild_if_resized() {
  const dg::Expected<dg::PixelSize, dg::WindowError> size = manager_->drawable_size(window_);
  if (!size) {
    return false;
  }
  const dg::PixelSize pixels = size.value();
  if (pixels.width <= 0 || pixels.height <= 0) {
    return true;
  }
  if (surface_.has_value() && surface_->width() == pixels.width &&
      surface_->height() == pixels.height) {
    return true;
  }

  surface_ = dg::RasterSurface::create(pixels.width, pixels.height);
  if (!surface_.has_value()) {
    return false;
  }

  // Rebuilt rather than resized: the scene has no layout engine behind it, so
  // its geometry is computed from the viewport at construction. A resize is
  // the one moment a full repaint is unavoidable anyway.
  dg::TreeSpec spec;
  spec.viewport = pixels;
  spec.background.fill = dg::Color::from_argb(0xFF14171C);
  spec.max_damage_rects = settings_.max_damage_rects;
  spec.paint_mode = settings_.paint_mode;
  scene_.emplace(scene::build(spec));

  readout_.viewport = pixels;
  readout_.nodes = scene_->tree.node_count();
  readout_.damage_cap = settings_.max_damage_rects;
  readout_.paint_mode = settings_.paint_mode;
  return true;
}

void Runner::refresh_readout() {
  readout_.damage = damage_samples_.summarize();
  readout_.full = full_samples_.summarize();
  readout_refreshed_ = Clock::now();
}

void Runner::draw_readout(const scene::Scene& built, dg::RasterSurface& surface) {
  if (!hud_.ready()) {
    return;
  }
  const dg::PixelRect bounds = built.tree.absolute_bounds(built.handles.readout);
  SkCanvas* canvas = surface.sk_canvas();
  for (const dg::PixelRect& painted : built.tree.painted().rects()) {
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

bool Runner::present_painted(const scene::Scene& built, const dg::RasterSurface& surface) {
  const dg::PixelView view = surface.peek_pixels();
  if (view.pixels == nullptr || !view.is_bgra8888) {
    return false;
  }
  const dg::ImageView image{view.pixels, view.width, view.height, view.row_bytes,
                            dg::PixelFormat::kBgra8888};
  return manager_->present(window_, image, built.tree.painted().rects()).has_value();
}

bool Runner::frame() {
  if (!scene_.has_value() || !surface_.has_value()) {
    return false;
  }
  scene::Scene& built = *scene_;
  dg::RasterSurface& surface = *surface_;

  const Clock::time_point frame_started = Clock::now();
  const bool damage_driven_frame = damage_driven();

  scene::apply_frame(built.tree, built.handles, frame_index_++);

  // A frame that redraws the readout is measuring the instrument: the panel
  // is a twelfth of the window, so those frames are excluded from the lanes
  // and from the per-frame line, and the readout says so on screen.
  const bool repaints_readout = ms_since(readout_refreshed_) >= kReadoutRefreshMs;
  if (repaints_readout) {
    refresh_readout();
    built.tree.damage_rect(built.tree.absolute_bounds(built.handles.readout));
  }

  const Clock::time_point raster_started = Clock::now();
  const dg::RepaintStats stats =
      damage_driven_frame ? built.tree.repaint(surface) : built.tree.repaint_full(surface);
  readout_.damage_mode = damage_driven_frame;

  // Only damage-driven frames, so the per-frame line keeps reporting what a
  // partial repaint costs even while the demo is showing the full-repaint
  // half of the comparison.
  if (!repaints_readout && damage_driven_frame) {
    readout_.last = stats;
  }
  draw_readout(built, surface);
  const double raster_ms = ms_since(raster_started);

  const Clock::time_point present_started = Clock::now();
  if (!present_painted(built, surface)) {
    return false;
  }
  const double present_ms = ms_since(present_started);

  if (!repaints_readout) {
    (damage_driven_frame ? damage_samples_ : full_samples_).add(raster_ms, present_ms);
  }

  // Paced rather than free-running. The work per frame is what is being
  // measured; spinning as fast as the machine allows would only measure how
  // fast the machine allows, and would make the animation unwatchable.
  const double remaining =
      static_cast<double>(settings_.frame_budget_ms) - ms_since(frame_started);
  if (remaining > 0.0) {
    std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(remaining));
  }
  return true;
}

void Runner::write_summary(std::ostream& out) const {
  const hud::Lane damage = damage_samples_.summarize();
  const hud::Lane full = full_samples_.summarize();
  const auto line = [&out](const char* label, const hud::Lane& lane) {
    if (lane.frames == 0) {
      out << "  " << label << "  not measured in this mode\n";
      return;
    }
    out << "  " << label << "  raster " << lane.raster.median_ms << " ms (p95 "
        << lane.raster.p95_ms << ")  present " << lane.present.median_ms << " ms (p95 "
        << lane.present.p95_ms << ")  over " << lane.frames << " frames\n";
  };
  out << "\nfinal medians at " << readout_.viewport.width << "x" << readout_.viewport.height
      << ", cap " << readout_.damage_cap << " rect(s), "
      << (readout_.paint_mode == dg::PaintMode::kPicture ? "picture" : "direct")
      << " painting\n";
  line("damage", damage);
  line("full  ", full);

  const double damage_total = damage.raster.median_ms + damage.present.median_ms;
  const double full_total = full.raster.median_ms + full.present.median_ms;
  if (damage.frames > 0 && full.frames > 0 && damage_total > 0.0) {
    out << "  speedup  raster " << (full.raster.median_ms / damage.raster.median_ms)
        << "x  present " << (full.present.median_ms / damage.present.median_ms) << "x  total "
        << (full_total / damage_total) << "x\n";
  }
}

}  // namespace

int run(const Settings& settings, std::ostream& out) {
  dg::Expected<dg::WindowManager, dg::WindowError> created = dg::WindowManager::create();
  if (!created) {
    out << "cannot start the window manager: " << created.error().message << "\n";
    return 1;
  }
  dg::WindowManager manager = std::move(created).value();

  const dg::Expected<dg::WindowId, dg::WindowError> opened = manager.open(
      dg::WindowSpec{"drawgui - damage-driven retained repaint", settings.size.width,
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
    if (!runner.rebuild_if_resized() || !runner.frame()) {
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
