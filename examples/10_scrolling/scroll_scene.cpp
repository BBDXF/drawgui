#include "scroll_scene.h"

#include <cstdint>
#include <initializer_list>
#include <tuple>
#include <utility>

#include "drawgui/layout/box.h"
#include "drawgui/theme/theme.h"
#include "drawgui/theme/theme_loader.h"
#include "drawgui/theme/token_ids.generated.h"

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

// The scrollbar look, from the builtin theme's dark variant (this scene's
// own background is dark) - the same direct token read the disabled
// palette uses in examples/11_form_controls.
dg::ScrollbarStyle scrollbar_tokens() {
  dg::ScrollbarStyle style;
  const dg::Expected<dg::Theme, dg::ThemeLoadError> theme = dg::load_builtin_theme();
  if (!theme.has_value()) {
    return style;
  }
  const dg::Theme& t = theme.value();
  const dg::ThemeVariant dark = dg::ThemeVariant::kDark;
  style.track = t.color_value(DG_TOKEN_COLOR_SCROLLBAR_TRACK, dark);
  style.thumb = t.color_value(DG_TOKEN_COLOR_SCROLLBAR_THUMB, dark);
  style.thumb_hover = t.color_value(DG_TOKEN_COLOR_SCROLLBAR_THUMB_HOVER, dark);
  style.thumb_pressed = t.color_value(DG_TOKEN_COLOR_SCROLLBAR_THUMB_PRESSED, dark);
  style.size = t.int_value(DG_TOKEN_SIZE_SCROLLBAR).value_or(style.size);
  return style;
}

// The track (added after the content, so it paints and hit-tests on top)
// and its thumb. Their boxes do not matter: sync_scrollbar() places both.
std::pair<NodeId, NodeId> add_scrollbar(LayoutTree& tree, dg::WidgetSet& widgets,
                                        NodeId viewport) {
  const NodeId track = tree.add_child(viewport, BoxStyle{}, NodeStyle{});
  const NodeId thumb = tree.add_child(track, BoxStyle{}, NodeStyle{});
  widgets.attach_scrollbar(tree.render(), viewport, track, thumb);
  return {track, thumb};
}

}  // namespace

Scene build(const dg::TreeSpec& spec) {
  LayoutTree tree{spec};
  dg::WidgetSet widgets;
  Handles handles;
  widgets.set_scrollbar_style(scrollbar_tokens());
  handles.gutter = widgets.scrollbar_style().size;

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
  vertical_box.padding.right = handles.gutter;  // the scrollbar's reserved gutter
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
    item.width = kVerticalViewportWidth - handles.gutter;
    item.height = kVerticalItemHeight;
    handles.vertical_items.push_back(
        tree.add_child(handles.vertical_content, item, chip_style(i, kVerticalItemCount)));
  }

  Widget vertical_widget;
  vertical_widget.kind = WidgetKind::kScrollView;
  vertical_widget.scroll_axis = ScrollAxis::kVertical;
  vertical_widget.scroll_content = handles.vertical_content;
  widgets.attach(handles.vertical_viewport, vertical_widget);
  std::tie(handles.vertical_track, handles.vertical_thumb) =
      add_scrollbar(tree, widgets, handles.vertical_viewport);

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
  horizontal_box.padding.bottom = handles.gutter;
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
    item.height = kHorizontalViewportHeight - handles.gutter;
    handles.horizontal_items.push_back(
        tree.add_child(handles.horizontal_content, item, chip_style(i, kHorizontalItemCount)));
  }

  Widget horizontal_widget;
  horizontal_widget.kind = WidgetKind::kScrollView;
  horizontal_widget.scroll_axis = ScrollAxis::kHorizontal;
  horizontal_widget.scroll_content = handles.horizontal_content;
  widgets.attach(handles.horizontal_viewport, horizontal_widget);
  std::tie(handles.horizontal_track, handles.horizontal_thumb) =
      add_scrollbar(tree, widgets, handles.horizontal_viewport);

  tree.layout_full();
  Scene scene{std::move(tree), std::move(widgets), std::move(handles)};
  sync_scrollbars(scene);
  return scene;
}

int scroll_extent(const Scene& scene, NodeId viewport) {
  const dg::PixelRect content = scene.tree.content_bounds(viewport);
  return scene.widgets.at(viewport).scroll_axis == ScrollAxis::kHorizontal ? content.width
                                                                           : content.height;
}

void sync_scrollbars(Scene& scene) {
  for (const NodeId viewport :
       {scene.handles.vertical_viewport, scene.handles.horizontal_viewport}) {
    scene.widgets.sync_scrollbar(scene.tree.render(), viewport, scroll_extent(scene, viewport));
  }
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
  if (id == scene.handles.vertical_track || id == scene.handles.vertical_thumb) {
    return id == scene.handles.vertical_thumb ? "vertical scrollbar thumb"
                                              : "vertical scrollbar track";
  }
  if (id == scene.handles.horizontal_track || id == scene.handles.horizontal_thumb) {
    return id == scene.handles.horizontal_thumb ? "horizontal scrollbar thumb"
                                                : "horizontal scrollbar track";
  }
  if (id == scene.handles.body) {
    return "body";
  }
  return "node " + std::to_string(id.index);
}

}  // namespace scroll_scene
