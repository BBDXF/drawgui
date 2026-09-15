// List virtualization: a fixed, permanent pool of item nodes recycled as the
// visible range moves, never one node per logical item.
//
// The property this file exists to pin, stated exactly because doc/list.md
// argues it at length: recycling a pool slot is a RenderTree-only operation
// (`set_local_bounds`, the same primitive `set_scroll_offset` and a slider's
// thumb already move through) and never touches LayoutTree, so it costs a
// repaint and never a relayout - the identical claim doc/scrolling.md already
// measured for a plain offset, extended here to the discrete "swap identity"
// half virtualization adds on top of it.
//
// Two acceptance shapes, matching every prior slice: hand-derived ring-buffer
// arithmetic for what recycling MEANS, and a LayoutStats measurement for
// whether it costs what the doc claims rather than what the code comment
// asserts.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/layout/box.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/widget/widget_set.h"

namespace {

using dg::BoxStyle;
using dg::Color;
using dg::LayoutKind;
using dg::LayoutTree;
using dg::ListSlot;
using dg::NodeId;
using dg::Overflow;
using dg::PixelPoint;
using dg::PixelRect;
using dg::PixelSize;
using dg::RenderTree;
using dg::ScrollAxis;
using dg::Widget;
using dg::WidgetKind;
using dg::WidgetSet;

constexpr int kItemExtent = 20;
constexpr int kItemCount = 1000;
constexpr int kViewportExtent = 90;  // fits 4 full items + a partial 5th
constexpr int kPoolSize = 6;         // ceil(90/20) + 2 spare rows
constexpr int kCross = 60;           // the OTHER axis - width, for a vertical list

dg::TreeSpec spec_for() {
  dg::TreeSpec spec;
  spec.viewport = PixelSize{400, 400};
  spec.background.fill = Color::from_argb(0xFF14171C);
  return spec;
}

// Builds a kList directly against a raw RenderTree, the same shortcut
// test_scroll.cpp takes to test the offset primitive on its own, without a
// layout pass in the way - list_sync()/list_scroll_by() need only
// RenderTree, never LayoutTree, which this test exercises by construction.
struct Handles {
  NodeId viewport;
  std::vector<NodeId> pool;
};

Handles populate(RenderTree& tree, WidgetSet& widgets, ScrollAxis axis,
                 int pool_size = kPoolSize, int item_count = kItemCount) {
  Handles handles;
  handles.viewport = tree.add_child(RenderTree::root(),
                                    PixelRect{0, 0, kCross, kViewportExtent}, dg::NodeStyle{});

  Widget widget;
  widget.kind = WidgetKind::kList;
  widget.list_axis = axis;
  widget.list_item_count = item_count;
  widget.list_item_extent = kItemExtent;
  for (int i = 0; i < pool_size; ++i) {
    const PixelRect initial = axis == ScrollAxis::kHorizontal
                                  ? PixelRect{0, 0, kItemExtent, kCross}
                                  : PixelRect{0, 0, kCross, kItemExtent};
    handles.pool.push_back(tree.add_child(handles.viewport, initial, dg::NodeStyle{}));
  }
  widget.list_pool = handles.pool;
  widgets.attach(handles.viewport, widget);
  return handles;
}

// Every pool slot's currently-visible logical index, read back independently
// of WidgetSet's own bookkeeping - straight from where list_sync() actually
// placed each node - so this is a check on the OBSERVABLE effect, not a
// second read of the same state list_sync() just wrote.
std::vector<int> visible_logical_indices(const RenderTree& tree, const Handles& handles,
                                         ScrollAxis axis) {
  std::vector<int> indices;
  indices.reserve(handles.pool.size());
  for (const NodeId node : handles.pool) {
    const PixelRect local = tree.local_bounds(node);
    const int main = axis == ScrollAxis::kHorizontal ? local.x : local.y;
    indices.push_back(main / kItemExtent);
  }
  return indices;
}

}  // namespace

// ----------------------------------------------------------------------
// The ring-buffer assignment: hand-derived.
// ----------------------------------------------------------------------

TEST_CASE("list_sync assigns pool slot (logical % pool_size) and positions it there") {
  RenderTree tree{spec_for()};
  WidgetSet widgets;
  const Handles handles = populate(tree, widgets, ScrollAxis::kVertical);

  const std::vector<ListSlot> slots = widgets.list_sync(tree, handles.viewport, 0);
  REQUIRE(slots.size() == static_cast<std::size_t>(kPoolSize));

  for (const ListSlot& slot : slots) {
    // The pool holds the FIRST kPoolSize logical indices in slot order,
    // logical index L living at pool position L % pool_size - trivially
    // L itself while L < pool_size.
    const auto pool_position = static_cast<std::size_t>(slot.logical_index % kPoolSize);
    CHECK(handles.pool[pool_position] == slot.node);
    CHECK(tree.local_bounds(slot.node) ==
          PixelRect{0, slot.logical_index * kItemExtent, kCross, kItemExtent});
  }
}

TEST_CASE("a second list_sync at the same top_index recycles nothing") {
  RenderTree tree{spec_for()};
  WidgetSet widgets;
  const Handles handles = populate(tree, widgets, ScrollAxis::kVertical);
  widgets.list_sync(tree, handles.viewport, 0);
  CHECK(widgets.list_sync(tree, handles.viewport, 0).empty());
}

TEST_CASE("crossing exactly one item's extent recycles exactly the slot that scrolled out") {
  RenderTree tree{spec_for()};
  WidgetSet widgets;
  const Handles handles = populate(tree, widgets, ScrollAxis::kVertical);
  widgets.list_sync(tree, handles.viewport, 0);

  const std::vector<ListSlot> slots =
      widgets.list_scroll_by(tree, handles.viewport, kViewportExtent, 0, kItemExtent);
  REQUIRE(slots.size() == 1);
  // Logical index `kPoolSize` (the first one not already covered) lands at
  // slot `kPoolSize % kPoolSize == 0` - the same slot that used to hold
  // logical index 0, which has just scrolled fully out of the new
  // [1, 1+kPoolSize) window.
  CHECK(slots.front().logical_index == kPoolSize);
  CHECK(slots.front().node == handles.pool[0]);

  const std::vector<int> visible =
      visible_logical_indices(tree, handles, ScrollAxis::kVertical);
  const std::set<int> unique(visible.begin(), visible.end());
  CHECK(unique.size() == visible.size());  // no two slots show the same item
  CHECK(unique == std::set<int>{1, 2, 3, 4, 5, 6});
}

TEST_CASE("a jump larger than the pool recycles at most pool_size slots, never item_count") {
  RenderTree tree{spec_for()};
  WidgetSet widgets;
  const Handles handles = populate(tree, widgets, ScrollAxis::kVertical);
  widgets.list_sync(tree, handles.viewport, 0);

  // Jump straight to item 500 - far more than one pool's width away.
  const std::vector<ListSlot> slots =
      widgets.list_scroll_by(tree, handles.viewport, kViewportExtent, 0, 500 * kItemExtent);
  CHECK(slots.size() <= static_cast<std::size_t>(kPoolSize));
  CHECK(tree.node_count() == handles.pool.size() + 2);  // root + viewport; the pool never grew

  const std::vector<int> visible =
      visible_logical_indices(tree, handles, ScrollAxis::kVertical);
  const std::set<int> unique(visible.begin(), visible.end());
  CHECK(unique.size() == visible.size());
  const int expected_top = (500 * kItemExtent) / kItemExtent;
  for (int row = 0; row < kPoolSize; ++row) {
    CHECK(unique.count(expected_top + row) == 1);
  }
}

TEST_CASE("scrolling back up recycles the slot that scrolled out the OTHER edge") {
  RenderTree tree{spec_for()};
  WidgetSet widgets;
  const Handles handles = populate(tree, widgets, ScrollAxis::kVertical);
  widgets.list_sync(tree, handles.viewport, 0);
  widgets.list_scroll_by(tree, handles.viewport, kViewportExtent, 0, 3 * kItemExtent);

  // Now showing [3, 3+kPoolSize). Scroll back to the very top.
  const std::vector<ListSlot> slots =
      widgets.list_scroll_by(tree, handles.viewport, kViewportExtent, 0, -3 * kItemExtent);
  REQUIRE(slots.size() == 3);
  const std::vector<int> visible =
      visible_logical_indices(tree, handles, ScrollAxis::kVertical);
  const std::set<int> unique(visible.begin(), visible.end());
  CHECK(unique == std::set<int>{0, 1, 2, 3, 4, 5});
}

TEST_CASE("a sub-item delta recycles nothing but still moves the offset") {
  RenderTree tree{spec_for()};
  WidgetSet widgets;
  const Handles handles = populate(tree, widgets, ScrollAxis::kVertical);
  widgets.list_sync(tree, handles.viewport, 0);

  const std::vector<ListSlot> slots =
      widgets.list_scroll_by(tree, handles.viewport, kViewportExtent, 0, kItemExtent - 1);
  CHECK(slots.empty());
  CHECK(tree.scroll_offset(handles.viewport).y == kItemExtent - 1);
}

// ----------------------------------------------------------------------
// The clamp: the same shape scroll_by()'s is, arithmetic instead of a
// measured child.
// ----------------------------------------------------------------------

TEST_CASE("list_scroll_by clamps to [0, item_count * item_extent - viewport_extent]") {
  RenderTree tree{spec_for()};
  WidgetSet widgets;
  const Handles handles = populate(tree, widgets, ScrollAxis::kVertical);
  widgets.list_sync(tree, handles.viewport, 0);

  widgets.list_scroll_by(tree, handles.viewport, kViewportExtent, 0, -500);
  CHECK(tree.scroll_offset(handles.viewport).y == 0);

  widgets.list_scroll_by(tree, handles.viewport, kViewportExtent, 0, 10000000);
  const int max_offset = (kItemCount * kItemExtent) - kViewportExtent;
  CHECK(tree.scroll_offset(handles.viewport).y == max_offset);

  const std::vector<ListSlot> nudge =
      widgets.list_scroll_by(tree, handles.viewport, kViewportExtent, 0, 500);
  CHECK(nudge.empty());
  CHECK(tree.scroll_offset(handles.viewport).y == max_offset);
}

// A list SHORTER than its own viewport - item_count * item_extent <
// viewport_extent - is a shape none of this file's other cases build (every
// other populate() call uses kItemCount == 1000, far larger than
// kViewportExtent). Without this the floor at zero in
// `max_offset = std::max(0, content_extent - viewport_extent)` has no test
// that can tell it apart from `content_extent - viewport_extent` unclamped -
// the same "a diagnostic/clamp reachable only through a shape nothing
// builds" lesson doc/scrolling.md's own injection campaign (case J) already
// recorded once, applied here before an injection has to rediscover it.
TEST_CASE("a list shorter than its own viewport clamps its max offset to zero, not negative") {
  RenderTree tree{spec_for()};
  WidgetSet widgets;
  const Handles handles = populate(tree, widgets, ScrollAxis::kVertical, kPoolSize, 3);
  widgets.list_sync(tree, handles.viewport, 0);

  const std::vector<ListSlot> slots =
      widgets.list_scroll_by(tree, handles.viewport, kViewportExtent, 0, 1000000);
  CHECK(slots.empty());
  CHECK(tree.scroll_offset(handles.viewport).y == 0);
}

// A weak-assertion trap the defect-injection campaign found, not predicted:
// scrolled all the way to `max_offset`, the pool's trailing buffer rows
// cover logical indices up to `top_index + pool_size - 1`, which can equal
// or exceed `item_count` (kItemCount=1000, kPoolSize=6 covers one more row
// than the 5 that fit kViewportExtent=90 at kItemExtent=20, and
// `max_offset`'s own top_index is 995, so row 5 asks for logical 1000 - one
// past the last real item). list_sync()'s in-range guard is what refuses to
// assign that slot at all; removing it produces NO crash and NO value
// `check_exact_content`-style content comparisons would catch, because
// item_fill()/item_text()-style deterministic formulas answer ANY integer,
// in or out of range, with an equally well-formed-looking result. Only a
// test that asserts the BOUND itself - every visible logical index is in
// [0, item_count) - can tell "item 1000 rendered" from "item 1000 correctly
// never rendered because there is no such item".
TEST_CASE("no pool slot is ever assigned a logical index outside [0, item_count)") {
  RenderTree tree{spec_for()};
  WidgetSet widgets;
  const Handles handles = populate(tree, widgets, ScrollAxis::kVertical);
  widgets.list_sync(tree, handles.viewport, 0);
  widgets.list_scroll_by(tree, handles.viewport, kViewportExtent, 0, 10000000);

  const std::vector<int> visible =
      visible_logical_indices(tree, handles, ScrollAxis::kVertical);
  for (const int logical : visible) {
    CHECK(logical >= 0);
    CHECK(logical < kItemCount);
  }
}

TEST_CASE("the horizontal axis works the same way, and ignores a vertical delta") {
  RenderTree tree{spec_for()};
  WidgetSet widgets;
  const Handles handles = populate(tree, widgets, ScrollAxis::kHorizontal);
  widgets.list_sync(tree, handles.viewport, 0);

  const std::vector<ListSlot> from_vertical_delta =
      widgets.list_scroll_by(tree, handles.viewport, kViewportExtent, 0, kItemExtent * 4);
  CHECK(from_vertical_delta.empty());
  CHECK(tree.scroll_offset(handles.viewport) == PixelPoint{0, 0});

  const std::vector<ListSlot> from_horizontal_delta =
      widgets.list_scroll_by(tree, handles.viewport, kViewportExtent, kItemExtent, 0);
  CHECK(from_horizontal_delta.size() == 1);
  CHECK(tree.scroll_offset(handles.viewport) == PixelPoint{kItemExtent, 0});
}

// ----------------------------------------------------------------------
// The central tension named in the task: does recycling reintroduce
// relayout? Measured, not assumed - the same acceptance shape
// doc/scrolling.md section 4 already used for a plain offset.
// ----------------------------------------------------------------------

TEST_CASE("recycling costs a repaint and never a relayout") {
  dg::TreeSpec spec = spec_for();
  LayoutTree tree{spec};
  WidgetSet widgets;

  BoxStyle viewport_box;
  viewport_box.kind = LayoutKind::kLeaf;
  viewport_box.width = kCross;
  viewport_box.height = kViewportExtent;
  dg::NodeStyle viewport_style;
  viewport_style.overflow = Overflow::kClip;
  const NodeId viewport = tree.add_child(LayoutTree::root(), viewport_box, viewport_style);

  Widget widget;
  widget.kind = WidgetKind::kList;
  widget.list_axis = ScrollAxis::kVertical;
  widget.list_item_count = kItemCount;
  widget.list_item_extent = kItemExtent;
  std::vector<NodeId> pool;
  for (int i = 0; i < kPoolSize; ++i) {
    BoxStyle item_box;
    item_box.width = kCross;
    item_box.height = kItemExtent;
    pool.push_back(tree.add_child(viewport, item_box, dg::NodeStyle{}));
  }
  widget.list_pool = pool;
  widgets.attach(viewport, widget);

  tree.layout_full();
  widgets.list_sync(tree.render(), viewport, 0);
  tree.layout();  // settle any leftover dirt from build()

  // A big jump: recycles every pool slot in one call.
  widgets.list_scroll_by(tree.render(), viewport, kViewportExtent, 0, 777 * kItemExtent);

  const dg::LayoutStats stats = tree.layout();
  CHECK(stats.nodes_visited == 0);
  CHECK(stats.nodes_relaid_out == 0);
}

TEST_CASE("list_owner_of climbs from a recycled item node to its kList ancestor") {
  RenderTree tree{spec_for()};
  WidgetSet widgets;
  const Handles handles = populate(tree, widgets, ScrollAxis::kVertical);
  widgets.list_sync(tree, handles.viewport, 0);

  CHECK(widgets.list_owner_of(tree, handles.pool[2]) == handles.viewport);
  CHECK(widgets.list_owner_of(tree, RenderTree::root()) == std::nullopt);
}
