// The shared vocabulary of the Skia CPU gallery.
//
// This example talks to Skia directly rather than through dg::Canvas, and
// that is deliberate. Its subject IS Skia: it exists to show what the
// rasterizer can draw and to measure what each capability costs, so that step
// 3 of the plan can design layout and widgets against measured numbers. A
// forty-method wrapper written today would be shaped by "what Skia offers"
// rather than by what a widget needs, which is exactly the mistake this
// project already made once with the platform headers and then deleted.
//
// So: not a template for application code. The surface, the window and the
// blit all go through drawgui's real API; only the drawing does not.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "include/core/SkCanvas.h"
#include "include/core/SkImage.h"
#include "include/core/SkRect.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkSize.h"
#include "include/core/SkTypeface.h"

namespace gallery {

// The gallery is authored at a fixed size and scaled to whatever the window
// is, rather than reflowing. Two reasons, and the second is the important
// one: there is no layout engine yet to reflow with, and a benchmark that
// changes both the resolution and the content between samples measures
// neither. Same scene, four resolutions, is a comparison.
inline constexpr float kDesignWidth = 1400.0F;
inline constexpr float kDesignHeight = 920.0F;

// Everything loaded once and reused by every frame. A GUI toolkit amortizes
// exactly these costs, so charging them to each frame would flatter the
// alternative and slander CPU raster.
struct Resources {
  sk_sp<SkTypeface> sans;
  sk_sp<SkTypeface> sans_bold;
  sk_sp<SkTypeface> serif;
  sk_sp<SkTypeface> mono;
  sk_sp<SkTypeface> cjk;
  sk_sp<SkImage> photo;

  // What actually loaded, so a run says so on stdout instead of silently
  // drawing boxes where the Chinese should be.
  std::vector<std::string> notes;

  [[nodiscard]] bool complete() const;
};

[[nodiscard]] Resources load_resources(const std::string& font_dir);

// The grouping the benchmark isolates by. Blur and text are the two costs
// worth naming separately on a CPU rasterizer, so they are the two groups
// that can be switched off.
enum class Category : std::uint8_t {
  kGeometry,
  kText,
  kEffects,
};

struct Panel {
  const char* title;
  Category category;

  // A plain function pointer rather than std::function: a per-panel heap
  // allocation inside the thing being timed is a measurement artifact.
  // `body` is panel-local, with its origin already translated to (0, 0).
  void (*draw)(SkCanvas& canvas, const Resources& resources, const SkRect& body);
};

[[nodiscard]] std::vector<Panel> geometry_panels();
[[nodiscard]] std::vector<Panel> text_panels();
[[nodiscard]] std::vector<Panel> effect_panels();
[[nodiscard]] std::vector<Panel> all_panels();

struct Selection {
  bool geometry = true;
  bool text = true;
  bool effects = true;

  [[nodiscard]] bool includes(Category category) const;
};

// Draws one whole frame, scaled to `target`. Honours whatever clip the caller
// has already set, which is what makes a dirty-rect repaint cost less than a
// full one rather than merely present less.
void draw_frame(SkCanvas& canvas, const Resources& resources, const SkISize& target,
                const Selection& selection);

}  // namespace gallery
