#include "drawgui/layout/layout_tree.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/layout/box.h"
#include "drawgui/render/damage.h"
#include "drawgui/render/render_tree.h"

#include "layout/layout_impl.h"

namespace dg {
namespace {

// Bounded so that a window size which trips a conflict on every node cannot
// turn a per-frame diagnostic into a per-frame allocation storm. The count is
// still reported, so the truncation is visible rather than silent.
constexpr std::size_t kMaxDiagnostics = 8;

const char* kind_name(LayoutKind kind) {
  switch (kind) {
    case LayoutKind::kLeaf:
      return "leaf";
    case LayoutKind::kRow:
      return "row";
    case LayoutKind::kColumn:
      return "column";
    case LayoutKind::kWrapRow:
      return "wrap-row";
    case LayoutKind::kWrapColumn:
      return "wrap-column";
    case LayoutKind::kAbsolute:
      return "absolute";
  }
  return "?";
}

// Fields a node carries but its PARENT consumes.
//
// design.md section 5.8 decision 7 calls these parentData, and
// props/drawgui.props.toml marks them `parent_data = true`: margin on the
// ParentData base, grow and align_self on the flex scope, left/top/right/bottom
// on the stack scope. Those are exactly the fields listed here; shrink and
// basis are parentData too but BoxStyle has no member for them, so a slice
// that adds one has to extend this predicate with it.
//
// They matter here because they break the assumption mark_needs_layout() rests
// on. That function absorbs a node's OWN style change when the node's incoming
// constraints are tight, reasoning that a style cannot override a constraint
// the parent already fixed. True for everything the node consumes itself - and
// false for these, because the tight constraint was DERIVED from the value
// that just changed. A flexible, stretched child is tight on both axes, so
// changing its weight from 1 to 0 was absorbed by the child and never reached
// the row that hands weights out: the incremental pass left it at its old
// size while a full pass resized it. Measured, not theorised.
[[nodiscard]] bool differs_in_parent_data(const BoxStyle& before, const BoxStyle& after) {
  return before.margin != after.margin || before.grow != after.grow ||
         before.align_self != after.align_self || before.left != after.left ||
         before.top != after.top || before.right != after.right ||
         before.bottom != after.bottom;
}

}  // namespace

LayoutTree::Impl::Impl(const TreeSpec& spec)
    : render(spec), max_damage_rects(spec.max_damage_rects) {
  LayoutNode root_node;
  root_node.box.kind = LayoutKind::kColumn;
  root_node.constraints = BoxConstraints::tight(spec.viewport);
  root_node.local = PixelRect{0, 0, spec.viewport.width, spec.viewport.height};
  nodes.push_back(std::move(root_node));
  dirty.push_back(0);
}

std::string LayoutTree::Impl::path_of(std::uint32_t index) const {
  std::vector<std::uint32_t> chain;
  for (std::uint32_t current = index;; current = nodes[current].parent) {
    chain.push_back(current);
    if (current == 0) {
      break;
    }
  }
  std::string path;
  for (auto step = chain.rbegin(); step != chain.rend(); ++step) {
    if (!path.empty()) {
      path += " > ";
    }
    path += *step == 0 ? std::string{"root"} : "#" + std::to_string(*step);
    path += "(";
    path += kind_name(nodes[*step].box.kind);
    path += ")";
  }
  return path;
}

void LayoutTree::Impl::report(std::uint32_t index, const std::string& message) {
  if (diagnostics.size() >= kMaxDiagnostics) {
    return;
  }
  diagnostics.push_back("layout: " + message + "\n    at " + path_of(index));
}

bool LayoutTree::Impl::is_boundary(std::uint32_t index) const {
  // The root always stops the walk: it has no parent to climb to, and its
  // constraints are tight to the viewport, so it is a boundary by the same
  // rule as everything else. A node that has never been laid out has no
  // boundary to consult yet, so the mark keeps climbing until it reaches one
  // that has - the pass that lays the ancestor out will reach it.
  return index == 0 || (nodes[index].has_cached && nodes[index].relayout_boundary == index);
}

void LayoutTree::Impl::mark_needs_layout(std::uint32_t index, bool own_style_changed) {
  std::uint32_t current = index;
  bool first = true;
  while (true) {
    LayoutNode& node = nodes[current];
    if (node.needs_layout && !first) {
      return;
    }
    const bool was_marked = node.needs_layout;
    node.needs_layout = true;

    const bool absorbs = first && own_style_changed
                             ? current == 0 || (node.has_cached && node.constraints.is_tight())
                             : is_boundary(current);
    if (absorbs) {
      if (!was_marked || first) {
        dirty.push_back(current);
      }
      return;
    }
    if (was_marked && !first) {
      return;
    }
    first = false;
    current = node.parent;
  }
}

PixelSize LayoutTree::Impl::layout_node(std::uint32_t index,
                                        const BoxConstraints& constraints) {
  LayoutNode& node = nodes[index];
  ++stats.nodes_visited;

  // Two ways a node's size can be settled before its children are consulted,
  // and both make it a boundary. Tight constraints are the obvious one. The
  // other is a style that fixes both dimensions inside what the parent
  // permits - a row with a definite height stretched across its column - and
  // leaving it out is what made a list of a thousand rows cost a thousand
  // nodes to change one cell. Measured, not assumed: without this clause the
  // incremental pass's cost grew linearly with the tree.
  const SizeLimits limits = limits_for(node.box, constraints);
  const bool settled =
      constraints.is_tight() || (limits.tight_width() && limits.tight_height());
  const std::uint32_t boundary =
      index == 0 || settled ? index : nodes[node.parent].relayout_boundary;

  // The cheap half of incremental layout. A node re-entered with exactly the
  // constraints it was last laid out under, not itself marked dirty, and
  // still under the same boundary, cannot produce a different size or a
  // different arrangement - L1 in design.md section 5.4.1 says its size
  // depends on nothing else. So the whole subtree is skipped.
  //
  // The boundary term is the one part of that key no test can justify, and
  // the reason is worth writing down so the next reader does not go hunting
  // for one. The state it describes is real, and needs no resize: a style
  // change on an ancestor can flip that ancestor's settled-ness while handing
  // this node byte-identical constraints, and tests/unit builds exactly that
  // case. But the value stored here is only ever read by is_boundary(), which
  // compares it against the node's own index - and `boundary == index` holds
  // precisely when `index == 0 || settled`, a pure function of the box and
  // the constraints this key has already pinned equal. A stale boundary and a
  // fresh one therefore agree on the only question anybody asks of them.
  // Deleting this term leaves every test green; that was measured, not
  // assumed.
  //
  // It stays because that equivalence is a property of today's single reader.
  // The obvious next optimisation - having mark_needs_layout() jump straight
  // to nodes[index].relayout_boundary instead of climbing to it - would read
  // the stored value for its own sake, and a stale one would land the mark on
  // a node that has stopped being a boundary. Dropping the term now would buy
  // a handful of cache hits and leave a trap for that change.
  if (!node.needs_layout && node.has_cached && node.constraints == constraints &&
      node.relayout_boundary == boundary) {
    return node.size;
  }

  ++stats.nodes_relaid_out;
  node.constraints = constraints;
  node.relayout_boundary = boundary;
  node.has_cached = true;

  const PixelSize size = measure(index, constraints);

  LayoutNode& laid_out = nodes[index];
  laid_out.size = size;
  laid_out.local.width = size.width;
  laid_out.local.height = size.height;
  laid_out.needs_layout = false;
  return size;
}

void LayoutTree::Impl::add_subtree(std::uint32_t index, DamageRegion& region) const {
  std::vector<std::uint32_t> stack{index};
  while (!stack.empty()) {
    const std::uint32_t current = stack.back();
    stack.pop_back();
    region.add(render.absolute_bounds(NodeId{current}));
    for (const std::uint32_t child : nodes[current].children) {
      stack.push_back(child);
    }
  }
}

void LayoutTree::Impl::apply_bounds(std::uint32_t index, DamageRegion& moved,
                                    bool parent_moved) {
  const NodeId id{index};
  const bool changed = render.local_bounds(id) != nodes[index].local;
  if (changed) {
    ++stats.nodes_moved;
    if (!parent_moved) {
      // Both extents, in this order. The pixels the subtree vacated are only
      // knowable before the move and the ones it occupies only after, and
      // damaging one without the other is what leaves a trail behind
      // something that slides.
      add_subtree(index, moved);
      render.set_local_bounds(id, nodes[index].local);
      add_subtree(index, moved);
    } else {
      render.set_local_bounds(id, nodes[index].local);
    }
  }
  for (const std::uint32_t child : nodes[index].children) {
    apply_bounds(child, moved, parent_moved || changed);
  }
}

LayoutStats LayoutTree::Impl::run(bool full) {
  stats = LayoutStats{};
  stats.nodes_total = nodes.size();
  diagnostics.clear();

  if (full) {
    for (LayoutNode& node : nodes) {
      node.needs_layout = true;
      node.has_cached = false;
    }
    dirty.clear();
    dirty.push_back(0);
  }

  std::vector<std::uint32_t> roots;
  roots.swap(dirty);

  // Shallowest first, so that a boundary nested inside another one is already
  // clean by the time its turn comes and is skipped instead of laid out
  // twice. That is a cost claim, not a correctness one: reversing this
  // comparator leaves every bound identical, because a nested boundary laid
  // out too early is laid out again on the way down from its ancestor and its
  // rectangle overwritten rather than trusted. What reversing it does change
  // is dirty_roots, and that is what tests/unit pins. The demo scene cannot
  // pin it - its four boundaries sit in four different subtrees, so no order
  // of them is wrong - which is why that test builds a tree of its own.
  //
  // The index tie-break is what makes this a total order, and std::unique
  // below needs one: a node whose own constraints are tight absorbs its own
  // style change, so it is pushed on every set_box rather than only the
  // first, and unique() removes duplicates only where they are adjacent.
  std::sort(roots.begin(), roots.end(), [this](std::uint32_t a, std::uint32_t b) {
    return nodes[a].depth != nodes[b].depth ? nodes[a].depth < nodes[b].depth : a < b;
  });
  roots.erase(std::unique(roots.begin(), roots.end()), roots.end());

  DamageRegion moved{max_damage_rects};
  for (const std::uint32_t index : roots) {
    if (!nodes[index].needs_layout) {
      continue;
    }
    ++stats.dirty_roots;
    layout_node(index, nodes[index].constraints);
    apply_bounds(index, moved, false);
  }

  stats.damage_area = moved.area();
  stats.damage_rects = moved.size();
  return stats;
}

LayoutTree::LayoutTree(const TreeSpec& spec) : impl_(std::make_unique<Impl>(spec)) {}

LayoutTree::LayoutTree(LayoutTree&&) noexcept = default;
LayoutTree& LayoutTree::operator=(LayoutTree&&) noexcept = default;

LayoutTree::~LayoutTree() = default;

NodeId LayoutTree::add_child(NodeId parent, const BoxStyle& box, const NodeStyle& style) {
  // An empty rectangle rather than a guess: nothing outside this class is
  // entitled to an opinion about where the node goes, and a placeholder that
  // looked plausible would paint one wrong frame before the first layout.
  const NodeId id = impl_->render.add_child(parent, PixelRect{}, style);

  LayoutNode node;
  node.box = box;
  node.parent = parent.value;
  node.depth = impl_->nodes[parent.value].depth + 1;
  impl_->nodes.push_back(std::move(node));
  impl_->nodes[parent.value].children.push_back(id.value);
  impl_->mark_needs_layout(id.value, true);
  return id;
}

std::size_t LayoutTree::node_count() const {
  return impl_->nodes.size();
}

const BoxStyle& LayoutTree::box(NodeId id) const {
  return impl_->nodes[id.value].box;
}

void LayoutTree::set_box(NodeId id, const BoxStyle& box) {
  const bool parent_data_changed = differs_in_parent_data(impl_->nodes[id.value].box, box);
  impl_->nodes[id.value].box = box;
  impl_->mark_needs_layout(id.value, true);

  // A parentData change has to reach the node that reads it. Marking only this
  // node is correct for everything it consumes itself, and silently wrong for
  // the six fields above: the arrangement that would notice runs one level up.
  // Passing false says "something inside you changed", which is what happened
  // from the parent's point of view and which stops at the parent's own
  // relayout boundary rather than climbing to the root.
  if (parent_data_changed && id != root()) {
    impl_->mark_needs_layout(impl_->nodes[id.value].parent, false);
  }
}

RenderTree& LayoutTree::render() {
  return impl_->render;
}

const RenderTree& LayoutTree::render() const {
  return impl_->render;
}

PixelRect LayoutTree::bounds(NodeId id) const {
  return impl_->render.absolute_bounds(id);
}

PixelRect LayoutTree::content_bounds(NodeId id) const {
  const PixelRect border_box = impl_->render.absolute_bounds(id);
  const EdgeInsets insets = content_insets(impl_->nodes[id.value].box);
  return PixelRect::from_edges(border_box.left() + insets.left, border_box.top() + insets.top,
                               border_box.right() - insets.right,
                               border_box.bottom() - insets.bottom);
}

LayoutStats LayoutTree::layout() {
  return impl_->run(false);
}

LayoutStats LayoutTree::layout_full() {
  return impl_->run(true);
}

void LayoutTree::resize(PixelSize viewport) {
  impl_->render.resize(viewport);
  impl_->nodes[0].constraints = BoxConstraints::tight(viewport);
  impl_->nodes[0].needs_layout = true;
  impl_->dirty.push_back(0);
}

PixelSize LayoutTree::viewport() const {
  return impl_->render.viewport();
}

const std::vector<std::string>& LayoutTree::diagnostics() const {
  return impl_->diagnostics;
}

std::string LayoutTree::path_of(NodeId id) const {
  return impl_->path_of(id.value);
}

}  // namespace dg
