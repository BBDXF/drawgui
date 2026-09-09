#include "form_check.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/render_tree.h"

#include "form_scene.h"

namespace form_check {
namespace {

using dg::NodeId;
using dg::PixelRect;
using dg::PixelSize;
using dg::RasterSurface;

dg::TreeSpec spec_for(PixelSize size) {
  dg::TreeSpec spec;
  spec.viewport = size;
  spec.background.fill = dg::Color::from_argb(0xFF0E1218);
  return spec;
}

constexpr PixelSize kSize{760, 420};

std::vector<std::uint8_t> snapshot(const RasterSurface& surface) {
  const dg::PixelView view = surface.peek_pixels();
  const auto* bytes = static_cast<const std::uint8_t*>(view.pixels);
  return std::vector<std::uint8_t>{
      bytes, bytes + (view.row_bytes * static_cast<std::size_t>(view.height))};
}

// The formula reposition_slider() implements, recomputed independently here
// so a defect in the widget code has something external to disagree with -
// not the tree read back a second time.
int expected_thumb_x(float value, float min_value, float max_value, int track_width,
                     int thumb_width) {
  const int travel = std::max(0, track_width - thumb_width);
  const float fraction = (value - min_value) / (max_value - min_value);
  return static_cast<int>(
      std::lround(static_cast<double>(fraction) * static_cast<double>(travel)));
}

// --------------------------------------------------------------------------
// Claim 1: the checkbox still works, unmodified by this slice's changes to
// toggle() - a regression check, not a new behaviour.
// --------------------------------------------------------------------------

bool check_checkbox_unaffected(std::ostream& out) {
  form_scene::Scene scene = form_scene::build(spec_for(kSize));
  bool ok = true;

  if (scene.widgets.is_checked(scene.handles.checkbox)) {
    out << "  FAIL: the checkbox starts checked; the scene is meant to start it clear\n";
    ok = false;
  }
  const bool first = scene.widgets.toggle(scene.handles.checkbox);
  if (!first || !scene.widgets.is_checked(scene.handles.checkbox)) {
    out << "  FAIL: toggling the plain checkbox once did not check it\n";
    ok = false;
  }
  const bool second = scene.widgets.toggle(scene.handles.checkbox);
  if (second || scene.widgets.is_checked(scene.handles.checkbox)) {
    out << "  FAIL: toggling the plain checkbox twice did not clear it - a plain checkbox "
           "must still flip freely, unlike a grouped one\n";
    ok = false;
  }
  if (!scene.widgets.group_members(scene.handles.checkbox).empty()) {
    out << "  FAIL: an ungrouped checkbox reported group members\n";
    ok = false;
  }

  if (ok) {
    out << "  OK: the plain checkbox still flips freely and reports no group members\n";
  }
  return ok;
}

// --------------------------------------------------------------------------
// Claim 2: radio group exclusivity, and cross-group independence.
// --------------------------------------------------------------------------

bool check_radio_exclusivity(std::ostream& out) {
  form_scene::Scene scene = form_scene::build(spec_for(kSize));
  bool ok = true;

  for (const NodeId option : scene.handles.radio_group_a) {
    if (scene.widgets.is_checked(option)) {
      out << "  FAIL: radio group A starts with an option already checked\n";
      ok = false;
    }
  }

  scene.widgets.toggle(scene.handles.radio_group_a[0]);
  if (!scene.widgets.is_checked(scene.handles.radio_group_a[0])) {
    out << "  FAIL: selecting radio a/0 did not check it\n";
    ok = false;
  }

  scene.widgets.toggle(scene.handles.radio_group_a[2]);
  if (!scene.widgets.is_checked(scene.handles.radio_group_a[2])) {
    out << "  FAIL: selecting radio a/2 did not check it\n";
    ok = false;
  }
  if (scene.widgets.is_checked(scene.handles.radio_group_a[0]) ||
      scene.widgets.is_checked(scene.handles.radio_group_a[1])) {
    out << "  FAIL: selecting radio a/2 left another option in group A checked\n";
    ok = false;
  }

  // Re-selecting the same option is a no-op, not a toggle-off.
  const bool reselected = scene.widgets.toggle(scene.handles.radio_group_a[2]);
  if (!reselected || !scene.widgets.is_checked(scene.handles.radio_group_a[2])) {
    out << "  FAIL: re-selecting the already-checked radio a/2 cleared it\n";
    ok = false;
  }

  // Group B is untouched by anything done to group A.
  for (const NodeId option : scene.handles.radio_group_b) {
    if (scene.widgets.is_checked(option)) {
      out << "  FAIL: group B was affected by group A's selections\n";
      ok = false;
    }
  }
  scene.widgets.toggle(scene.handles.radio_group_b[1]);
  if (!scene.widgets.is_checked(scene.handles.radio_group_b[1])) {
    out << "  FAIL: selecting radio b/1 did not check it\n";
    ok = false;
  }
  if (!scene.widgets.is_checked(scene.handles.radio_group_a[2])) {
    out << "  FAIL: selecting a group B option cleared group A's selection\n";
    ok = false;
  }

  if (ok) {
    out << "  OK: group A is mutually exclusive, re-selection is a no-op, and group B does "
           "not interfere with group A or vice versa\n";
  }
  return ok;
}

// --------------------------------------------------------------------------
// Claim 2b: a REAL click, through dispatch() - not raw toggle() - repaints
// both the newly-selected option and the sibling it just deselected.
//
// Found the hard way: the demo's own --preset-radio-a originally called
// toggle() directly and never repainted anything, so a preset changed the
// stored `checked` bit but the screenshot looked untouched. toggle() has no
// RenderTree to paint with by design (doc/form-controls.md section 1), so
// repainting is a caller obligation - this pins it against the actual
// dispatch() path a real click takes, not only the raw primitive.
// --------------------------------------------------------------------------

bool check_radio_click_repaints_both_widgets(std::ostream& out) {
  form_scene::Scene scene = form_scene::build(spec_for(kSize));
  dg::Interaction interaction;
  bool ok = true;

  const auto centre_of = [&](NodeId id) {
    const PixelRect bounds = scene.tree.bounds(id);
    return dg::PixelPoint{bounds.x + (bounds.width / 2), bounds.y + (bounds.height / 2)};
  };
  const auto click = [&](NodeId id) {
    const dg::PixelPoint at = centre_of(id);
    form_scene::dispatch(
        scene, interaction,
        dg::PointerEvent{dg::WindowId{}, dg::PointerAction::kMove, at.x, at.y});
    form_scene::dispatch(
        scene, interaction,
        dg::PointerEvent{dg::WindowId{}, dg::PointerAction::kDown, at.x, at.y});
    form_scene::dispatch(scene, interaction,
                         dg::PointerEvent{dg::WindowId{}, dg::PointerAction::kUp, at.x, at.y});
  };

  const NodeId first = scene.handles.radio_group_a[0];
  const NodeId second = scene.handles.radio_group_a[1];
  const NodeId first_indicator = scene.widgets.at(first).indicator;
  const NodeId second_indicator = scene.widgets.at(second).indicator;
  const dg::Color off = scene.tree.render().style(first_indicator).fill;

  click(first);
  if (scene.tree.render().style(first_indicator).fill == off) {
    out << "  FAIL: clicking radio a/0 did not repaint its indicator\n";
    ok = false;
  }

  click(second);
  if (scene.tree.render().style(second_indicator).fill == off) {
    out << "  FAIL: clicking radio a/1 did not repaint its own indicator\n";
    ok = false;
  }
  if (scene.tree.render().style(first_indicator).fill != off) {
    out << "  FAIL: selecting radio a/1 left radio a/0's indicator painted as checked\n";
    ok = false;
  }

  if (ok) {
    out << "  OK: a real click repaints both the selected option's indicator and the sibling "
           "it deselects\n";
  }
  return ok;
}

// --------------------------------------------------------------------------
// Claim 3: slider clamping, and thumb geometry hand-derived from the
// formula reposition_slider() implements - not read back from the tree.
// --------------------------------------------------------------------------

bool check_slider_clamps_and_geometry(std::ostream& out) {
  form_scene::Scene scene = form_scene::build(spec_for(kSize));
  bool ok = true;

  const PixelRect volume_track = scene.tree.bounds(scene.handles.slider_volume);
  const PixelRect volume_thumb =
      scene.tree.render().local_bounds(scene.widgets.at(scene.handles.slider_volume).thumb);
  const int expected_initial =
      expected_thumb_x(form_scene::kVolumeInitial, form_scene::kVolumeMin,
                       form_scene::kVolumeMax, volume_track.width, volume_thumb.width);
  if (volume_thumb.x != expected_initial) {
    out << "  FAIL: volume thumb starts at x=" << volume_thumb.x << ", expected "
        << expected_initial << " (track " << volume_track.width << "px, thumb "
        << volume_thumb.width << "px, value " << form_scene::kVolumeInitial << " of ["
        << form_scene::kVolumeMin << "," << form_scene::kVolumeMax << "])\n";
    ok = false;
  }
  // The track is deliberately SHORTER than the thumb (a thin pill under a
  // round overlapping thumb), so the thumb must be centred on the track's
  // vertical midpoint rather than merely placed at its top - the two only
  // disagree because the two heights differ, which is exactly why they were
  // made to differ (doc/form-controls.md section 4).
  const int expected_y = (volume_track.height - volume_thumb.height) / 2;
  if (volume_thumb.y != expected_y) {
    out << "  FAIL: volume thumb sits at y=" << volume_thumb.y << ", expected " << expected_y
        << " (track " << volume_track.height << "px tall, thumb " << volume_thumb.height
        << "px tall, centred)\n";
    ok = false;
  }

  scene.widgets.set_slider_value(scene.tree.render(), scene.handles.slider_volume, -1000.0F);
  if (scene.widgets.slider_value(scene.handles.slider_volume) != form_scene::kVolumeMin) {
    out << "  FAIL: dragging far below the minimum did not clamp to " << form_scene::kVolumeMin
        << "\n";
    ok = false;
  }
  if (scene.tree.render().local_bounds(scene.widgets.at(scene.handles.slider_volume).thumb).x !=
      0) {
    out << "  FAIL: at the minimum, the thumb is not flush with the track's start\n";
    ok = false;
  }

  scene.widgets.set_slider_value(scene.tree.render(), scene.handles.slider_volume, 1000.0F);
  if (scene.widgets.slider_value(scene.handles.slider_volume) != form_scene::kVolumeMax) {
    out << "  FAIL: dragging far above the maximum did not clamp to " << form_scene::kVolumeMax
        << "\n";
    ok = false;
  }
  const int travel = volume_track.width - volume_thumb.width;
  if (scene.tree.render().local_bounds(scene.widgets.at(scene.handles.slider_volume).thumb).x !=
      travel) {
    out << "  FAIL: at the maximum, the thumb is not flush with the track's end (expected x="
        << travel << ")\n";
    ok = false;
  }

  // The stepped slider: 3.0 is already a whole step, so it must land exactly
  // - the day snapping is broken, this is the case least likely to hide it.
  const PixelRect brightness_track = scene.tree.bounds(scene.handles.slider_brightness);
  const PixelRect brightness_thumb =
      scene.tree.render().local_bounds(scene.widgets.at(scene.handles.slider_brightness).thumb);
  const int expected_brightness = expected_thumb_x(
      form_scene::kBrightnessInitial, form_scene::kBrightnessMin, form_scene::kBrightnessMax,
      brightness_track.width, brightness_thumb.width);
  if (brightness_thumb.x != expected_brightness) {
    out << "  FAIL: brightness thumb starts at x=" << brightness_thumb.x << ", expected "
        << expected_brightness << "\n";
    ok = false;
  }
  scene.widgets.set_slider_value(scene.tree.render(), scene.handles.slider_brightness, 4.4F);
  if (scene.widgets.slider_value(scene.handles.slider_brightness) != 4.0F) {
    out << "  FAIL: 4.4 did not snap down to 4 under step=1\n";
    ok = false;
  }
  scene.widgets.set_slider_value(scene.tree.render(), scene.handles.slider_brightness, 4.6F);
  if (scene.widgets.slider_value(scene.handles.slider_brightness) != 5.0F) {
    out << "  FAIL: 4.6 did not snap up to 5 under step=1\n";
    ok = false;
  }

  if (ok) {
    out << "  OK: both sliders clamp at their bounds, park the thumb flush with the track at "
           "either end, and the stepped slider snaps 4.4->4 and 4.6->5\n";
  }
  return ok;
}

// --------------------------------------------------------------------------
// Claim 4: a resize widens the grow-weighted slider's track, and the thumb
// follows the SAME fraction on the new geometry - the fixed-width slider is
// the control case that must NOT move.
// --------------------------------------------------------------------------

bool check_resize_reflows_thumb(std::ostream& out) {
  form_scene::Scene scene = form_scene::build(spec_for(kSize));
  bool ok = true;

  const PixelRect volume_before = scene.tree.bounds(scene.handles.slider_volume);
  const PixelRect brightness_before = scene.tree.bounds(scene.handles.slider_brightness);
  const int brightness_thumb_x_before =
      scene.tree.render()
          .local_bounds(scene.widgets.at(scene.handles.slider_brightness).thumb)
          .x;

  const PixelSize wider{kSize.width + 400, kSize.height};
  scene.tree.resize(wider);
  scene.tree.layout();
  scene.widgets.resync_sliders(scene.tree.render());

  const PixelRect volume_after = scene.tree.bounds(scene.handles.slider_volume);
  const PixelRect brightness_after = scene.tree.bounds(scene.handles.slider_brightness);
  if (volume_after.width <= volume_before.width) {
    out << "  FAIL: widening the window by 400px did not widen the grow-weighted track (was "
        << volume_before.width << ", now " << volume_after.width << ")\n";
    ok = false;
  }
  if (brightness_after.width != brightness_before.width) {
    out << "  FAIL: the fixed-width slider's track changed size on a resize (was "
        << brightness_before.width << ", now " << brightness_after.width << ")\n";
    ok = false;
  }

  const PixelRect volume_thumb =
      scene.tree.render().local_bounds(scene.widgets.at(scene.handles.slider_volume).thumb);
  const int expected_after_resize =
      expected_thumb_x(form_scene::kVolumeInitial, form_scene::kVolumeMin,
                       form_scene::kVolumeMax, volume_after.width, volume_thumb.width);
  if (volume_thumb.x != expected_after_resize) {
    out << "  FAIL: after widening, the volume thumb sits at x=" << volume_thumb.x
        << ", expected " << expected_after_resize << " for the new track width "
        << volume_after.width << "\n";
    ok = false;
  }

  const int brightness_thumb_x_after =
      scene.tree.render()
          .local_bounds(scene.widgets.at(scene.handles.slider_brightness).thumb)
          .x;
  if (brightness_thumb_x_after != brightness_thumb_x_before) {
    out << "  FAIL: the fixed-width slider's thumb moved on a resize that did not change its "
           "track (was x="
        << brightness_thumb_x_before << ", now x=" << brightness_thumb_x_after << ")\n";
    ok = false;
  }

  if (ok) {
    out << "  OK: widening the window grows the flexible track (from " << volume_before.width
        << "px to " << volume_after.width << "px) and moves its thumb to x=" << volume_thumb.x
        << " - the same 0.3 fraction of the new travel distance; the fixed-width slider's "
           "track and thumb do not move at all\n";
  }
  return ok;
}

// --------------------------------------------------------------------------
// Claim 5: dragging a slider costs a repaint, never a relayout - the same
// claim doc/scrolling.md section 4 measured for the scroll offset.
// --------------------------------------------------------------------------

bool check_no_relayout_during_drag(std::ostream& out) {
  form_scene::Scene scene = form_scene::build(spec_for(kSize));
  scene.tree.layout();  // settle any leftover dirt from build()

  scene.widgets.set_slider_value(scene.tree.render(), scene.handles.slider_volume, 70.0F);
  const dg::LayoutStats stats = scene.tree.layout();

  bool ok = true;
  if (stats.nodes_visited != 0 || stats.nodes_relaid_out != 0) {
    out << "  FAIL: a slider-only frame visited " << stats.nodes_visited << " and relaid out "
        << stats.nodes_relaid_out << " nodes; expected 0 of each\n";
    ok = false;
  }
  if (ok) {
    out << "  OK: layout() visited 0 nodes after a slider-value-only change - dragging a "
           "slider costs a repaint, never a relayout\n";
  }
  return ok;
}

// --------------------------------------------------------------------------
// Claim 6: resync_sliders() touches ONLY kSlider widgets. Found the hard
// way: a non-slider widget's `thumb` field defaults to NodeId{0} - the
// ROOT - so a resync_sliders() that forgot its own kind check moved the
// root node itself once per non-slider widget in the scene, silently
// corrupting the whole layout. Every geometry check above reads LOCAL
// bounds, which this defect does not touch, and the byte-identity check
// cannot see it either - both the incremental and full scenes are built
// (and corrupted) identically, so they still match each other pixel for
// pixel. Only a direct assertion on the ROOT's own bounds catches it.
// --------------------------------------------------------------------------

bool check_resync_does_not_move_root(std::ostream& out) {
  form_scene::Scene scene = form_scene::build(spec_for(kSize));

  // Compared against the rectangle root MUST have - (0,0) at the declared
  // viewport size - rather than against a snapshot taken after build(),
  // which already calls resync_sliders() once: a snapshot taken AFTER the
  // first (buggy) call would already be corrupted, and calling the same
  // deterministic bug a second time reproduces the identical corruption,
  // making an idempotent defect look like "no change". Found the hard way
  // - this was the first form of this check, and it did not catch
  // injection I at all.
  const PixelRect expected{0, 0, kSize.width, kSize.height};
  scene.widgets.resync_sliders(scene.tree.render());
  const PixelRect after = scene.tree.render().local_bounds(dg::RenderTree::root());

  bool ok = true;
  if (after != expected) {
    out << "  FAIL: resync_sliders() left the root node at " << after.x << "," << after.y << " "
        << after.width << "x" << after.height << ", expected " << expected.x << ","
        << expected.y << " " << expected.width << "x" << expected.height << "\n";
    ok = false;
  }
  if (ok) {
    out << "  OK: resync_sliders() left the root node at its declared viewport rectangle\n";
  }
  return ok;
}

// --------------------------------------------------------------------------
// The standing gate: incremental equals full, across a script mixing radio
// clicks, checkbox toggles and slider drags.
// --------------------------------------------------------------------------

bool check_identity(std::ostream& out) {
  std::optional<RasterSurface> damaged = RasterSurface::create(kSize.width, kSize.height);
  std::optional<RasterSurface> whole = RasterSurface::create(kSize.width, kSize.height);
  if (!damaged.has_value() || !whole.has_value()) {
    out << "  FAIL: could not allocate a raster surface\n";
    return false;
  }

  form_scene::Scene incremental = form_scene::build(spec_for(kSize));
  form_scene::Scene full = form_scene::build(spec_for(kSize));
  incremental.tree.render().repaint_full(*damaged);
  full.tree.render().repaint_full(*whole);

  const float volume_steps[] = {30.0F, 0.0F, 55.0F, 100.0F, 12.0F};
  const float brightness_steps[] = {3.0F, 7.4F, 0.0F, 10.0F, 5.0F};

  for (std::size_t step = 0; step < std::size(volume_steps); ++step) {
    const auto apply = [&](form_scene::Scene& scene) {
      scene.widgets.set_slider_value(scene.tree.render(), scene.handles.slider_volume,
                                     volume_steps[step]);
      scene.widgets.set_slider_value(scene.tree.render(), scene.handles.slider_brightness,
                                     brightness_steps[step]);
      if (step % 2 == 0) {
        scene.widgets.toggle(
            scene.handles.radio_group_a[step % scene.handles.radio_group_a.size()]);
      } else {
        scene.widgets.toggle(scene.handles.checkbox);
      }
    };
    apply(incremental);
    apply(full);

    incremental.tree.render().repaint(*damaged);
    full.tree.render().repaint_full(*whole);
    if (snapshot(*damaged) != snapshot(*whole)) {
      out << "  FAIL: pixels differ at step " << step << "\n";
      return false;
    }
  }

  out << "  OK: incremental repaint matches a full repaint, byte for byte, across a script "
         "mixing radio selection, checkbox toggles and slider drags on both sliders\n";
  return true;
}

}  // namespace

int run(std::ostream& out) {
  out << "verify: form controls, on the scene the demo puts on screen\n";
  bool ok = true;
  ok = check_checkbox_unaffected(out) && ok;
  ok = check_radio_exclusivity(out) && ok;
  ok = check_radio_click_repaints_both_widgets(out) && ok;
  ok = check_slider_clamps_and_geometry(out) && ok;
  ok = check_resize_reflows_thumb(out) && ok;
  ok = check_no_relayout_during_drag(out) && ok;
  ok = check_resync_does_not_move_root(out) && ok;
  ok = check_identity(out) && ok;
  out << (ok ? "PASS\n" : "FAIL\n");
  return ok ? 0 : 1;
}

}  // namespace form_check
