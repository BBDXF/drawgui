// The two things this demo can do without a display: prove itself correct,
// and time itself.
//
// Kept out of main.cpp because neither needs a window, and because the
// verification is the more important of the two - a damage system that is
// fast and wrong is worse than one that is slow.

#pragma once

#include <cstddef>
#include <iosfwd>

#include "drawgui/render/render_tree.h"

namespace selfcheck {

struct Config {
  dg::PixelSize viewport;
  std::size_t max_damage_rects = dg::DamageRegion::kDefaultMaxRects;
  dg::PaintMode paint_mode = dg::PaintMode::kDirect;
  int frames = 240;
};

// Runs the scene twice - once repainting only what was marked dirty, once
// repainting everything - and requires the two framebuffers to be identical
// byte for byte after every frame. Returns false on the first frame that
// disagrees, having reported where.
[[nodiscard]] bool verify_damage(const Config& config, std::ostream& out);

// Rasterization only, at four resolutions, comparing damage against full and
// direct traversal against picture replay. Presentation is not measurable
// without a window, so the demo prints that half when it exits.
void bench(int frames, std::ostream& out);

}  // namespace selfcheck
