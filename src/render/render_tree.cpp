#include "drawgui/render/render_tree.h"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

// The tree owns an sk_sp<SkPicture>, so ~Impl - and therefore ~RenderTree,
// which is defined here - needs the complete type.
#include "include/core/SkPicture.h"

#include "render/tree_impl.h"

namespace dg {

bool clips_atomically(const NodeStyle& style) {
  return !style.radii.is_zero();
}

void RenderTree::Impl::rebuild_paint_order() {
  paint_order.clear();
  paint_order.reserve(nodes.size());
  std::vector<std::uint32_t> stack{0};
  while (!stack.empty()) {
    const std::uint32_t index = stack.back();
    stack.pop_back();
    paint_order.push_back(index);
    const std::vector<std::uint32_t>& children = nodes[index].children;
    for (auto child = children.rbegin(); child != children.rend(); ++child) {
      stack.push_back(*child);
    }
  }
}

void RenderTree::Impl::reposition(std::uint32_t root_index) {
  std::vector<std::uint32_t> stack{root_index};
  while (!stack.empty()) {
    const std::uint32_t index = stack.back();
    stack.pop_back();
    Node& node = nodes[index];
    node.absolute = index == 0 ? node.local
                               : node.local.offset_by(nodes[node.parent].absolute.x,
                                                      nodes[node.parent].absolute.y);
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
    damage.add(nodes[index].absolute);
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
  impl_->paint_order.push_back(0);
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

  impl_->rebuild_paint_order();
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

PixelRect RenderTree::local_bounds(NodeId id) const {
  return impl_->nodes[id.value].local;
}

PixelRect RenderTree::absolute_bounds(NodeId id) const {
  return impl_->nodes[id.value].absolute;
}

void RenderTree::set_style(NodeId id, const NodeStyle& style) {
  impl_->nodes[id.value].style = style;
  impl_->nodes[id.value].clip_atomic = clips_atomically(style);
  impl_->invalidate(id.value);
}

void RenderTree::set_fill(NodeId id, Color fill) {
  impl_->nodes[id.value].style.fill = fill;
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
