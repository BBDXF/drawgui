// The headless check for examples/15_list, run via --verify-list and wired
// into CTest as list.verify_demo_scene, matching every other demo's pattern.
//
// Claim 3 (residue) is this file's reason to exist: it scrolls forward,
// jumps, and scrolls backward, and after EVERY step asserts every visible
// pool node's fill/text/image EXACTLY equals list_scene::style_for() for the
// logical index it currently shows - not a loose shape ("has some text"),
// the precise value, because doc/text-input.md's own catalogued failure mode
// (an assertion weak enough that a whole family of wrong answers passes) is
// exactly what a loose check here would reproduce.

#include "list_check.h"

#include <optional>
#include <ostream>
#include <vector>

#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/render_tree.h"

#include "list_scene.h"

namespace list_check {
namespace {

using dg::ListSlot;
using dg::NodeId;
using dg::PixelSize;

constexpr PixelSize kSize{500, 620};

list_scene::Options options_for(int item_count) {
  list_scene::Options options;
  options.spec.viewport = kSize;
  options.spec.background.fill = dg::Color::from_argb(0xFF14171C);
  options.item_count = item_count;
  return options;
}

// Every pool node's CURRENT content must exactly match style_for() of the
// logical index list_sync() actually placed there - read back independently
// via local_bounds(), not via the ListSlot vectors already consumed.
bool check_exact_content(list_scene::Scene& scene, const std::vector<NodeId>& pool,
                         std::ostream& out, const char* moment) {
  bool ok = true;
  const bool has_font = scene.fonts.has_value();
  for (const NodeId node : pool) {
    const int logical = list_scene::logical_index_of(scene.tree.render(), node);
    const dg::NodeStyle expected =
        list_scene::style_for(logical, scene.font, scene.image_a, scene.image_b, has_font);
    const dg::NodeStyle& actual = scene.tree.render().style(node);
    if (!(actual.fill == expected.fill)) {
      out << "  FAIL [" << moment << "]: node showing logical " << logical
          << " has the WRONG fill (residue from a previous item)\n";
      ok = false;
    }
    if (has_font && actual.text.text != expected.text.text) {
      out << "  FAIL [" << moment << "]: node showing logical " << logical << " has label \""
          << actual.text.text << "\", expected \"" << expected.text.text
          << "\" (residue from a previous item)\n";
      ok = false;
    }
    if (!(actual.image.source == expected.image.source) ||
        !(actual.image.placeholder == expected.image.placeholder)) {
      out << "  FAIL [" << moment << "]: node showing logical " << logical
          << " carries the WRONG image/placeholder (residue from a previous item)\n";
      ok = false;
    }
  }
  return ok;
}

// --------------------------------------------------------------------------
// Claim 1: the pool never grows. node_count is a function of the VIEWPORT,
// not of item_count - the whole point of virtualization.
// --------------------------------------------------------------------------

bool check_pool_bounded(std::ostream& out) {
  list_scene::Scene small = list_scene::build(options_for(10));
  list_scene::Scene large = list_scene::build(options_for(1000));
  bool ok = true;
  if (small.tree.node_count() != large.tree.node_count()) {
    out << "  FAIL: node_count depends on item_count (" << small.tree.node_count() << " at 10 "
        << "items vs " << large.tree.node_count() << " at 1000) - the pool is not fixed\n";
    ok = false;
  }
  // body + list viewport + pool.
  const std::size_t expected = 2 + static_cast<std::size_t>(list_scene::kPoolSize);
  if (large.tree.node_count() != expected) {
    out << "  FAIL: node_count is " << large.tree.node_count() << " at 1000 items, expected "
        << expected << " (2 + kPoolSize)\n";
    ok = false;
  }
  if (ok) {
    out << "  OK: node_count is " << large.tree.node_count()
        << " at both 10 and 1000 items - the pool is fixed, not one node per item\n";
  }
  return ok;
}

// --------------------------------------------------------------------------
// Claim 2: no relayout, for both a plain sub-item scroll and a full-pool
// recycle - the central tension this slice's doc argues out in full.
// --------------------------------------------------------------------------

bool check_no_relayout(std::ostream& out) {
  list_scene::Scene scene = list_scene::build(options_for(1000));
  scene.tree.layout();  // settle any leftover dirt from build()

  scene.widgets.list_scroll_by(scene.tree.render(), scene.handles.list,
                               list_scene::kViewportHeight, 0, 5);
  const dg::LayoutStats sub_item = scene.tree.layout();

  const std::vector<ListSlot> slots = scene.widgets.list_scroll_by(
      scene.tree.render(), scene.handles.list, list_scene::kViewportHeight, 0,
      777 * list_scene::kItemHeight);
  list_scene::refresh(scene, slots);
  const dg::LayoutStats big_jump = scene.tree.layout();

  bool ok = true;
  if (sub_item.nodes_visited != 0 || sub_item.nodes_relaid_out != 0) {
    out << "  FAIL: a sub-item scroll visited " << sub_item.nodes_visited << " nodes\n";
    ok = false;
  }
  if (big_jump.nodes_visited != 0 || big_jump.nodes_relaid_out != 0) {
    out << "  FAIL: a full-pool recycle visited " << big_jump.nodes_visited
        << " nodes and relaid out " << big_jump.nodes_relaid_out << " - recycling reintroduced "
        << "relayout\n";
    ok = false;
  }
  if (ok) {
    out << "  OK: layout() visits 0 nodes after both a sub-item scroll and a " << slots.size()
        << "-slot recycle - recycling is a RenderTree-only operation, "
        << "same as a plain scroll\n";
  }
  return ok;
}

// --------------------------------------------------------------------------
// Claim 3: residue. Forward, jump, and backward - a scene shaped so a stale
// field is visible (fill/text/image all differ item to item), checked with
// exact equality after every move.
// --------------------------------------------------------------------------

bool check_residue(std::ostream& out) {
  list_scene::Scene scene = list_scene::build(options_for(1000));
  bool ok = check_exact_content(scene, scene.widgets.at(scene.handles.list).list_pool, out,
                                "at rest");

  for (int step = 0; step < 40; ++step) {
    const std::vector<ListSlot> slots =
        scene.widgets.list_scroll_by(scene.tree.render(), scene.handles.list,
                                     list_scene::kViewportHeight, 0, list_scene::kItemHeight);
    list_scene::refresh(scene, slots);
  }
  ok = check_exact_content(scene, scene.widgets.at(scene.handles.list).list_pool, out,
                           "after 40 forward steps") &&
       ok;

  const std::vector<ListSlot> jump = scene.widgets.list_scroll_by(
      scene.tree.render(), scene.handles.list, list_scene::kViewportHeight, 0,
      900 * list_scene::kItemHeight);
  list_scene::refresh(scene, jump);
  ok = check_exact_content(scene, scene.widgets.at(scene.handles.list).list_pool, out,
                           "after a forward jump") &&
       ok;

  for (int step = 0; step < 60; ++step) {
    const std::vector<ListSlot> slots =
        scene.widgets.list_scroll_by(scene.tree.render(), scene.handles.list,
                                     list_scene::kViewportHeight, 0, -list_scene::kItemHeight);
    list_scene::refresh(scene, slots);
  }
  ok = check_exact_content(scene, scene.widgets.at(scene.handles.list).list_pool, out,
                           "after scrolling back 60 steps") &&
       ok;

  const std::vector<ListSlot> back_to_start = scene.widgets.list_scroll_by(
      scene.tree.render(), scene.handles.list, list_scene::kViewportHeight, 0, -1000000);
  list_scene::refresh(scene, back_to_start);
  ok = check_exact_content(scene, scene.widgets.at(scene.handles.list).list_pool, out,
                           "back at the very top") &&
       ok;

  if (ok) {
    out << "  OK: every recycled node's fill/text/image exactly matches its currently-assigned "
           "item, forward, after a jump, and backward\n";
  }
  return ok;
}

// --------------------------------------------------------------------------
// Claim 4: overscroll clamps, the same shape doc/scrolling.md already
// established for kScrollView.
// --------------------------------------------------------------------------

bool check_overscroll_clamps(std::ostream& out) {
  list_scene::Scene scene = list_scene::build(options_for(1000));
  bool ok = true;

  scene.widgets.list_scroll_by(scene.tree.render(), scene.handles.list,
                               list_scene::kViewportHeight, 0, -500);
  if (scene.tree.render().scroll_offset(scene.handles.list).y != 0) {
    out << "  FAIL: scrolling up from rest did not clamp to 0\n";
    ok = false;
  }

  scene.widgets.list_scroll_by(scene.tree.render(), scene.handles.list,
                               list_scene::kViewportHeight, 0, 100000000);
  const int max_offset =
      (list_scene::kItemCount * list_scene::kItemHeight) - list_scene::kViewportHeight;
  const int offset = scene.tree.render().scroll_offset(scene.handles.list).y;
  if (offset != max_offset) {
    out << "  FAIL: scrolling far past the bottom produced offset " << offset << ", expected "
        << max_offset << "\n";
    ok = false;
  }
  if (ok) {
    out << "  OK: overscroll clamps at 0 and at exactly " << max_offset << "\n";
  }
  return ok;
}

}  // namespace

int run(std::ostream& out) {
  out << "verify: virtualized list, on the scene the demo puts on screen\n";
  bool ok = true;
  ok = check_pool_bounded(out) && ok;
  ok = check_no_relayout(out) && ok;
  ok = check_residue(out) && ok;
  ok = check_overscroll_clamps(out) && ok;
  out << (ok ? "PASS\n" : "FAIL\n");
  return ok ? 0 : 1;
}

}  // namespace list_check
