#include "drawgui/render/render_tree.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

// The tree owns an sk_sp<SkPicture>, so ~Impl - and therefore ~RenderTree,
// which is defined here - needs the complete type.
#include "include/core/SkPicture.h"

#include "render/tree_impl.h"

namespace dg {

bool clips_atomically(const NodeStyle& style) {
  // Rounded corners only, and text deliberately NOT - which is the opposite of
  // what sub-step 3 first assumed.
  //
  // Both are anti-aliased, so the expectation was that both would be
  // clip-dependent. Measured instead (examples/05_widgets --clip-probe, 600
  // random clips that cut the shape): a rounded rectangle differs by 582
  // pixels at zero slack and needs one pixel of slack to reach zero, matching
  // sub-step 1; TEXT DIFFERS BY ZERO at zero slack. Glyphs go through a mask
  // cache and a clip masks the blit, rather than through the analytic coverage
  // a path fill computes.
  //
  // Adding text here costs 1.57x the demo's damage and buys nothing.
  // tests/unit/test_text_damage.cpp holds the shape that proves it - a damage
  // rectangle laid across a run of glyphs - because the interaction scene
  // never cuts a label and so cannot see this either way.
  return !style.radii.is_zero();
}

void RenderTree::Impl::reposition(std::uint32_t root_index) {
  std::vector<std::uint32_t> stack{root_index};
  while (!stack.empty()) {
    const std::uint32_t index = stack.back();
    stack.pop_back();
    Node& node = nodes[index];
    if (index == 0) {
      node.absolute = node.local;
      node.clip_bounds.reset();
    } else {
      const Node& parent = nodes[node.parent];
      node.absolute = node.local.offset_by(parent.absolute.x - parent.scroll_offset.x,
                                           parent.absolute.y - parent.scroll_offset.y);

      // The clip a child inherits is everything its ancestors impose, and
      // this is where "nested clips intersect" is actually implemented: the
      // parent's own box joins the set only if the parent asked to clip, and
      // it is INTERSECTED with what the parent had already inherited rather
      // than replacing it. Taking the innermost clip alone is the classic
      // wrong answer - an inner box wider than its clipped grandparent would
      // hand its children back the space the grandparent removed.
      node.clip_bounds = parent.clip_bounds;
      if (clips_subtree(parent.style)) {
        node.clip_bounds = node.clip_bounds.has_value()
                               ? intersect(*node.clip_bounds, parent.absolute)
                               : parent.absolute;
      }
    }
    for (const std::uint32_t child : node.children) {
      stack.push_back(child);
    }
  }
}

void RenderTree::Impl::damage_subtree(std::uint32_t root_index) {
  std::vector<std::uint32_t> stack{root_index};
  while (!stack.empty()) {
    const std::uint32_t index = stack.back();
    stack.pop_back();

    // The VISIBLE extent, not the declared one. A node inside a clip cannot
    // put a pixel outside it, so damaging its whole box would ask for a
    // repaint - and, through painted(), a present - of a region the user
    // cannot see. A node clipped away entirely contributes nothing at all,
    // and DamageRegion::add already ignores an empty rectangle.
    damage.add(nodes[index].visible_bounds());
    for (const std::uint32_t child : nodes[index].children) {
      stack.push_back(child);
    }
  }
}

void RenderTree::Impl::invalidate(std::uint32_t index) {
  picture_stale = true;
  damage_subtree(index);
}

void RenderTree::Impl::retire_damage(DamageRegion just_painted) {
  painted = std::move(just_painted);
  damage = DamageRegion{max_damage_rects};
}

RenderTree::RenderTree(const TreeSpec& spec) : impl_(std::make_unique<Impl>()) {
  impl_->viewport = spec.viewport;
  impl_->fonts = spec.fonts;
  impl_->images = spec.images;
  impl_->paint_mode = spec.paint_mode;
  impl_->max_damage_rects = spec.max_damage_rects;
  impl_->damage = DamageRegion{spec.max_damage_rects};
  impl_->painted = DamageRegion{spec.max_damage_rects};

  Node root_node;
  root_node.local = PixelRect{0, 0, spec.viewport.width, spec.viewport.height};
  root_node.absolute = root_node.local;
  root_node.style = spec.background;
  root_node.clip_atomic = clips_atomically(spec.background);
  impl_->nodes.push_back(std::move(root_node));
  damage_all();
}

RenderTree::RenderTree(RenderTree&&) noexcept = default;
RenderTree& RenderTree::operator=(RenderTree&&) noexcept = default;

// Out of line because Impl is incomplete in the header.
RenderTree::~RenderTree() = default;

NodeId RenderTree::add_child(NodeId parent, const PixelRect& bounds, const NodeStyle& style) {
  const auto index = static_cast<std::uint32_t>(impl_->nodes.size());

  Node node;
  node.local = bounds;
  node.style = style;
  node.clip_atomic = clips_atomically(style);
  node.parent = parent.value;
  impl_->nodes.push_back(std::move(node));
  impl_->nodes[parent.value].children.push_back(index);

  impl_->reposition(index);
  impl_->invalidate(index);
  return NodeId{index};
}

std::size_t RenderTree::node_count() const {
  return impl_->nodes.size();
}

PixelSize RenderTree::viewport() const {
  return impl_->viewport;
}

const NodeStyle& RenderTree::style(NodeId id) const {
  return impl_->nodes[id.value].style;
}

NodeId RenderTree::parent(NodeId id) const {
  return NodeId{impl_->nodes[id.value].parent};
}

std::optional<NodeId> RenderTree::hit_test(PixelPoint point) const {
  const std::uint32_t index = impl_->hit_test(point);
  if (index >= impl_->nodes.size()) {
    return std::nullopt;
  }
  return NodeId{index};
}

PixelRect RenderTree::local_bounds(NodeId id) const {
  return impl_->nodes[id.value].local;
}

PixelRect RenderTree::absolute_bounds(NodeId id) const {
  return impl_->nodes[id.value].absolute;
}

void RenderTree::set_style(NodeId id, const NodeStyle& style) {
  Node& node = impl_->nodes[id.value];

  // A change to the clip is the one style change that moves pixels the node
  // does not own. Turning a clip ON hides descendants that were painted
  // outside it, and those pixels are damaged HERE, while clip_bounds still
  // describes where they used to be allowed to go - after the update they are
  // unreachable and nothing would ever repaint them. Same shape as
  // set_local_bounds()'s damage-then-move, and the same class of bug.
  const bool clip_changed = clips_subtree(node.style) != clips_subtree(style) ||
                            (clips_subtree(style) && node.style.radii != style.radii);
  if (clip_changed) {
    impl_->damage_subtree(id.value);
  }

  node.style = style;
  node.clip_atomic = clips_atomically(style);
  if (clip_changed) {
    impl_->reposition(id.value);
  }
  impl_->invalidate(id.value);
}

void RenderTree::set_fill(NodeId id, Color fill) {
  impl_->nodes[id.value].style.fill = fill;
  impl_->invalidate(id.value);
}

void RenderTree::set_text(NodeId id, const TextStyle& text) {
  NodeStyle& style = impl_->nodes[id.value].style;
  if (style.text == text) {
    return;
  }
  style.text = text;
  impl_->nodes[id.value].clip_atomic = clips_atomically(style);
  impl_->invalidate(id.value);
}

void RenderTree::set_image(NodeId id, const ImageStyle& image) {
  NodeStyle& style = impl_->nodes[id.value].style;
  if (style.image == image) {
    return;
  }
  style.image = image;
  impl_->invalidate(id.value);
}

void RenderTree::set_local_bounds(NodeId id, const PixelRect& bounds) {
  // The vacated pixels are damaged before the move and the occupied ones
  // after it. Damaging only the destination is what leaves a trail of stale
  // paint behind a moving node.
  impl_->damage_subtree(id.value);
  impl_->nodes[id.value].local = bounds;
  impl_->reposition(id.value);
  impl_->invalidate(id.value);
}

void RenderTree::set_local_origin(NodeId id, int x, int y) {
  const PixelRect& local = impl_->nodes[id.value].local;
  set_local_bounds(id, PixelRect{x, y, local.width, local.height});
}

void RenderTree::set_scroll_offset(NodeId id, PixelPoint offset) {
  Node& node = impl_->nodes[id.value];
  if (node.scroll_offset == offset) {
    // A caller re-asserting the offset it already has is the common case in
    // an interaction loop that recomputes a clamp on every event, exactly as
    // set_text() does for a label re-asserting its string.
    return;
  }
  // Damage-then-move: the OLD positions of every descendant are damaged while
  // `scroll_offset` still describes where they used to be, then the offset
  // updates, `reposition()` recomputes `absolute` for the whole subtree, and
  // `invalidate()` damages the NEW positions. Skipping the first half is what
  // leaves a trail of the previous frame's content behind, the same defect
  // set_local_bounds() exists to avoid for an ordinary move.
  impl_->damage_subtree(id.value);
  node.scroll_offset = offset;
  impl_->reposition(id.value);
  impl_->invalidate(id.value);
}

PixelPoint RenderTree::scroll_offset(NodeId id) const {
  return impl_->nodes[id.value].scroll_offset;
}

void RenderTree::resize(PixelSize viewport) {
  impl_->viewport = viewport;
  impl_->nodes[0].local = PixelRect{0, 0, viewport.width, viewport.height};
  impl_->reposition(0);
  impl_->picture_stale = true;
  damage_all();
}

void RenderTree::set_paint_mode(PaintMode mode) {
  impl_->paint_mode = mode;
  impl_->picture_stale = true;
}

PaintMode RenderTree::paint_mode() const {
  return impl_->paint_mode;
}

const DamageRegion& RenderTree::damage() const {
  return impl_->damage;
}

const DamageRegion& RenderTree::painted() const {
  return impl_->painted;
}

void RenderTree::damage_rect(const PixelRect& rect) {
  impl_->damage.add(rect);
}

void RenderTree::damage_all() {
  impl_->damage.clear();
  impl_->damage.add(PixelRect{0, 0, impl_->viewport.width, impl_->viewport.height});
}

RepaintStats RenderTree::repaint(RasterSurface& surface) {
  DamageRegion expanded = impl_->expand(impl_->damage);
  RepaintStats stats = impl_->paint(surface, expanded);
  stats.requested_pixels = impl_->damage.area();
  impl_->retire_damage(std::move(expanded));
  return stats;
}

RepaintStats RenderTree::repaint_full(RasterSurface& surface) {
  DamageRegion whole{1};
  whole.add(PixelRect{0, 0, impl_->viewport.width, impl_->viewport.height});
  RepaintStats stats = impl_->paint(surface, whole);
  stats.requested_pixels = stats.pixels;
  impl_->retire_damage(std::move(whole));
  return stats;
}

}  // namespace dg
