// dg::Window - one window of a dg::App: its node tree, focus, popups and the
// widgets composed inside its nodes.
//
// Nodes are created with create() and named by NodeId afterwards. A node of
// a composed kind (a checkbox's indicator, an editor's caret, a list's rows,
// a component's parts) owns children create() never returns; they are
// reached only through the node that owns them. Every call naming a node
// that is not a live create() node of THIS window answers kInvalidNode.
//
// Owned by its App (App::create_window()/destroy_window()).

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "drawgui/app/node_kind.h"
#include "drawgui/app/status.h"
#include "drawgui/base/expected.h"
#include "drawgui/base/pixel_geometry.h"
#include "drawgui/props/node_props.h"
#include "drawgui/render/image_catalog.h"
#include "drawgui/render/node_id.h"
#include "drawgui/window/window_manager.h"

namespace dg {

class ActionScopes;
class AnimationEngine;
class Focus;
class Interaction;
class LayoutTree;
class PopupHost;
class ThemeBindings;
class WidgetSet;
struct MenuItem;
struct TableColumn;
struct TreeItem;

namespace app_detail {
struct WinState;
}  // namespace app_detail

class Window {
 public:
  explicit Window(std::unique_ptr<app_detail::WinState> state);
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;
  Window(Window&&) = delete;
  Window& operator=(Window&&) = delete;
  ~Window();

  [[nodiscard]] WindowId id() const;
  // The user closed it (WindowClosedEvent); every later call is a no-op.
  [[nodiscard]] bool closed() const;
  [[nodiscard]] std::optional<NodeId> root() const;

  // -- Tree --------------------------------------------------------------
  // Builds a node of `kind` under `parent` (none = becomes the window's
  // root; kAlreadyAttached when it has one), before the direct child
  // `before` (none = last). A scroll view's children go into its content.
  [[nodiscard]] Expected<NodeId, Status> create(NodeKind kind,
                                                std::optional<NodeId> parent = std::nullopt,
                                                std::optional<NodeId> before = std::nullopt);
  // Moves a live node under `parent` before `before`; kInvalidArgument for
  // a cycle.
  Status move(NodeId child, NodeId parent, std::optional<NodeId> before = std::nullopt);
  // Removes `node` and its subtree; on_removed() then fires once per created
  // node that went with it.
  Status remove(NodeId node);
  void set_on_removed(std::function<void(NodeId)> hook);
  // Where children inserted under `node` actually live (a scroll view's
  // content node; `node` itself otherwise).
  [[nodiscard]] NodeId child_container(NodeId node) const;
  [[nodiscard]] std::optional<NodeKind> kind_of(NodeId node) const;
  // Marks `node`'s subtree for repaint.
  void invalidate(NodeId node);

  // -- Properties --------------------------------------------------------
  // A property write, routed to the node it lands on (a scroll view's
  // container properties go to its content, an editor's text properties to
  // its text). Statuses as dg::set_prop() reports them.
  Status set_prop(NodeId node, std::uint16_t prop_id, const PropValue& value);
  // Binds `prop_id` to the active theme's `token_id`.
  Status bind_token(NodeId node, std::uint16_t prop_id, std::uint16_t token_id);
  Status set_text(NodeId node, std::uint16_t prop_id, std::string_view text);
  // An editor's committed text.
  [[nodiscard]] Expected<std::string, Status> text(NodeId node) const;
  [[nodiscard]] Expected<bool, Status> checked(NodeId node) const;
  Status set_disabled(NodeId node, bool disabled);
  Status set_tooltip(NodeId node, std::string text);
  Status set_image(NodeId node, ImageId image);
  Status scope_action(NodeId node, std::uint16_t action_id);
  [[nodiscard]] Expected<PixelPoint, Status> scroll_offset(NodeId node) const;

  // -- Focus -------------------------------------------------------------
  Status focus(NodeId node);
  void clear_focus();
  [[nodiscard]] std::optional<NodeId> focused() const;

  // -- Lists, dropdowns, tabs, trees, tables -----------------------------
  Status list_set_items(NodeId list, std::vector<std::string> items, int item_height);
  [[nodiscard]] Expected<int, Status> list_selected(NodeId list) const;
  Status list_select(NodeId list, int index);

  Status dropdown_set_items(NodeId dropdown, std::vector<std::string> items);
  [[nodiscard]] Expected<int, Status> dropdown_selected(NodeId dropdown) const;
  Status dropdown_select(NodeId dropdown, int index);

  // Adds a tab and returns its (created, initially empty) panel node.
  [[nodiscard]] Expected<NodeId, Status> tabs_add(NodeId tabs, std::string label,
                                                  bool disabled);
  [[nodiscard]] Expected<int, Status> tabs_selected(NodeId tabs) const;
  Status tabs_select(NodeId tabs, int index);

  Status tree_set_items(NodeId tree, std::vector<TreeItem> items);
  [[nodiscard]] Expected<int, Status> tree_selected(NodeId tree) const;
  // -1 deselects.
  Status tree_select(NodeId tree, int index);
  Status tree_set_expanded(NodeId tree, int index, bool expanded);

  Status table_set_columns(NodeId table, std::vector<TableColumn> columns);
  [[nodiscard]] Expected<int, Status> table_column_count(NodeId table) const;
  // Row-major, one string per column per row.
  Status table_set_rows(NodeId table, std::vector<std::vector<std::string>> rows);
  [[nodiscard]] Expected<int, Status> table_selected(NodeId table) const;
  // -1 deselects.
  Status table_select(NodeId table, int row);
  Status table_set_sort_indicator(NodeId table, std::optional<int> column, bool ascending);

  // -- Popups ------------------------------------------------------------
  // Opens a menu below `anchor`'s bounds (or at `at`); returns its id, which
  // its MenuEvent repeats. A menu already open is dismissed first.
  [[nodiscard]] Expected<std::int32_t, Status> open_menu(std::optional<NodeId> anchor,
                                                         PixelPoint at,
                                                         const std::vector<MenuItem>& items);
  void close_menu();
  [[nodiscard]] bool has_dialog() const;
  [[nodiscard]] Expected<std::int32_t, Status> open_dialog(std::string title, std::string body,
                                                           std::vector<std::string> buttons,
                                                           std::optional<int> default_button,
                                                           std::optional<int> cancel_button);
  void close_dialog();

  // -- The OS window -----------------------------------------------------
  Expected<void, WindowError> set_title(const std::string& title);
  [[nodiscard]] Expected<std::string, WindowError> title() const;
  Expected<void, WindowError> set_size(int width, int height);
  [[nodiscard]] Expected<PixelSize, WindowError> size() const;
  Expected<void, WindowError> set_position(int x, int y);
  [[nodiscard]] Expected<PixelPoint, WindowError> position() const;
  Expected<void, WindowError> minimize();
  Expected<void, WindowError> maximize();
  Expected<void, WindowError> restore();
  Expected<void, WindowError> set_fullscreen(bool fullscreen);
  Expected<void, WindowError> set_resizable(bool resizable);
  Expected<void, WindowError> set_min_size(int width, int height);
  Expected<void, WindowError> set_max_size(int width, int height);
  Expected<void, WindowError> set_icon(const ImageView& icon);
  [[nodiscard]] Expected<WindowState, WindowError> state() const;

  // -- Escape hatches ----------------------------------------------------
  [[nodiscard]] LayoutTree& layout();
  [[nodiscard]] const LayoutTree& layout() const;
  [[nodiscard]] WidgetSet& widgets();
  [[nodiscard]] Focus& focus_state();
  [[nodiscard]] Interaction& interaction();
  [[nodiscard]] ThemeBindings& theme_bindings();
  [[nodiscard]] AnimationEngine& animation();
  [[nodiscard]] ActionScopes& action_scopes();
  [[nodiscard]] PopupHost& popups();

  // Internal: the runtime's own state (src/app).
  [[nodiscard]] app_detail::WinState& impl() { return *state_; }
  [[nodiscard]] const app_detail::WinState& impl() const { return *state_; }

 private:
  std::unique_ptr<app_detail::WinState> state_;
};

}  // namespace dg
