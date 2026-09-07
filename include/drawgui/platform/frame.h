// FrameTarget - what IWindow::begin_frame() hands to the graphics layer.
//
// design.md section 5.1 sketches this as begin_frame() returning a pointer to
// Skia's surface type. It is not spelled that way here, and the reasoning is
// worth recording because it is the one place where the literal design text
// and its own layering rule disagree.
//
// A platform header that names a Skia type makes layer 1 unbuildable without
// Skia and drags Skia into every consumer of IWindow - the host application,
// the ABI layer, and every test that only wanted a window. It also fixes a
// Ganesh-shaped assumption into the interface whose whole purpose (constraint
// C1) is to be shaped by no single implementation. A forward declaration
// would dodge the include but not the coupling: the type in the platform
// vocabulary would still be Skia's.
//
// So the split runs the other way round. The platform layer owns the window
// and its rendering context and describes the render target for this frame;
// the graphics layer owns Skia and turns that description into a surface
// through a GrDirectContext it alone holds. Neither layer needs the other's
// types, and the description below is a tagged handle - the same shape
// design.md section 5.14.4 already chose for native window handles, and the
// same shape every embedder API converges on for the same reason.
//
// The kinds listed are the ones design.md section 5.3.1 actually schedules.
// Vulkan and D3D are deliberately absent: that section admits a new backend
// only against measured evidence, and an enumerator here would be a promise
// nobody asked for.

#pragma once

#include <cstdint>

#include "drawgui/platform/types.h"

namespace dg {

enum class RenderTargetKind : std::uint8_t {
  // No surface this frame. Paired with PlatformError::kFrameNotAvailable it
  // would be redundant, so it exists for the case that is not an error at
  // all: an attached native window whose host has not yet given drawgui
  // anything to draw into.
  kNone,

  // handle is a GL framebuffer name (a GLuint). Zero is the default
  // framebuffer and is the normal value for an on-screen window, which is why
  // handle alone cannot signal absence and kind has to.
  kOpenGlFramebuffer,

  // handle is a CAMetalDrawable. Reacquired every frame, unlike a GL
  // framebuffer name.
  kMetalDrawable,
};

// A render target, described without naming any graphics API type.
//
// handle is an integer rather than a void* because the GL case carries a name
// and not an address; a pointer-shaped handle would force every backend to
// launder one through the other. The graphics layer casts it back according
// to kind, and that cast is the only place either side of this seam knows
// what the other is.
struct FrameTarget {
  RenderTargetKind kind = RenderTargetKind::kNone;
  std::uintptr_t handle = 0;

  // Physical pixels. Equal to IWindow::physical_size() except during a resize,
  // when the window has already reported its new size and the surface backing
  // this frame is still the old one. The graphics layer must trust this field
  // and not the window, or the first frame after every resize is scaled wrong.
  PixelSize pixel_size;

  // Samples per pixel of this target, 1 for no multisampling. design.md
  // section 5.3.4 fixes rendering quality and does not enable MSAA, so this
  // reports what the context was actually created with rather than offering a
  // choice.
  int sample_count = 1;

  int stencil_bits = 0;

  friend constexpr bool operator==(const FrameTarget&, const FrameTarget&) = default;
};

}  // namespace dg
