#include "anim_check.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <vector>

#include "drawgui/anim/animation_engine.h"
#include "drawgui/anim/clock.h"
#include "drawgui/anim/curve.h"
#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/props/node_props.h"
#include "drawgui/render/prop_ids.generated.h"
#include "drawgui/render/render_tree.h"

#include "anim_scene.h"

namespace anim_check {
namespace {

using dg::AnimationEngine;
using dg::AnimEventKind;
using dg::AnimHandle;
using dg::AnimTime;
using dg::Color;
using dg::PixelRect;
using dg::PropValue;

std::uint32_t pixel_at(const dg::PixelView& view, int x, int y) {
  const std::size_t offset =
      (static_cast<std::size_t>(y) * view.row_bytes) + (static_cast<std::size_t>(x) * 4);
  return (static_cast<std::uint32_t>(view.pixels[offset + 3]) << 24U) |
         (static_cast<std::uint32_t>(view.pixels[offset + 2]) << 16U) |
         (static_cast<std::uint32_t>(view.pixels[offset + 1]) << 8U) |
         static_cast<std::uint32_t>(view.pixels[offset]);
}

// The SAME formula AnimationEngine's own lerp_color (src/anim/
// animation_engine.cpp) computes - recomputed independently here, matching
// examples/16_complex_properties's own discipline of never reading an
// expectation off a previous run.
Color lerp_color_independent(Color from, Color to, float t) {
  const auto mix = [t](std::uint8_t a, std::uint8_t b) {
    const float blended =
        static_cast<float>(a) + ((static_cast<float>(b) - static_cast<float>(a)) * t);
    return static_cast<std::uint8_t>(std::lround(static_cast<double>(blended)));
  };
  return Color::rgba(mix(from.red(), to.red()), mix(from.green(), to.green()),
                     mix(from.blue(), to.blue()));
}

// design.md section 5.16.1's ease-in-out has no name for a specific formula;
// this is the one src/anim/curve.cpp implements, recomputed here rather than
// called, so a broken implementation cannot pass its own check.
float ease_in_out_independent(float t) {
  return t < 0.5F ? 2.0F * t * t : 1.0F - (((-2.0F * t) + 2.0F) * ((-2.0F * t) + 2.0F) / 2.0F);
}

bool near(int got, int want, const char* what, std::ostream& out, bool& ok) {
  if (got != want) {
    out << "  FAIL " << what << ": got " << got << " expected " << want << "\n";
    ok = false;
    return false;
  }
  return true;
}

bool check_pixel(const dg::PixelView& view, int x, int y, Color expected, const char* what,
                 std::ostream& out, bool& ok) {
  const std::uint32_t got = pixel_at(view, x, y);
  const std::uint32_t want = expected.argb();
  if (got != want) {
    out << "  FAIL " << what << " at " << x << "," << y << ": got 0x" << std::hex << got
        << " expected 0x" << want << std::dec << "\n";
    ok = false;
    return false;
  }
  return true;
}

// 1. slide - explicit animate() on `left`, hand-derived ease-in-out, and the
// PIXEL actually moves.
bool check_slide(const dg::TreeSpec& spec, std::ostream& out) {
  bool ok = true;
  anim_scene::Scene scene = anim_scene::build(spec);
  AnimationEngine engine;
  const AnimHandle handle = engine.animate(
      scene.tree, scene.handles.slide_chip, DG_PROP_LEFT, PropValue::number(0.0F),
      PropValue::number(static_cast<float>(anim_scene::kSlideTravelPx)),
      anim_scene::kSlideDurationMs, dg::kCurveEaseInOut);

  engine.tick(scene.tree, AnimTime{0});
  scene.tree.layout();
  near(scene.tree.box(scene.handles.slide_chip).left.value_or(-1), 0, "slide at t=0", out, ok);

  const std::int64_t half = anim_scene::kSlideDurationMs / 2;
  engine.tick(scene.tree, AnimTime{half});
  scene.tree.layout();
  const int expected_half = static_cast<int>(std::lround(
      static_cast<double>(ease_in_out_independent(0.5F)) * anim_scene::kSlideTravelPx));
  near(scene.tree.box(scene.handles.slide_chip).left.value_or(-1), expected_half,
       "slide at t=duration/2", out, ok);

  std::optional<dg::RasterSurface> surface =
      dg::RasterSurface::create(spec.viewport.width, spec.viewport.height);
  if (surface.has_value()) {
    scene.tree.render().repaint_full(*surface);
    const dg::PixelView view = surface->peek_pixels();
    const PixelRect chip = scene.tree.bounds(scene.handles.slide_chip);
    check_pixel(view, chip.left() + (chip.width / 2), chip.top() + (chip.height / 2),
                anim_scene::kChipFill, "slide chip's own colour, at its NEW position", out, ok);
  }

  engine.tick(scene.tree, AnimTime{anim_scene::kSlideDurationMs});
  scene.tree.layout();
  near(scene.tree.box(scene.handles.slide_chip).left.value_or(-1), anim_scene::kSlideTravelPx,
       "slide at t=duration", out, ok);
  if (engine.has_active()) {
    out << "  FAIL: slide is still active after reaching its declared duration\n";
    ok = false;
  }
  const std::vector<dg::AnimEvent> events = engine.poll_events();
  if (events.size() != 1 || events[0].handle != handle ||
      events[0].kind != AnimEventKind::kCompleted) {
    out << "  FAIL: slide did not raise exactly one kCompleted event for its own handle\n";
    ok = false;
  }
  return ok;
}

// 2. hover - the implicit transition's first trigger, and the retarget-mid-
// flight case, against the panel this scene actually draws.
bool check_hover_retarget(const dg::TreeSpec& spec, std::ostream& out) {
  bool ok = true;
  anim_scene::Scene scene = anim_scene::build(spec);
  AnimationEngine engine;
  engine.set_transition(scene.handles.hover_panel, DG_PROP_BACKGROUND_COLOR,
                        anim_scene::kHoverTransitionMs, dg::kCurveEaseInOut);

  engine.set_value(scene.tree, scene.handles.hover_panel, DG_PROP_BACKGROUND_COLOR,
                   PropValue::color(anim_scene::kButtonHover));
  engine.tick(scene.tree, AnimTime{0});
  const std::int64_t half = anim_scene::kHoverTransitionMs / 2;
  engine.tick(scene.tree, AnimTime{half});
  const Color at_half = lerp_color_independent(
      anim_scene::kButtonNormal, anim_scene::kButtonHover,
      ease_in_out_independent(static_cast<float>(half) /
                              static_cast<float>(anim_scene::kHoverTransitionMs)));
  if (scene.tree.render().style(scene.handles.hover_panel).fill.argb() != at_half.argb()) {
    out << "  FAIL hover halfway colour: got 0x" << std::hex
        << scene.tree.render().style(scene.handles.hover_panel).fill.argb() << " expected 0x"
        << at_half.argb() << std::dec << "\n";
    ok = false;
  }

  // RETARGET: pointer leaves before the hover-in transition finished.
  engine.set_value(scene.tree, scene.handles.hover_panel, DG_PROP_BACKGROUND_COLOR,
                   PropValue::color(anim_scene::kButtonNormal));
  engine.tick(scene.tree, AnimTime{half});  // same instant: must read exactly `at_half`
  if (scene.tree.render().style(scene.handles.hover_panel).fill.argb() != at_half.argb()) {
    out << "  FAIL retarget did not preserve the pre-retarget colour at the same instant\n";
    ok = false;
  }
  engine.tick(scene.tree, AnimTime{half + anim_scene::kHoverTransitionMs});
  if (scene.tree.render().style(scene.handles.hover_panel).fill.argb() !=
      anim_scene::kButtonNormal.argb()) {
    out << "  FAIL retargeted transition did not reach kButtonNormal\n";
    ok = false;
  }
  return ok;
}

// 3. caret - the four-phase blink, chained by completion events, plus the
// reduced-motion CLIENT pattern (freeze solid, never loop).
bool check_caret_blink(const dg::TreeSpec& spec, std::ostream& out) {
  bool ok = true;
  anim_scene::Scene scene = anim_scene::build(spec);
  AnimationEngine engine;
  const AnimHandle fade_out =
      engine.animate(scene.tree, scene.handles.caret, DG_PROP_OPACITY, PropValue::number(1.0F),
                     PropValue::number(0.0F), anim_scene::kBlinkFadeMs, dg::kCurveLinear);
  engine.tick(scene.tree, AnimTime{0});
  engine.tick(scene.tree, AnimTime{anim_scene::kBlinkFadeMs});
  if (scene.tree.render().style(scene.handles.caret).opacity != 0.0F) {
    out << "  FAIL: fade-out phase did not reach opacity 0\n";
    ok = false;
  }
  const std::vector<dg::AnimEvent> after_fade = engine.poll_events();
  if (after_fade.size() != 1 || after_fade[0].handle != fade_out) {
    out << "  FAIL: fade-out phase did not raise its own completion event\n";
    ok = false;
  }

  // Reduced motion, the CLIENT'S OWN pattern (see anim_window.cpp): freeze
  // solid rather than loop. A no-op animate() (from == to == 1) still
  // completes on the very next tick, per the engine's unconditional
  // duration-collapse policy, and nothing re-triggers afterward.
  AnimationEngine reduced_engine;
  reduced_engine.set_reduced_motion(true);
  const AnimHandle frozen = reduced_engine.animate(
      scene.tree, scene.handles.caret, DG_PROP_OPACITY, PropValue::number(1.0F),
      PropValue::number(1.0F), anim_scene::kBlinkFadeMs, dg::kCurveLinear);
  reduced_engine.tick(scene.tree, AnimTime{0});
  if (scene.tree.render().style(scene.handles.caret).opacity != 1.0F) {
    out << "  FAIL: reduced-motion freeze did not read as solid (opacity 1)\n";
    ok = false;
  }
  if (reduced_engine.has_active()) {
    out << "  FAIL: reduced-motion freeze left the engine active with nothing left to run\n";
    ok = false;
  }
  (void)frozen;
  // Many more ticks, far apart: nothing re-triggers this handle (the client
  // chose not to loop), so the engine stays idle throughout.
  for (const std::int64_t t : {1000, 5000, 20000}) {
    reduced_engine.tick(scene.tree, AnimTime{t});
    if (reduced_engine.has_active()) {
      out << "  FAIL: reduced-motion engine unexpectedly became active again\n";
      ok = false;
    }
  }
  return ok;
}

// 4. has_active(): true while running, false once nothing is - the exact
// signal design.md section 5.15.1's on-demand frame loop reads to decide
// between a short pump() timeout and blocking indefinitely.
bool check_has_active_state_machine(const dg::TreeSpec& spec, std::ostream& out) {
  bool ok = true;
  anim_scene::Scene scene = anim_scene::build(spec);
  AnimationEngine engine;
  if (engine.has_active()) {
    out << "  FAIL: a fresh engine reports has_active() before anything was created\n";
    ok = false;
  }
  const AnimHandle handle =
      engine.animate(scene.tree, scene.handles.caret, DG_PROP_OPACITY, PropValue::number(1.0F),
                     PropValue::number(0.0F), 50, dg::kCurveLinear);
  if (!engine.has_active()) {
    out << "  FAIL: has_active() is false immediately after animate(), before any tick()\n";
    ok = false;
  }
  engine.tick(scene.tree, AnimTime{0});
  engine.tick(scene.tree, AnimTime{50});
  if (engine.has_active()) {
    out << "  FAIL: has_active() is still true after the only animation finished\n";
    ok = false;
  }
  (void)handle;
  return ok;
}

}  // namespace

int run(std::ostream& out) {
  out << "ANIMATION: dg::AnimationEngine over the real demo scene - explicit slide, implicit\n"
         "  hover transition, chained-event caret blink, and the has_active() state "
         "machine.\n\n";

  dg::TreeSpec spec;
  spec.viewport = anim_scene::kDemoViewport;
  spec.background.fill = Color::from_argb(0xFF14171C);

  const bool slide_ok = check_slide(spec, out);
  out << "  slide oracle: " << (slide_ok ? "PASS" : "FAIL") << "\n";

  const bool hover_ok = check_hover_retarget(spec, out);
  out << "  hover retarget oracle: " << (hover_ok ? "PASS" : "FAIL") << "\n";

  const bool caret_ok = check_caret_blink(spec, out);
  out << "  caret blink + reduced-motion oracle: " << (caret_ok ? "PASS" : "FAIL") << "\n";

  const bool state_machine_ok = check_has_active_state_machine(spec, out);
  out << "  has_active() state-machine oracle: " << (state_machine_ok ? "PASS" : "FAIL")
      << "\n";

  const bool ok = slide_ok && hover_ok && caret_ok && state_machine_ok;
  out << (ok ? "\nPASS\n" : "\nFAIL\n");
  return ok ? 0 : 1;
}

}  // namespace anim_check
