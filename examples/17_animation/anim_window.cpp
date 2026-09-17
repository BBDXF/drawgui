#include "anim_window.h"

#include <sys/resource.h>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "drawgui/anim/animation_engine.h"
#include "drawgui/anim/clock.h"
#include "drawgui/anim/curve.h"
#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/props/node_props.h"
#include "drawgui/render/prop_ids.generated.h"
#include "drawgui/window/window_manager.h"

#include "anim_scene.h"

namespace anim_window {
namespace {

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point started) {
  return std::chrono::duration<double, std::milli>(Clock::now() - started).count();
}

// The frame-pacer interval WindowManager::pump() is handed while an
// animation is active - a CPU-side approximation of 60Hz vsync, because
// there is no GPU backend here to hand the real signal off to. See
// doc/animation.md section on "no GPU, so no real vsync" for why this is
// candid about that rather than calling it vsync.
constexpr int kFramePacerMs = 16;

dg::TreeSpec spec_for(dg::PixelSize size) {
  dg::TreeSpec spec;
  spec.viewport = size;
  spec.background.fill = dg::Color::from_argb(0xFF14171C);
  return spec;
}

// The caret's four-phase blink, chained by completion events rather than
// looped inside the engine - design.md section 5.16.1 gives dg_animate() no
// repeat count, so a client that wants a cycle builds it out of ordinary
// one-shot animations, exactly the way this one does.
enum class BlinkPhase : std::uint8_t { kFadeOut, kHoldOff, kFadeIn, kHoldOn };

BlinkPhase next_phase(BlinkPhase phase) {
  switch (phase) {
    case BlinkPhase::kFadeOut:
      return BlinkPhase::kHoldOff;
    case BlinkPhase::kHoldOff:
      return BlinkPhase::kFadeIn;
    case BlinkPhase::kFadeIn:
      return BlinkPhase::kHoldOn;
    case BlinkPhase::kHoldOn:
      return BlinkPhase::kFadeOut;
  }
  return BlinkPhase::kFadeOut;
}

struct BlinkStep {
  float from = 1.0F;
  float to = 1.0F;
  std::int64_t duration_ms = anim_scene::kBlinkHoldMs;
};

BlinkStep step_for(BlinkPhase phase) {
  switch (phase) {
    case BlinkPhase::kFadeOut:
      return BlinkStep{1.0F, 0.0F, anim_scene::kBlinkFadeMs};
    case BlinkPhase::kHoldOff:
      return BlinkStep{0.0F, 0.0F, anim_scene::kBlinkHoldMs};
    case BlinkPhase::kFadeIn:
      return BlinkStep{0.0F, 1.0F, anim_scene::kBlinkFadeMs};
    case BlinkPhase::kHoldOn:
      return BlinkStep{1.0F, 1.0F, anim_scene::kBlinkHoldMs};
  }
  return BlinkStep{};
}

void legend(std::ostream& out, bool reduced_motion) {
  out << "  three real clients of dg::AnimationEngine:\n"
      << "    slide   an explicit animate() on `left`, ping-ponging, easing in and out\n"
      << "    hover   an implicit transition on `background_color` - hover the panel\n"
      << "    caret   a blinking cursor built from four chained one-shot animations\n";
  if (reduced_motion) {
    out << "  --reduced-motion: the slide finishes in one frame; the caret freezes solid\n"
        << "  rather than flickering at the frame-pacer's rate - see anim_window.cpp.\n";
  }
}

class Runner {
 public:
  Runner(dg::WindowManager& manager, dg::WindowId window, const Settings& settings,
         std::ostream& out)
      : settings_(settings), out_(&out), manager_(&manager), window_(window) {}

  int run();

 private:
  // Every one of these takes the scene/surface as a PLAIN REFERENCE rather
  // than reaching into scene_/surface_ itself - the same shape
  // opacity_window.cpp's own Runner uses. scene_/surface_ are dereferenced
  // exactly twice, both in run() itself right after the has_value() check
  // that makes it safe, and the resulting references are threaded through
  // from there; a method that dereferenced the optional member on its own
  // would be safe in practice (run() never calls one before resize_if_needed()
  // has succeeded) but not provably so from inside that one function, which
  // is what bugprone-unchecked-optional-access is unable to verify across a
  // function boundary.
  bool resize_if_needed();
  void draw(anim_scene::Scene& scene, dg::RasterSurface& surface);
  void start_slide(anim_scene::Scene& scene, bool forward);
  void start_blink(anim_scene::Scene& scene, BlinkPhase phase);
  void handle_pointer(anim_scene::Scene& scene, const dg::PointerEvent& event);
  void drain_engine_events(anim_scene::Scene& scene);

  Settings settings_;
  std::ostream* out_;
  dg::WindowManager* manager_;
  dg::WindowId window_;

  std::optional<anim_scene::Scene> scene_;
  std::optional<dg::RasterSurface> surface_;
  dg::AnimationEngine engine_;

  std::optional<dg::AnimHandle> slide_handle_;
  bool slide_forward_ = true;

  std::optional<dg::AnimHandle> blink_handle_;
  BlinkPhase blink_phase_ = BlinkPhase::kFadeOut;

  bool hovering_ = false;
};

void Runner::start_slide(anim_scene::Scene& scene, bool forward) {
  const float from = forward ? 0.0F : static_cast<float>(anim_scene::kSlideTravelPx);
  const float to = forward ? static_cast<float>(anim_scene::kSlideTravelPx) : 0.0F;
  slide_handle_ = engine_.animate(scene.tree, scene.handles.slide_chip, DG_PROP_LEFT,
                                  dg::PropValue::number(from), dg::PropValue::number(to),
                                  anim_scene::kSlideDurationMs, dg::kCurveEaseInOut);
  slide_forward_ = forward;
}

void Runner::start_blink(anim_scene::Scene& scene, BlinkPhase phase) {
  const BlinkStep step = step_for(phase);
  blink_phase_ = phase;
  blink_handle_ = engine_.animate(
      scene.tree, scene.handles.caret, DG_PROP_OPACITY, dg::PropValue::number(step.from),
      dg::PropValue::number(step.to), step.duration_ms, dg::kCurveLinear);
}

bool Runner::resize_if_needed() {
  const dg::Expected<dg::PixelSize, dg::WindowError> size = manager_->drawable_size(window_);
  if (!size) {
    return false;
  }
  const dg::PixelSize pixels = size.value();
  if (scene_.has_value()) {
    anim_scene::Scene& scene = *scene_;
    if (surface_.has_value() && scene.tree.viewport() == pixels) {
      return true;
    }
    scene.tree.resize(pixels);
    scene.tree.layout();
  } else {
    scene_ = anim_scene::build(spec_for(pixels));
    anim_scene::Scene& scene = *scene_;
    engine_.set_reduced_motion(settings_.reduced_motion);
    engine_.set_transition(scene.handles.hover_panel, DG_PROP_BACKGROUND_COLOR,
                           anim_scene::kHoverTransitionMs, dg::kCurveEaseInOut);
    start_slide(scene, true);
    if (settings_.reduced_motion) {
      // The CLIENT'S OWN reduced-motion decision, layered on top of the
      // engine's unconditional "duration -> zero" policy - see this file's
      // header comment. Freezing solid, once, through the ordinary explicit
      // door (a zero-length no-op animation) rather than looping at all.
      blink_handle_ = engine_.animate(scene.tree, scene.handles.caret, DG_PROP_OPACITY,
                                      dg::PropValue::number(1.0F), dg::PropValue::number(1.0F),
                                      0, dg::kCurveLinear);
    } else {
      start_blink(scene, BlinkPhase::kFadeOut);
    }
  }
  surface_ = dg::RasterSurface::create(pixels.width, pixels.height);
  return surface_.has_value() && scene_.has_value();
}

void Runner::draw(anim_scene::Scene& scene, dg::RasterSurface& surface) {
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

void Runner::handle_pointer(anim_scene::Scene& scene, const dg::PointerEvent& event) {
  // An immediately-invoked lambda rather than a variable assigned in one
  // branch and read in the other - the same shape 4-9's own text-field slice
  // settled on for the identical reason: a plain `bool now_hovering =
  // hovering_;` followed by both branches overwriting it is a dead store on
  // its initializer, not a real read of the old value.
  const bool now_hovering = [&] {
    if (event.action == dg::PointerAction::kLeave) {
      return false;
    }
    const std::optional<dg::NodeId> hit =
        scene.tree.render().hit_test(dg::PixelPoint{event.x, event.y});
    return hit.has_value() && *hit == scene.handles.hover_panel;
  }();
  if (now_hovering == hovering_) {
    return;
  }
  hovering_ = now_hovering;
  (void)engine_.set_value(
      scene.tree, scene.handles.hover_panel, DG_PROP_BACKGROUND_COLOR,
      dg::PropValue::color(hovering_ ? anim_scene::kButtonHover : anim_scene::kButtonNormal));
}

void Runner::drain_engine_events(anim_scene::Scene& scene) {
  for (const dg::AnimEvent& event : engine_.poll_events()) {
    if (event.kind != dg::AnimEventKind::kCompleted) {
      continue;
    }
    if (slide_handle_.has_value() && event.handle == *slide_handle_) {
      start_slide(scene, !slide_forward_);
    } else if (blink_handle_.has_value() && event.handle == *blink_handle_) {
      start_blink(scene, next_phase(blink_phase_));
    }
  }
}

int Runner::run() {
  const Clock::time_point started = Clock::now();
  legend(*out_, settings_.reduced_motion);

  while (manager_->open_window_count() > 0) {
    if (settings_.run_ms > 0 && ms_since(started) >= settings_.run_ms) {
      manager_->request_close(window_);
    }

    // design.md section 5.15.1's three states: -1 blocks
    // (SDL_WaitEventTimeout's own "wait indefinitely" convention) while
    // nothing is animating; a short interval paces frames while something
    // is.
    const int timeout_ms = engine_.has_active() ? kFramePacerMs : -1;
    const dg::PumpResult pumped = manager_->pump(timeout_ms);

    const bool stale = !pumped.needs_repaint.empty() || !surface_.has_value();
    if (stale && !resize_if_needed()) {
      return 1;
    }
    if (!scene_.has_value() || !surface_.has_value()) {
      return 1;
    }
    anim_scene::Scene& scene = *scene_;
    dg::RasterSurface& surface = *surface_;

    for (const dg::PointerEvent& event : pumped.pointer) {
      handle_pointer(scene, event);
    }

    engine_.tick(scene.tree, dg::steady_anim_time());
    drain_engine_events(scene);
    draw(scene, surface);
  }
  return 0;
}

}  // namespace

int run(const Settings& settings, std::ostream& out) {
  dg::Expected<dg::WindowManager, dg::WindowError> made = dg::WindowManager::create();
  if (!made) {
    out << "error: " << made.error().message << "\n";
    return 1;
  }
  dg::WindowManager manager = std::move(made.value());

  dg::WindowSpec spec;
  spec.title = "drawgui animation";
  spec.width = settings.size.width;
  spec.height = settings.size.height;
  spec.fill = dg::Color::from_argb(0xFF14171C);
  const dg::Expected<dg::WindowId, dg::WindowError> window = manager.open(spec);
  if (!window) {
    out << "error: " << window.error().message << "\n";
    return 1;
  }
  Runner runner(manager, window.value(), settings, out);
  return runner.run();
}

int dump_png(const Settings& settings, const std::string& path, std::ostream& out) {
  std::optional<dg::RasterSurface> surface =
      dg::RasterSurface::create(settings.size.width, settings.size.height);
  if (!surface.has_value()) {
    out << "error: could not create a raster surface\n";
    return 1;
  }
  anim_scene::Scene scene = anim_scene::build(spec_for(settings.size));
  scene.tree.render().repaint_full(*surface);
  const std::vector<std::uint8_t> png = surface->encode_png();
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    out << "error: could not open " << path << "\n";
    return 1;
  }
  file.write(reinterpret_cast<const char*>(png.data()),
             static_cast<std::streamsize>(png.size()));
  out << "wrote " << path << " (" << png.size() << " bytes, initial-state frame - see "
      << "--verify-animation for the animated assertions)\n";
  return 0;
}

int idle_probe(const Settings& settings, int idle_probe_ms, std::ostream& out) {
  dg::Expected<dg::WindowManager, dg::WindowError> made = dg::WindowManager::create();
  if (!made) {
    out << "error: " << made.error().message << "\n";
    return 1;
  }
  dg::WindowManager manager = std::move(made.value());

  dg::WindowSpec spec;
  spec.title = "drawgui animation (idle probe)";
  spec.width = settings.size.width;
  spec.height = settings.size.height;
  spec.fill = dg::Color::from_argb(0xFF14171C);
  const dg::Expected<dg::WindowId, dg::WindowError> window = manager.open(spec);
  if (!window) {
    out << "error: " << window.error().message << "\n";
    return 1;
  }

  const auto to_ms = [](const timeval& tv) {
    return (static_cast<double>(tv.tv_sec) * 1000.0) +
           (static_cast<double>(tv.tv_usec) / 1000.0);
  };

  // Opening a window queues its own creation/expose events - draining them
  // here keeps the timed block below honest about steady-state idle rather
  // than measuring startup noise.
  (void)manager.pump(200);

  struct rusage before {};
  getrusage(RUSAGE_SELF, &before);
  const Clock::time_point wall_start = Clock::now();

  // A single pump() call is not a reliable measurement on a real desktop:
  // the window system itself delivers occasional events (focus, an
  // expose from the compositor) that are not this engine's doing and would
  // otherwise end the block early and understate what was asked for. This
  // loops pump() with the REMAINING time each time it wakes early, so the
  // total wall time matches what was requested regardless of how many
  // spurious wakeups happened - design.md section 5.15.1's "空闲" row
  // measured over its full requested span rather than over whichever
  // fraction of it the first wakeup happened to cover.
  int wakeups = 0;
  double remaining_ms = static_cast<double>(idle_probe_ms);
  while (remaining_ms > 0.0) {
    (void)manager.pump(static_cast<int>(std::lround(remaining_ms)));
    ++wakeups;
    remaining_ms = static_cast<double>(idle_probe_ms) - ms_since(wall_start);
  }

  const double wall_ms = ms_since(wall_start);
  struct rusage after {};
  getrusage(RUSAGE_SELF, &after);
  const double cpu_ms = (to_ms(after.ru_utime) - to_ms(before.ru_utime)) +
                        (to_ms(after.ru_stime) - to_ms(before.ru_stime));

  out << "idle probe (design.md section 5.15.1's \"wait_events() blocks, CPU 0%\"):\n"
      << "  requested block:  " << idle_probe_ms << " ms\n"
      << "  actual wall time: " << wall_ms << " ms across " << wakeups << " pump() call(s)\n"
      << "  process CPU time consumed (user+sys) while blocked: " << cpu_ms << " ms\n"
      << "  CPU utilisation over the block: "
      << (wall_ms > 0.0 ? (cpu_ms / wall_ms * 100.0) : 0.0) << "%\n";
  return 0;
}

}  // namespace anim_window
