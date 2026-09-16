// The popup's own content: two plain kButton widgets - textless, matching
// examples/14_popup's own precedent of a deliberately minimal, non-widget
// menu (doc/popup.md section 6), except THIS demo needs real tab stops, so
// these two ARE attached WidgetKind::kButton nodes rather than flat colour
// blocks.
//
// build() takes the WidgetSet to attach into as an explicit parameter
// rather than owning one itself - the finding that shaped this file: the
// OVERLAY branch appends its content into the HOST's own RenderTree
// (doc/popup.md section 3), and Tab order is computed by walking that same
// RenderTree against ONE WidgetSet (dg::focus_order()'s own signature) - so
// an overlay popup's buttons must be attached to the HOST's WidgetSet, not
// a second one, or focus_next() would never see them. The NATIVE branch,
// by contrast, owns a genuinely separate RenderTree, so it is correct (and
// necessary - the node ids are a different space entirely) to attach into
// a fresh, separate WidgetSet the caller owns for exactly that popup.

#pragma once

#include "drawgui/graphics/types.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/widget/widget_set.h"

namespace popup_menu {

inline constexpr dg::PixelSize kSize{160, 88};

struct Handles {
  dg::NodeId btn1;
  dg::NodeId btn2;
};

Handles build(dg::RenderTree& tree, dg::WidgetSet& widgets, dg::NodeId parent);

}  // namespace popup_menu
