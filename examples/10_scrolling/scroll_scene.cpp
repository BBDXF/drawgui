#include "scroll_scene.h"

#include <cstdint>

#include "drawgui/layout/box.h"

namespace scroll_scene {
namespace {

using dg::BoxStyle;
using dg::Color;
using dg::LayoutKind;
using dg::LayoutTree;
using dg::NodeId;
using dg::NodeStyle;
using dg::Overflow;
using dg::ScrollAxis;
using dg::Widget;
using dg::WidgetKind;

// A colour ramp rather than a label - see the file header. `t` is the item's
// position in its list, 0..1, so the ramp is reproducible from the index
// alone and a check can recompute the expected colour without reading the
// tree back.
Color ramp_colour(int index, int count) {
  const float t = count > 1 ? static_cast<float>(index) / static_cast<float>(count - 1) : 0.0F;
  const auto lo = static_cast<std::uint32_t>(0x2C);
  const auto hi = static_cast<std::uint32_t>(0xE8);
  const auto channel =
      static_cast<std::uint32_t>(static_cast<float>(lo) + (t * static_cast<float>(hi - lo)));
  return Color::from_argb(0xFF000000U | (channel << 16U) | 0x3C00U | (0xB4U - (channel / 3U)));
}

NodeStyle chip_style(int index, int count) {
  NodeStyle style;
  style.fill = ramp_colour(index, count);
  return style;
}

}  // namespace

Scene build(const dg::TreeSpec& spec) {
  LayoutTree tree{spec};
  dg::WidgetSet widgets;
  Handles handles;

  BoxStyle body_box;
  body_box.kind = LayoutKind::kColumn;
  body_box.gap = 20;
  body_box.padding = dg::EdgeInsets::all(20);
  handles.body = LayoutTree::root();
  tree.set_box(handles.body, body_box);

  // --- The vertical list --------------------------------------------------

  BoxStyle vertical_box;
  vertical_box.kind = LayoutKind::kLeaf;
  vertical_box.scroll_axis = ScrollAxis::kVertical;
  vertical_box.width = kVerticalViewportWidth;
  vertical_box.height = kVerticalViewportHeight;
  NodeStyle vertical_style;
  vertical_style.fill = Color::from_argb(0xFF14171C);
  vertical_style.overflow = Overflow::kClip;
  handles.vertical_viewport = tree.add_child(handles.body, vertical_box, vertical_style);

  BoxStyle vertical_content_box;
  vertical_content_box.kind = LayoutKind::kColumn;
  vertical_content_box.gap = 4;
  handles.vertical_content =
      tree.add_child(handles.vertical_viewport, vertical_content_box, NodeStyle{});

  handles.vertical_items.reserve(kVerticalItemCount);
  for (int i = 0; i < kVerticalItemCount; ++i) {
    BoxStyle item;
    item.width = kVerticalViewportWidth;
    item.height = kVerticalItemHeight;
    handles.vertical_items.push_back(
        tree.add_child(handles.vertical_content, item, chip_style(i, kVerticalItemCount)));
  }

  Widget vertical_widget;
  vertical_widget.kind = WidgetKind::kScrollView;
  vertical_widget.scroll_axis = ScrollAxis::kVertical;
  vertical_widget.scroll_content = handles.vertical_content;
  widgets.attach(handles.vertical_viewport, vertical_widget);

  // --- The horizontal strip ------------------------------------------------

  BoxStyle horizontal_box;
  horizontal_box.kind = LayoutKind::kLeaf;
  horizontal_box.scroll_axis = ScrollAxis::kHorizontal;
  // No declared width: `align_self: stretch` fills whatever the column's
  // own width ends up being, rather than main_size (which only affects a
  // node's own MAIN axis and does nothing on a kLeaf, which arranges no
  // children of its own to fill anything with).
  horizontal_box.align_self = dg::CrossAlign::kStretch;
  horizontal_box.height = kHorizontalViewportHeight;
  NodeStyle horizontal_style;
  horizontal_style.fill = Color::from_argb(0xFF14171C);
  horizontal_style.overflow = Overflow::kClip;
  handles.horizontal_viewport = tree.add_child(handles.body, horizontal_box, horizontal_style);

  BoxStyle horizontal_content_box;
  horizontal_content_box.kind = LayoutKind::kRow;
  horizontal_content_box.gap = 4;
  handles.horizontal_content =
      tree.add_child(handles.horizontal_viewport, horizontal_content_box, NodeStyle{});

  handles.horizontal_items.reserve(kHorizontalItemCount);
  for (int i = 0; i < kHorizontalItemCount; ++i) {
    BoxStyle item;
    item.width = kHorizontalItemWidth;
    item.height = kHorizontalViewportHeight;
    handles.horizontal_items.push_back(
        tree.add_child(handles.horizontal_content, item, chip_style(i, kHorizontalItemCount)));
  }

  Widget horizontal_widget;
  horizontal_widget.kind = WidgetKind::kScrollView;
  horizontal_widget.scroll_axis = ScrollAxis::kHorizontal;
  horizontal_widget.scroll_content = handles.horizontal_content;
  widgets.attach(handles.horizontal_viewport, horizontal_widget);

  tree.layout_full();
  return Scene{std::move(tree), std::move(widgets), std::move(handles)};
}

std::string describe(const Scene& scene, NodeId id) {
  const auto index_of = [&](const std::vector<NodeId>& items, const char* label) {
    for (std::size_t i = 0; i < items.size(); ++i) {
      if (items[i] == id) {
        return std::string{label} + " item " + std::to_string(i);
      }
    }
    return std::string{};
  };
  std::string found = index_of(scene.handles.vertical_items, "vertical");
  if (!found.empty()) {
    return found;
  }
  found = index_of(scene.handles.horizontal_items, "horizontal");
  if (!found.empty()) {
    return found;
  }
  if (id == scene.handles.vertical_viewport) {
    return "vertical viewport background";
  }
  if (id == scene.handles.horizontal_viewport) {
    return "horizontal viewport background";
  }
  if (id == scene.handles.body) {
    return "body";
  }
  return "node " + std::to_string(id.index);
}

}  // namespace scroll_scene
