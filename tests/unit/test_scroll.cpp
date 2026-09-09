// Scrolling: an offset that moves children without moving what they declare.
//
// `RenderTree::set_scroll_offset` is a runtime primitive with one job - shift
// a node's children relative to where their own `local` rectangles say they
// are - and this file is where that job is pinned. `overflow: kClip` (already
// tested in test_clip.cpp/test_clip_damage.cpp) supplies the confinement; the
// scroll_axis/measure_leaf half that lets content exceed its viewport is
// pinned in test_layout.cpp. This file is the part that is new here: the
// offset itself, and everything painting, hit testing and damage have to
// agree about because of it.
//
// Same acceptance shape as every other slice: hand-derived geometry for what
// the offset MEANS, and a byte-identity pair for whether doing less produces
// the same pixels as doing everything - because identity alone drives two
// copies of the same code and cannot tell a wrong decision from a right one.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/graphics/types.h"
#include "drawgui/layout/box.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/widget/widget_set.h"

namespace {

using dg::BoxStyle;
using dg::Color;
using dg::LayoutKind;
using dg::LayoutTree;
using dg::NodeId;
using dg::NodeStyle;
using dg::Overflow;
using dg::PixelPoint;
using dg::PixelRect;
using dg::PixelSize;
using dg::RasterSurface;
using dg::RenderTree;

constexpr int kWidth = 241;
constexpr int kHeight = 181;

// Item height chosen so the ten-item column (400 px) overflows a 100 px
// viewport by exactly 300 px - the number every hand-derived offset below is
// checked against.
constexpr int kItemHeight = 40;
constexpr int kItemCount = 10;
constexpr int kViewportHeight = 100;
constexpr int kViewportWidth = 90;

NodeStyle styled(std::uint32_t argb, Overflow overflow = Overflow::kVisible) {
  NodeStyle style;
  style.fill = Color::from_argb(argb);
  style.overflow = overflow;
  return style;
}

std::vector<std::uint8_t> snapshot(const RasterSurface& surface) {
  const dg::PixelView view = surface.peek_pixels();
  const auto row = static_cast<std::size_t>(view.width) * 4;
  std::vector<std::uint8_t> pixels(row * static_cast<std::size_t>(view.height));
  for (int y = 0; y < view.height; ++y) {
    std::memcpy(pixels.data() + (static_cast<std::size_t>(y) * row),
                view.pixels + (static_cast<std::size_t>(y) * view.row_bytes), row);
  }
  return pixels;
}

// A clipping viewport at (10, 10), 90x100, holding ten stacked 40 px items -
// exactly what examples/10_scrolling composes out of a leaf and a column,
// built here with plain RenderTree::add_child so the scroll primitive is
// tested on its own, without a layout pass in the way.
struct Handles {
  NodeId viewport;
  std::vector<NodeId> items;
};

Handles populate(RenderTree& tree) {
  Handles handles;
  handles.viewport =
      tree.add_child(RenderTree::root(), PixelRect{10, 10, kViewportWidth, kViewportHeight},
                     styled(0xFF243040, Overflow::kClip));
  for (int i = 0; i < kItemCount; ++i) {
    const std::uint32_t shade = 0xFF200000U + (static_cast<std::uint32_t>(i) * 0x101010U);
    handles.items.push_back(tree.add_child(
        handles.viewport, PixelRect{5, i * kItemHeight, kViewportWidth - 10, kItemHeight - 4},
        styled(shade)));
  }
  return handles;
}

dg::TreeSpec spec_for(std::size_t max_rects = dg::DamageRegion::kDefaultMaxRects) {
  dg::TreeSpec spec;
  spec.viewport = PixelSize{kWidth, kHeight};
  spec.background = styled(0xFF14171C);
  spec.max_damage_rects = max_rects;
  return spec;
}

// --------------------------------------------------------------------------
// What the offset means: geometry, hand-derived.
// --------------------------------------------------------------------------

TEST_CASE("scroll offset shifts children's absolute position, not their local one") {
  RenderTree tree{spec_for()};
  const Handles handles = populate(tree);

  const PixelRect before_local = tree.local_bounds(handles.items[3]);
  const PixelRect before_absolute = tree.absolute_bounds(handles.items[3]);

  tree.set_scroll_offset(handles.viewport, PixelPoint{0, 55});

  CHECK(tree.local_bounds(handles.items[3]) == before_local);
  CHECK(tree.absolute_bounds(handles.items[3]) == before_absolute.offset_by(0, -55));

  // The node CARRYING the offset does not move itself - only its children -
  // the same shape a clip confines descendants but not its own paint
  // (doc/clipping.md section 2) and a parent applies margin but is not
  // shrunk by it (design.md section 5.9.4).
  CHECK(tree.absolute_bounds(handles.viewport) ==
        PixelRect{10, 10, kViewportWidth, kViewportHeight});
}

TEST_CASE("scroll offset composes down a subtree without being read twice") {
  RenderTree tree{spec_for()};
  const NodeId viewport = tree.add_child(RenderTree::root(), PixelRect{0, 0, 200, 100},
                                         styled(0xFF243040, Overflow::kClip));
  const NodeId row = tree.add_child(viewport, PixelRect{0, 0, 400, 100}, styled(0xFF35506E));
  const NodeId leaf = tree.add_child(row, PixelRect{20, 20, 30, 30}, styled(0xFFCC4125));

  tree.set_scroll_offset(viewport, PixelPoint{60, 0});

  // `row` (the direct child) is shifted by the viewport's offset; `leaf` (the
  // grandchild) is shifted only once - by inheriting row's already-shifted
  // absolute position, not by the viewport's offset a second time.
  CHECK(tree.absolute_bounds(row) == PixelRect{-60, 0, 400, 100});
  CHECK(tree.absolute_bounds(leaf) == PixelRect{-40, 20, 30, 30});
}

// --------------------------------------------------------------------------
// A pixel that is scrolled out is not hittable; one scrolled in, is.
// --------------------------------------------------------------------------

bool is_one_of(const std::optional<NodeId>& hit, const std::vector<NodeId>& items) {
  if (!hit.has_value()) {
    return false;
  }
  return std::ranges::any_of(items, [&](NodeId item) { return item == *hit; });
}

TEST_CASE("hit testing follows the offset: scrolled out means not hittable") {
  RenderTree tree{spec_for()};
  const Handles handles = populate(tree);

  // At rest, item 0 (absolute y 10..46) is hittable at the viewport's top
  // edge.
  CHECK(tree.hit_test(PixelPoint{20, 15}) == handles.items[0]);

  // Scroll down by 160: item 0 is now at absolute y -150..-114, entirely
  // above the viewport - clipped away, and per doc/clipping.md a pixel not
  // painted because it was clipped is not hittable. Item 4 has moved to
  // absolute y 10..46, exactly where item 0 used to be, and IS hittable
  // there.
  tree.set_scroll_offset(handles.viewport, PixelPoint{0, 160});
  CHECK(tree.hit_test(PixelPoint{20, 15}) == handles.items[4]);

  const std::optional<NodeId> at_old_item0 = tree.hit_test(PixelPoint{20, 15});
  if (!at_old_item0.has_value()) {
    FAIL("expected a hit after scrolling - the point is still inside the viewport's clip");
    return;
  }
  CHECK(*at_old_item0 != handles.items[0]);
}

TEST_CASE("a click just outside the clip never hits the scrolled content") {
  RenderTree tree{spec_for()};
  const Handles handles = populate(tree);
  tree.set_scroll_offset(handles.viewport, PixelPoint{0, 200});

  // One pixel above the viewport's top edge (y=9, viewport starts at y=10):
  // the same equivalence test_clip.cpp checks exhaustively for a static
  // clip - it must hold just as well while the content under it is moving.
  // The pixel there is the ROOT's own background (nothing else paints
  // above the viewport), and hit testing must agree - it must NOT resolve
  // to any of the scrolled-away items, at any offset.
  const std::optional<NodeId> hit = tree.hit_test(PixelPoint{20, 9});
  CHECK_FALSE(is_one_of(hit, handles.items));
  CHECK(hit == RenderTree::root());
}

// --------------------------------------------------------------------------
// Overscroll: WidgetSet::scroll_by is what clamps, but the raw primitive
// itself must not misbehave on an offset a caller sends anyway - it has no
// clamping obligation, and this proves it does not silently corrupt anything
// by not having one.
// --------------------------------------------------------------------------

TEST_CASE("an offset past the content still repositions consistently") {
  RenderTree tree{spec_for()};
  const Handles handles = populate(tree);

  tree.set_scroll_offset(handles.viewport, PixelPoint{0, 10000});
  CHECK(tree.absolute_bounds(handles.items[0]).y == 10 - 10000);
  // Every item has scrolled off screen; the point now shows the viewport's
  // OWN background (nothing under it clips a level further), which is what
  // hit testing must agree with - not any item, and not a crash.
  CHECK_FALSE(is_one_of(tree.hit_test(PixelPoint{20, 15}), handles.items));
  CHECK(tree.hit_test(PixelPoint{20, 15}) == handles.viewport);

  tree.set_scroll_offset(handles.viewport, PixelPoint{0, -500});
  CHECK(tree.absolute_bounds(handles.items[0]).y == 10 + 500);
}

// --------------------------------------------------------------------------
// A re-assertion of the same offset damages nothing, matching set_text()'s
// "the common case in an interaction loop must not repaint" rule.
// --------------------------------------------------------------------------

TEST_CASE("re-asserting the same offset is a no-op") {
  RenderTree tree{spec_for()};
  const Handles handles = populate(tree);
  tree.set_scroll_offset(handles.viewport, PixelPoint{0, 40});
  std::optional<RasterSurface> surface = RasterSurface::create(kWidth, kHeight);
  if (!surface.has_value()) {
    FAIL("could not allocate a raster surface");
    return;
  }
  (void)tree.repaint_full(*surface);

  tree.set_scroll_offset(handles.viewport, PixelPoint{0, 40});
  CHECK(tree.damage().is_empty());
}

// --------------------------------------------------------------------------
// Byte identity: scrolling costs a repaint, and an incremental one matches a
// full one exactly, across a script that scrolls, changes a child under the
// clip, and scrolls again.
// --------------------------------------------------------------------------

struct Scene {
  RenderTree tree;
  Handles handles;
};

Scene build(const dg::TreeSpec& spec) {
  RenderTree tree{spec};
  Handles handles = populate(tree);
  return Scene{std::move(tree), std::move(handles)};
}

struct Pair {
  Scene damaged;
  Scene reference;
  RasterSurface damaged_surface;
  RasterSurface reference_surface;

  [[nodiscard]] bool identical() const {
    return snapshot(damaged_surface) == snapshot(reference_surface);
  }

  template <typename Body>
  void step(const Body& body) {
    body(damaged.tree, damaged.handles);
    body(reference.tree, reference.handles);
    damaged.tree.repaint(damaged_surface);
    reference.tree.repaint_full(reference_surface);
  }
};

template <typename Body>
void with_pair(const dg::TreeSpec& spec, const Body& body) {
  std::optional<RasterSurface> damaged =
      RasterSurface::create(spec.viewport.width, spec.viewport.height);
  std::optional<RasterSurface> reference =
      RasterSurface::create(spec.viewport.width, spec.viewport.height);
  if (!damaged.has_value() || !reference.has_value()) {
    FAIL("could not allocate a raster surface");
    return;
  }
  Pair pair{build(spec), build(spec), *std::move(damaged), *std::move(reference)};
  body(pair);
}

TEST_CASE("scrolling stays byte-identical between an incremental and a full repaint") {
  with_pair(spec_for(), [](Pair& pair) {
    const int offsets[] = {0, 15, 55, 90, 200, 120, 0, -30, 400, 175};
    for (std::size_t frame = 0; frame < std::size(offsets); ++frame) {
      pair.step([&](RenderTree& tree, const Handles& handles) {
        tree.set_scroll_offset(handles.viewport, PixelPoint{0, offsets[frame]});
        // Every third frame also changes a child under the clip, so the
        // scene exercises "damage while scrolled" and not only "damage from
        // scrolling itself".
        if (frame % 3 == 0) {
          tree.set_fill(handles.items[frame % handles.items.size()],
                        Color::from_argb(0xFF00FF00));
        }
      });
      INFO("frame ", frame, " offset ", offsets[frame]);
      REQUIRE(pair.identical());
    }
  });
}

TEST_CASE("scrolling stays byte-identical under a one-rectangle damage cap") {
  // The always-union policy (doc/damage-repaint.md), which is the harshest
  // case for a stale-pixel bug: everything lands in one rectangle, so a
  // clipping or offset mistake that only shows up when damage is TIGHT would
  // be invisible here and this case would falsely pass. It is included
  // anyway because the OPPOSITE failure - a rectangle that is too small and
  // misses a moved pixel - still cannot hide behind the cap.
  with_pair(spec_for(1), [](Pair& pair) {
    const int offsets[] = {0, 40, 300, 40, 0};
    for (const int offset : offsets) {
      pair.step([&](RenderTree& tree, const Handles& handles) {
        tree.set_scroll_offset(handles.viewport, PixelPoint{0, offset});
      });
      REQUIRE(pair.identical());
    }
  });
}

// --------------------------------------------------------------------------
// The relayout claim: scrolling costs a repaint, never a layout pass.
// --------------------------------------------------------------------------

TEST_CASE("scrolling marks nothing dirty in the layout tree") {
  LayoutTree tree{[] {
    dg::TreeSpec spec;
    spec.viewport = PixelSize{200, 150};
    return spec;
  }()};
  tree.set_box(LayoutTree::root(), [] {
    BoxStyle box;
    box.kind = LayoutKind::kColumn;
    return box;
  }());

  BoxStyle viewport_box;
  viewport_box.kind = LayoutKind::kLeaf;
  viewport_box.scroll_axis = dg::ScrollAxis::kVertical;
  viewport_box.width = 150;
  viewport_box.height = 80;
  const NodeId viewport = tree.add_child(LayoutTree::root(), viewport_box, NodeStyle{});
  RenderTree& render = tree.render();
  NodeStyle style = render.style(viewport);
  style.overflow = Overflow::kClip;
  render.set_style(viewport, style);

  BoxStyle column_box;
  column_box.kind = LayoutKind::kColumn;
  const NodeId column = tree.add_child(viewport, column_box, NodeStyle{});
  for (int i = 0; i < 6; ++i) {
    BoxStyle item;
    item.width = 130;
    item.height = 30;
    tree.add_child(column, item, NodeStyle{});
  }
  tree.layout_full();

  const dg::LayoutStats first = tree.layout();
  CHECK(first.nodes_visited == 0);

  // The scroll itself: a real offset change, through the render tree the
  // layout tree owns.
  render.set_scroll_offset(viewport, PixelPoint{0, 45});

  const dg::LayoutStats after_scroll = tree.layout();
  CHECK(after_scroll.nodes_visited == 0);
  CHECK(after_scroll.nodes_relaid_out == 0);
}

// --------------------------------------------------------------------------
// WidgetSet::scroll_by ignores the axis it was not asked to move, even when
// BOTH axes have room to move - built with deliberately mismatched content so
// that a cross-axis bug cannot hide behind "there was nothing to scroll on
// that axis anyway", which examples/10_scrolling's own scenes cannot rule
// out on their own (their items fill the cross axis exactly, by design).
// --------------------------------------------------------------------------

TEST_CASE("scroll_by moves only its own axis, even when the other one has room too") {
  RenderTree tree{spec_for()};
  const NodeId viewport = tree.add_child(RenderTree::root(), PixelRect{0, 0, 100, 100},
                                         styled(0xFF243040, Overflow::kClip));
  // Content both taller AND wider than the viewport, unlike the demo's
  // scenes - this is what gives a cross-axis-delta bug something to move.
  const NodeId content =
      tree.add_child(viewport, PixelRect{0, 0, 300, 300}, styled(0xFF35506E));

  dg::WidgetSet widgets;
  dg::Widget widget;
  widget.kind = dg::WidgetKind::kScrollView;
  widget.scroll_axis = dg::ScrollAxis::kVertical;
  widget.scroll_content = content;
  widgets.attach(viewport, widget);

  const PixelRect viewport_content{0, 0, 100, 100};
  const bool moved_x = widgets.scroll_by(tree, viewport, viewport_content, 50, 0);
  CHECK_FALSE(moved_x);
  CHECK(tree.scroll_offset(viewport) == PixelPoint{0, 0});

  const bool moved_y = widgets.scroll_by(tree, viewport, viewport_content, 0, 50);
  CHECK(moved_y);
  CHECK(tree.scroll_offset(viewport) == PixelPoint{0, 50});
}

}  // namespace
