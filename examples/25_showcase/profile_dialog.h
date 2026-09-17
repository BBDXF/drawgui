// The "profile" modal dialog's own content: a translucent backdrop (never
// interactive) plus a centred panel carrying a CJK-capable "name"
// kTextField (the surface a synthesized IME composition event drives -
// showcase_check.cpp's own cross-feature claim) and a "Close" kButton.
// Structurally identical to examples/23_menu_tooltip_dialog/
// dialog_panel.{h,cpp} except for the added TextField - kept as its own
// file per this project's established per-example-content-builder
// precedent (dialog_panel.cpp is not shared either).

#pragma once

#include "drawgui/graphics/types.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/theme/theme.h"
#include "drawgui/widget/widget_set.h"

namespace profile_dialog {

struct Handles {
  dg::NodeId backdrop;
  dg::NodeId panel;
  dg::NodeId name_field;
  dg::NodeId close_button;
};

Handles build(dg::RenderTree& tree, dg::WidgetSet& widgets, dg::NodeId parent,
              dg::PixelSize window_size, dg::PixelSize panel_size, dg::FontId font,
              const dg::Theme& theme, dg::ThemeVariant variant);

}  // namespace profile_dialog
