#include "demo.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/widget/interaction.h"
#include "drawgui/window/window_manager.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkRect.h"

#include "hud.h"
#include "widget_scene.h"

namespace demo {
namespace {

using Clock = std::chrono::steady_clock;

constexpr std::size_t kRollingSamples = 120;

// The readout strip is a fifth of the window, so a frame that redraws it costs
// far more than the interaction that triggered it. Refreshing it on a timer
// rather than on every event keeps most frames free of it - and those are the
// frames whose damage figure is worth reporting. Example 04 arrived at the
// same arrangement for the same reason.
constexpr int kReadoutRefreshMs = 150;

double ms_since(Clock::time_point started) {
  return std::chrono::duration<double, std::milli>(Clock::now() - started).count();
}

SkRect to_sk(const dg::PixelRect& rect) {
  return SkRect::MakeXYWH(static_cast<float>(rect.x), static_cast<float>(rect.y),
                          static_cast<float>(rect.width), static_cast<float>(rect.height));
}

// A rolling median of the pixels one interaction repainted, so the two figures
// on screen are comparable steady-state numbers rather than whatever the last
// event happened to be.
class Track {
 public:
  void add(std::int64_t pixels) {
    if (samples_.size() < kRollingSamples) {
      samples_.push_back(pixels);
    } else {
      samples_[next_] = pixels;
    }
    next_ = (next_ + 1) % kRollingSamples;
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

// One scripted pointer step, and what it is for. Named so that a run printed
// to the terminal reads as a list of claims rather than as a list of
// coordinates.
struct Step {
  dg::PixelPoint at;
  bool press = false;
  bool release = false;
  bool leave = false;
  const char* why = "";
};

class Runner {
 public:
  Runner(dg::WindowManager& manager, dg::WindowId window, Settings settings)
      : manager_(&manager),
        window_(window),
        settings_(std::move(settings)),
        hud_(hud::Hud::load(settings_.font_dir)) {}

  [[nodiscard]] bool sync_with_window();
  void handle(const dg::PointerEvent& event);
  [[nodiscard]] bool draw_if_dirty();
  void build_script();
  [[nodiscard]] bool advance_script();
  [[nodiscard]] bool script_done() const { return step_ >= script_.size(); }
  void write_summary(std::ostream& out) const;

 private:
  void rebuild(dg::PixelSize pixels);
  void note_change(const dg::InteractionChange& change);
  [[nodiscard]] bool present_painted(const widget_scene::Scene& built,
                                     const dg::RasterSurface& surface);
  void draw_readout(dg::RasterSurface& surface);

  dg::WindowManager* manager_;
  dg::WindowId window_;
  Settings settings_;
  hud::Hud hud_;

  std::optional<dg::RasterSurface> surface_;
  std::optional<widget_scene::Scene> scene_;
  dg::Interaction interaction_;
  bool built_rounded_ = false;

  dg::PixelPoint pointer_;
  bool pointer_inside_ = false;

  Track square_;
  Track rounded_;
  hud::Readout readout_;
  bool readout_stale_ = true;
  Clock::time_point readout_drawn_;

  std::vector<Step> script_;
  std::size_t step_ = 0;
  Clock::time_point step_started_ = Clock::now();
};

void Runner::rebuild(dg::PixelSize pixels) {
  widget_scene::Options options;
  options.spec.viewport = pixels;
  options.spec.background.fill = dg::Color::from_argb(0xFF11161D);
  options.spec.max_damage_rects = settings_.max_damage_rects;
  options.rounded_controls = settings_.rounded_controls;
  options.rounded_containers = settings_.rounded_containers;
  options.font_dir = settings_.font_dir;

  scene_.emplace(widget_scene::build(options));
  built_rounded_ = options.rounded_controls;

  // A rebuild throws away every widget, so the state machine's answers name
  // nodes in a tree that no longer exists. Starting it over is the only
  // correct response, and forgetting to is how a hover survives a rebuild and
  // lights up whatever inherited that index.
  interaction_ = dg::Interaction{};

  readout_.viewport = pixels;
  readout_.nodes = scene_->tree.node_count();
  readout_.widgets = scene_->widgets.count();
  readout_.rounded_controls = built_rounded_;
  readout_stale_ = true;
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

  if (!scene_.has_value() || built_rounded_ != settings_.rounded_controls) {
    rebuild(pixels);
    return true;
  }
  if (resized) {
    scene_->tree.resize(pixels);
    scene_->tree.layout();
    readout_.viewport = pixels;
    readout_stale_ = true;

    // The widgets moved and the pointer did not, so the hover has to be
    // re-resolved against the layout that now exists. Nothing sends a motion
    // event for a resize, which is what makes this the classic stale-hover
    // bug rather than something the event loop handles by accident.
    if (pointer_inside_) {
      note_change(widget_scene::resync(*scene_, interaction_, pointer_));
    }
  }
  return true;
}

void Runner::note_change(const dg::InteractionChange& change) {
  if (!scene_.has_value()) {
    return;
  }
  readout_.enters += change.entered.has_value() ? 1 : 0;
  readout_.leaves += change.left.has_value() ? 1 : 0;
  readout_.hovered = interaction_.hovered().has_value()
                         ? widget_scene::describe(*scene_, *interaction_.hovered())
                         : "-";
  readout_.pressed = interaction_.holding().has_value()
                         ? widget_scene::describe(*scene_, *interaction_.holding())
                         : "-";
  if (change.clicked.has_value()) {
    readout_.last_click = widget_scene::describe(*scene_, *change.clicked);
    readout_.clicks = scene_->clicks;
  }
  if (change.any()) {
    readout_stale_ = true;
  }
}

void Runner::handle(const dg::PointerEvent& event) {
  if (!scene_.has_value()) {
    return;
  }
  if (event.action == dg::PointerAction::kLeave) {
    pointer_inside_ = false;
  } else {
    pointer_inside_ = true;
    pointer_ = dg::PixelPoint{event.x, event.y};
  }
  readout_.pointer = pointer_;
  readout_.pointer_inside = pointer_inside_;
  readout_stale_ = true;

  note_change(widget_scene::dispatch(*scene_, interaction_, event));
}

void Runner::draw_readout(dg::RasterSurface& surface) {
  if (!hud_.ready() || !scene_.has_value()) {
    return;
  }
  const dg::PixelRect bounds = scene_->tree.bounds(scene_->handles.readout);
  SkCanvas* canvas = surface.sk_canvas();
  for (const dg::PixelRect& painted : scene_->tree.render().painted().rects()) {
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
// checked. clang-tidy cannot see that `scene_` was tested in draw_if_dirty(),
// and silencing it would silence the one check that would catch the day that
// test moves.
bool Runner::present_painted(const widget_scene::Scene& built,
                             const dg::RasterSurface& surface) {
  const dg::PixelView view = surface.peek_pixels();
  if (view.pixels == nullptr || !view.is_bgra8888) {
    return false;
  }
  const dg::ImageView image{view.pixels, view.width, view.height, view.row_bytes,
                            dg::PixelFormat::kBgra8888};
  return manager_->present(window_, image, built.tree.render().painted().rects()).has_value();
}

bool Runner::draw_if_dirty() {
  if (!scene_.has_value() || !surface_.has_value()) {
    return false;
  }
  widget_scene::Scene& built = *scene_;
  dg::RasterSurface& surface = *surface_;

  const bool repaint_readout = readout_stale_ && ms_since(readout_drawn_) >= kReadoutRefreshMs;
  if (repaint_readout) {
    readout_.square_repaint = square_.median();
    readout_.rounded_repaint = rounded_.median();
    readout_.diagnostic =
        built.tree.diagnostics().empty() ? std::string{} : built.tree.diagnostics().front();

    // The readout is an overlay the tree does not own, so the strip it sits on
    // has to be damaged explicitly or nothing would present it.
    built.tree.render().damage_rect(built.tree.bounds(built.handles.readout));
  }
  if (built.tree.render().damage().is_empty()) {
    return true;
  }

  built.tree.layout();
  const dg::RepaintStats stats = built.tree.render().repaint(surface);

  // Only frames that did NOT redraw the readout are counted, so the figure on
  // screen is the cost of an interaction rather than the cost of reporting it.
  if (repaint_readout) {
    readout_stale_ = false;
    readout_drawn_ = Clock::now();
  } else {
    readout_.last_paint = stats;
    (built_rounded_ ? rounded_ : square_).add(stats.pixels);
  }

  // Called unconditionally. It draws only where the repaint actually landed on
  // the strip, so it costs nothing on the frames that did not touch it - and
  // it means a full repaint (a resize, say) redraws the readout even when the
  // timer has not come round, instead of leaving an empty panel.
  draw_readout(surface);
  return present_painted(built, surface);
}

void Runner::build_script() {
  script_.clear();
  step_ = 0;
  if (!scene_.has_value()) {
    return;
  }
  const widget_scene::Scene& built = *scene_;
  const auto centre = [&built](dg::NodeId id) {
    const dg::PixelRect bounds = built.tree.bounds(id);
    return dg::PixelPoint{bounds.x + (bounds.width / 2), bounds.y + (bounds.height / 2)};
  };

  const dg::PixelRect readout = built.tree.bounds(built.handles.readout);
  const dg::PixelPoint nowhere{readout.x + 12, readout.y + readout.height - 12};

  for (const dg::NodeId id : built.handles.interactive) {
    const dg::PixelPoint at = centre(id);
    script_.push_back(Step{at, false, false, false, "hover on"});
    script_.push_back(Step{at, true, false, false, "press"});
    script_.push_back(Step{at, false, true, false, "release - expect a click"});
    script_.push_back(Step{at, true, false, false, "press again"});
    script_.push_back(Step{nowhere, false, false, false, "drag off the widget"});
    script_.push_back(Step{nowhere, false, true, false, "release away - expect NO click"});
  }

  // The overflowing button, aimed where it sticks out of its parent.
  const dg::PixelRect spill = built.tree.bounds(built.handles.lab_overflow);
  const dg::PixelRect host = built.tree.bounds(built.handles.lab_host);
  if (spill.right() > host.right()) {
    const dg::PixelPoint outside{spill.right() - 3, spill.y + (spill.height / 2)};
    script_.push_back(Step{outside, false, false, false, "hover the part that spills out"});
    script_.push_back(Step{outside, true, false, false, "press it there"});
    script_.push_back(Step{outside, false, true, false, "release - expect a click"});
  }

  // Straight from one overlapping widget to the other.
  script_.push_back(
      Step{centre(built.handles.lab_under), false, false, false, "hover 'under'"});
  script_.push_back(Step{centre(built.handles.lab_over), false, false, false,
                         "cross to 'over' - expect one leave and one enter"});
  script_.push_back(Step{nowhere, false, false, true, "pointer leaves the window"});
}

bool Runner::advance_script() {
  if (script_done() || ms_since(step_started_) < settings_.script_step_ms) {
    return true;
  }
  const Step& step = script_[step_];
  ++step_;
  step_started_ = Clock::now();

  if (step.leave) {
    // A genuine leave, produced by moving the real pointer out of the window
    // rather than by fabricating the event the window manager would send.
    manager_->warp_pointer(window_, step.at.x, -8);
    return true;
  }
  manager_->warp_pointer(window_, step.at.x, step.at.y);
  if (step.press) {
    manager_->post_pointer_button(window_, true, step.at.x, step.at.y);
  }
  if (step.release) {
    manager_->post_pointer_button(window_, false, step.at.x, step.at.y);
  }
  readout_.note = std::string{"script: "} + step.why;
  readout_stale_ = true;
  return true;
}

void Runner::write_summary(std::ostream& out) const {
  out << "\n"
      << readout_.clicks << " click(s), " << readout_.enters << " enter(s), " << readout_.leaves
      << " leave(s)\n";
  const int outstanding = readout_.enters - readout_.leaves;
  out << "enter/leave balance: " << outstanding
      << (outstanding == 0 || outstanding == 1 ? "  (balanced)\n"
                                               : "  DRIFTED - a crossing reported one side\n");

  const std::int64_t square = square_.median();
  const std::int64_t round = rounded_.median();
  out << "median pixels repainted per interaction: ";
  if (square > 0) {
    out << "square controls " << square << " px\n";
  }
  if (round > 0) {
    out << "rounded controls " << round << " px\n";
  }
  if (square > 0 && round > 0) {
    out << "  ratio " << (static_cast<double>(round) / static_cast<double>(square)) << "x\n";
  }
  if (scene_.has_value()) {
    for (const std::string& diagnostic : scene_->tree.diagnostics()) {
      out << "layout says: " << diagnostic << "\n";
    }
  }
}

// One turn of the loop, so that run() reads as "pump and draw until the window
// closes" instead of as five nested conditions.
struct Session {
  // Pointers rather than references, so the struct stays assignable and so
  // that cppcoreguidelines-avoid-const-or-ref-data-members has nothing to
  // object to. It owns none of them; it is a named loop body, not a lifetime.
  dg::WindowManager* manager = nullptr;
  dg::WindowId window;
  Runner* runner = nullptr;
  const Settings* settings = nullptr;
  std::ostream* out = nullptr;
  Clock::time_point started = Clock::now();
  bool asked_to_close = false;
  bool script_built = false;
  bool failed = false;

  [[nodiscard]] bool step();
  void close_when_done();
};

bool Session::step() {
  // A scripted run cannot block indefinitely - it has its own clock - but an
  // interactive one should, because a GUI with nothing to do should cost
  // nothing. That is the whole argument for damage tracking, and a demo
  // spinning at 60 Hz to prove it would be refuting itself.
  const dg::PumpResult pumped = manager->pump(settings->script ? 10 : 200);
  if (!pumped.closed.empty()) {
    return false;
  }

  // Repaint notifications FIRST. A resize in the same pump has already moved
  // every widget, and hit-testing a pointer event against the old layout is the
  // bug this ordering exists to prevent.
  if (!runner->sync_with_window()) {
    *out << "the demo could not prepare a frame\n";
    failed = true;
    return false;
  }
  for (const dg::PointerEvent& event : pumped.pointer) {
    runner->handle(event);
  }

  if (settings->script && !script_built) {
    runner->build_script();
    script_built = true;
  }
  if (settings->script && !runner->advance_script()) {
    return false;
  }

  if (!runner->draw_if_dirty()) {
    *out << "the demo could not draw a frame\n";
    failed = true;
    return false;
  }

  close_when_done();
  return true;
}

void Session::close_when_done() {
  if (asked_to_close) {
    return;
  }
  const bool script_finished = settings->script && script_built && runner->script_done();
  const bool timed_out = settings->run_ms > 0 && ms_since(started) >= settings->run_ms;
  if (script_finished || timed_out) {
    manager->request_close(window);
    asked_to_close = true;
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

  const dg::Expected<dg::WindowId, dg::WindowError> opened =
      manager.open(dg::WindowSpec{"drawgui - widgets", settings.size.width,
                                  settings.size.height, dg::Color::from_argb(0xFF11161D)});
  if (!opened) {
    out << "cannot open a window: " << opened.error().message << "\n";
    return 1;
  }
  const dg::WindowId window = opened.value();

  Runner runner{manager, window, settings};
  Session session{&manager, window, &runner, &settings, &out};

  while (manager.open_window_count() > 0 && session.step()) {
    // Every iteration is one pump and one frame; the body is named so that the
    // ORDER in it - repaint notifications before pointer events - is visible
    // rather than buried.
  }

  runner.write_summary(out);
  return session.failed ? 1 : 0;
}

}  // namespace demo
