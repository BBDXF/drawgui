// dg::Window: node records, creation (the one place a node's default box and
// composed children are chosen), removal, property routing, and the thin
// forwards to the OS window.

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "app/components/components.h"
#include "app/internal.h"
#include "drawgui/render/node_lifecycle.h"
#include "drawgui/theme/theme_bindings.h"

namespace dg::app_detail {

NodeRecord* record_of(WinState& window, dg::NodeId id) {
  const auto found = window.records.find(id);
  return found == window.records.end() ? nullptr : &found->second;
}

const NodeRecord* record_of(const WinState& window, dg::NodeId id) {
  const auto found = window.records.find(id);
  return found == window.records.end() ? nullptr : &found->second;
}

std::vector<NodeRecord*> records_in_order(WinState& window) {
  std::vector<NodeRecord*> out;
  out.reserve(window.order.size());
  for (const dg::NodeId id : window.order) {
    out.push_back(&window.records.at(id));
  }
  return out;
}

std::optional<dg::NodeId> exact_node(const WinState& window, dg::NodeId id) {
  return window.records.contains(id) ? std::optional<dg::NodeId>(id) : std::nullopt;
}

std::optional<dg::NodeId> nearest_node(const WinState& window, dg::NodeId id) {
  const dg::RenderTree& tree = window.tree.render();
  dg::NodeId current = id;
  while (true) {
    if (window.records.contains(current)) {
      return current;
    }
    const dg::NodeId parent = tree.parent(current);
    if (parent == current) {
      return std::nullopt;
    }
    current = parent;
  }
}

void emit(AppState& app, const dg::Event& event) {
  app.pending_events.push_back(event);
}

dg::Status to_status(dg::PropStatus status) {
  switch (status) {
    case dg::PropStatus::kApplied:
      return dg::Status::kOk;
    case dg::PropStatus::kUnknownId:
      return dg::Status::kUnknownId;
    case dg::PropStatus::kTypeMismatch:
      return dg::Status::kTypeMismatch;
    case dg::PropStatus::kValueOutOfRange:
      return dg::Status::kValueOutOfRange;
    case dg::PropStatus::kNotApplicable:
      return dg::Status::kNotApplicable;
    case dg::PropStatus::kUnsupported:
      return dg::Status::kUnsupported;
  }
  return dg::Status::kUnsupported;
}

bool is_text_editor(const WinState& window, dg::NodeId id) {
  return window.widgets.has(id) && dg::is_editable_text(window.widgets.at(id).kind);
}

bool is_text_area(const WinState& window, dg::NodeId id) {
  return window.widgets.has(id) && window.widgets.at(id).kind == dg::WidgetKind::kTextArea;
}

// The one place a node comes into existence - shared by the parentless
// create() (parent_id == the tree's own structural root), a create() under
// a live node, and tabs_add()'s panels. Defaults are chosen by kind because
// a property write needs a real LayoutTree&/NodeId - so a node needs a sane
// box the instant it is added, not an invisible 0x0 one waiting for a
// property write.
dg::NodeId attach_new(WinState& window, NodeKind kind, dg::NodeId parent_id) {
  dg::BoxStyle box;
  dg::NodeStyle style;
  components::defaults(window, kind, box, style);
  const dg::NodeId new_id = window.tree.add_child(parent_id, box, style);
  NodeRecord& record = window.records[new_id];
  record.kind = kind;
  record.id = new_id;
  window.order.push_back(new_id);
  components::compose(window, record);
  return new_id;
}

namespace {

// NodeKind::kScrollView forwards these to its content node.
bool is_container_prop(std::uint16_t prop_id) {
  return prop_id == DG_PROP_DIRECTION || prop_id == DG_PROP_JUSTIFY ||
         prop_id == DG_PROP_ALIGN || prop_id == DG_PROP_GAP || prop_id == DG_PROP_MAIN_SIZE ||
         prop_id == DG_PROP_RUN_GAP || prop_id == DG_PROP_ALIGN_CONTENT ||
         prop_id == DG_PROP_WRAP;
}

// The text editors forward these to their composed content node.
bool is_text_prop(std::uint16_t prop_id) {
  return prop_id == DG_PROP_FONT_SIZE || prop_id == DG_PROP_TEXT_ALIGN ||
         prop_id == DG_PROP_TEXT_COLOR;
}

// Which node a property write on `record` lands on.
dg::NodeId prop_target(const NodeRecord& record, const WinState& window,
                       std::uint16_t prop_id) {
  if (record.kind == NodeKind::kScrollView && is_container_prop(prop_id)) {
    return record.content;
  }
  if (is_editor_kind(record.kind) && is_text_prop(prop_id) && window.widgets.has(record.id)) {
    return window.widgets.at(record.id).content;
  }
  return record.id;
}

// A kFloat value carrying a whole number within [lo, hi], or nothing.
std::optional<int> whole_number(const dg::PropValue& value, int lo, int hi) {
  const float number = value.scalar();
  if (value.type() != dg::PropType::k_float || !std::isfinite(number) ||
      number != std::trunc(number) || number < static_cast<float>(lo) ||
      number > static_cast<float>(hi)) {
    return std::nullopt;
  }
  return static_cast<int>(number);
}

// DG_PROP_TAB_INDEX / DG_PROP_MAX_LENGTH: widget state, not node style.
dg::Status set_widget_prop(WinState& window, const NodeRecord& record, std::uint16_t prop_id,
                           const dg::PropValue& value) {
  if (value.type() != dg::PropType::k_float) {
    return dg::Status::kTypeMismatch;
  }
  constexpr int kLimit = 1 << 24;
  const std::optional<int> number =
      whole_number(value, prop_id == DG_PROP_MAX_LENGTH ? 0 : -kLimit, kLimit);
  if (!number.has_value()) {
    return dg::Status::kValueOutOfRange;
  }
  if (prop_id == DG_PROP_MAX_LENGTH) {
    if (!is_editor_kind(record.kind)) {
      return dg::Status::kNotApplicable;
    }
    window.widgets.editor_set_max_length(record.id, *number);
    return dg::Status::kOk;
  }
  if (!window.widgets.has(record.id) || !dg::is_focusable(window.widgets.at(record.id).kind)) {
    return dg::Status::kNotApplicable;
  }
  // attach() replaces the stored Widget by value; a copy with only the tab
  // index changed keeps the rest of its state.
  dg::Widget updated = window.widgets.at(record.id);
  updated.tab_index = number;
  window.widgets.attach(record.id, updated);
  return dg::Status::kOk;
}

// dg::set_prop() on `target`, except that DG_PROP_DIRECTION on a plain BOX
// (a leaf until now) makes it a row/column container first - the C++ layer
// only switches between container kinds, and a C caller has no other way
// to build one. A refused write leaves the leaf as it was.
dg::PropWrite write_prop(WinState& window, const NodeRecord& record, dg::NodeId target,
                         std::uint16_t prop_id, const dg::PropValue& value) {
  const dg::BoxStyle before = window.tree.box(target);
  const bool make_container = prop_id == DG_PROP_DIRECTION && record.kind == NodeKind::kBox &&
                              before.kind == dg::LayoutKind::kLeaf &&
                              before.scroll_axis == dg::ScrollAxis::kNone;
  if (make_container) {
    dg::BoxStyle container = before;
    container.kind = dg::LayoutKind::kColumn;
    window.tree.set_box(target, container);
  }
  dg::PropWrite result = dg::set_prop(window.tree, target, prop_id, value);
  if (!result.ok() && make_container) {
    window.tree.set_box(target, before);
  }
  return result;
}

// The widget-layer follow-ups of a successful write.
void after_prop_write(WinState& window, const NodeRecord& record, dg::NodeId target,
                      std::uint16_t prop_id) {
  if (prop_id == DG_PROP_DISABLED) {
    sync_disabled(window, record.id);
  } else if (prop_id == DG_PROP_SCROLL_AXIS && record.kind == NodeKind::kScrollView) {
    follow_scroll_axis(window, record);
  } else if (prop_id == DG_PROP_SCROLLBAR) {
    window.relaid = true;  // re-derive the bar's visibility at the next pump
  }
  if (target != record.id && is_editor_kind(record.kind)) {
    refresh_editor_text(window, record);
  }
}

// Where an insertion under `parent` goes, or the Status that refuses it.
dg::Status insertion_point(const WinState& window, dg::NodeId parent,
                           std::optional<dg::NodeId> before, dg::NodeId& container) {
  const NodeRecord* record = record_of(window, parent);
  if (record == nullptr) {
    return dg::Status::kInvalidNode;
  }
  if (!accepts_children(record->kind)) {
    return dg::Status::kUnsupported;
  }
  container = record->kind == NodeKind::kScrollView ? record->content : record->id;
  // `before` must be a created DIRECT child of the container - anything
  // else names no position.
  if (before.has_value() && (!window.records.contains(*before) ||
                             window.tree.render().parent(*before) != container)) {
    return dg::Status::kInvalidArgument;
  }
  return dg::Status::kOk;
}

}  // namespace
}  // namespace dg::app_detail

namespace dg {

using app_detail::NodeRecord;
using app_detail::WinState;

Window::Window(std::unique_ptr<WinState> state) : state_(std::move(state)) {
  state_->self = this;
}

Window::~Window() = default;

WindowId Window::id() const {
  return state_->sdl_window;
}

bool Window::closed() const {
  return state_->closed;
}

std::optional<NodeId> Window::root() const {
  return state_->root;
}

Expected<NodeId, Status> Window::create(NodeKind kind, std::optional<NodeId> parent,
                                        std::optional<NodeId> before) {
  WinState& window = *state_;
  if (!parent.has_value()) {
    if (window.root.has_value()) {
      return Unexpected(Status::kAlreadyAttached);
    }
    const NodeId id = app_detail::attach_new(window, kind, LayoutTree::root());
    window.root = id;
    return id;
  }
  NodeId container;
  if (const Status status = app_detail::insertion_point(window, *parent, before, container);
      status != Status::kOk) {
    return Unexpected(status);
  }
  const NodeId id = app_detail::attach_new(window, kind, container);
  if (before.has_value()) {
    // attach_new() always appends; a fresh last child moved before a
    // sibling of the same parent cannot fail.
    window.tree.move_child(id, container, before);
  }
  return id;
}

Status Window::move(NodeId child, NodeId parent, std::optional<NodeId> before) {
  WinState& window = *state_;
  if (!window.records.contains(child)) {
    return Status::kInvalidNode;
  }
  NodeId container;
  if (const Status status = app_detail::insertion_point(window, parent, before, container);
      status != Status::kOk) {
    return status;
  }
  if (before == child) {
    return Status::kInvalidArgument;
  }
  // A move keeps the node's NodeId, widget state and record, so no side
  // table is touched. Refused by LayoutTree::move_child() when `parent`
  // lies inside `child`'s own subtree (including parent == child) - a cycle.
  if (!window.tree.move_child(child, container, before)) {
    return Status::kInvalidArgument;
  }
  if (window.root == child) {
    // Unreachable in practice - a window root moved under any live node is
    // always a cycle - but kept honest rather than leaving a stale root.
    window.root.reset();
  }
  return Status::kOk;
}

Status Window::remove(NodeId node) {
  WinState& window = *state_;
  if (!window.records.contains(node)) {
    return Status::kInvalidNode;
  }
  // Collected BEFORE removal: afterwards the subtree's NodeIds are
  // tombstoned and may be reused by the next add_child().
  const std::vector<NodeId> subtree = subtree_of(window.tree.render(), node);
  const std::optional<NodeId> focused_before = window.focus.current();
  app_detail::components::before_remove(window, subtree);
  if (!remove_subtree(window.tree, node, app_detail::components::tables_of(window))) {
    // Unreachable: a record never names the structural root
    // LayoutTree::root() (the window's root is always a CHILD of it).
    // Reported rather than asserted.
    return Status::kUnsupported;
  }
  if (focused_before.has_value() && window.focus.current() != focused_before) {
    // The focused node went with the subtree: report the blur.
    app_detail::emit(window.app, events::FocusChangedEvent{window.sdl_window, std::nullopt});
  }
  const auto inside = [&subtree](NodeId id) {
    return std::find(subtree.begin(), subtree.end(), id) != subtree.end();
  };
  // Removing the window's root node clears the root: the window shows its
  // bare background and a parentless create() may attach a new one.
  if (window.root.has_value() && inside(*window.root)) {
    window.root.reset();
  }
  // Every created DESCENDANT is gone too - its record must not keep
  // resolving a NodeId a later add_child() may reuse.
  std::vector<NodeId> removed;
  for (const NodeId id : window.order) {
    if (inside(id)) {
      removed.push_back(id);
    }
  }
  std::erase_if(window.order, inside);
  for (const NodeId id : removed) {
    window.records.erase(id);
  }
  for (NodeRecord* record : app_detail::records_in_order(window)) {
    if (auto* tabs = std::get_if<app_detail::TabsState>(&record->state)) {
      for (NodeId& panel : tabs->panels) {
        if (inside(panel)) {
          panel = NodeId{};
        }
      }
    }
  }
  if (window.on_removed) {
    for (const NodeId id : removed) {
      window.on_removed(id);
    }
  }
  window.reported_focus = window.focus.current();
  return Status::kOk;
}

void Window::set_on_removed(std::function<void(NodeId)> hook) {
  state_->on_removed = std::move(hook);
}

NodeId Window::child_container(NodeId node) const {
  const NodeRecord* record = app_detail::record_of(*state_, node);
  if (record != nullptr && record->kind == NodeKind::kScrollView) {
    return record->content;
  }
  return node;
}

std::optional<NodeKind> Window::kind_of(NodeId node) const {
  const NodeRecord* record = app_detail::record_of(*state_, node);
  return record != nullptr ? std::optional<NodeKind>(record->kind) : std::nullopt;
}

void Window::invalidate(NodeId node) {
  RenderTree& tree = state_->tree.render();
  if (tree.is_valid(node)) {
    tree.damage_rect(tree.absolute_bounds(node));
    state_->relaid = true;
  }
}

namespace {

template <class S>
Status kind_status(app_detail::WinState& window, NodeId node) {
  return app_detail::components::component_of<S>(window, node).status;
}

}  // namespace

Expected<ListRef, Status> Window::list(NodeId node) {
  if (const Status status = kind_status<app_detail::ListState>(*state_, node);
      status != Status::kOk) {
    return Unexpected(status);
  }
  return ListRef(state_.get(), node);
}

Expected<DropdownRef, Status> Window::dropdown(NodeId node) {
  if (const Status status = kind_status<app_detail::DropdownState>(*state_, node);
      status != Status::kOk) {
    return Unexpected(status);
  }
  return DropdownRef(state_.get(), node);
}

Expected<TabsRef, Status> Window::tabs(NodeId node) {
  if (const Status status = kind_status<app_detail::TabsState>(*state_, node);
      status != Status::kOk) {
    return Unexpected(status);
  }
  return TabsRef(state_.get(), node);
}

Expected<TreeRef, Status> Window::tree(NodeId node) {
  if (const Status status = kind_status<app_detail::TreeState>(*state_, node);
      status != Status::kOk) {
    return Unexpected(status);
  }
  return TreeRef(state_.get(), node);
}

Expected<TableRef, Status> Window::table(NodeId node) {
  if (const Status status = kind_status<app_detail::TableState>(*state_, node);
      status != Status::kOk) {
    return Unexpected(status);
  }
  return TableRef(state_.get(), node);
}

Expected<ProgressRef, Status> Window::progress(NodeId node) {
  if (const Status status = kind_status<app_detail::ProgressState>(*state_, node);
      status != Status::kOk) {
    return Unexpected(status);
  }
  return ProgressRef(state_.get(), node);
}

Status Window::set_prop(NodeId node, std::uint16_t prop_id, const PropValue& value) {
  WinState& window = *state_;
  NodeRecord* record = app_detail::record_of(window, node);
  if (record == nullptr) {
    return Status::kInvalidNode;
  }
  const NodeId target = app_detail::prop_target(*record, window, prop_id);
  if (const std::optional<Status> handled =
          app_detail::components::set_prop(window, *record, prop_id, value)) {
    return *handled;
  }
  if (prop_id == DG_PROP_TAB_INDEX || prop_id == DG_PROP_MAX_LENGTH) {
    return app_detail::set_widget_prop(window, *record, prop_id, value);
  }
  if (record->kind == NodeKind::kList &&
      (prop_id == DG_PROP_SCROLL_AXIS || prop_id == DG_PROP_FONT_SIZE)) {
    return app_detail::set_list_prop(window, *record, prop_id, value);
  }
  const PropWrite result = app_detail::write_prop(window, *record, target, prop_id, value);
  if (result.ok()) {
    app_detail::after_prop_write(window, *record, target, prop_id);
    app_detail::components::after_prop(window, *record, prop_id);
  }
  return app_detail::to_status(result.status);
}

Status Window::bind_token(NodeId node, std::uint16_t prop_id, std::uint16_t token_id) {
  WinState& window = *state_;
  const NodeRecord* record = app_detail::record_of(window, node);
  if (record == nullptr) {
    return Status::kInvalidNode;
  }
  const NodeId target = app_detail::prop_target(*record, window, prop_id);
  // 7-6: a $token live reference (design.md section 5.7.2) by token_id,
  // resolved through dg::set_prop() unchanged.
  if (window.app.theme == nullptr) {
    return Status::kNoActiveTheme;
  }
  const PropWrite result =
      dg::bind_token(window.tree, window.theme_bindings, target, prop_id, *window.app.theme,
                     window.app.variant, static_cast<dg_token_id>(token_id));
  return app_detail::to_status(result.status);
}

Status Window::set_text(NodeId node, std::uint16_t prop_id, std::string_view text) {
  WinState& window = *state_;
  NodeRecord* record = app_detail::record_of(window, node);
  if (record == nullptr) {
    return Status::kInvalidNode;
  }
  if (prop_id == DG_PROP_TEXT) {
    if (const std::optional<Status> handled =
            app_detail::components::set_text(window, *record, text)) {
      return *handled;
    }
  }
  if (prop_id == DG_PROP_TEXT && app_detail::is_text_editor(window, record->id)) {
    // An editor's text is its MODEL, not its own node's TextStyle: replace
    // it (caret to the end). Not reported as a TextChangedEvent - that
    // event is for user edits, and the caller already knows what it set.
    if (const FontCatalog* fonts = app_detail::components::fonts_of(window.app)) {
      window.widgets.editor_set_text(window.tree.render(), *fonts, record->id, text,
                                     window.focus.current() == record->id);
    }
    return Status::kOk;
  }
  const PropWrite result = dg::set_text(window.tree, record->id, prop_id, text);
  return app_detail::to_status(result.status);
}

Expected<std::string, Status> Window::text(NodeId node) const {
  const NodeRecord* record = app_detail::record_of(*state_, node);
  if (record == nullptr) {
    return Unexpected(Status::kInvalidNode);
  }
  if (!is_editor_kind(record->kind)) {
    return Unexpected(Status::kUnsupported);
  }
  const TextEditState* state = state_->widgets.editor_state(record->id);
  return state != nullptr ? state->text : std::string();
}

Expected<bool, Status> Window::checked(NodeId node) const {
  const NodeRecord* record = app_detail::record_of(*state_, node);
  if (record == nullptr) {
    return Unexpected(Status::kInvalidNode);
  }
  if (record->kind != NodeKind::kCheckbox) {
    return Unexpected(Status::kUnsupported);
  }
  return state_->widgets.is_checked(record->id);
}

Status Window::set_disabled(NodeId node, bool disabled) {
  return set_prop(node, DG_PROP_DISABLED, PropValue::boolean(disabled));
}

Status Window::set_image(NodeId node, ImageId image) {
  const NodeRecord* record = app_detail::record_of(*state_, node);
  if (record == nullptr) {
    return Status::kInvalidNode;
  }
  if (record->kind != NodeKind::kImage && record->kind != NodeKind::kBox) {
    return Status::kUnsupported;
  }
  RenderTree& tree = state_->tree.render();
  ImageStyle style = tree.style(record->id).image;
  style.source = image;
  tree.set_image(record->id, style);
  return Status::kOk;
}

Status Window::scope_action(NodeId node, std::uint16_t action_id) {
  const NodeRecord* record = app_detail::record_of(*state_, node);
  if (record == nullptr) {
    return Status::kInvalidNode;
  }
  state_->action_scopes.scope(record->id, static_cast<dg_action_id>(action_id));
  return Status::kOk;
}

Expected<PixelPoint, Status> Window::scroll_offset(NodeId node) const {
  const NodeRecord* record = app_detail::record_of(*state_, node);
  if (record == nullptr) {
    return Unexpected(Status::kInvalidNode);
  }
  if (record->kind != NodeKind::kScrollView && record->kind != NodeKind::kList &&
      record->kind != NodeKind::kTextArea) {
    return Unexpected(Status::kUnsupported);
  }
  return state_->tree.render().scroll_offset(record->id);
}

// -- The OS window ------------------------------------------------------------

Expected<void, WindowError> Window::set_title(const std::string& title) {
  return state_->app.wm.set_title(state_->sdl_window, title);
}

Expected<std::string, WindowError> Window::title() const {
  return state_->app.wm.title(state_->sdl_window);
}

Expected<void, WindowError> Window::set_size(int width, int height) {
  return state_->app.wm.set_size(state_->sdl_window, width, height);
}

Expected<PixelSize, WindowError> Window::size() const {
  return state_->app.wm.size(state_->sdl_window);
}

Expected<void, WindowError> Window::set_position(int x, int y) {
  return state_->app.wm.set_position(state_->sdl_window, x, y);
}

Expected<PixelPoint, WindowError> Window::position() const {
  return state_->app.wm.position(state_->sdl_window);
}

Expected<void, WindowError> Window::minimize() {
  return state_->app.wm.minimize(state_->sdl_window);
}

Expected<void, WindowError> Window::maximize() {
  return state_->app.wm.maximize(state_->sdl_window);
}

Expected<void, WindowError> Window::restore() {
  return state_->app.wm.restore(state_->sdl_window);
}

Expected<void, WindowError> Window::set_fullscreen(bool fullscreen) {
  return state_->app.wm.set_fullscreen(state_->sdl_window, fullscreen);
}

Expected<void, WindowError> Window::set_resizable(bool resizable) {
  return state_->app.wm.set_resizable(state_->sdl_window, resizable);
}

Expected<void, WindowError> Window::set_min_size(int width, int height) {
  return state_->app.wm.set_min_size(state_->sdl_window, width, height);
}

Expected<void, WindowError> Window::set_max_size(int width, int height) {
  return state_->app.wm.set_max_size(state_->sdl_window, width, height);
}

Expected<void, WindowError> Window::set_icon(const ImageView& icon) {
  return state_->app.wm.set_icon(state_->sdl_window, icon);
}

Expected<WindowState, WindowError> Window::state() const {
  return state_->app.wm.state(state_->sdl_window);
}

// -- Escape hatches -----------------------------------------------------------

LayoutTree& Window::layout() {
  return state_->tree;
}

const LayoutTree& Window::layout() const {
  return state_->tree;
}

WidgetSet& Window::widgets() {
  return state_->widgets;
}

Focus& Window::focus_state() {
  return state_->focus;
}

Interaction& Window::interaction() {
  return state_->interaction;
}

ThemeBindings& Window::theme_bindings() {
  return state_->theme_bindings;
}

AnimationEngine& Window::animation() {
  return state_->animation;
}

ActionScopes& Window::action_scopes() {
  return state_->action_scopes;
}

PopupHost& Window::popups() {
  if (!state_->popups.has_value()) {
    state_->popups.emplace(state_->app.wm);
  }
  return *state_->popups;
}

}  // namespace dg
