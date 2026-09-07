#include "opacity_check.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <ostream>
#include <set>
#include <vector>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/graphics/types.h"
#include "drawgui/render/render_tree.h"

#include "opacity_scene.h"

namespace opacity_check {
namespace {

using dg::NodeId;
using dg::PixelPoint;
using dg::PixelRect;
using dg::PixelSize;

// Wide enough that the chips keep a solo band, narrow enough that the layout
// clamps a definite chip width to the panel's content box - so the ladder
// varies both where a layer's extent sits and how wide it is.
constexpr int kWidths[] = {1560, 1400, 1180, 1000, 900, 820};
constexpr int kHeight = 460;

constexpr std::size_t kChipCount = 3;

struct Frame {
  opacity_scene::Scene scene;
  dg::RasterSurface surface;
};

std::optional<Frame> render(PixelSize size) {
  std::optional<dg::RasterSurface> surface = dg::RasterSurface::create(size.width, size.height);
  if (!surface.has_value()) {
    return std::nullopt;
  }
  dg::TreeSpec spec;
  spec.viewport = size;
  spec.background.fill = dg::Color::from_argb(0xFF14171C);
  opacity_scene::Scene scene = opacity_scene::build(spec);
  scene.tree.render().repaint_full(*surface);
  return Frame{std::move(scene), *std::move(surface)};
}

std::uint32_t pixel_at(const dg::PixelView& view, PixelPoint point) {
  const std::size_t offset = (static_cast<std::size_t>(point.y) * view.row_bytes) +
                             (static_cast<std::size_t>(point.x) * 4);
  return (static_cast<std::uint32_t>(view.pixels[offset + 2]) << 16U) |
         (static_cast<std::uint32_t>(view.pixels[offset + 1]) << 8U) |
         static_cast<std::uint32_t>(view.pixels[offset]);
}

// How many of `chips` cover `point`, and which of them is on top. Derived from
// the bounds the tree reports rather than from anything the painter did, so it
// is a statement about the scene rather than a reading of the picture.
struct Coverage {
  std::size_t count = 0;
  std::size_t topmost = 0;
};

Coverage coverage_at(const opacity_scene::Scene& scene, const std::vector<NodeId>& chips,
                     PixelPoint point) {
  Coverage found;
  for (std::size_t index = 0; index < chips.size(); ++index) {
    if (dg::contains(scene.tree.bounds(chips[index]), point)) {
      ++found.count;
      found.topmost = index;
    }
  }
  return found;
}

PixelRect union_of(const opacity_scene::Scene& scene, const std::vector<NodeId>& chips) {
  PixelRect all;
  for (const NodeId chip : chips) {
    all = dg::join(all, scene.tree.bounds(chip));
  }
  return all;
}

// What the two comparable panels showed, compared pixel by pixel over the
// chips they have in common.
struct Comparison {
  std::size_t single_pixels = 0;
  std::size_t overlapped_pixels = 0;

  // The largest per-channel difference between the two panels among pixels one
  // chip covers, and the SMALLEST among pixels two chips cover. The claim is
  // that the first stays within a rounding level and the second does not.
  int worst_single_delta = 0;
  int least_overlap_delta = 255;

  // Pixels the two panels do not classify the same way, because the layout's
  // remainder made one of them a pixel wider. Skipped, and bounded.
  std::size_t unclassifiable = 0;

  // One entry per topmost chip. A group that fades as one image has exactly
  // one colour per entry; per-object alpha has one per depth.
  std::set<std::uint32_t> grouped_tones[kChipCount];
  std::set<std::uint32_t> per_object_tones[kChipCount];
};

// The offset that carries a pixel of the per-object panel onto the matching
// pixel of the grouped one, taken from the first chip of each.
//
// It is an offset and NOT a congruence, which took a measurement to accept.
// The panels are grow=1 siblings, the free space is split by exact integer
// division, and the remainder lands on some of them and not others - so at
// 900x460 the left panel is 203 wide and the one beside it 204, and a chip
// whose definite width the panel's content box clamps inherits that one
// pixel. Requiring the two panels to be congruent simply refused to run at
// half the ladder. What the comparison does instead is classify each pixel in
// BOTH panels and skip the few where the two classifications disagree, then
// bound how many it skipped.
PixelPoint panel_delta(const opacity_scene::Scene& scene) {
  const PixelRect left = scene.tree.bounds(scene.handles.per_object_chips[0]);
  const PixelRect right = scene.tree.bounds(scene.handles.grouped_chips[0]);
  return PixelPoint{right.x - left.x, right.y - left.y};
}

// The largest difference between two colours in any single channel.
int channel_distance(std::uint32_t a, std::uint32_t b) {
  int worst = 0;
  for (unsigned shift = 0; shift <= 16U; shift += 8U) {
    const int left = static_cast<int>((a >> shift) & 0xFFU);
    const int right = static_cast<int>((b >> shift) & 0xFFU);
    worst = std::max(worst, std::abs(left - right));
  }
  return worst;
}

Comparison compare_panels(const Frame& frame, PixelPoint offset) {
  const dg::PixelView view = frame.surface.peek_pixels();
  const std::vector<NodeId>& left = frame.scene.handles.per_object_chips;
  const std::vector<NodeId>& right = frame.scene.handles.grouped_chips;
  const PixelRect area = dg::join(union_of(frame.scene, left),
                                  union_of(frame.scene, right).offset_by(-offset.x, -offset.y));

  Comparison out;
  for (int y = area.top(); y < area.bottom(); ++y) {
    for (int x = area.left(); x < area.right(); ++x) {
      const PixelPoint here{x, y};
      const PixelPoint there{x + offset.x, y + offset.y};
      const Coverage cover = coverage_at(frame.scene, left, here);
      const Coverage mirrored = coverage_at(frame.scene, right, there);
      if (cover.count == 0 && mirrored.count == 0) {
        continue;
      }
      if (cover.count != mirrored.count || cover.topmost != mirrored.topmost) {
        ++out.unclassifiable;
        continue;
      }
      const std::uint32_t per_object = pixel_at(view, here);
      const std::uint32_t grouped = pixel_at(view, there);

      out.per_object_tones[cover.topmost].insert(per_object);
      out.grouped_tones[cover.topmost].insert(grouped);

      const int distance = channel_distance(per_object, grouped);

      if (cover.count == 1) {
        ++out.single_pixels;
        out.worst_single_delta = std::max(out.worst_single_delta, distance);
      } else {
        ++out.overlapped_pixels;
        out.least_overlap_delta = std::min(out.least_overlap_delta, distance);
      }
    }
  }
  return out;
}

// The fraction of the way from the card's colour to the chip's that a pixel
// ended up, which is the effective alpha the compositor applied - together
// with the resolution that recovery has.
//
// ONLY HIGH-CONTRAST CHANNELS ARE USED, and that is load-bearing rather than
// tidy. A channel whose card and chip differ by 32 levels recovers alpha in
// steps of 1/32, so a single level of eight-bit rounding moves the answer by
// 0.03 - which is four times the difference between "fades twice" and "fades
// once and a bit". Averaging such a channel in does not cancel the noise, it
// imports it. The first cut of this check did exactly that and reported a
// nested alpha of 0.240 against a true 0.251, entirely from the blue channel.
struct Recovered {
  double alpha = -1.0;
  double resolution = 1.0;
};

Recovered effective_alpha(std::uint32_t observed, std::uint32_t card, std::uint32_t chip) {
  constexpr double kMinSpan = 64.0;
  double total = 0.0;
  double narrowest = 255.0;
  int used = 0;
  for (unsigned shift = 0; shift <= 16U; shift += 8U) {
    const auto channel = [shift](std::uint32_t value) {
      return static_cast<double>((value >> shift) & 0xFFU);
    };
    const double span = channel(chip) - channel(card);
    if (std::abs(span) < kMinSpan) {
      continue;
    }
    total += (channel(observed) - channel(card)) / span;
    narrowest = std::min(narrowest, std::abs(span));
    ++used;
  }
  if (used == 0) {
    return Recovered{};
  }
  return Recovered{total / static_cast<double>(used), 1.0 / narrowest};
}

bool check_group_versus_per_object(const Frame& frame, std::ostream& out) {
  const Comparison seen = compare_panels(frame, panel_delta(frame.scene));

  bool ok = true;

  // ONE LEVEL, NOT ZERO, and that is a measured property of the two routes
  // rather than a slackened assertion. Per-object alpha premultiplies the
  // chip's colour once; group opacity rasterizes the chip opaque into an
  // eight-bit layer and multiplies that, so the group's answer carries one
  // extra quantization. Measured across this scene the two agree exactly or
  // differ by a single level, never more - doc/compositing.md records it.
  if (seen.worst_single_delta > 1) {
    out << "    FAIL where ONE chip covers, the panels differ by " << seen.worst_single_delta
        << " levels; at most one is rounding\n";
    ok = false;
  }
  if (seen.least_overlap_delta < 8) {
    out << "    FAIL where TWO chips overlap, some pixel differs by only "
        << seen.least_overlap_delta
        << " levels; a group must resolve the overlap before it fades\n";
    ok = false;
  }
  if (seen.single_pixels < 2000 || seen.overlapped_pixels < 500) {
    out << "    FAIL the scene has too little of one class to prove anything: "
        << seen.single_pixels << " single, " << seen.overlapped_pixels << " overlapped\n";
    ok = false;
  }
  if (seen.unclassifiable * 50 > seen.single_pixels + seen.overlapped_pixels) {
    out << "    FAIL " << seen.unclassifiable
        << " pixels could not be classified, which is more than the layout's remainder "
        << "can explain\n";
    ok = false;
  }

  std::size_t grouped_tones = 0;
  std::size_t per_object_tones = 0;
  for (std::size_t index = 0; index < kChipCount; ++index) {
    grouped_tones = std::max(grouped_tones, seen.grouped_tones[index].size());
    per_object_tones = std::max(per_object_tones, seen.per_object_tones[index].size());
  }
  if (grouped_tones != 1) {
    out << "    FAIL a chip of the faded group shows " << grouped_tones
        << " tones; a group that fades as one image shows exactly one\n";
    ok = false;
  }
  if (per_object_tones < 2) {
    out << "    FAIL the per-object panel shows one tone per chip, so it is not "
        << "per-object alpha at all\n";
    ok = false;
  }

  out << "    group vs per-object: " << seen.single_pixels << " single pixels within "
      << seen.worst_single_delta << " level, " << seen.overlapped_pixels
      << " overlapped pixels differ by at least " << seen.least_overlap_delta
      << "; tones per chip " << grouped_tones << " grouped vs " << per_object_tones
      << " per-object; " << seen.unclassifiable << " skipped\n";
  return ok;
}

bool check_nested_multiplies(const Frame& frame, std::ostream& out) {
  const dg::PixelView view = frame.surface.peek_pixels();
  // The FIRST chip, whose solo band is the one no sibling can eat: it is
  // bounded on the left by the stage and on the right by chip 1, which starts
  // a fixed distance away. The last chip's band disappears as soon as the
  // panel is narrow enough to clamp it, which is a property of the window
  // rather than of compositing.
  const PixelRect band =
      opacity_scene::solo_band(frame.scene, frame.scene.handles.nested_chips, 0);
  const PixelRect single =
      opacity_scene::solo_band(frame.scene, frame.scene.handles.grouped_chips, 0);
  if (band.is_empty() || single.is_empty()) {
    out << "    FAIL the window is too narrow for a chip to keep a solo band\n";
    return false;
  }

  const auto middle = [](const PixelRect& rect) {
    return PixelPoint{rect.left() + (rect.width / 2), rect.top() + (rect.height / 2)};
  };

  // The card is sampled from the panel itself rather than assumed, so a theme
  // change does not silently turn this into a comparison against a constant.
  const PixelRect nested_box = frame.scene.tree.bounds(frame.scene.handles.nested_chips[0]);
  const std::uint32_t card =
      pixel_at(view, PixelPoint{middle(band).x, nested_box.bottom() + 4});

  // Asked of the scene rather than written down again here. A second copy of
  // the chip's colour is exactly the drift doc/properties.md exists to remove,
  // and it would make this check pass against a scene it no longer describes.
  const std::uint32_t chip =
      frame.scene.tree.render().style(frame.scene.handles.nested_chips[0]).fill.argb() &
      0xFFFFFFU;

  const Recovered one = effective_alpha(pixel_at(view, middle(single)), card, chip);
  const Recovered two = effective_alpha(pixel_at(view, middle(band)), card, chip);
  const double expected = one.alpha * one.alpha;
  const double tolerance = one.resolution + two.resolution;
  const bool ok =
      one.alpha > 0.0 && two.alpha > 0.0 && std::abs(two.alpha - expected) <= tolerance;

  out << "    nested fade: one layer a=" << one.alpha << ", two layers a=" << two.alpha
      << ", expected " << expected << " +/- " << tolerance << (ok ? "\n" : "  <-- FAIL\n");
  return ok;
}

std::vector<std::uint8_t> snapshot(const dg::RasterSurface& surface) {
  const dg::PixelView view = surface.peek_pixels();
  const auto row = static_cast<std::size_t>(view.width) * 4;
  std::vector<std::uint8_t> pixels(row * static_cast<std::size_t>(view.height));
  for (int y = 0; y < view.height; ++y) {
    std::memcpy(pixels.data() + (static_cast<std::size_t>(y) * row),
                view.pixels + (static_cast<std::size_t>(y) * view.row_bytes), row);
  }
  return pixels;
}

// Sixty frames of the fade the demo animates, incremental against full.
//
// A fade is the shape most likely to leave a trail: every frame changes every
// pixel of the group, and a damage rectangle that fell one pixel short of the
// layer's extent would leave a rim of the previous frame behind - visible on
// screen as a bright outline that never goes away.
bool check_fade_leaves_nothing_behind(PixelSize size, std::ostream& out) {
  std::optional<Frame> incremental = render(size);
  std::optional<Frame> whole = render(size);
  if (!incremental.has_value() || !whole.has_value()) {
    out << "    FAIL could not render\n";
    return false;
  }

  constexpr int kFrames = 60;
  for (int frame = 0; frame < kFrames; ++frame) {
    const float alpha = static_cast<float>((frame * 3) % 41) / 40.0F;
    opacity_scene::set_opacity(incremental->scene, incremental->scene.handles.fading_stage,
                               alpha);
    opacity_scene::set_opacity(whole->scene, whole->scene.handles.fading_stage, alpha);
    incremental->scene.tree.render().repaint(incremental->surface);
    whole->scene.tree.render().repaint_full(whole->surface);
    if (snapshot(incremental->surface) != snapshot(whole->surface)) {
      out << "    FAIL frame " << frame << " of the fade differs from a full repaint\n";
      return false;
    }
  }
  out << "    fade: " << kFrames << " frames, incremental byte-identical to full\n";
  return true;
}

// A stage faded to nothing: invisible, and still clickable at every pixel of
// every chip it holds.
bool check_invisible_is_still_clickable(PixelSize size, std::ostream& out) {
  std::optional<Frame> frame = render(size);
  if (!frame.has_value()) {
    out << "    FAIL could not render\n";
    return false;
  }
  opacity_scene::set_opacity(frame->scene, frame->scene.handles.fading_stage, 0.0F);
  frame->scene.tree.render().repaint_full(frame->surface);

  const dg::PixelView view = frame->surface.peek_pixels();
  const std::vector<NodeId>& chips = frame->scene.handles.fading_chips;
  const PixelRect area = union_of(frame->scene, chips);
  const std::uint32_t card = pixel_at(view, PixelPoint{area.left(), area.top()});

  std::size_t painted = 0;
  std::size_t unclickable = 0;
  for (int y = area.top(); y < area.bottom(); ++y) {
    for (int x = area.left(); x < area.right(); ++x) {
      const PixelPoint here{x, y};
      if (coverage_at(frame->scene, chips, here).count == 0) {
        continue;
      }
      if (pixel_at(view, here) != card) {
        ++painted;
      }
      const std::optional<NodeId> hit = frame->scene.tree.render().hit_test(here);
      const bool named =
          hit.has_value() && std::find(chips.begin(), chips.end(), *hit) != chips.end();
      if (!named) {
        ++unclickable;
      }
    }
  }

  const bool ok = painted == 0 && unclickable == 0;
  out << "    faded to zero: " << painted << " pixels still painted, " << unclickable
      << " pixels no longer clickable" << (ok ? "\n" : "  <-- FAIL\n");
  return ok;
}

}  // namespace

int run(std::ostream& out) {
  out << "opacity check\n";

  bool ok = true;
  std::set<int> distinct_extent_widths;
  std::set<int> distinct_chip_origins;

  for (const int width : kWidths) {
    const PixelSize size{width, kHeight};
    out << "  " << width << "x" << kHeight << "\n";

    const std::optional<Frame> frame = render(size);
    if (!frame.has_value()) {
      out << "    FAIL could not render\n";
      return 1;
    }
    distinct_chip_origins.insert(
        frame->scene.tree.bounds(frame->scene.handles.grouped_chips[0]).x);
    distinct_extent_widths.insert(
        union_of(frame->scene, frame->scene.handles.fading_chips).width);

    ok = check_group_versus_per_object(*frame, out) && ok;
    ok = check_nested_multiplies(*frame, out) && ok;
  }

  const PixelSize size{kWidths[0], kHeight};
  ok = check_fade_leaves_nothing_behind(size, out) && ok;
  ok = check_invisible_is_still_clickable(size, out) && ok;

  // A ladder that produced one layout would be four runs of one case, which is
  // the failure doc/wrapping.md records for the wrapping ladder.
  // A ladder that produced one layout would be six runs of one case, which is
  // the failure doc/wrapping.md records for the wrapping ladder. Both the SIZE
  // of a layer's extent and its POSITION have to vary: an extent computed in
  // local rather than absolute coordinates is wrong only in its position.
  if (distinct_extent_widths.size() < 3 || distinct_chip_origins.size() < 4) {
    out << "  FAIL the width ladder never varied the layer extent: "
        << distinct_extent_widths.size() << " extent widths, " << distinct_chip_origins.size()
        << " positions\n";
    ok = false;
  } else {
    out << "  the ladder produced " << distinct_extent_widths.size()
        << " distinct layer extents at " << distinct_chip_origins.size() << " positions\n";
  }

  out << (ok ? "opacity check PASSED\n" : "opacity check FAILED\n");
  return ok ? 0 : 1;
}

}  // namespace opacity_check
