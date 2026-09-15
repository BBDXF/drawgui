#include "anim_scene.h"

#include <utility>

#include "drawgui/layout/box.h"
#include "drawgui/props/node_props.h"
#include "drawgui/render/prop_ids.generated.h"

namespace anim_scene {
namespace {

using dg::BoxStyle;
using dg::EdgeInsets;
using dg::LayoutKind;
using dg::NodeStyle;
using dg::PropValue;

BoxStyle sized(int width, int height) {
  BoxStyle box;
  box.width = width;
  box.height = height;
  return box;
}

NodeStyle framed_panel() {
  NodeStyle style;
  style.fill = kPanelFill;
  style.border_color = kFrameEdge;
  style.border_width = dg::BorderWidths::all(1.0F);
  return style;
}

}  // namespace

Scene build(const dg::TreeSpec& spec) {
  dg::LayoutTree tree{spec};
  Handles handles;

  const dg::NodeId root = dg::LayoutTree::root();
  BoxStyle row;
  row.kind = LayoutKind::kRow;
  row.gap = 24;
  row.padding = EdgeInsets::all(24);
  handles.row = tree.add_child(root, row, NodeStyle{});

  // slide: an absolute-positioned container so `left` has a consumer
  // (design.md section 5.4.2), holding one child the engine moves.
  BoxStyle slide_frame_box = sized(300, 172);
  slide_frame_box.kind = LayoutKind::kAbsolute;
  handles.slide_frame = tree.add_child(handles.row, slide_frame_box, framed_panel());
  BoxStyle chip_box = sized(60, 60);
  NodeStyle chip_style;
  chip_style.fill = kChipFill;
  handles.slide_chip = tree.add_child(handles.slide_frame, chip_box, chip_style);
  (void)dg::set_prop(tree, handles.slide_chip, DG_PROP_LEFT, PropValue::number(0.0F));
  (void)dg::set_prop(tree, handles.slide_chip, DG_PROP_TOP, PropValue::number(56.0F));

  // hover: a plain panel standing in for a button - dg_node_set_transition()
  // is declared on this node by the window/check driving this scene, not
  // here, because the declaration is a per-caller policy (design.md section
  // 5.16.1's own API split: the SCENE states what exists, the CALLER states
  // how it animates).
  BoxStyle hover_box = sized(200, 172);
  NodeStyle hover_style = framed_panel();
  hover_style.fill = kButtonNormal;
  handles.hover_panel = tree.add_child(handles.row, hover_box, hover_style);

  // caret: a thin vertical bar, the shape design.md's own §5.5.2 text-caret
  // discussion assumes - centred in its own frame rather than positioned by
  // `left`/`top`, so its animated property (`opacity`) is independent of the
  // slide panel's.
  BoxStyle caret_frame_box = sized(160, 172);
  caret_frame_box.kind = LayoutKind::kAbsolute;
  handles.caret_frame = tree.add_child(handles.row, caret_frame_box, framed_panel());
  BoxStyle caret_box = sized(4, 96);
  NodeStyle caret_style;
  caret_style.fill = kCaretColor;
  handles.caret = tree.add_child(handles.caret_frame, caret_box, caret_style);
  (void)dg::set_prop(tree, handles.caret, DG_PROP_LEFT, PropValue::number(76.0F));
  (void)dg::set_prop(tree, handles.caret, DG_PROP_TOP, PropValue::number(38.0F));

  tree.layout_full();
  return Scene{std::move(tree), handles};
}

}  // namespace anim_scene
