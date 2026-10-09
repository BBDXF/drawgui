// The runtime's internal state behind dg::App / dg::Window (pimpl), shared by
// every src/app translation unit. Nothing here is public API.
//
// A node created through Window::create() has a NodeRecord, keyed by its
// NodeId in its window's `records`; `order` holds the same ids in creation
// order, and every per-node loop below walks `order`, so iteration order is
// deterministic (the C ABI's former node arena was walked in creation order
// too). A component-typed node keeps its src/ui component, plus the data
// needed to rebuild it (a theme switch or a size change rebuilds the composed
// nodes from it), in NodeRecord::state.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "drawgui/anim/animation_engine.h"
#include "drawgui/app/app.h"
#include "drawgui/app/events.h"
#include "drawgui/app/node_kind.h"
#include "drawgui/app/window.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/font_catalog.h"
#include "drawgui/render/image_catalog.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/shortcuts/action_scopes.h"
#include "drawgui/theme/theme.h"
#include "drawgui/theme/theme_bindings.h"
#include "drawgui/ui/dialog.h"
#include "drawgui/ui/dropdown.h"
#include "drawgui/ui/menu.h"
#include "drawgui/ui/progress_bar.h"
#include "drawgui/ui/table.h"
#include "drawgui/ui/tabs.h"
#include "drawgui/ui/tooltip_popup.h"
#include "drawgui/ui/tree_view.h"
#include "drawgui/widget/focus.h"
#include "drawgui/widget/interaction.h"
#include "drawgui/widget/widget_set.h"
#include "drawgui/window/popup_host.h"
#include "drawgui/window/window_manager.h"

namespace dg::app_detail {

// NodeKind::kList: the rows list_set_items() copied, and the selected row -
// the data the recycled pool rows are painted from.
struct ListState {
  std::vector<std::string> items;
  int selected = -1;
};

struct DropdownState {
  std::optional<dg::Dropdown> dropdown;
  std::string placeholder;
};

struct ProgressState {
  dg::ProgressBar progress;
};

struct TabsState {
  dg::Tabs tabs;
  std::vector<dg::TabItem> items;
  // The caller-visible panel BOX of each tab (NodeId{} once removed).
  std::vector<dg::NodeId> panels;
};

struct TreeState {
  dg::TreeView tree;
  std::vector<dg::TreeItem> items;
};

struct TableState {
  dg::Table table;
  std::vector<dg::TableColumn> columns;
  std::vector<std::vector<std::string>> rows;
  std::optional<int> sort_column;
  bool sort_ascending = true;
};

using ComponentState = std::variant<std::monostate, ListState, DropdownState, TabsState,
                                    TreeState, TableState, ProgressState>;

struct NodeRecord {
  NodeKind kind = NodeKind::kBox;
  dg::NodeId id{};
  // NodeKind::kScrollView: the composed content node every child inserted
  // under it actually goes into (the widget's scroll_content).
  dg::NodeId content{};
  ComponentState state;
  // The text Window::set_tooltip() bound.
  std::string tooltip;
};

struct NodeIdHash {
  std::size_t operator()(dg::NodeId id) const noexcept {
    return std::hash<std::uint64_t>{}((static_cast<std::uint64_t>(id.generation) << 32U) |
                                      id.index);
  }
};

struct AppState;

// One open window: the platform id window_manager.h already owns, and the
// layout/render/widget/interaction state the runtime drives through it -
// the same four objects examples/11_form_controls's own Runner and Scene
// hold, generalised from "one example, one window" to "one app, N windows".
struct WinState {
  WinState(AppState& owner, const dg::TreeSpec& spec) : app(owner), tree(spec) {}

  AppState& app;
  dg::Window* self = nullptr;

  dg::WindowId sdl_window;
  dg::LayoutTree tree;
  dg::WidgetSet widgets;
  dg::Interaction interaction;
  std::optional<dg::RasterSurface> surface;
  bool closed = false;

  // A layout pass re-placed nodes since the composed children of scroll
  // views/lists/text areas were last re-derived (resync_widgets()).
  bool relaid = true;

  // The click count of the latest primary press (PointerEvent::clicks),
  // reported by the ClickEvent its release completes.
  int last_press_clicks = 1;
  // A key press since the last primary press restarts the multi-click
  // count: click_offset is subtracted from PointerEvent::clicks.
  bool key_since_press = false;
  int click_offset = 0;

  // Set once by the first parentless create(); a second is kAlreadyAttached.
  std::optional<dg::NodeId> root;

  // 7-6: this window's own $token binding side table (ThemeBindings,
  // unchanged) - one per window, mirroring the fact that a
  // NodeId numbering space is per-window (dg::ThemeBindings is keyed by
  // NodeId), not per-app.
  dg::ThemeBindings theme_bindings;

  // 8-3d: which node currently has keyboard focus, and which nodes scope
  // which action_ids - the SAME per-window placement as theme_bindings
  // immediately above, for the identical reason (design.md section 5.5.2's
  // NodeId-keyed router state has no meaning outside the NodeId numbering
  // space of one window's own RenderTree). `focus` is driven the same way
  // examples/21_focus's own Scene drives it (focus_scene.cpp's
  // dispatch_pointer(): a pointer press on a focusable widget focuses it,
  // one anywhere else blurs) - process_pointer_event() is the runtime's
  // one caller of that rule. `action_scopes` backs Window::scope_action().
  dg::Focus focus;
  dg::ActionScopes action_scopes;

  // 8-5: this window's own AnimationEngine, present for exactly one
  // reason - remove_subtree() requires one as a NON-optional reference
  // member of NodeSideTables (include/drawgui/render/node_lifecycle.h's own
  // header comment explains why cleanup takes every side table this way).
  // Nothing in the runtime ever calls animate()/set_transition() on this
  // instance - it only ever receives cancel_all_for() calls, which are
  // no-ops against an engine with no slots.
  dg::AnimationEngine animation;

  // ABI 0.2 phase B: the overlay popups (menus, dropdown menus, the
  // dialog, tooltips) all share this host; at most one open_menu() menu
  // and one dialog are open at a time.
  std::optional<dg::PopupHost> popups;
  dg::Menu menu;
  std::int32_t menu_id = 0;
  std::optional<dg::NodeId> menu_anchor;
  dg::Dialog dialog;
  std::int32_t dialog_id = 0;
  dg::Tooltip tooltip;
  // The last pointer position inside the window (tooltip hover), and the
  // focus last reported through FocusChangedEvent - components move
  // focus themselves, the pump reports the difference.
  std::optional<dg::PixelPoint> pointer;
  std::optional<dg::NodeId> reported_focus;

  std::unordered_map<dg::NodeId, NodeRecord, NodeIdHash> records;
  std::vector<dg::NodeId> order;
  std::function<void(dg::NodeId)> on_removed;
};

// One dg::App: the window manager every one of its windows shares (SDL3's
// own multi-window design already supports this - examples/01_sdl3_multi_window
// is the precedent), its windows, and the event queue pump()/poll() drain.
struct AppState {
  explicit AppState(dg::WindowManager manager) : wm(std::move(manager)) {}

  dg::WindowManager wm;
  std::vector<std::unique_ptr<dg::Window>> windows;

  // The active theme and its variant (set_theme()); null = none, and the
  // runtime then styles everything from the builtin theme, dark variant.
  std::shared_ptr<const dg::Theme> theme;
  dg::ThemeVariant variant = dg::ThemeVariant::kLight;

  std::vector<dg::Event> pending_events;
  // App::quit() during App::run().
  bool quit_requested = false;

  // P6 sub-step a1: this app's ONE shared FontCatalog (design.md's
  // dg::FontCatalog is copyable and a copy names the same fonts - a
  // reference-counted table, per its own header comment - so every window
  // this app opens gets a cheap copy in its dg::TreeSpec rather than a
  // second scan). Absent until the first successful add_font() call; a
  // window opened before that has no fonts and therefore draws no text
  // (render_tree.h's own "absent means no node in this tree draws text").
  std::optional<dg::FontCatalog> fonts;

  // The family add_font() added FIRST, successfully - NodeKind::kLabel's
  // engine-chosen default font. Default-constructed FontId::is_valid() is
  // false, matching "no font added yet" without a second field to
  // disagree with it.
  dg::FontId default_font;

  // Every image a node shows, shared by all of this app's windows (each
  // TreeSpec gets a copy of this handle).
  dg::ImageCatalog images;

  // set_tooltip_delay(), and the ids handed out by open_menu()/open_dialog().
  int tooltip_delay_ms = 500;
  std::int32_t next_popup_id = 1;
};

}  // namespace dg::app_detail
