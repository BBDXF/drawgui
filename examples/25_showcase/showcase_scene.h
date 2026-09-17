// examples/25_showcase: a single coherent screen ("media library settings")
// combining every WidgetKind this project has (kPanel, kLabel, kButton,
// kCheckbox [plain and radio-grouped], kSlider, kScrollView is NOT used
// directly - see below -, kTextField, kList, kDropdown) plus a context menu,
// a tooltip and a modal dialog, themed entirely through 6-2's tokens.
//
// THIS IS ALSO A CROSS-FEATURE REGRESSION BED, not only a demo - the
// project's own audit (doc/completeness.md, and every "05_widgets only
// builds 3 of the 9 kinds it names" finding this slice's own task was
// commissioned against) found that no existing example puts two non-trivial
// features together. showcase_check.cpp is where the assertions that matter
// live; this scene exists to give them real geometry to assert against.
//
// kScrollView is deliberately NOT used as a separate wrapper around the
// "recent files" list: `doc/list.md`'s own `kList` is already "a clipping
// node (NodeStyle::overflow) whose children are a small, PERMANENT pool" -
// a kList IS a scrolling viewport, and wrapping a second clipping/scrolling
// node around it would test nothing an existing 10_scrolling/15_list
// example does not already cover. What this slice adds instead is
// INTERACTIVE list rows: unlike 15_list's pool nodes (plain content, no
// attached Widget - doc/menus.md section 3's own reason kList was not
// reused for dropdown/menu rows), every pool node here gets a real
// `Widget{kind = kButton}` attached, so Tab can reach a list row and
// 7-4's `Focus::blur_if_any_of()` (built for exactly this hazard, never
// exercised end-to-end before this slice) has a real caller.

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "drawgui/anim/animation_engine.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/font_catalog.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/theme/theme.h"
#include "drawgui/theme/theme_bindings.h"
#include "drawgui/widget/focus.h"
#include "drawgui/widget/interaction.h"
#include "drawgui/widget/tooltip.h"
#include "drawgui/widget/widget_set.h"
#include "drawgui/window/window_manager.h"

namespace showcase_scene {

inline constexpr int kListPoolSize = 8;
inline constexpr int kListItemHeight = 32;
inline constexpr int kListItemCount = 60;
inline constexpr int kListViewportHeight = kListPoolSize * kListItemHeight;
inline constexpr int kSidebarWidth = 220;
inline constexpr int kMainWidth = 380;

// The CJK description text: 20+ UNSPACED Chinese characters, the same
// "no space to fall back on, UAX#14 or nothing" shape
// examples/20_multiline_text's own kCjkText proves libgrapheme actually
// wraps - reused here under a `shadow`, which no existing example or test
// combines with a wrapping paragraph.
inline const std::string kDescriptionText =
    "\u672c\u9762\u677f\u5c55\u793a\u4e00\u4e2a\u865a\u62df\u5316\u5217\u8868"
    "\u4e0e\u4e00\u4e2a\u5e26\u9634\u5f71\u7684\u6362\u884c\u6bb5\u843d\u540c"
    "\u65f6\u5b58\u5728\uff0c\u9a8c\u8bc1\u4e24\u8005\u7ec4\u5408\u540e\u4ecd"
    "\u80fd\u6b63\u786e\u8ba1\u7b97\u9634\u5f71\u8fb9\u754c\u3002";

inline const std::vector<std::string> kSortOptions = {"\u540d\u79f0", "\u65e5\u671f",
                                                      "\u5927\u5c0f"};

struct Handles {
  dg::NodeId body;
  dg::NodeId header_row;
  dg::NodeId title_label;
  dg::NodeId theme_toggle;
  dg::NodeId info_button;  // right-click: context menu
  dg::NodeId help_button;  // hover: tooltip

  dg::NodeId content_row;

  dg::NodeId sidebar;
  dg::NodeId anim_checkbox;
  dg::NodeId radio_light;
  dg::NodeId radio_dark;
  dg::NodeId radio_auto;
  dg::NodeId volume_slider;
  dg::NodeId sort_dropdown;
  dg::NodeId search_field;

  dg::NodeId main_column;
  dg::NodeId description_panel;
  dg::NodeId recent_list;
  dg::NodeId profile_button;
};

struct Scene {
  dg::LayoutTree tree;
  dg::WidgetSet widgets;
  dg::Interaction interaction;
  dg::Focus focus;
  dg::FocusRing ring;
  dg::HoverTimer hover_timer;
  dg::ThemeBindings bindings;
  Handles handles;
  std::optional<dg::FontCatalog> fonts;
  dg::FontId ui_font;
  dg::Theme theme;
  dg::ThemeVariant variant = dg::ThemeVariant::kLight;
  int list_top_index = 0;
};

struct Options {
  dg::TreeSpec spec;
  std::string font_dir = "/usr/share/fonts";
};

Scene build(const Options& options);

[[nodiscard]] std::string describe(const Scene& scene, dg::NodeId id);

// Re-resolves every $token binding for the OTHER variant (colour/border
// panels, list-row separators) AND re-derives every interactive widget's
// fill_normal/fill_hover/fill_pressed from the same Theme - Widget's own
// three fills (widget_set.h) are plain Color fields, never dg::set_prop()
// consumers, so a live theme switch has to touch them through a SEPARATE,
// explicit re-derivation rather than through ThemeBindings::apply() alone.
// This is the one place this slice's own scene touches BOTH theming
// mechanisms in the same call - see doc/showcase.md for why that is a real
// (if unsurprising) distinction rather papered over as "just tokens".
std::size_t switch_theme_variant(Scene& scene);

// Applies the widget-fill half of switch_theme_variant() to exactly the
// node ids passed - used both by switch_theme_variant() itself (every
// interactive widget currently built) and by a caller that just attached a
// FRESH widget after the fact (a newly opened popup's rows), which must be
// coloured for the theme ALREADY active rather than left at whatever
// literal default Widget{} would otherwise carry.
void paint_interactive_widget(Scene& scene, dg::NodeId id, dg::WidgetKind kind);

void apply_focus_change(Scene& scene, dg::WindowManager& manager, dg::WindowId window,
                        std::optional<dg::NodeId> target);
void tab(Scene& scene, dg::WindowManager& manager, dg::WindowId window, bool backwards);

// Re-derives every kList pool slot from `top_index`, applies the resulting
// content through paint_interactive_widget()-consistent styling, and blurs
// focus off any pool node that got reassigned to a different logical item
// (7-4's own blur_if_any_of(), exercised end to end for the first time -
// doc/focus.md section 6 names this as unit-tested but never demonstrated
// inside a running example).
dg::FocusChange list_scroll_to(Scene& scene, int top_index);

bool dispatch_pointer(Scene& scene, dg::WindowManager& manager, dg::WindowId window,
                      const dg::PointerEvent& event, bool* wants_context_menu,
                      bool* wants_dropdown);

void dispatch_key(Scene& scene, dg::WindowManager& manager, dg::WindowId window,
                  const dg::KeyEvent& event);

}  // namespace showcase_scene
