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
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "drawgui/anim/curve.h"
#include "drawgui/app/items.h"
#include "drawgui/app/node_kind.h"
#include "drawgui/app/prop.h"
#include "drawgui/app/refs.h"
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
class RasterSurface;
class ThemeBindings;
class WidgetSet;

namespace app_detail {
struct WinState;
}  // namespace app_detail

// Names one Window::animate() animation of one window; 0 = none.
using AnimationId = std::uint32_t;

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
  // Whether `node` is `ancestor` or lies in its subtree (both live create()
  // nodes). Events name the innermost created node under the pointer - a
  // label inside a card, say - so "was it inside the card" asks this.
  [[nodiscard]] bool contains(NodeId ancestor, NodeId node) const;
  // Marks `node`'s subtree for repaint and its composed children for
  // re-sync on the next pump(). See "Escape hatches" below.
  void invalidate(NodeId node);

  // -- Properties --------------------------------------------------------
  // A property write, routed to the node it lands on (a scroll view's
  // container properties go to its content, an editor's text properties to
  // its text). Statuses as dg::set_prop() reports them.
  Status set_prop(NodeId node, std::uint16_t prop_id, const PropValue& value);
  // Typed form of set_prop() (drawgui/app/prop.h). The list form applies
  // every setting and answers the first refusal, or kOk.
  Status set(NodeId node, const prop::Setting& setting);
  Status set(NodeId node, std::initializer_list<prop::Setting> settings);
  // Binds `prop_id` to the active theme's `token_id`.
  Status bind_token(NodeId node, std::uint16_t prop_id, std::uint16_t token_id);
  Status set_text(NodeId node, std::uint16_t prop_id, std::string_view text);
  // An editor's committed text.
  [[nodiscard]] Expected<std::string, Status> text(NodeId node) const;
  // A checkbox's or radio button's checked mark, or a toggle button's
  // pressed state. A kButton becomes a toggle button once DG_PROP_CHECKED
  // is written on it (true or false): each click then flips it before its
  // ClickEvent is queued, like a checkbox.
  [[nodiscard]] Expected<bool, Status> checked(NodeId node) const;
  // DG_PROP_DISABLED: a disabled node and its subtree take no pointer or
  // key input (a press in progress produces no ClickEvent) and lose focus.
  Status set_disabled(NodeId node, bool disabled);
  Status set_tooltip(NodeId node, std::string text);
  // An opaque per-node value for the caller (an index, a pointer cast to an
  // integer): the runtime stores it and never reads or dereferences it.
  // 0 until set; gone with the node. Events do not carry it.
  Status set_user_data(NodeId node, std::uint64_t value);
  [[nodiscard]] Expected<std::uint64_t, Status> user_data(NodeId node) const;
  Status set_image(NodeId node, ImageId image);
  Status scope_action(NodeId node, std::uint16_t action_id);
  // Binds `chord` (e.g. "Ctrl+Shift+K", the input/shortcuts.toml syntax) to
  // `action_id` (>= kUserActionBase) while focus is inside `scope` (router
  // level 2); it reports ActionEvent{node = scope}. kInvalidArgument for a
  // chord that does not parse or an id below kUserActionBase.
  Status add_shortcut(NodeId scope, std::string_view chord, std::uint16_t action_id);
  [[nodiscard]] Expected<PixelPoint, Status> scroll_offset(NodeId node) const;

  // -- Focus -------------------------------------------------------------
  Status focus(NodeId node);
  void clear_focus();
  // The focused create() node: a component's composed part (a table's
  // frame, a tab) reports the component, matching FocusChangedEvent::node.
  // nullopt when nothing, or only popup-internal UI (a menu row), is focused.
  [[nodiscard]] std::optional<NodeId> focused() const;
  // The raw focused render node, composed parts and popup rows included
  // (an open menu's highlighted row) - for diagnostics and examples.
  [[nodiscard]] std::optional<NodeId> focused_node() const;

  // -- Components ----------------------------------------------------------
  // The typed ref of a component node: kInvalidNode when `node` is not a
  // live create() node of this window, kUnsupported when it is another kind.
  [[nodiscard]] Expected<ListRef, Status> list(NodeId node);
  [[nodiscard]] Expected<DropdownRef, Status> dropdown(NodeId node);
  [[nodiscard]] Expected<TabsRef, Status> tabs(NodeId node);
  [[nodiscard]] Expected<TreeRef, Status> tree(NodeId node);
  [[nodiscard]] Expected<TableRef, Status> table(NodeId node);
  [[nodiscard]] Expected<ProgressRef, Status> progress(NodeId node);
  [[nodiscard]] Expected<SliderRef, Status> slider(NodeId node);
  [[nodiscard]] Expected<RadioRef, Status> radio(NodeId node);
  [[nodiscard]] Expected<MenuBarRef, Status> menu_bar(NodeId node);

  // -- Popups ------------------------------------------------------------
  // Opens a menu below `anchor`'s bounds (or at `at`); returns its id, which
  // its MenuEvent repeats. A menu already open is dismissed first.
  // kInvalidArgument for no entries, more than limits::kMaxMenuItems, or
  // none that can be chosen. Allowed while a dialog is open: the menu stacks
  // above it and closing the menu leaves the dialog (and its focus trap).
  [[nodiscard]] Expected<std::int32_t, Status> open_menu(std::optional<NodeId> anchor,
                                                         PixelPoint at,
                                                         const std::vector<MenuEntry>& items);
  void close_menu();
  [[nodiscard]] bool has_dialog() const;
  // Modal: at most one per window (kUnsupported while one is open). Its
  // DialogResultEvent repeats the returned id.
  [[nodiscard]] Expected<std::int32_t, Status> open_dialog(DialogOptions options);
  void close_dialog();
  // Disables (or re-enables) one button of the open dialog `dialog_id`.
  // kUnknownId when that dialog is not open; kValueOutOfRange for a
  // missing button.
  Status set_dialog_button_disabled(std::int32_t dialog_id, int button, bool disabled);

  // -- Animation ---------------------------------------------------------
  // Interpolates `prop_id` of `node` from `from` (none = its current value)
  // to `to` over `duration_ms`, easing by `curve` (kCurve*), one step per
  // App::pump() - which wakes about every 16 ms while anything animates.
  // Float, length and colour properties interpolate; any other type jumps
  // to `to` at once and answers 0. AnimationFinishedEvent repeats the id.
  // A property the routing of set_prop() sends to a composed child (an
  // editor's text, a scroll view's content) is animated on the node itself.
  // kInvalidNode, kUnknownId, kTypeMismatch (`from`/`to` against the
  // property), kValueOutOfRange (negative duration, unknown curve), and
  // kUnsupported when `from` is none and the current value cannot be read
  // back (only background/border colour, opacity, left/top/width/height
  // can).
  [[nodiscard]] Expected<AnimationId, Status> animate(NodeId node, std::uint16_t prop_id,
                                                      std::optional<PropValue> from,
                                                      const PropValue& to, int duration_ms,
                                                      std::uint16_t curve = kCurveEaseInOut);
  // From now on every set_prop()/set() of `prop_id` on `node` interpolates
  // from the current value (or the in-flight one) to the new one over
  // `duration_ms`. A duration of 0 removes the transition. Statuses as
  // animate(); a property whose value cannot be read back jumps on its
  // first write.
  Status set_transition(NodeId node, std::uint16_t prop_id, int duration_ms,
                        std::uint16_t curve = kCurveEaseInOut);
  // Stops an animate() animation where it is (no AnimationFinishedEvent).
  // kUnknownId once it finished, was cancelled, or its node was removed.
  Status cancel_animation(AnimationId animation);
  // Whether an animation or transition of this window is running.
  [[nodiscard]] bool animating() const;

  // -- The OS window -----------------------------------------------------
  // Off by default: the title-bar close closes the window
  // (WindowClosedEvent). On: it queues CloseRequestedEvent instead and the
  // window stays open until close() or App::destroy_window().
  void set_close_confirmation(bool enabled);
  [[nodiscard]] bool close_confirmation() const;
  // Closes the OS window now and queues WindowClosedEvent; the Window stays
  // in App::windows() (closed()) until App::destroy_window(). No-op once
  // closed.
  void close();
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

  // -- Debugging -------------------------------------------------------
  // Every node in this window's render tree, composed children and popups
  // included - what a "the pool stays bounded" check counts.
  [[nodiscard]] int node_count() const;
  // The frame the last pump() painted, or null before the first one. Valid
  // until the next pump(); --dump-png modes encode it.
  [[nodiscard]] const RasterSurface* frame() const;

  // -- Escape hatches ----------------------------------------------------
  // The objects the runtime drives. The contract:
  //  * Reading them is always safe between pump() calls.
  //  * Writing them is allowed for nodes the runtime does not compose
  //    (nodes you create through layout() under a kBox, styles of create()
  //    nodes); call invalidate(node) afterwards so the next pump() relays
  //    out and repaints that subtree.
  //  * Never remove or re-parent a create() node or a composed child
  //    through them (use remove()/move()), and never keep a reference
  //    across App::destroy_window(): the runtime's records would dangle.
  //  * The runtime re-derives composed children and theme-bound properties
  //    on restyle/resize, overwriting direct writes to them.
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
