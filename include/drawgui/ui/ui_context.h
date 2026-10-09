// The component layer's shared plumbing: what every library popup component
// (Menu, Dropdown, Tooltip, Dialog - include/drawgui/ui/) needs to build
// nodes, register widgets, move focus and open/destroy its overlay.
//
// WHY A LAYER ABOVE WidgetSet: WidgetSet never creates nodes (it attaches
// behaviour to nodes a caller already made), and PopupHost never knows what
// goes inside its container. These components are the first library code
// that does both - they create the content nodes under a PopupHandle's
// content_root, attach ordinary kButton widgets to them, and destroy() the
// whole overlay on close. They add no render primitive, no WidgetKind and
// no callback: the host loop calls handle_pointer()/handle_key()/tick() and
// reads a typed result, the same "the caller drives, the library reports"
// shape every existing widget API has.
//
// NOT A STORED CONTEXT: a UiContext is a short-lived bundle of references a
// caller builds per call (or keeps next to the tables it names). Components
// never store one, so a component outliving a tree is a stale NodeId at
// worst, never a dangling reference.

#pragma once

#include <string>

#include "drawgui/graphics/types.h"
#include "drawgui/render/font_catalog.h"
#include "drawgui/render/node_lifecycle.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/theme/theme.h"
#include "drawgui/window/popup_host.h"
#include "drawgui/window/window_manager.h"

namespace dg {

// Every colour/size a component paints with. Defaults are a usable dark
// palette; popup_style_from_theme() fills it from the theme's existing
// tokens.
struct PopupStyle {
  Color surface = Color::from_argb(0xFF2B303B);
  Color on_surface = Color::from_argb(0xFFE6E9F0);
  Color border = Color::from_argb(0xFF3A4152);
  Color item_hover = Color::from_argb(0xFF35415A);     // highlighted menu item / button hover
  Color on_item_hover = Color::from_argb(0xFFE6E9F0);  // text on a highlighted item
  Color item_pressed = Color::from_argb(0xFF3B4C6E);
  Color accent = Color::from_argb(0xFF6699FF);  // check marks, the default button
  Color on_accent = Color::from_argb(0xFF0D1117);
  Color accent_hover = Color::from_argb(0xFF7FAAFF);
  Color accent_pressed = Color::from_argb(0xFF5580D9);
  Color on_disabled = Color::from_argb(0xFF6E757D);
  Color focus_ring = Color::from_argb(0xFFFFA733);
  Color scrim = Color::from_argb(0x99000000);  // dims the window behind a dialog

  int item_height = 30;      // one menu item row
  int separator_height = 9;  // one menu separator row (a 1px line, centred)
  int padding = 8;           // inner padding of menus, tooltips, dialog panels
  int radius = 6;
  float font_size = 14.0F;
  float small_font_size = 13.0F;  // tooltips
};

// surface/on-surface/border/primary-hover/on-primary/primary-pressed/
// primary/on-primary/on-disabled/focus-ring/scrim colours for `variant`, and
// radius.md/space.md - every token missing from `theme` keeps its default.
// item_hover/item_pressed are then re-derived as a primary tint of the
// surface (on_item_hover = on_surface).
[[nodiscard]] PopupStyle popup_style_from_theme(const Theme& theme, ThemeVariant variant);

// The references a component call needs. `tables` carries WidgetSet/Focus/
// Interaction (and the other three side tables) because PopupHost::destroy()
// needs all six anyway. `fonts` is optional: without it text widths are
// approximated and Dropdown cannot commit a selection (WidgetSet::
// dropdown_select() needs a FontCatalog).
//
// Reference members on purpose, like NodeSideTables: a per-call argument
// bundle, never stored or assigned.
// NOLINTBEGIN(cppcoreguidelines-avoid-const-or-ref-data-members,cppcoreguidelines-pro-type-member-init)
struct UiContext {
  PopupHost& popups;
  WindowId window;
  RenderTree& tree;
  NodeSideTables tables;
  const FontCatalog* fonts = nullptr;
  FontId font;
  PopupStyle style;
};
// NOLINTEND(cppcoreguidelines-avoid-const-or-ref-data-members,cppcoreguidelines-pro-type-member-init)

// The pixel width `text` occupies at `font`/`size`: a real shaped
// measurement when `fonts` is non-null, a 0.6em-per-byte estimate otherwise.
[[nodiscard]] int measure_text_width(const FontCatalog* fonts, FontId font, float size,
                                     const std::string& text);

}  // namespace dg
