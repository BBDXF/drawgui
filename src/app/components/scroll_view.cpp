// NodeKind::kScrollView: a clipping viewport whose children live in a
// composed content node, with a scrollbar.

#include "app/components/components.h"
#include "app/internal.h"

namespace dg::app_detail {
namespace {

constexpr int kScrollViewWidth = 240;
constexpr int kScrollViewHeight = 160;

}  // namespace

// DG_PROP_SCROLL_AXIS on a scroll view: the widget's axis, the gutter's
// edge and the content's direction follow the layout's axis.
void follow_scroll_axis(WinState& window, const NodeRecord& record) {
  const dg::ScrollAxis axis = window.tree.box(record.id).scroll_axis;
  dg::Widget updated = window.widgets.at(record.id);
  updated.scroll_axis = axis;
  window.widgets.attach(record.id, updated);
  const int gutter = window.widgets.scrollbar_style().size;
  dg::BoxStyle box = window.tree.box(record.id);
  box.padding.right = axis == dg::ScrollAxis::kVertical ? gutter : 0;
  box.padding.bottom = axis == dg::ScrollAxis::kHorizontal ? gutter : 0;
  window.tree.set_box(record.id, box);
  dg::BoxStyle content = window.tree.box(record.content);
  const bool wraps = dg::wraps_children(content.kind);
  if (axis == dg::ScrollAxis::kHorizontal) {
    content.kind = wraps ? dg::LayoutKind::kWrapRow : dg::LayoutKind::kRow;
  } else {
    content.kind = wraps ? dg::LayoutKind::kWrapColumn : dg::LayoutKind::kColumn;
  }
  window.tree.set_box(record.content, content);
  window.tree.render().set_scroll_offset(record.id, dg::PixelPoint{0, 0});
  window.relaid = true;
}

namespace components {

// A clipping kLeaf viewport with the scrollbar's gutter reserved on its
// trailing edge (examples/10_scrolling's / 15_list's recipe). No fill.
void scroll_view_defaults(const WinState& window, dg::BoxStyle& box, dg::NodeStyle& style) {
  box.kind = dg::LayoutKind::kLeaf;
  box.width = kScrollViewWidth;
  box.height = kScrollViewHeight;
  box.scroll_axis = dg::ScrollAxis::kVertical;
  box.padding.right = window.widgets.scrollbar_style().size;
  style.overflow = dg::Overflow::kClip;
}

void compose_scroll_view(WinState& window, NodeRecord& record) {
  const dg::NodeId new_id = record.id;
  // The content node every caller child goes into (create()), then the
  // scrollbar track + thumb, added last so they paint on top.
  dg::BoxStyle content_box;
  content_box.kind = dg::LayoutKind::kColumn;
  record.content = window.tree.add_child(new_id, content_box, dg::NodeStyle{});
  dg::Widget widget;
  widget.kind = dg::WidgetKind::kScrollView;
  widget.scroll_axis = dg::ScrollAxis::kVertical;
  widget.scroll_content = record.content;
  window.widgets.attach(new_id, widget);
  const dg::NodeId track = window.tree.add_child(new_id, dg::BoxStyle{}, dg::NodeStyle{});
  const dg::NodeId thumb = window.tree.add_child(track, dg::BoxStyle{}, dg::NodeStyle{});
  window.widgets.attach_scrollbar(window.tree.render(), new_id, track, thumb);
  window.relaid = true;
}

}  // namespace components
}  // namespace dg::app_detail
