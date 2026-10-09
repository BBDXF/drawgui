// The in-window data controls' shared plumbing: ProgressBar, Tabs, TreeView
// and Table (include/drawgui/ui/). Like the popup components (ui_context.h)
// they create nodes, attach ordinary widgets and report typed results from
// handle_pointer()/handle_key() - no callback, no new render primitive, and
// no new WidgetKind: Tree and Table reuse kList's recycling pool, Tabs and
// the virtualized controls are focused as ordinary kButton widgets.
//
// Unlike the popup components they live INSIDE the caller's layout, so they
// build through a LayoutTree and carry one caller obligation every
// paint-time-positioned control in this library already has (resync_sliders(),
// sync_scrollbar(), list_sync()): call sync() after every layout pass.
//
// A ControlContext is a per-call bundle of references, never stored.

#pragma once

#include "drawgui/graphics/types.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/font_catalog.h"
#include "drawgui/render/node_lifecycle.h"
#include "drawgui/theme/theme.h"

namespace dg {

// Every colour/size the data controls paint with. Defaults are a usable dark
// palette; control_style_from_theme() fills it from existing theme tokens.
struct ControlStyle {
  Color surface = Color::from_argb(0xFF2B303B);
  Color on_surface = Color::from_argb(0xFFE6E9F0);
  Color border = Color::from_argb(0xFF3A4152);  // also the progress track, tab hover
  Color accent = Color::from_argb(0xFF6699FF);  // progress fill, selection, active tab
  Color on_accent = Color::from_argb(0xFF0D1117);
  Color on_disabled = Color::from_argb(0xFF6E757D);

  int row_height = 26;  // one tree/table row, and the table header
  int tab_height = 32;
  int indent = 16;  // tree indent per depth level, and the disclosure column
  int padding = 8;
  int radius = 4;
  float font_size = 14.0F;

  // Indeterminate progress: one sweep of the moving segment, and the
  // segment's length as a percentage of the track.
  int indeterminate_period_ms = 1200;
  int indeterminate_percent = 30;
};

// surface/on-surface/border/primary/on-primary/on-disabled, radius.sm and
// space.md; every token missing from `theme` keeps its default.
[[nodiscard]] ControlStyle control_style_from_theme(const Theme& theme, ThemeVariant variant);

// NOLINTBEGIN(cppcoreguidelines-avoid-const-or-ref-data-members,cppcoreguidelines-pro-type-member-init)
struct ControlContext {
  LayoutTree& layout;
  NodeSideTables tables;
  const FontCatalog* fonts = nullptr;
  FontId font;
  ControlStyle style;

  [[nodiscard]] RenderTree& tree() const { return layout.render(); }
};
// NOLINTEND(cppcoreguidelines-avoid-const-or-ref-data-members,cppcoreguidelines-pro-type-member-init)

}  // namespace dg
