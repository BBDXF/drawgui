// See abi_impl.h's own header comment: everything here is hand-written,
// throws freely, and is called only from the generated trampolines in
// src/abi/drawgui_abi.generated.cpp.

#include "abi_impl.h"

#include <algorithm>
#include <deque>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "drawgui/base/expected.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/graphics/types.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/props/node_props.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/widget/interaction.h"
#include "drawgui/widget/widget_set.h"
#include "drawgui/window/window_manager.h"

#include "abi_types.h"

namespace dg::abi {
namespace {

// -- Global arenas ------------------------------------------------------
//
// dg_app_create() takes no app parameter, so the top-level arena has to
// live at file scope. Function-local statics rather than a plain global,
// which sidesteps static-initialization-order questions entirely - nothing
// else in this translation unit constructs before first use needs these.
std::vector<std::unique_ptr<AppImpl>>& all_apps() {
  static std::vector<std::unique_ptr<AppImpl>> apps;
  return apps;
}

std::vector<bool>& all_apps_alive() {
  static std::vector<bool> alive;
  return alive;
}

// Handle-wrapper storage - see abi_types.h's own header comment for the
// "why" in full. THE KEY PROPERTY THIS BUYS: a std::deque never invalidates
// a previously-returned element's address as more are appended (unlike
// std::vector, which may reallocate), so a pointer this file hands out as
// dg_app_t*/dg_window_t*/dg_node_t* stays valid for exactly as long as the
// process runs, independent of how many more handles are created later.
//
// GLOBAL, NOT per-AppImpl, and that is the fix for a real defect the first
// version of this file had: putting a window's/node's handle deque INSIDE
// AppImpl would free it the moment dg_app_destroy() resets that AppImpl,
// turning "the handle is now stale" into an actual dangling pointer the
// instant a host dereferenced it - a real use-after-free, not the ordinary
// DG_ERR_INVALID_HANDLE this design exists to guarantee instead. Kept here,
// a handle wrapper outlives its owning app's teardown exactly as it outlives
// dg_node_remove(): its own two integers stay readable forever; only
// resolve_app()'s liveness check (all_apps_alive()) stops answering yes.
//
// Measured, not assumed: `new`-ing each wrapper and never freeing it (this
// design's first draft) passed every functional check but was reported by
// LeakSanitizer as exactly the "leaked" allocations it looked like from the
// allocator's point of view - an UNREACHABLE block is what LSan defines a
// leak to be, and a bare `new` with no owner anywhere is unreachable the
// moment the pointer returned from it is only ever read via a C caller LSan
// cannot see into. A deque owned by a reachable static IS a root LSan's own
// scan already walks, so the identical bytes stop being a "leak" without the
// pointer itself moving, being freed, or losing the stability property this
// design depends on.
std::deque<dg_app_s>& all_app_handles() {
  static std::deque<dg_app_s> handles;
  return handles;
}

std::deque<dg_window_s>& all_window_handles() {
  static std::deque<dg_window_s> handles;
  return handles;
}

std::deque<dg_node_s>& all_node_handles() {
  static std::deque<dg_node_s> handles;
  return handles;
}

// Per-thread scratch storage, wrapped in accessors for the identical reason
// all_apps()/all_apps_alive() above are: a function-local static is not
// "globally accessible" the way a namespace-scope variable is
// (cppcoreguidelines-avoid-non-const-global-variables draws exactly that
// line), and design.md section 5.8's `dg_last_error(void)` signature - no
// app parameter - is what forces this to live somewhere other than an
// AppImpl member in the first place.
std::string& last_error_storage() {
  static thread_local std::string message;
  return message;
}

std::string& dump_buffer_storage() {
  static thread_local std::string buffer;
  return buffer;
}

// -- Handle resolution ----------------------------------------------------
//
// Every one of these returns nullptr for a handle naming nothing live,
// INCLUDING a null pointer itself - no caller below has to null-check a
// handle before resolving it, only after.

AppImpl* resolve_app(dg_app_t* handle) {
  if (handle == nullptr) {
    return nullptr;
  }
  std::vector<std::unique_ptr<AppImpl>>& apps = all_apps();
  std::vector<bool>& alive = all_apps_alive();
  if (handle->index >= apps.size() || !alive[handle->index]) {
    return nullptr;
  }
  return apps[handle->index].get();
}

WindowImpl* resolve_window(dg_window_t* handle) {
  if (handle == nullptr) {
    return nullptr;
  }
  AppImpl* impl = resolve_app(handle->app);
  if (impl == nullptr || handle->index >= impl->windows.size()) {
    return nullptr;
  }
  WindowImpl* window = impl->windows[handle->index].get();
  if (window == nullptr || window->closed) {
    return nullptr;
  }
  return window;
}

NodeSlot* resolve_node_slot(dg_node_t* handle) {
  if (handle == nullptr) {
    return nullptr;
  }
  AppImpl* impl = resolve_app(handle->app);
  if (impl == nullptr || handle->index >= impl->nodes.size()) {
    return nullptr;
  }
  NodeSlot& slot = impl->nodes[handle->index];
  if (slot.state == NodeSlot::State::kRemoved) {
    return nullptr;
  }
  return &slot;
}

WindowImpl* find_window_by_sdl_id(AppImpl& impl, dg::WindowId id) {
  for (const std::unique_ptr<WindowImpl>& window : impl.windows) {
    if (!window->closed && window->sdl_window == id) {
      return window.get();
    }
  }
  return nullptr;
}

dg_node_t* find_node_handle(AppImpl& impl, std::size_t window_index, dg::NodeId node_id) {
  for (NodeSlot& slot : impl.nodes) {
    if (slot.state == NodeSlot::State::kLive && slot.window_index == window_index &&
        slot.node_id == node_id) {
      return slot.self_handle;
    }
  }
  return nullptr;
}

// -- dg_value / dg::PropStatus conversions ---------------------------------

std::optional<dg::PropValue> to_prop_value(const dg_value& value) {
  switch (value.type) {
    case DG_VALUE_FLOAT:
      return dg::PropValue::number(value.number);
    case DG_VALUE_LENGTH:
      return dg::PropValue::length(value.number);
    case DG_VALUE_COLOR:
      return dg::PropValue::color(dg::Color::from_argb(value.bits));
    case DG_VALUE_ENUM:
      return dg::PropValue::option(value.bits);
    default:
      return std::nullopt;
  }
}

std::int32_t to_error_code(dg::PropStatus status) {
  switch (status) {
    case dg::PropStatus::kApplied:
      return DG_ERR_OK;
    case dg::PropStatus::kUnknownId:
      return DG_ERR_UNKNOWN_ID;
    case dg::PropStatus::kTypeMismatch:
      return DG_ERR_TYPE_MISMATCH;
    case dg::PropStatus::kValueOutOfRange:
      return DG_ERR_VALUE_OUT_OF_RANGE;
    case dg::PropStatus::kNotApplicable:
      return DG_ERR_NOT_APPLICABLE;
    case dg::PropStatus::kUnsupported:
      return DG_ERR_UNSUPPORTED;
  }
  return DG_ERR_INTERNAL;
}

// -- Node attachment --------------------------------------------------------
//
// The one place a PENDING node becomes LIVE - shared by dg_window_set_root()
// (parent_id == the tree's own structural root) and dg_node_insert_before()
// (parent_id == an already-live node). Defaults are chosen by type_id
// because dg_node_set_prop() only works on a LIVE node (see node_props.h's
// PropWrite/set_prop(): a write needs a real LayoutTree&/NodeId, and a
// pending slot has neither) - so a node needs a sane box the instant it is
// attached, not an invisible 0x0 one waiting for a property write that
// cannot happen yet. doc/abi.md section 5 records this scoping decision.
std::int32_t attach_pending_node(AppImpl& impl, std::size_t window_index, dg::NodeId parent_id,
                                 std::size_t child_index) {
  NodeSlot& slot = impl.nodes[child_index];
  if (slot.state != NodeSlot::State::kPending) {
    return DG_ERR_ALREADY_ATTACHED;
  }

  WindowImpl& window = *impl.windows[window_index];

  dg::BoxStyle box;
  dg::NodeStyle style;
  style.fill = dg::Color::rgba(0x2B, 0x2F, 0x3A);

  if (slot.type_id == DG_NODE_TYPE_BUTTON) {
    box.width = 200;
    box.height = 64;
  } else {
    box.width = 240;
    box.height = 120;
  }

  const dg::NodeId new_id = window.tree.add_child(parent_id, box, style);

  slot.state = NodeSlot::State::kLive;
  slot.window_index = window_index;
  slot.node_id = new_id;

  if (slot.type_id == DG_NODE_TYPE_BUTTON) {
    dg::Widget widget;
    widget.kind = dg::WidgetKind::kButton;
    widget.fill_normal = dg::Color::rgba(0x35, 0x6B, 0xC7);
    widget.fill_hover = dg::Color::rgba(0x4A, 0x86, 0xE3);
    widget.fill_pressed = dg::Color::rgba(0x25, 0x50, 0x9C);
    window.widgets.attach(new_id, widget);
    window.widgets.refresh(window.tree.render(), new_id, dg::PointerState{});
  }
  return DG_ERR_OK;
}

// -- The internal frame loop ------------------------------------------------
//
// Mirrors examples/11_form_controls/form_window.cpp's Runner::resize_if_needed()
// + Runner::draw() exactly, generalised from one window to any window this
// app owns - design.md section 5.16.1's "宿主只声明" applies to painting too:
// a C host never gets a canvas, it gets dg_poll_events()/dg_wait_events(),
// and drawgui paints and presents every frame internally.

bool ensure_surface_current(dg::WindowManager& wm, WindowImpl& window) {
  const dg::Expected<dg::PixelSize, dg::WindowError> size = wm.drawable_size(window.sdl_window);
  if (!size) {
    return false;
  }
  if (window.surface.has_value() && window.tree.viewport() == size.value()) {
    return true;
  }
  window.tree.resize(size.value());
  window.tree.layout();
  window.surface = dg::RasterSurface::create(size.value().width, size.value().height);
  return window.surface.has_value();
}

void repaint_and_present(dg::WindowManager& wm, WindowImpl& window) {
  if (!ensure_surface_current(wm, window)) {
    return;
  }
  // ensure_surface_current() returning true is what guarantees window.surface
  // is engaged, but that guarantee crosses a function boundary clang-tidy's
  // bugprone-unchecked-optional-access cannot see through - this repeats the
  // check locally, which is a real defensive redundancy as well as what
  // satisfies it: a future edit to ensure_surface_current() that stops
  // engaging the optional on some path fails here loudly instead of
  // dereferencing an empty optional.
  if (!window.surface.has_value()) {
    return;
  }
  window.tree.layout();
  if (window.tree.render().damage().is_empty()) {
    return;
  }
  window.tree.render().repaint(*window.surface);
  const dg::PixelView view = window.surface->peek_pixels();
  if (view.pixels == nullptr || !view.is_bgra8888) {
    return;
  }
  const dg::ImageView image{view.pixels, view.width, view.height, view.row_bytes,
                            dg::PixelFormat::kBgra8888};
  (void)wm.present(window.sdl_window, image, window.tree.render().painted().rects());
}

// Every transition dg::Interaction reports, refreshed the same way every
// prior C++ example's own dispatch()/react() pair already does (examples/
// 05_widgets/widget_scene.cpp) - asking dg::Interaction::state_of() for each
// touched widget rather than deriving an appearance from the event kind.
void refresh_touched(WindowImpl& window, const dg::InteractionChange& change) {
  const std::optional<dg::NodeId> touched[] = {change.left, change.entered, change.pressed,
                                               change.released, change.clicked};
  for (const std::optional<dg::NodeId>& id : touched) {
    if (id.has_value()) {
      window.widgets.refresh(window.tree.render(), *id, window.interaction.state_of(*id));
    }
  }
}

void process_pointer_event(AppImpl& impl, WindowImpl& window, const dg::PointerEvent& event) {
  const dg::PixelPoint at{event.x, event.y};
  dg::InteractionChange change;
  switch (event.action) {
    case dg::PointerAction::kMove:
      change =
          window.interaction.moved_over(window.widgets.widget_at(window.tree.render(), at));
      break;
    case dg::PointerAction::kDown:
      change =
          window.interaction.pressed_on(window.widgets.widget_at(window.tree.render(), at));
      break;
    case dg::PointerAction::kUp:
      change =
          window.interaction.released_on(window.widgets.widget_at(window.tree.render(), at));
      break;
    case dg::PointerAction::kLeave:
      change = window.interaction.left_window();
      break;
    case dg::PointerAction::kWheel:
      return;
  }
  refresh_touched(window, change);
  if (change.clicked.has_value()) {
    dg_event out{};
    out.size = sizeof(dg_event);
    out.kind = DG_EVENT_CLICK;
    out.window = window.self_handle;
    out.node = find_node_handle(impl, window.self_index, *change.clicked);
    out.x = event.x;
    out.y = event.y;
    impl.pending_events.push_back(out);
  }
}

void process_pump(AppImpl& impl, std::int32_t timeout_ms) {
  const dg::PumpResult pumped = impl.wm.pump(timeout_ms);

  // A node attached since the last pump (dg_window_set_root()/
  // dg_node_insert_before()) has all-zero bounds() until LayoutTree::
  // layout() has run at least once - LayoutTree::add_child()'s own header
  // comment: "reading absolute_bounds() before then answers with zero".
  // Every still-open window's layout must therefore be brought current
  // BEFORE this pump's own pointer events are hit-tested against it, not
  // only at the end where repaint_and_present() would otherwise be the
  // first thing to call layout() - a click delivered in the very same
  // pump() call that first attached its target's parent must not be
  // hit-tested against a stale, never-laid-out tree.
  for (const std::unique_ptr<WindowImpl>& window : impl.windows) {
    if (!window->closed) {
      ensure_surface_current(impl.wm, *window);
    }
  }

  for (const dg::WindowId closed_id : pumped.closed) {
    WindowImpl* window = find_window_by_sdl_id(impl, closed_id);
    if (window == nullptr) {
      continue;
    }
    window->closed = true;
    dg_event out{};
    out.size = sizeof(dg_event);
    out.kind = DG_EVENT_WINDOW_CLOSED;
    out.window = window->self_handle;
    impl.pending_events.push_back(out);
  }

  for (const dg::PointerEvent& event : pumped.pointer) {
    WindowImpl* window = find_window_by_sdl_id(impl, event.window);
    if (window != nullptr) {
      process_pointer_event(impl, *window, event);
    }
  }

  for (const std::unique_ptr<WindowImpl>& window : impl.windows) {
    if (!window->closed) {
      repaint_and_present(impl.wm, *window);
    }
  }
}

// -- dg_dump_layout_tree ------------------------------------------------

const char* layout_kind_name(dg::LayoutKind kind) {
  switch (kind) {
    case dg::LayoutKind::kLeaf:
      return "leaf";
    case dg::LayoutKind::kRow:
      return "row";
    case dg::LayoutKind::kColumn:
      return "column";
    case dg::LayoutKind::kWrapRow:
      return "wrap_row";
    case dg::LayoutKind::kWrapColumn:
      return "wrap_column";
    case dg::LayoutKind::kAbsolute:
      return "absolute";
  }
  return "unknown";
}

// design.md section 5.8's dump_layout_tree sketch asks for "类型/约束/尺寸/
// 偏移/parentData/边界标记" - type/constraints/size/offset/parentData/
// boundary flags. What is actually emitted, and what is not, honestly:
//
//   type       LayoutKind, via BoxStyle::kind.               EMITTED
//   size       the resolved border box, via LayoutTree::bounds().  EMITTED
//   offset     the box's own x/y is the offset from its parent's
//              content origin - the same rectangle carries both.  EMITTED
//   parentData grow/shrink/basis/left/top/right/bottom - the fields
//              box.h documents as parentData - when set to something
//              other than their default.                          EMITTED
//   constraints  NOT stored per-node anywhere LayoutTree exposes
//                publicly once layout() returns; only the resolved
//                size survives. Omitted rather than approximated.
//   boundary flags  "is this node a relayout boundary" is a fact
//                   layout_impl.h computes and discards per pass; no
//                   public accessor exists. Omitted for the same reason.
//
// Both omissions are named here rather than silently missing - see
// doc/abi.md section 7.
void dump_node(const dg::LayoutTree& tree, dg::NodeId id, std::string& out) {
  const dg::BoxStyle& box = tree.box(id);
  const dg::PixelRect bounds = tree.bounds(id);

  out += "{\"type\":\"";
  out += layout_kind_name(box.kind);
  out += "\",\"bounds\":{\"x\":";
  out += std::to_string(bounds.x);
  out += ",\"y\":";
  out += std::to_string(bounds.y);
  out += ",\"w\":";
  out += std::to_string(bounds.width);
  out += ",\"h\":";
  out += std::to_string(bounds.height);
  out += "},\"parent_data\":{";

  bool first = true;
  const auto emit = [&](const char* name, int field_value) {
    if (!first) {
      out += ",";
    }
    first = false;
    out += "\"";
    out += name;
    out += "\":";
    out += std::to_string(field_value);
  };
  if (box.grow != 0) {
    emit("grow", box.grow);
  }
  if (box.shrink != 0) {
    emit("shrink", box.shrink);
  }
  if (box.basis.has_value()) {
    emit("basis", *box.basis);
  }
  if (box.left.has_value()) {
    emit("left", *box.left);
  }
  if (box.top.has_value()) {
    emit("top", *box.top);
  }
  if (box.right.has_value()) {
    emit("right", *box.right);
  }
  if (box.bottom.has_value()) {
    emit("bottom", *box.bottom);
  }

  out += "},\"children\":[";
  const std::vector<dg::NodeId> kids = tree.render().children(id);
  for (std::size_t i = 0; i < kids.size(); ++i) {
    if (i != 0) {
      out += ",";
    }
    dump_node(tree, kids[i], out);
  }
  out += "]}";
}

}  // namespace

void set_last_error(std::string message) {
  last_error_storage() = std::move(message);
}

std::uint32_t abi_version() {
  return (static_cast<std::uint32_t>(DG_ABI_VERSION_MAJOR) << 16) |
         static_cast<std::uint32_t>(DG_ABI_VERSION_MINOR);
}

dg_app_t* app_create(const dg_app_opts* opts) {
  // dg_app_opts carries only `size` today (design.md section 5.8 decision 4:
  // a struct is future-proofed by its size field even before it has a second
  // one to append) - opts is accepted, matching the ABI signature, but there
  // is nothing in it yet for this function to read.
  (void)opts;
  dg::Expected<dg::WindowManager, dg::WindowError> created = dg::WindowManager::create();
  if (!created) {
    set_last_error(std::string("dg_app_create: ") + created.error().message);
    return nullptr;
  }

  std::vector<std::unique_ptr<AppImpl>>& apps = all_apps();
  std::vector<bool>& alive = all_apps_alive();
  apps.push_back(std::make_unique<AppImpl>(std::move(created).value()));
  alive.push_back(true);
  all_app_handles().emplace_back(dg_app_s{static_cast<std::uint32_t>(apps.size() - 1)});
  return &all_app_handles().back();
}

void app_destroy(dg_app_t* app) {
  AppImpl* impl = resolve_app(app);
  if (impl == nullptr) {
    set_last_error("dg_app_destroy: invalid handle");
    return;
  }
  all_apps()[app->index].reset();
  all_apps_alive()[app->index] = false;
}

dg_window_t* window_create(dg_app_t* app, const dg_window_opts* opts) {
  AppImpl* impl = resolve_app(app);
  if (impl == nullptr) {
    set_last_error("dg_window_create: invalid app handle");
    return nullptr;
  }
  if (opts == nullptr) {
    set_last_error("dg_window_create: opts must not be null");
    return nullptr;
  }
  if (opts->width <= 0 || opts->height <= 0) {
    set_last_error("dg_window_create: width and height must be positive");
    return nullptr;
  }

  dg::WindowSpec spec;
  spec.title = opts->title != nullptr ? std::string(opts->title) : std::string();
  spec.width = opts->width;
  spec.height = opts->height;
  spec.fill = dg::Color::from_argb(opts->background_argb);

  dg::Expected<dg::WindowId, dg::WindowError> opened = impl->wm.open(spec);
  if (!opened) {
    set_last_error(std::string("dg_window_create: ") + opened.error().message);
    return nullptr;
  }

  dg::TreeSpec tree_spec;
  tree_spec.viewport = dg::PixelSize{opts->width, opts->height};
  tree_spec.background.fill = dg::Color::from_argb(opts->background_argb);

  auto window = std::make_unique<WindowImpl>(tree_spec);
  window->sdl_window = opened.value();
  window->self_index = impl->windows.size();
  impl->windows.push_back(std::move(window));

  all_window_handles().emplace_back(
      dg_window_s{app, static_cast<std::uint32_t>(impl->windows.size() - 1)});
  dg_window_t* handle = &all_window_handles().back();
  impl->windows.back()->self_handle = handle;
  return handle;
}

std::int32_t window_set_root(dg_window_t* window_h, dg_node_t* node_h) {
  if (window_h == nullptr || node_h == nullptr) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  if (window_h->app != node_h->app) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  WindowImpl* window = resolve_window(window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  AppImpl* impl = resolve_app(window_h->app);
  if (impl == nullptr || node_h->index >= impl->nodes.size()) {
    return DG_ERR_INVALID_HANDLE;
  }
  NodeSlot& slot = impl->nodes[node_h->index];
  if (slot.state == NodeSlot::State::kRemoved) {
    return DG_ERR_INVALID_HANDLE;
  }
  if (slot.state != NodeSlot::State::kPending) {
    return DG_ERR_ALREADY_ATTACHED;
  }
  if (window->root.has_value()) {
    return DG_ERR_ALREADY_ATTACHED;
  }

  const std::int32_t status =
      attach_pending_node(*impl, window_h->index, dg::LayoutTree::root(), node_h->index);
  if (status == DG_ERR_OK) {
    window->root = slot.node_id;
  }
  return status;
}

dg_node_t* node_create(dg_app_t* app, std::uint16_t type_id) {
  AppImpl* impl = resolve_app(app);
  if (impl == nullptr) {
    set_last_error("dg_node_create: invalid app handle");
    return nullptr;
  }
  if (type_id != DG_NODE_TYPE_BOX && type_id != DG_NODE_TYPE_BUTTON) {
    set_last_error("dg_node_create: unknown type_id");
    return nullptr;
  }

  NodeSlot slot;
  slot.type_id = type_id;
  impl->nodes.push_back(slot);
  all_node_handles().emplace_back(
      dg_node_s{app, static_cast<std::uint32_t>(impl->nodes.size() - 1)});
  dg_node_t* handle = &all_node_handles().back();
  impl->nodes.back().self_handle = handle;
  return handle;
}

std::int32_t node_set_prop(dg_node_t* node_h, std::uint16_t prop_id, const dg_value* value) {
  if (node_h == nullptr || value == nullptr) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  if (value->size < sizeof(dg_value)) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  NodeSlot* slot = resolve_node_slot(node_h);
  if (slot == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  if (slot->state != NodeSlot::State::kLive) {
    return DG_ERR_NO_WINDOW;
  }
  const std::optional<dg::PropValue> prop_value = to_prop_value(*value);
  if (!prop_value.has_value()) {
    return DG_ERR_INVALID_ARGUMENT;
  }

  AppImpl* impl = resolve_app(node_h->app);
  WindowImpl& window = *impl->windows[slot->window_index];
  const dg::PropWrite result = dg::set_prop(window.tree, slot->node_id, prop_id, *prop_value);
  return to_error_code(result.status);
}

std::int32_t node_insert_before(dg_node_t* parent_h, dg_node_t* child_h, dg_node_t* ref_h) {
  if (ref_h != nullptr) {
    // Append-only this slice (doc/abi.md section 5): neither RenderTree::
    // add_child() nor LayoutTree::add_child() can insert anywhere but the
    // end of a parent's child list, so a real `ref` cannot be honoured.
    return DG_ERR_UNSUPPORTED;
  }
  if (parent_h == nullptr || child_h == nullptr) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  if (parent_h->app != child_h->app) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  NodeSlot* parent_slot = resolve_node_slot(parent_h);
  if (parent_slot == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  if (parent_slot->state != NodeSlot::State::kLive) {
    return DG_ERR_NO_WINDOW;
  }
  NodeSlot* child_slot = resolve_node_slot(child_h);
  if (child_slot == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  if (child_slot->state != NodeSlot::State::kPending) {
    return DG_ERR_ALREADY_ATTACHED;
  }

  AppImpl* impl = resolve_app(parent_h->app);
  if (impl == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  return attach_pending_node(*impl, parent_slot->window_index, parent_slot->node_id,
                             child_h->index);
}

std::int32_t node_remove(dg_node_t* node_h) {
  NodeSlot* slot = resolve_node_slot(node_h);
  if (slot == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  // Invalidates the HANDLE (every later call on it is DG_ERR_INVALID_HANDLE).
  // Does NOT detach the node from its window's tree: RenderTree/LayoutTree
  // have no removal primitive at all (doc/list.md section 1 evaluated this
  // exact question for list virtualization and found no caller that needed
  // one), and inventing one here, ungrounded in a working caller, is the
  // "twelve deleted platform headers" mistake design.md itself already
  // names. doc/abi.md section 5 records this as a real, load-bearing scope
  // boundary rather than a silent gap.
  slot->state = NodeSlot::State::kRemoved;
  return DG_ERR_OK;
}

std::int32_t wait_events(dg_app_t* app, std::int32_t timeout_ms) {
  AppImpl* impl = resolve_app(app);
  if (impl == nullptr) {
    set_last_error("dg_wait_events: invalid app handle");
    return -1;
  }
  process_pump(*impl, timeout_ms);
  return static_cast<std::int32_t>(impl->pending_events.size());
}

std::int32_t poll_events(dg_app_t* app, dg_event* out, std::int32_t max) {
  AppImpl* impl = resolve_app(app);
  if (impl == nullptr) {
    set_last_error("dg_poll_events: invalid app handle");
    return -1;
  }
  if (out == nullptr || max < 0) {
    set_last_error("dg_poll_events: invalid argument");
    return -1;
  }
  const auto count =
      std::min<std::size_t>(static_cast<std::size_t>(max), impl->pending_events.size());
  for (std::size_t i = 0; i < count; ++i) {
    out[i] = impl->pending_events[i];
  }
  impl->pending_events.erase(impl->pending_events.begin(),
                             impl->pending_events.begin() + static_cast<std::ptrdiff_t>(count));
  return static_cast<std::int32_t>(count);
}

const char* last_error() {
  return last_error_storage().c_str();
}

const char* dump_layout_tree(dg_node_t* node_h) {
  NodeSlot* slot = resolve_node_slot(node_h);
  if (slot == nullptr) {
    set_last_error("dg_dump_layout_tree: invalid handle");
    return nullptr;
  }
  if (slot->state != NodeSlot::State::kLive) {
    set_last_error("dg_dump_layout_tree: node is not attached to any window");
    return nullptr;
  }
  AppImpl* impl = resolve_app(node_h->app);
  WindowImpl& window = *impl->windows[slot->window_index];

  std::string& buffer = dump_buffer_storage();
  buffer.clear();
  dump_node(window.tree, slot->node_id, buffer);
  return buffer.c_str();
}

std::int32_t debug_warp_pointer(dg_window_t* window_h, std::int32_t x, std::int32_t y) {
  WindowImpl* window = resolve_window(window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  AppImpl* impl = resolve_app(window_h->app);
  impl->wm.warp_pointer(window->sdl_window, x, y);
  return DG_ERR_OK;
}

std::int32_t debug_post_pointer_button(dg_window_t* window_h, std::int32_t down, std::int32_t x,
                                       std::int32_t y) {
  WindowImpl* window = resolve_window(window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  AppImpl* impl = resolve_app(window_h->app);
  impl->wm.post_pointer_button(window->sdl_window, down != 0, x, y);
  return DG_ERR_OK;
}

std::int32_t debug_trigger_exception(std::int32_t kind) {
  // The exception-boundary test's own fault injector (design.md section
  // 5.17.1's risk-register mitigation). Three distinct throw shapes so the
  // test can verify all three catch clauses the generator emits: bad_alloc
  // -> DG_ERR_OOM, any other std::exception -> DG_ERR_INTERNAL with a real
  // message, and a value nothing but catch(...) can see.
  if (kind == 1) {
    throw std::bad_alloc();
  }
  if (kind == 2) {
    throw std::runtime_error(
        "dg_debug_trigger_exception: deliberate fault for the exception-boundary test");
  }
  throw 42;  // Not a std::exception at all - only catch(...) can see this one.
}

}  // namespace dg::abi
