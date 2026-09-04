// Canvas - the drawing surface handed to render objects.
//
// design.md section 5.3: a thin wrapper over SkCanvas, converging on the ~40
// drawing operations this project actually uses. P0 defines only the handful
// the golden-image pipeline consumes; operations are added when a render
// object or theme feature needs them (design.md section 5.3.3, admission
// rule 4: no capability without a real consumer).
//
// Rendering quality is not a parameter. design.md section 5.3.4 fixes
// anti-aliasing, sampling and dithering to a single configuration, because
// every exposed quality knob multiplies the golden-image baseline matrix.

#pragma once

#include "drawgui/graphics/types.h"

class SkCanvas;

namespace dg {

class RasterSurface;

// A non-owning view onto a drawing target. Valid only while the surface that
// produced it is alive.
class Canvas {
 public:
  // Replaces every pixel, ignoring any existing content and blending.
  void clear(Color color);

  void fill_rect(const Rect& rect, Color color);
  void stroke_rect(const Rect& rect, Color color, float stroke_width);

  // With zero radii these are equivalent to the rect forms; the rounded path
  // is kept separate because Skia has a dedicated fast path for it.
  void fill_rrect(const Rect& rect, const Radii& radii, Color color);
  void stroke_rrect(const Rect& rect, const Radii& radii, Color color, float stroke_width);

 private:
  friend class RasterSurface;

  explicit Canvas(SkCanvas* canvas) : canvas_(canvas) {}

  SkCanvas* canvas_;
};

}  // namespace dg
