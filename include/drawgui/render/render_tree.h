// RenderTree - a retained tree of nodes that remembers what changed.
//
// This is the smallest thing that can answer the question step 2 left open:
// full-window repaint at 1080p costs 3.91 ms of raster plus 7.71 ms of
// presentation and is already over the 16.6 ms budget at p95, while the same
// scene under a 260x72 clip costs 0.12 ms and does not grow with resolution.
// A widget system that repaints everything would have to be retrofitted with
// damage tracking, so damage tracking comes first and widgets are born
// knowing about it.
//
// There is no virtual function here, no node interface and no visitor. A node
// is a rectangle plus a fixed set of appearance fields, and painting is a
// switch-free function over those fields - design.md section 5.15.3 asks for
// exactly that storage shape, and this project has already once written an
// abstraction ahead of its implementation and deleted it. When a node needs
// to draw something this struct cannot express, the struct grows a field or
// the set of node kinds grows an enumerator; neither requires a vtable.
//
// Coordinates are physical device pixels throughout. Layout will introduce
// logical pixels and a DPI transform (design.md section 5.4.9); this layer is
// below that and speaks the framebuffer's units, because damage rectangles
// have to line up with the pixels present() copies.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/graphics/types.h"
#include "drawgui/render/damage.h"

namespace dg {

// Identifies one node in one tree. A struct rather than a bare index so it
// cannot be passed where a count or a coordinate was meant.
struct NodeId {
  std::uint32_t value = 0;

  friend bool operator==(NodeId, NodeId) = default;
};

// Everything a node paints.
//
// Fills and borders only, deliberately. Text needs a font manager and the one
// this project has has no fallback chain (doc/cpu-raster-findings.md), which
// is its own sub-step; blur is 52% of a frame's raster time and belongs in a
// budgeted feature rather than in the primitive every node carries.
struct NodeStyle {
  Color fill;

  // Rounded corners are not free here, and the reason is measured rather than
  // aesthetic: Skia's anti-aliased rounded-rect rasterization is NOT
  // clip-invariant. Cutting a rounded node with a clip changes the coverage
  // it produces even at pixels well inside the clip, by a level or two, so a
  // damage repaint that clipped one in half would not match a full repaint.
  // Integer-aligned square-cornered rectangles have no such problem - 1800
  // randomized clips produced not one differing pixel.
  //
  // A rounded node is therefore repainted whole or not at all: any damage
  // rectangle touching one grows to contain it. Square corners cost nothing.
  Radii radii;

  Color border_color;

  // Painted entirely inside the node's bounds, not straddling the edge the
  // way a centred stroke would. A node that draws outside the rectangle it
  // declared is the exact bug damage tracking cannot survive: the pixels it
  // touched are not the pixels it said it would touch.
  float border_width = 0.0F;
};

// How a repaint turns nodes into draw calls.
//
// design.md section 5.15.2 argues that a retained-mode GUI spends its time
// traversing the tree and recording draw commands rather than rasterizing,
// and that SkPicture caching at repaint boundaries therefore matters more
// than dirty rectangles. That is a claim about this codebase's own cost, and
// it is measurable here: kDirect walks the tree and issues draw calls under
// the clip, kPicture records the whole tree once and replays it, letting
// Skia cull. examples/03_damage_repaint reports both.
enum class PaintMode : std::uint8_t {
  kDirect,
  kPicture,
};

// What one repaint actually did, so that a demo can report cost per unit of
// work rather than a bare millisecond count.
struct RepaintStats {
  std::size_t rects = 0;

  // Under kDirect this is the number of node-into-rectangle draws issued,
  // after culling. Under kPicture the culling happens inside Skia and is not
  // reported back, so it counts the nodes submitted instead - the two are not
  // comparable, and the demo labels them separately for that reason.
  std::size_t nodes_drawn = 0;

  std::size_t nodes_total = 0;

  // Pixels actually repainted, and pixels the dirty nodes asked for. The two
  // differ because a damage rectangle is grown to swallow whole rounded
  // nodes - see NodeStyle::radii - so the gap between them is what that rule
  // costs.
  std::int64_t pixels = 0;
  std::int64_t requested_pixels = 0;

  // True when a kPicture repaint had to re-record the scene, which is the
  // cost that caching is trading against.
  bool recorded = false;
};

struct TreeSpec {
  PixelSize viewport;

  // The root node's appearance. The root covers the viewport and is painted
  // first, which is what guarantees a damage rectangle starts from a known
  // background instead of from whatever was there last frame.
  NodeStyle background;

  std::size_t max_damage_rects = DamageRegion::kDefaultMaxRects;
  PaintMode paint_mode = PaintMode::kDirect;
};

class RenderTree {
 public:
  explicit RenderTree(const TreeSpec& spec);

  RenderTree(RenderTree&&) noexcept;
  RenderTree& operator=(RenderTree&&) noexcept;
  RenderTree(const RenderTree&) = delete;
  RenderTree& operator=(const RenderTree&) = delete;
  ~RenderTree();

  [[nodiscard]] static constexpr NodeId root() { return NodeId{0}; }

  // Appends a child. `bounds` is relative to `parent`'s top-left, so moving a
  // parent moves its children with it. Siblings paint in the order they were
  // added, and a child paints over its parent.
  NodeId add_child(NodeId parent, const PixelRect& bounds, const NodeStyle& style);

  [[nodiscard]] std::size_t node_count() const;
  [[nodiscard]] PixelSize viewport() const;
  [[nodiscard]] const NodeStyle& style(NodeId id) const;

  // Where the node sits relative to its parent, and where it sits in the
  // framebuffer. Both are answered rather than recomputed by callers, because
  // a damage rectangle is only correct in absolute coordinates.
  [[nodiscard]] PixelRect local_bounds(NodeId id) const;
  [[nodiscard]] PixelRect absolute_bounds(NodeId id) const;

  void set_style(NodeId id, const NodeStyle& style);
  void set_fill(NodeId id, Color fill);

  // Both damage the node's old subtree extent and its new one. Damaging only
  // the new extent leaves the pixels it vacated showing last frame's paint,
  // which is the classic stale-pixel corruption partial repaint is prone to.
  void set_local_bounds(NodeId id, const PixelRect& bounds);
  void set_local_origin(NodeId id, int x, int y);

  void resize(PixelSize viewport);
  void set_paint_mode(PaintMode mode);
  [[nodiscard]] PaintMode paint_mode() const;

  [[nodiscard]] const DamageRegion& damage() const;

  // Regions covered by the most recent repaint. This is what present() wants,
  // and holding it here rather than returning a vector keeps a per-frame heap
  // allocation out of the loop being timed.
  [[nodiscard]] const DamageRegion& painted() const;

  // For pixels inside the viewport that the tree does not own - an overlay
  // drawn straight onto the surface after repaint(), for instance. Without
  // it such an overlay would never be presented.
  void damage_rect(const PixelRect& rect);
  void damage_all();

  // Redraws only the damaged regions, then moves the damage to painted().
  // `surface` must be the size of the viewport; anything outside it is
  // clipped away rather than trusted.
  RepaintStats repaint(RasterSurface& surface);

  // Redraws everything, discarding accumulated damage. This is the reference
  // the damage path is verified against, and it is what the demo switches to
  // when asked to show what damage tracking is worth.
  RepaintStats repaint_full(RasterSurface& surface);

 private:
  struct Impl;

  std::unique_ptr<Impl> impl_;
};

}  // namespace dg
