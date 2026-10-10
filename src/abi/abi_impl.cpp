// See abi_impl.h's own header comment: everything here is hand-written,
// throws freely, and is called only from the generated trampolines in
// src/abi/drawgui_abi.generated.cpp.
//
// A thin shell over the C++ runtime (dg::App / dg::Window, src/app): this
// file owns the handle arenas and their validation, converts dg_value and
// dg::Event to and from their C shapes, and maps dg::Status to DG_ERR_*.
// Everything a window does - layout, painting, focus, editing, routing - is
// the runtime's.

#include "abi_impl.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "app/runtime_debug.h"
#include "drawgui/app/limits.h"
#include "drawgui/app/node_kind.h"
#include "drawgui/base/expected.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/graphics/types.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/props/node_props.h"
#include "drawgui/render/image_catalog.h"
#include "drawgui/render/node_lifecycle.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/shortcuts/chord.h"
#include "drawgui/theme/theme_loader.h"
#include "drawgui/window/window_manager.h"

#include "abi_internal.h"
#include "abi_types.h"

namespace dg::abi {
namespace {

// A dg_node_t's type_id IS a dg::NodeKind.
static_assert(static_cast<std::uint16_t>(dg::NodeKind::kBox) == DG_NODE_TYPE_BOX);
static_assert(static_cast<std::uint16_t>(dg::NodeKind::kButton) == DG_NODE_TYPE_BUTTON);
static_assert(static_cast<std::uint16_t>(dg::NodeKind::kLabel) == DG_NODE_TYPE_LABEL);
static_assert(static_cast<std::uint16_t>(dg::NodeKind::kCheckbox) == DG_NODE_TYPE_CHECKBOX);
static_assert(static_cast<std::uint16_t>(dg::NodeKind::kTextField) == DG_NODE_TYPE_TEXT_FIELD);
static_assert(static_cast<std::uint16_t>(dg::NodeKind::kTextArea) == DG_NODE_TYPE_TEXT_AREA);
static_assert(static_cast<std::uint16_t>(dg::NodeKind::kScrollView) ==
              DG_NODE_TYPE_SCROLL_VIEW);
static_assert(static_cast<std::uint16_t>(dg::NodeKind::kList) == DG_NODE_TYPE_LIST);
static_assert(static_cast<std::uint16_t>(dg::NodeKind::kImage) == DG_NODE_TYPE_IMAGE);
static_assert(static_cast<std::uint16_t>(dg::NodeKind::kDropdown) == DG_NODE_TYPE_DROPDOWN);
static_assert(static_cast<std::uint16_t>(dg::NodeKind::kProgress) == DG_NODE_TYPE_PROGRESS);
static_assert(static_cast<std::uint16_t>(dg::NodeKind::kTabs) == DG_NODE_TYPE_TABS);
static_assert(static_cast<std::uint16_t>(dg::NodeKind::kTree) == DG_NODE_TYPE_TREE);
static_assert(static_cast<std::uint16_t>(dg::NodeKind::kTable) == DG_NODE_TYPE_TABLE);
static_assert(static_cast<std::uint16_t>(dg::NodeKind::kSlider) == DG_NODE_TYPE_SLIDER);
static_assert(static_cast<std::uint16_t>(dg::NodeKind::kRadio) == DG_NODE_TYPE_RADIO);
static_assert(static_cast<std::uint16_t>(dg::NodeKind::kMenuBar) == DG_NODE_TYPE_MENUBAR);
static_assert(dg::kNodeKindMin == DG_NODE_TYPE_BOX && dg::kNodeKindMax == DG_NODE_TYPE_MENUBAR);
static_assert(dg::kUserActionBase == DG_USER_ACTION_BASE);

// The longest dg_list_set_items() a caller can pass.
constexpr auto kListMaxItems = dg::limits::kListMaxItems;

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

// 7-6: identical shape/reasoning to the three handle decks above - see
// abi_types.h's own header comment for the "never freed, stable address"
// argument this deck relies on exactly as they do.
std::deque<dg_theme_s>& all_theme_handles() {
  static std::deque<dg_theme_s> handles;
  return handles;
}

std::string& theme_err_storage() {
  static thread_local std::string buffer;
  return buffer;
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

// dg_debug_capture_png()'s own buffer - the identical lifetime rule
// dump_buffer_storage() already has, except this one holds bytes rather
// than text, which is exactly why dg_debug_capture_png_len() exists beside
// it: a caller cannot strlen() a PNG.
std::string& capture_png_storage() {
  static thread_local std::string buffer;
  return buffer;
}

// 8-3d: dg_shortcut_label()'s returned `const char*` needs an owned, stable
// buffer for the SAME reason dump_buffer_storage() above and
// last_error_storage() do (dg::shortcut_label() returns a std::string BY
// VALUE - there is no C++-side object outliving the call to point at)
// rather than a second lifetime rule invented for one more function.
std::string& shortcut_label_storage() {
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
  if (window == nullptr || window->window->closed()) {
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

ThemeImpl* resolve_theme(dg_theme_t* handle) {
  if (handle == nullptr) {
    return nullptr;
  }
  AppImpl* impl = resolve_app(handle->app);
  if (impl == nullptr || handle->index >= impl->themes.size()) {
    return nullptr;
  }
  return impl->themes[handle->index].get();
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

// The window an event names (closed ones included: DG_EVENT_WINDOW_CLOSED
// names a window that just closed).
const WindowImpl* window_named(const AppImpl& impl, dg::WindowId id) {
  for (const std::unique_ptr<WindowImpl>& window : impl.windows) {
    if (window->window->id() == id) {
      return window.get();
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
      if (value.bits == DG_LENGTH_UNIT_PERCENT) {
        return dg::PropValue::percent(value.number);
      }
      if (value.bits == DG_LENGTH_UNIT_AUTO) {
        return dg::PropValue::auto_length();
      }
      if (value.bits != DG_LENGTH_UNIT_PX) {
        return std::nullopt;
      }
      return dg::PropValue::length(value.number);
    case DG_VALUE_COLOR:
      return dg::PropValue::color(dg::Color::from_argb(value.bits));
    case DG_VALUE_ENUM:
      return dg::PropValue::option(value.bits);
    case DG_VALUE_BOOL:
      return dg::PropValue::boolean_bits(value.bits);
    default:
      return std::nullopt;
  }
}

// -- 7-6: theme handling ----------------------------------------------------

std::uint32_t to_theme_err_status(dg::ThemeLoadStatus status) {
  switch (status) {
    case dg::ThemeLoadStatus::kParseError:
      return DG_THEME_ERR_PARSE_ERROR;
    case dg::ThemeLoadStatus::kMissingField:
      return DG_THEME_ERR_MISSING_FIELD;
    case dg::ThemeLoadStatus::kUnknownToken:
      return DG_THEME_ERR_UNKNOWN_TOKEN;
    case dg::ThemeLoadStatus::kTypeMismatch:
      return DG_THEME_ERR_TYPE_MISMATCH;
    case dg::ThemeLoadStatus::kUnsupportedSchemaVersion:
      return DG_THEME_ERR_UNSUPPORTED_SCHEMA_VERSION;
    case dg::ThemeLoadStatus::kIoError:
      return DG_THEME_ERR_IO_ERROR;
    case dg::ThemeLoadStatus::kPathTraversal:
      return DG_THEME_ERR_PATH_TRAVERSAL;
    case dg::ThemeLoadStatus::kResourceTooLarge:
      return DG_THEME_ERR_RESOURCE_TOO_LARGE;
    case dg::ThemeLoadStatus::kTooManyResources:
      return DG_THEME_ERR_TOO_MANY_RESOURCES;
  }
  return DG_THEME_ERR_IO_ERROR;
}

void fill_theme_err(dg_theme_err* err, const dg::ThemeLoadError& error) {
  theme_err_storage() = error.message;
  set_last_error(std::string("dg_theme_load: ") + error.message);
  if (err != nullptr && err->size >= sizeof(dg_theme_err)) {
    err->status = to_theme_err_status(error.status);
  }
}

std::optional<dg::ThemeVariant> parse_variant(const char* variant) {
  if (variant == nullptr) {
    return std::nullopt;
  }
  const std::string name(variant);
  if (name == "light") {
    return dg::ThemeVariant::kLight;
  }
  if (name == "dark") {
    return dg::ThemeVariant::kDark;
  }
  return std::nullopt;
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
// Both omissions are named here rather than silently missing.
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

// -- dg::Event -> dg_event --------------------------------------------------

template <class... Fs>
struct Overloaded : Fs... {
  using Fs::operator()...;
};

std::uint16_t clicks_of(int clicks, int lo) {
  return static_cast<std::uint16_t>(std::clamp(clicks, lo, 0xFFFF));
}

dg_event to_dg_event(AppImpl& impl, const dg::Event& event) {
  dg_event out{};
  out.size = sizeof(dg_event);
  out.kind = dg_event_kind(event);
  out.item = -1;
  out.column = -1;
  std::size_t window_index = 0;
  const auto on_window = [&](dg::WindowId id) {
    if (const WindowImpl* window = window_named(impl, id)) {
      out.window = window->self_handle;
      window_index = window->self_index;
    }
  };
  const auto node = [&](std::optional<dg::NodeId> id) -> dg_node_t* {
    return id.has_value() && out.window != nullptr ? find_node_handle(impl, window_index, *id)
                                                   : nullptr;
  };
  std::visit(
      Overloaded{
          [&](const dg::events::ClickEvent& e) {
            on_window(e.window);
            out.node = node(e.node);
            out.x = e.x;
            out.y = e.y;
            out.clicks = clicks_of(e.clicks, 0);
            out.item = e.item.value_or(-1);
            out.column = e.column.value_or(-1);
            out.item_key = e.item_key;
          },
          [&](const dg::events::ContextClickEvent& e) {
            on_window(e.window);
            out.node = node(e.node);
            out.x = e.x;
            out.y = e.y;
            out.clicks = clicks_of(e.clicks, 1);
            out.item = e.item.value_or(-1);
            out.column = e.column.value_or(-1);
            out.item_key = e.item_key;
          },
          [&](const dg::events::ActionEvent& e) {
            on_window(e.window);
            out.node = node(e.node);
            out.action_id = e.action_id;
          },
          [&](const dg::events::TextChangedEvent& e) {
            on_window(e.window);
            out.node = node(e.node);
          },
          [&](const dg::events::FocusChangedEvent& e) {
            on_window(e.window);
            out.node = node(e.node);
          },
          [&](const dg::events::ListSelectEvent& e) {
            on_window(e.window);
            out.node = node(e.node);
            out.x = e.index;
            out.clicks = clicks_of(e.clicks, 0);
          },
          [&](const dg::events::ValueChangedEvent& e) {
            on_window(e.window);
            out.node = node(e.node);
            out.x = e.selected;
            out.y = e.previous;
          },
          [&](const dg::events::MenuEvent& e) {
            on_window(e.window);
            out.node = node(e.anchor);
            out.x = e.item.value_or(-1);
            out.y = e.menu_id;
            out.item = e.item.value_or(-1);
            out.column = e.bar_menu.value_or(-1);
            out.command = e.command;
          },
          [&](const dg::events::DialogResultEvent& e) {
            on_window(e.window);
            out.x = e.button;
            out.y = e.dialog_id;
          },
          [&](const dg::events::TabSelectEvent& e) {
            on_window(e.window);
            out.node = node(e.node);
            out.x = e.index;
          },
          [&](const dg::events::TreeEvent& e) {
            on_window(e.window);
            out.node = node(e.node);
            out.x = e.item;
          },
          [&](const dg::events::TableEvent& e) {
            on_window(e.window);
            out.node = node(e.node);
            out.x = e.index;
            out.clicks = clicks_of(e.clicks, 0);
          },
          [&](const dg::events::WindowStateEvent& e) {
            on_window(e.window);
            out.x = e.x;
            out.y = e.y;
          },
          [&](const dg::events::WindowClosedEvent& e) { on_window(e.window); },
          [&](const dg::events::TimerEvent& e) {
            out.x = static_cast<std::int32_t>(e.timer.value);
          },
          [&](const dg::events::UserEvent& e) {
            out.x =
                static_cast<std::int32_t>(static_cast<std::uint32_t>(e.payload & 0xFFFFFFFFU));
            out.y = static_cast<std::int32_t>(static_cast<std::uint32_t>(e.payload >> 32U));
          },
          [&](const dg::events::SliderEvent& e) {
            on_window(e.window);
            out.node = node(e.node);
          },
          [&](const dg::events::RowPartEvent& e) {
            on_window(e.window);
            out.node = node(e.node);
            out.item = e.item;
            out.column = e.column;
            out.x = e.part == dg::events::RowPartEvent::Part::kCheck ? DG_ROW_PART_CHECK
                                                                      : DG_ROW_PART_ACTION;
            out.y = e.action;
            out.item_key = e.item_key;
          },
          [&](const dg::events::CloseRequestedEvent& e) { on_window(e.window); },
          [&](const dg::events::AnimationFinishedEvent& e) {
            on_window(e.window);
            out.node = node(e.node);
            out.x = static_cast<std::int32_t>(e.animation);
          },
      },
      event);
  return out;
}

void append_events(AppImpl& impl, const std::vector<dg::Event>& events) {
  for (const dg::Event& event : events) {
    impl.pending_events.push_back(to_dg_event(impl, event));
  }
}

// A live slot and its window, or the DG_ERR_* that says why not.
struct LiveNode {
  NodeSlot* slot = nullptr;
  AppImpl* impl = nullptr;
  dg::Window* window = nullptr;
  std::int32_t status = DG_ERR_OK;
};

LiveNode resolve_live(dg_node_t* node_h) {
  LiveNode out;
  out.slot = resolve_node_slot(node_h);
  if (out.slot == nullptr) {
    out.status = DG_ERR_INVALID_HANDLE;
    return out;
  }
  if (out.slot->state != NodeSlot::State::kLive) {
    out.status = DG_ERR_NO_WINDOW;
    return out;
  }
  out.impl = resolve_app(node_h->app);
  out.window = out.impl->windows[out.slot->window_index]->window;
  return out;
}

// resolve_live() of a node of type `type_id` (DG_ERR_UNSUPPORTED otherwise).
LiveNode resolve_typed(dg_node_t* node_h, std::uint16_t type_id) {
  LiveNode out = resolve_live(node_h);
  if (out.status == DG_ERR_OK && out.slot->type_id != type_id) {
    out.status = DG_ERR_UNSUPPORTED;
  }
  return out;
}

dg::SvgOptions svg_options(std::uint32_t tint_argb) {
  dg::SvgOptions options;
  if ((tint_argb >> 24U) != 0U) {
    options.tint = dg::Color::from_argb(tint_argb);
  }
  return options;
}

std::int32_t show_image(LiveNode& live,
                        const dg::Expected<dg::ImageId, dg::ImageError>& decoded,
                        const char* function) {
  if (!decoded) {
    set_last_error(std::string(function) + ": " + decoded.error().message);
    return DG_ERR_INVALID_ARGUMENT;
  }
  return to_error_code(live.window->set_image(live.slot->node_id, decoded.value()));
}

LiveNode resolve_image_target(dg_node_t* node_h) {
  LiveNode out = resolve_live(node_h);
  if (out.status == DG_ERR_OK && out.slot->type_id != DG_NODE_TYPE_IMAGE &&
      out.slot->type_id != DG_NODE_TYPE_BOX) {
    out.status = DG_ERR_UNSUPPORTED;
  }
  return out;
}

// A WindowError from the runtime means the platform refused the request -
// argument and handle problems are caught before it is called - so every
// one maps to DG_ERR_UNSUPPORTED, with the platform's reason kept in
// dg_last_error().
std::int32_t to_status(const char* function,
                       const dg::Expected<void, dg::WindowError>& result) {
  if (!result) {
    set_last_error(std::string(function) + ": " + result.error().message);
    return DG_ERR_UNSUPPORTED;
  }
  return DG_ERR_OK;
}

// resolve_window()'s runtime window, or nullptr with the error recorded
// under `function`'s name.
dg::Window* resolve_os_window(const char* function, dg_window_t* window_h) {
  WindowImpl* window = resolve_window(window_h);
  if (window == nullptr) {
    set_last_error(std::string(function) + ": invalid window handle");
    return nullptr;
  }
  return window->window;
}

// The platform window manager and id behind a live window handle.
dg::WindowManager& manager_of(dg_window_t* window_h) {
  return resolve_app(window_h->app)->app.window_manager();
}

}  // namespace

// -- Seams (abi_internal.h) ---------------------------------------------------

AppImpl* app_of(dg_app_t* handle) {
  return resolve_app(handle);
}

WindowImpl* window_of(dg_window_t* handle) {
  return resolve_window(handle);
}

NodeSlot* slot_of(dg_node_t* handle) {
  return resolve_node_slot(handle);
}

std::int32_t to_error_code(dg::Status status) {
  switch (status) {
    case dg::Status::kOk:
      return DG_ERR_OK;
    case dg::Status::kInvalidArgument:
      return DG_ERR_INVALID_ARGUMENT;
    case dg::Status::kInvalidNode:
      return DG_ERR_INVALID_HANDLE;
    case dg::Status::kUnknownId:
      return DG_ERR_UNKNOWN_ID;
    case dg::Status::kTypeMismatch:
      return DG_ERR_TYPE_MISMATCH;
    case dg::Status::kValueOutOfRange:
      return DG_ERR_VALUE_OUT_OF_RANGE;
    case dg::Status::kNotApplicable:
      return DG_ERR_NOT_APPLICABLE;
    case dg::Status::kUnsupported:
      return DG_ERR_UNSUPPORTED;
    case dg::Status::kAlreadyAttached:
      return DG_ERR_ALREADY_ATTACHED;
    case dg::Status::kNoActiveTheme:
      return DG_ERR_NO_ACTIVE_THEME;
  }
  return DG_ERR_INTERNAL;
}

std::uint32_t dg_event_kind(const dg::Event& event) {
  using TreeKind = dg::events::TreeEvent::Kind;
  using TableKind = dg::events::TableEvent::Kind;
  using StateKind = dg::events::WindowStateEvent::Kind;
  return std::visit(
      Overloaded{
          [](const dg::events::ClickEvent&) -> std::uint32_t { return DG_EVENT_CLICK; },
          [](const dg::events::ContextClickEvent&) -> std::uint32_t {
            return DG_EVENT_CONTEXT_CLICK;
          },
          [](const dg::events::ActionEvent&) -> std::uint32_t { return DG_EVENT_ACTION; },
          [](const dg::events::TextChangedEvent&) -> std::uint32_t {
            return DG_EVENT_TEXT_CHANGED;
          },
          [](const dg::events::FocusChangedEvent&) -> std::uint32_t {
            return DG_EVENT_FOCUS_CHANGED;
          },
          [](const dg::events::ListSelectEvent&) -> std::uint32_t {
            return DG_EVENT_LIST_SELECT;
          },
          [](const dg::events::ValueChangedEvent&) -> std::uint32_t {
            return DG_EVENT_VALUE_CHANGED;
          },
          [](const dg::events::MenuEvent& e) -> std::uint32_t {
            return e.item.has_value() ? DG_EVENT_MENU_ACTIVATE : DG_EVENT_MENU_DISMISS;
          },
          [](const dg::events::DialogResultEvent&) -> std::uint32_t {
            return DG_EVENT_DIALOG_RESULT;
          },
          [](const dg::events::TabSelectEvent&) -> std::uint32_t {
            return DG_EVENT_TAB_SELECT;
          },
          [](const dg::events::TreeEvent& e) -> std::uint32_t {
            switch (e.kind) {
              case TreeKind::kSelect:
                return DG_EVENT_TREE_SELECT;
              case TreeKind::kExpand:
                return DG_EVENT_TREE_EXPAND;
              case TreeKind::kCollapse:
                return DG_EVENT_TREE_COLLAPSE;
            }
            return DG_EVENT_TREE_SELECT;
          },
          [](const dg::events::TableEvent& e) -> std::uint32_t {
            return e.kind == TableKind::kSort ? DG_EVENT_TABLE_SORT : DG_EVENT_TABLE_SELECT;
          },
          [](const dg::events::WindowStateEvent& e) -> std::uint32_t {
            switch (e.kind) {
              case StateKind::kResized:
                return DG_EVENT_WINDOW_RESIZED;
              case StateKind::kMoved:
                return DG_EVENT_WINDOW_MOVED;
              case StateKind::kMinimized:
                return DG_EVENT_WINDOW_MINIMIZED;
              case StateKind::kMaximized:
                return DG_EVENT_WINDOW_MAXIMIZED;
              case StateKind::kRestored:
                return DG_EVENT_WINDOW_RESTORED;
              case StateKind::kFocusGained:
                return DG_EVENT_WINDOW_FOCUS_GAINED;
              case StateKind::kFocusLost:
                return DG_EVENT_WINDOW_FOCUS_LOST;
              case StateKind::kEnteredFullscreen:
                return DG_EVENT_WINDOW_FULLSCREEN_ENTERED;
              case StateKind::kLeftFullscreen:
                return DG_EVENT_WINDOW_FULLSCREEN_LEFT;
            }
            return DG_EVENT_WINDOW_RESIZED;
          },
          [](const dg::events::WindowClosedEvent&) -> std::uint32_t {
            return DG_EVENT_WINDOW_CLOSED;
          },
          [](const dg::events::TimerEvent&) -> std::uint32_t { return DG_EVENT_TIMER; },
          [](const dg::events::UserEvent&) -> std::uint32_t { return DG_EVENT_USER; },
          [](const dg::events::SliderEvent&) -> std::uint32_t {
            return DG_EVENT_SLIDER_CHANGED;
          },
          [](const dg::events::RowPartEvent&) -> std::uint32_t { return DG_EVENT_ROW_PART; },
          [](const dg::events::CloseRequestedEvent&) -> std::uint32_t {
            return DG_EVENT_CLOSE_REQUESTED;
          },
          [](const dg::events::AnimationFinishedEvent&) -> std::uint32_t {
            return DG_EVENT_ANIMATION_FINISHED;
          },
      },
      event);
}

void drain_events(AppImpl& impl) {
  append_events(impl, impl.app.poll());
}

dg_node_t* adopt_node(dg_app_t* app, std::size_t window_index, dg::NodeId id,
                      std::uint16_t type_id) {
  AppImpl* impl = resolve_app(app);
  NodeSlot slot;
  slot.type_id = type_id;
  slot.state = NodeSlot::State::kLive;
  slot.window_index = window_index;
  slot.node_id = id;
  impl->nodes.push_back(slot);
  all_node_handles().emplace_back(
      dg_node_s{app, static_cast<std::uint32_t>(impl->nodes.size() - 1)});
  dg_node_t* handle = &all_node_handles().back();
  impl->nodes.back().self_handle = handle;
  return handle;
}

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
  dg::Expected<dg::App, dg::AppError> created = dg::App::create();
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

std::int32_t app_add_font(dg_app_t* app, const char* directory, const char* family) {
  AppImpl* impl = resolve_app(app);
  if (impl == nullptr) {
    set_last_error("dg_app_add_font: invalid app handle");
    return DG_ERR_INVALID_HANDLE;
  }
  if (directory == nullptr || family == nullptr) {
    set_last_error("dg_app_add_font: directory and family must not be null");
    return DG_ERR_INVALID_ARGUMENT;
  }
  // Only the first call scans `directory`; every later call ignores it,
  // exactly as this function's def.toml summary documents.
  const dg::Expected<void, dg::AppError> added = impl->app.add_font(directory, family);
  if (!added) {
    set_last_error(std::string("dg_app_add_font: ") + added.error().message);
    return to_error_code(added.error().status);
  }
  return DG_ERR_OK;
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

  dg::WindowOptions options;
  options.title = opts->title != nullptr ? std::string(opts->title) : std::string();
  options.width = opts->width;
  options.height = opts->height;
  options.background = dg::Color::from_argb(opts->background_argb);
  // ABI 0.2's appended fields, read only from a caller whose struct is long
  // enough to carry them - an ABI 0.1 build passes the shorter size and
  // gets 0.1's behaviour.
  if (opts->size >= offsetof(dg_window_opts, max_height) + sizeof(opts->max_height)) {
    options.resizable = opts->resizable != 0;
    options.min_width = opts->min_width;
    options.min_height = opts->min_height;
    options.max_width = opts->max_width;
    options.max_height = opts->max_height;
  }

  dg::Expected<dg::Window*, dg::AppError> opened = impl->app.create_window(options);
  if (!opened) {
    set_last_error(std::string("dg_window_create: ") + opened.error().message);
    return nullptr;
  }

  auto window = std::make_unique<WindowImpl>();
  window->window = opened.value();
  window->self_index = impl->windows.size();
  // Every created node that a dg::Window::remove() took with it: its handle
  // stops resolving, so a NodeId a later add_child() may reuse is never
  // reached through it.
  window->window->set_on_removed([impl, index = window->self_index](dg::NodeId id) {
    for (NodeSlot& slot : impl->nodes) {
      if (slot.state == NodeSlot::State::kLive && slot.window_index == index &&
          slot.node_id == id) {
        slot.state = NodeSlot::State::kRemoved;
      }
    }
  });
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
  const dg::Expected<dg::NodeId, dg::Status> created =
      window->window->create(static_cast<dg::NodeKind>(slot.type_id));
  if (!created) {
    return to_error_code(created.error());
  }
  slot.state = NodeSlot::State::kLive;
  slot.window_index = window_h->index;
  slot.node_id = created.value();
  (void)window->window->set_user_data(slot.node_id, slot.pending_user_data);
  return DG_ERR_OK;
}

dg_node_t* node_create(dg_app_t* app, std::uint16_t type_id) {
  AppImpl* impl = resolve_app(app);
  if (impl == nullptr) {
    set_last_error("dg_node_create: invalid app handle");
    return nullptr;
  }
  if (!dg::is_node_kind(type_id)) {
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
  const LiveNode live = resolve_live(node_h);
  if (live.status != DG_ERR_OK) {
    return live.status;
  }

  // 7-6: DG_VALUE_TOKEN is not a literal - it names a $token live reference
  // (design.md section 5.7.2) by token_id (carried in `bits`, the same
  // field an ordinary color value's ARGB already occupies). This reuses
  // dg_node_set_prop() unchanged rather than adding a second, bind-shaped
  // exported function - the ABI's own instance of the
  // "resolution reuses dg::set_prop() unchanged" rule, one layer up.
  if (value->type == DG_VALUE_TOKEN) {
    return to_error_code(live.window->bind_token(live.slot->node_id, prop_id,
                                                 static_cast<std::uint16_t>(value->bits)));
  }

  const std::optional<dg::PropValue> prop_value = to_prop_value(*value);
  if (!prop_value.has_value()) {
    // A dg_value no PropValue can carry. The writes that check the value's
    // type first (a progress bar's value/indeterminate, DG_PROP_TAB_INDEX/
    // MAX_LENGTH) call it a type mismatch; a length in an unknown unit is
    // out of range; anything else is not a value at all.
    const bool typed_first = (live.slot->type_id == DG_NODE_TYPE_PROGRESS &&
                              (prop_id == DG_PROP_VALUE || prop_id == DG_PROP_INDETERMINATE)) ||
                             (live.slot->type_id == DG_NODE_TYPE_SLIDER &&
                              (prop_id == DG_PROP_VALUE || prop_id == DG_PROP_MIN_VALUE ||
                               prop_id == DG_PROP_MAX_VALUE || prop_id == DG_PROP_STEP)) ||
                             (live.slot->type_id == DG_NODE_TYPE_RADIO &&
                              prop_id == DG_PROP_RADIO_GROUP) ||
                             prop_id == DG_PROP_TAB_INDEX || prop_id == DG_PROP_MAX_LENGTH;
    if (typed_first) {
      return DG_ERR_TYPE_MISMATCH;
    }
    if (value->type == DG_VALUE_LENGTH && value->bits > DG_LENGTH_UNIT_AUTO) {
      return DG_ERR_VALUE_OUT_OF_RANGE;
    }
    return DG_ERR_INVALID_ARGUMENT;
  }
  return to_error_code(live.window->set_prop(live.slot->node_id, prop_id, *prop_value));
}

std::int32_t node_set_text(dg_node_t* node_h, std::uint16_t prop_id, const char* utf8,
                           std::uint32_t len) {
  if (node_h == nullptr || (utf8 == nullptr && len != 0)) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  const LiveNode live = resolve_live(node_h);
  if (live.status != DG_ERR_OK) {
    return live.status;
  }
  const std::string_view text(utf8 == nullptr ? "" : utf8, len);
  return to_error_code(live.window->set_text(live.slot->node_id, prop_id, text));
}

std::int32_t node_insert_before(dg_node_t* parent_h, dg_node_t* child_h, dg_node_t* ref_h) {
  if (parent_h == nullptr || child_h == nullptr) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  if (parent_h->app != child_h->app || (ref_h != nullptr && ref_h->app != parent_h->app)) {
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
  AppImpl* impl = resolve_app(parent_h->app);
  if (impl == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  dg::Window& window = *impl->windows[parent_slot->window_index]->window;
  if (!dg::accepts_children(static_cast<dg::NodeKind>(parent_slot->type_id))) {
    // A list's rows come from dg_list_set_items(); a component composes its
    // own children (a DG_NODE_TYPE_TABS takes content through dg_tabs_add()'s
    // panels).
    return DG_ERR_UNSUPPORTED;
  }
  // A scroll view's children live in its composed content node.
  const dg::NodeId container = window.child_container(parent_slot->node_id);

  // `ref` must be a live DIRECT child of `parent` in the same window, and
  // never the node being inserted - anything else names no position.
  std::optional<dg::NodeId> before;
  if (ref_h != nullptr) {
    NodeSlot* ref_slot = resolve_node_slot(ref_h);
    if (ref_slot == nullptr) {
      return DG_ERR_INVALID_HANDLE;
    }
    if (ref_h == child_h || ref_slot->state != NodeSlot::State::kLive ||
        ref_slot->window_index != parent_slot->window_index ||
        window.layout().render().parent(ref_slot->node_id) != container) {
      return DG_ERR_INVALID_ARGUMENT;
    }
    before = ref_slot->node_id;
  }

  if (child_slot->state == NodeSlot::State::kPending) {
    const dg::Expected<dg::NodeId, dg::Status> created = window.create(
        static_cast<dg::NodeKind>(child_slot->type_id), parent_slot->node_id, before);
    if (!created) {
      return to_error_code(created.error());
    }
    child_slot->state = NodeSlot::State::kLive;
    child_slot->window_index = parent_slot->window_index;
    child_slot->node_id = created.value();
    (void)window.set_user_data(child_slot->node_id, child_slot->pending_user_data);
    return DG_ERR_OK;
  }

  // A live child is a MOVE, and only within its own window: its NodeId,
  // widget state and handle all survive.
  if (child_slot->window_index != parent_slot->window_index) {
    return DG_ERR_ALREADY_ATTACHED;
  }
  return to_error_code(window.move(child_slot->node_id, parent_slot->node_id, before));
}

std::int32_t node_remove(dg_node_t* node_h) {
  NodeSlot* slot = resolve_node_slot(node_h);
  if (slot == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  // A PENDING node was never attached to any window's tree at all, so
  // tombstoning the ABI handle below is the whole of what removing one
  // requires.
  if (slot->state == NodeSlot::State::kLive) {
    AppImpl* impl = resolve_app(node_h->app);
    // Events already queued keep naming the handles they were queued for.
    drain_events(*impl);
    const std::int32_t status =
        to_error_code(impl->windows[slot->window_index]->window->remove(slot->node_id));
    if (status != DG_ERR_OK) {
      return status;
    }
  }
  slot->state = NodeSlot::State::kRemoved;
  return DG_ERR_OK;
}

std::int32_t node_get_checked(dg_node_t* node_h) {
  const LiveNode live = resolve_live(node_h);
  if (live.status != DG_ERR_OK) {
    return live.status;
  }
  if (live.slot->type_id != DG_NODE_TYPE_CHECKBOX && live.slot->type_id != DG_NODE_TYPE_RADIO) {
    return DG_ERR_UNSUPPORTED;
  }
  const dg::Expected<bool, dg::Status> checked = live.window->checked(live.slot->node_id);
  if (!checked) {
    return to_error_code(checked.error());
  }
  return checked.value() ? 1 : 0;
}

std::int32_t node_get_text(dg_node_t* node_h, char* buf, std::uint32_t cap,
                           std::uint32_t* out_len) {
  const LiveNode live = resolve_live(node_h);
  if (live.status != DG_ERR_OK) {
    return live.status;
  }
  if (live.slot->type_id != DG_NODE_TYPE_TEXT_FIELD &&
      live.slot->type_id != DG_NODE_TYPE_TEXT_AREA) {
    return DG_ERR_UNSUPPORTED;
  }
  if (cap > 0 && buf == nullptr) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  const dg::Expected<std::string, dg::Status> text = live.window->text(live.slot->node_id);
  if (!text) {
    return to_error_code(text.error());
  }
  if (out_len != nullptr) {
    *out_len = static_cast<std::uint32_t>(text.value().size());
  }
  if (cap > 0) {
    const std::size_t to_copy = std::min<std::size_t>(cap, text.value().size());
    std::copy_n(text.value().data(), to_copy, buf);
  }
  return DG_ERR_OK;
}

std::int32_t node_scope_action(dg_node_t* node_h, std::uint16_t action_id) {
  const LiveNode live = resolve_live(node_h);
  if (live.status != DG_ERR_OK) {
    return live.status;
  }
  return to_error_code(live.window->scope_action(live.slot->node_id, action_id));
}

std::int32_t wait_events(dg_app_t* app, std::int32_t timeout_ms) {
  AppImpl* impl = resolve_app(app);
  if (impl == nullptr) {
    set_last_error("dg_wait_events: invalid app handle");
    return -1;
  }
  append_events(*impl, impl->app.pump(timeout_ms));
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
  drain_events(*impl);
  const auto count =
      std::min<std::size_t>(static_cast<std::size_t>(max), impl->pending_events.size());
  // A caller built against the 40-byte dg_event (before item/column/command)
  // says so in out[0].size; its entries are then written at that stride and
  // truncated. Any other size value means the current struct.
  constexpr std::size_t kLegacySize = offsetof(dg_event, item);
  std::size_t stride = sizeof(dg_event);
  if (count > 0 && out[0].size >= kLegacySize && out[0].size < sizeof(dg_event)) {
    stride = kLegacySize;
  }
  auto* bytes = reinterpret_cast<unsigned char*>(out);
  for (std::size_t i = 0; i < count; ++i) {
    dg_event entry = impl->pending_events[i];
    entry.size = static_cast<std::uint32_t>(stride);
    std::memcpy(bytes + (i * stride), &entry, stride);
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
  dg::Window& window = *impl->windows[slot->window_index]->window;

  std::string& buffer = dump_buffer_storage();
  buffer.clear();
  dump_node(window.layout(), slot->node_id, buffer);
  return buffer.c_str();
}

const char* shortcut_label(std::uint16_t action_id) {
  // Fully-qualified `dg::shortcut_label` (chord.h): unqualified would resolve
  // to THIS function (dg::abi::shortcut_label), recursing into itself - the
  // wrap this function exists to perform, not a call it can make unqualified
  // from inside its own namespace.
  const std::optional<std::string> label =
      dg::shortcut_label(static_cast<dg_action_id>(action_id), dg::Platform::kLinux);
  if (!label.has_value()) {
    set_last_error("dg_shortcut_label: action_id names no binding");
    return nullptr;
  }
  std::string& buffer = shortcut_label_storage();
  buffer = *label;
  return buffer.c_str();
}

dg_theme_t* theme_load_dir(dg_app_t* app, const char* dir, dg_theme_err* err) {
  AppImpl* impl = resolve_app(app);
  if (impl == nullptr) {
    set_last_error("dg_theme_load_dir: invalid app handle");
    return nullptr;
  }
  if (dir == nullptr) {
    set_last_error("dg_theme_load_dir: dir must not be null");
    return nullptr;
  }

  dg::Expected<dg::ThemePackage, dg::ThemeLoadError> package = dg::ThemePackage::open(dir);
  if (!package) {
    fill_theme_err(err, package.error());
    return nullptr;
  }
  dg::Expected<dg::Theme, dg::ThemeLoadError> theme = package.value().load_theme_json();
  if (!theme) {
    fill_theme_err(err, theme.error());
    return nullptr;
  }

  auto owned = std::make_unique<ThemeImpl>();
  owned->theme = std::make_shared<dg::Theme>(std::move(theme).value());
  owned->package = std::move(package).value();
  impl->themes.push_back(std::move(owned));

  all_theme_handles().emplace_back(
      dg_theme_s{app, static_cast<std::uint32_t>(impl->themes.size() - 1)});
  dg_theme_t* handle = &all_theme_handles().back();
  impl->themes.back()->self_handle = handle;
  return handle;
}

dg_theme_t* theme_load_memory(dg_app_t* app, const char* json, std::uint32_t len,
                              const char* base_dir, dg_theme_err* err) {
  AppImpl* impl = resolve_app(app);
  if (impl == nullptr) {
    set_last_error("dg_theme_load_memory: invalid app handle");
    return nullptr;
  }
  if (json == nullptr) {
    set_last_error("dg_theme_load_memory: json must not be null");
    return nullptr;
  }

  std::optional<dg::ThemePackage> package;
  if (base_dir != nullptr) {
    dg::Expected<dg::ThemePackage, dg::ThemeLoadError> opened =
        dg::ThemePackage::open(base_dir);
    if (!opened) {
      fill_theme_err(err, opened.error());
      return nullptr;
    }
    package = std::move(opened).value();
  }

  const std::string_view text(json, len);
  dg::Expected<dg::Theme, dg::ThemeLoadError> theme = dg::load_theme(text);
  if (!theme) {
    fill_theme_err(err, theme.error());
    return nullptr;
  }

  auto owned = std::make_unique<ThemeImpl>();
  owned->theme = std::make_shared<dg::Theme>(std::move(theme).value());
  owned->package = std::move(package);
  impl->themes.push_back(std::move(owned));

  all_theme_handles().emplace_back(
      dg_theme_s{app, static_cast<std::uint32_t>(impl->themes.size() - 1)});
  dg_theme_t* handle = &all_theme_handles().back();
  impl->themes.back()->self_handle = handle;
  return handle;
}

namespace {

// dg_app_set_theme()/dg_theme_set_variant()/dg_theme_override() all end
// here: the runtime re-applies the active theme's (possibly changed) data
// to every open window.
void reapply_theme_if_active(AppImpl& impl, const ThemeImpl& theme) {
  if (impl.active_theme == &theme) {
    impl.app.set_theme(theme.theme, theme.variant);
  }
}

}  // namespace

std::int32_t theme_set_variant(dg_theme_t* theme_h, const char* variant) {
  ThemeImpl* theme = resolve_theme(theme_h);
  if (theme == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  const std::optional<dg::ThemeVariant> parsed = parse_variant(variant);
  if (!parsed.has_value()) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  theme->variant = *parsed;
  reapply_theme_if_active(*resolve_app(theme_h->app), *theme);
  return DG_ERR_OK;
}

std::int32_t theme_override(dg_theme_t* theme_h, std::uint16_t token_id,
                            const dg_value* value) {
  if (value == nullptr || value->size < sizeof(dg_value)) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  ThemeImpl* theme = resolve_theme(theme_h);
  if (theme == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  const std::optional<dg::TokenType> kind = dg::token_type(token_id);
  if (!kind.has_value()) {
    return DG_ERR_UNKNOWN_ID;
  }
  if (*kind == dg::TokenType::k_color) {
    if (value->type != DG_VALUE_COLOR) {
      return DG_ERR_TYPE_MISMATCH;
    }
    theme->theme->set_color(token_id, theme->variant, dg::Color::from_argb(value->bits));
  } else {
    if (value->type != DG_VALUE_FLOAT && value->type != DG_VALUE_LENGTH) {
      return DG_ERR_TYPE_MISMATCH;
    }
    theme->theme->set_int(token_id, static_cast<int>(value->number));
  }
  reapply_theme_if_active(*resolve_app(theme_h->app), *theme);
  return DG_ERR_OK;
}

std::int32_t app_set_theme(dg_app_t* app, dg_theme_t* theme_h) {
  AppImpl* impl = resolve_app(app);
  if (impl == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  ThemeImpl* theme = resolve_theme(theme_h);
  if (theme == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  if (theme_h->app != app) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  impl->active_theme = theme;
  reapply_theme_if_active(*impl, *theme);
  return DG_ERR_OK;
}

// -- Window operations (ABI 0.2) ------------------------------------------

std::int32_t window_set_title(dg_window_t* window_h, const char* utf8) {
  dg::Window* window = resolve_os_window("dg_window_set_title", window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  if (utf8 == nullptr) {
    set_last_error("dg_window_set_title: utf8 must not be null");
    return DG_ERR_INVALID_ARGUMENT;
  }
  return to_status("dg_window_set_title", window->set_title(utf8));
}

std::int32_t window_get_title(dg_window_t* window_h, char* buf, std::uint32_t cap,
                              std::uint32_t* out_len) {
  dg::Window* window = resolve_os_window("dg_window_get_title", window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  if (cap > 0 && buf == nullptr) {
    set_last_error("dg_window_get_title: buf must not be null when cap > 0");
    return DG_ERR_INVALID_ARGUMENT;
  }
  const dg::Expected<std::string, dg::WindowError> title = window->title();
  if (!title) {
    set_last_error("dg_window_get_title: " + title.error().message);
    return DG_ERR_UNSUPPORTED;
  }
  const std::string& text = title.value();
  if (out_len != nullptr) {
    *out_len = static_cast<std::uint32_t>(text.size());
  }
  if (cap > 0) {
    std::copy_n(text.data(), std::min<std::size_t>(cap, text.size()), buf);
  }
  return DG_ERR_OK;
}

std::int32_t window_set_size(dg_window_t* window_h, std::int32_t width, std::int32_t height) {
  dg::Window* window = resolve_os_window("dg_window_set_size", window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  if (width <= 0 || height <= 0) {
    set_last_error("dg_window_set_size: width and height must be positive");
    return DG_ERR_INVALID_ARGUMENT;
  }
  return to_status("dg_window_set_size", window->set_size(width, height));
}

std::int32_t window_get_size(dg_window_t* window_h, std::int32_t* width, std::int32_t* height) {
  dg::Window* window = resolve_os_window("dg_window_get_size", window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  const dg::Expected<dg::PixelSize, dg::WindowError> size = window->size();
  if (!size) {
    set_last_error("dg_window_get_size: " + size.error().message);
    return DG_ERR_UNSUPPORTED;
  }
  if (width != nullptr) {
    *width = size.value().width;
  }
  if (height != nullptr) {
    *height = size.value().height;
  }
  return DG_ERR_OK;
}

std::int32_t window_set_position(dg_window_t* window_h, std::int32_t x, std::int32_t y) {
  dg::Window* window = resolve_os_window("dg_window_set_position", window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  return to_status("dg_window_set_position", window->set_position(x, y));
}

std::int32_t window_get_position(dg_window_t* window_h, std::int32_t* x, std::int32_t* y) {
  dg::Window* window = resolve_os_window("dg_window_get_position", window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  const dg::Expected<dg::PixelPoint, dg::WindowError> position = window->position();
  if (!position) {
    set_last_error("dg_window_get_position: " + position.error().message);
    return DG_ERR_UNSUPPORTED;
  }
  if (x != nullptr) {
    *x = position.value().x;
  }
  if (y != nullptr) {
    *y = position.value().y;
  }
  return DG_ERR_OK;
}

std::int32_t window_minimize(dg_window_t* window_h) {
  dg::Window* window = resolve_os_window("dg_window_minimize", window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  return to_status("dg_window_minimize", window->minimize());
}

std::int32_t window_maximize(dg_window_t* window_h) {
  dg::Window* window = resolve_os_window("dg_window_maximize", window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  return to_status("dg_window_maximize", window->maximize());
}

std::int32_t window_restore(dg_window_t* window_h) {
  dg::Window* window = resolve_os_window("dg_window_restore", window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  return to_status("dg_window_restore", window->restore());
}

std::int32_t window_set_fullscreen(dg_window_t* window_h, std::int32_t fullscreen) {
  dg::Window* window = resolve_os_window("dg_window_set_fullscreen", window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  return to_status("dg_window_set_fullscreen", window->set_fullscreen(fullscreen != 0));
}

std::int32_t window_set_resizable(dg_window_t* window_h, std::int32_t resizable) {
  dg::Window* window = resolve_os_window("dg_window_set_resizable", window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  return to_status("dg_window_set_resizable", window->set_resizable(resizable != 0));
}

std::int32_t window_set_min_size(dg_window_t* window_h, std::int32_t width,
                                 std::int32_t height) {
  dg::Window* window = resolve_os_window("dg_window_set_min_size", window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  return to_status("dg_window_set_min_size", window->set_min_size(width, height));
}

std::int32_t window_set_max_size(dg_window_t* window_h, std::int32_t width,
                                 std::int32_t height) {
  dg::Window* window = resolve_os_window("dg_window_set_max_size", window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  return to_status("dg_window_set_max_size", window->set_max_size(width, height));
}

std::int32_t window_set_icon(dg_window_t* window_h, const void* pixels, std::int32_t width,
                             std::int32_t height, std::uint32_t row_bytes,
                             std::uint32_t format) {
  dg::Window* window = resolve_os_window("dg_window_set_icon", window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  if (pixels == nullptr || width <= 0 || height <= 0 ||
      row_bytes < static_cast<std::uint64_t>(width) * 4U) {
    set_last_error(
        "dg_window_set_icon: pixels must be non-null, the size positive and "
        "row_bytes >= width * 4");
    return DG_ERR_INVALID_ARGUMENT;
  }
  dg::PixelFormat pixel_format = dg::PixelFormat::kBgra8888;
  if (format == DG_PIXEL_FORMAT_RGBA8888) {
    pixel_format = dg::PixelFormat::kRgba8888;
  } else if (format != DG_PIXEL_FORMAT_BGRA8888) {
    set_last_error("dg_window_set_icon: format must be a DG_PIXEL_FORMAT_* value");
    return DG_ERR_INVALID_ARGUMENT;
  }
  const dg::ImageView icon{static_cast<const std::uint8_t*>(pixels), width, height, row_bytes,
                           pixel_format};
  return to_status("dg_window_set_icon", window->set_icon(icon));
}

std::int32_t window_get_state(dg_window_t* window_h) {
  dg::Window* window = resolve_os_window("dg_window_get_state", window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  const dg::Expected<dg::WindowState, dg::WindowError> state = window->state();
  if (!state) {
    set_last_error("dg_window_get_state: " + state.error().message);
    return DG_ERR_UNSUPPORTED;
  }
  std::int32_t bits = 0;
  bits |= state.value().minimized ? DG_WINDOW_STATE_MINIMIZED : 0;
  bits |= state.value().maximized ? DG_WINDOW_STATE_MAXIMIZED : 0;
  bits |= state.value().fullscreen ? DG_WINDOW_STATE_FULLSCREEN : 0;
  bits |= state.value().focused ? DG_WINDOW_STATE_FOCUSED : 0;
  bits |= state.value().resizable ? DG_WINDOW_STATE_RESIZABLE : 0;
  return bits;
}

// -- Debug hooks: the platform's own test injection, reached through the
// runtime's escape hatches.

std::int32_t debug_warp_pointer(dg_window_t* window_h, std::int32_t x, std::int32_t y) {
  WindowImpl* window = resolve_window(window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  manager_of(window_h).warp_pointer(window->window->id(), x, y);
  return DG_ERR_OK;
}

std::int32_t debug_get_cursor(dg_window_t* window_h) {
  WindowImpl* window = resolve_window(window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  const dg::Expected<dg::Cursor, dg::WindowError> cursor =
      manager_of(window_h).current_cursor(window->window->id());
  if (!cursor) {
    return DG_ERR_INVALID_HANDLE;
  }
  return static_cast<std::int32_t>(cursor.value());
}

std::int32_t debug_post_pointer_button(dg_window_t* window_h, std::int32_t down, std::int32_t x,
                                       std::int32_t y) {
  WindowImpl* window = resolve_window(window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  manager_of(window_h).post_pointer_button(window->window->id(), down != 0, x, y);
  return DG_ERR_OK;
}

std::int32_t debug_post_key(dg_window_t* window_h, std::int32_t down,
                            const char* logical_key_name) {
  WindowImpl* window = resolve_window(window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  if (logical_key_name == nullptr) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  // dg::parse_chord() is the ONE existing parser for this grammar
  // (tests/unit/test_shortcuts.cpp already proves it correct) - reused
  // rather than a second, hand-rolled name->LogicalKey lookup written
  // just for this debug hook. A bare key name ("PageUp") parses to
  // Chord{mods=kNone, key=...}; anything carrying a modifier is refused
  // below, matching WindowManager::post_logical_key()'s own inability to
  // inject one (see this function's def.toml summary).
  const std::optional<dg::Chord> chord = dg::parse_chord(logical_key_name);
  if (!chord.has_value()) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  manager_of(window_h).post_logical_key(window->window->id(), down != 0, chord->key,
                                        chord->mods);
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

const char* debug_capture_png(dg_window_t* window_h) {
  WindowImpl* window = resolve_window(window_h);
  if (window == nullptr) {
    set_last_error("dg_debug_capture_png: invalid window handle");
    return nullptr;
  }
  const dg::RasterSurface* surface = dg::app_detail::painted_surface(*window->window);
  if (surface == nullptr) {
    set_last_error(
        "dg_debug_capture_png: this window has never painted a frame - call "
        "dg_wait_events()/dg_poll_events() at least once first");
    return nullptr;
  }

  const std::vector<std::uint8_t> png = surface->encode_png();
  if (png.empty()) {
    set_last_error("dg_debug_capture_png: PNG encoding failed");
    return nullptr;
  }

  std::string& buffer = capture_png_storage();
  buffer.assign(reinterpret_cast<const char*>(png.data()), png.size());
  return buffer.data();
}

std::uint32_t debug_capture_png_len() {
  return static_cast<std::uint32_t>(capture_png_storage().size());
}

std::int32_t debug_post_text_input(dg_window_t* window_h, const char* utf8) {
  WindowImpl* window = resolve_window(window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  if (utf8 == nullptr) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  manager_of(window_h).post_text_input(window->window->id(), std::string(utf8));
  return DG_ERR_OK;
}

std::int32_t debug_post_text_field_key(dg_window_t* window_h, std::int32_t down,
                                       const char* key_name, std::int32_t shift) {
  WindowImpl* window = resolve_window(window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  if (key_name == nullptr) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  // Not dg::parse_chord(): none of these six is a shortcut binding name
  // (input/shortcuts.toml never binds one) - see this function's own
  // def.toml summary for why dg_debug_post_key() cannot be reused here.
  const std::string_view name(key_name);
  dg::Key key = dg::Key::kOther;
  if (name == "Left") {
    key = dg::Key::kLeft;
  } else if (name == "Right") {
    key = dg::Key::kRight;
  } else if (name == "Home") {
    key = dg::Key::kHome;
  } else if (name == "End") {
    key = dg::Key::kEnd;
  } else if (name == "Backspace") {
    key = dg::Key::kBackspace;
  } else if (name == "Delete") {
    key = dg::Key::kDelete;
  } else if (name == "Up") {
    key = dg::Key::kUp;
  } else if (name == "Down") {
    key = dg::Key::kDown;
  } else if (name == "Enter") {
    key = dg::Key::kEnter;
  } else if (name == "PageUp") {
    key = dg::Key::kPageUp;
  } else if (name == "PageDown") {
    key = dg::Key::kPageDown;
  } else if (name == "Tab") {
    key = dg::Key::kTab;
  } else if (name == "Escape") {
    key = dg::Key::kEscape;  // phase B: dismisses menus and dialogs
  } else {
    return DG_ERR_INVALID_ARGUMENT;
  }
  manager_of(window_h).post_key(window->window->id(), down != 0, key, shift != 0);
  return DG_ERR_OK;
}

// -- ABI 0.2 phase A: focus, scrolling, lists, images, timers ------------

std::int32_t node_focus(dg_node_t* node_h) {
  const LiveNode live = resolve_live(node_h);
  if (live.status != DG_ERR_OK) {
    return live.status;
  }
  const dg::Status status = live.window->focus(live.slot->node_id);
  if (status == dg::Status::kUnsupported) {
    set_last_error("dg_node_focus: this node cannot take keyboard focus");
  }
  return to_error_code(status);
}

dg_node_t* window_get_focus(dg_window_t* window_h) {
  WindowImpl* window = resolve_window(window_h);
  if (window == nullptr) {
    set_last_error("dg_window_get_focus: invalid window handle");
    return nullptr;
  }
  const std::optional<dg::NodeId> focused = window->window->focused();
  if (!focused.has_value()) {
    return nullptr;
  }
  return find_node_handle(*resolve_app(window_h->app), window->self_index, *focused);
}

std::int32_t window_clear_focus(dg_window_t* window_h) {
  WindowImpl* window = resolve_window(window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  window->window->clear_focus();
  return DG_ERR_OK;
}

std::int32_t window_set_close_confirmation(dg_window_t* window_h, std::int32_t enabled) {
  WindowImpl* window = resolve_window(window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  window->window->set_close_confirmation(enabled != 0);
  return DG_ERR_OK;
}

std::int32_t window_close(dg_window_t* window_h) {
  WindowImpl* window = resolve_window(window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  window->window->close();
  return DG_ERR_OK;
}

std::int32_t node_animate(dg_node_t* node_h, std::uint16_t prop_id, const dg_value* from,
                          const dg_value* to, std::uint32_t duration_ms, std::uint32_t curve) {
  if (to == nullptr || to->size < sizeof(dg_value) ||
      (from != nullptr && from->size < sizeof(dg_value))) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  const LiveNode live = resolve_live(node_h);
  if (live.status != DG_ERR_OK) {
    return live.status;
  }
  if (duration_ms > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) ||
      curve > std::numeric_limits<std::uint16_t>::max()) {
    return DG_ERR_VALUE_OUT_OF_RANGE;
  }
  const std::optional<dg::PropValue> to_value = to_prop_value(*to);
  std::optional<dg::PropValue> from_value;
  if (from != nullptr) {
    from_value = to_prop_value(*from);
  }
  if (!to_value.has_value() || (from != nullptr && !from_value.has_value())) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  const dg::Expected<dg::AnimationId, dg::Status> started = live.window->animate(
      live.slot->node_id, prop_id, from_value, *to_value, static_cast<int>(duration_ms),
      static_cast<std::uint16_t>(curve));
  if (!started) {
    return to_error_code(started.error());
  }
  return static_cast<std::int32_t>(started.value());
}

std::int32_t node_set_transition(dg_node_t* node_h, std::uint16_t prop_id,
                                 std::uint32_t duration_ms, std::uint32_t curve) {
  const LiveNode live = resolve_live(node_h);
  if (live.status != DG_ERR_OK) {
    return live.status;
  }
  if (duration_ms > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) ||
      curve > std::numeric_limits<std::uint16_t>::max()) {
    return DG_ERR_VALUE_OUT_OF_RANGE;
  }
  return to_error_code(live.window->set_transition(live.slot->node_id, prop_id,
                                                   static_cast<int>(duration_ms),
                                                   static_cast<std::uint16_t>(curve)));
}

std::int32_t animation_cancel(dg_window_t* window_h, std::int32_t animation) {
  WindowImpl* window = resolve_window(window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  if (animation <= 0) {
    return DG_ERR_UNKNOWN_ID;
  }
  return to_error_code(window->window->cancel_animation(static_cast<dg::AnimationId>(animation)));
}

std::int32_t app_set_reduced_motion(dg_app_t* app_h, std::int32_t enabled) {
  AppImpl* impl = resolve_app(app_h);
  if (impl == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  impl->app.set_reduced_motion(enabled != 0);
  return DG_ERR_OK;
}

std::int32_t node_get_scroll_offset(dg_node_t* node_h, std::int32_t* x, std::int32_t* y) {
  const LiveNode live = resolve_live(node_h);
  if (live.status != DG_ERR_OK) {
    return live.status;
  }
  const dg::Expected<dg::PixelPoint, dg::Status> offset =
      live.window->scroll_offset(live.slot->node_id);
  if (!offset) {
    return to_error_code(offset.error());
  }
  if (x != nullptr) {
    *x = offset.value().x;
  }
  if (y != nullptr) {
    *y = offset.value().y;
  }
  return DG_ERR_OK;
}

std::int32_t list_set_items(dg_node_t* node_h, const char* const* items, std::uint32_t count,
                            std::int32_t item_height) {
  const LiveNode live = resolve_typed(node_h, DG_NODE_TYPE_LIST);
  if (live.status != DG_ERR_OK) {
    return live.status;
  }
  if ((items == nullptr && count > 0) || count > static_cast<std::uint32_t>(kListMaxItems)) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  std::vector<std::string> copied;
  copied.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    copied.emplace_back(items[i] == nullptr ? "" : items[i]);
  }
  return to_error_code(
      live.window->list(live.slot->node_id).value().set_items(std::move(copied), item_height));
}

std::int32_t list_get_selected(dg_node_t* node_h) {
  const LiveNode live = resolve_typed(node_h, DG_NODE_TYPE_LIST);
  if (live.status != DG_ERR_OK) {
    return live.status;
  }
  const dg::Expected<int, dg::Status> selected = live.window->list(live.slot->node_id).value().selected();
  return selected ? selected.value() : to_error_code(selected.error());
}

std::int32_t list_set_selected(dg_node_t* node_h, std::int32_t index) {
  const LiveNode live = resolve_typed(node_h, DG_NODE_TYPE_LIST);
  if (live.status != DG_ERR_OK) {
    return live.status;
  }
  return to_error_code(live.window->list(live.slot->node_id).value().select(index));
}

std::int32_t node_set_image_data(dg_node_t* node_h, const void* bytes, std::uint32_t len,
                                 std::uint32_t svg_tint_argb) {
  LiveNode live = resolve_image_target(node_h);
  if (live.status != DG_ERR_OK) {
    return live.status;
  }
  if (bytes == nullptr || len == 0) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  return show_image(live,
                    live.impl->app.images().decode_any(static_cast<const std::uint8_t*>(bytes),
                                                       len, svg_options(svg_tint_argb)),
                    "dg_node_set_image_data");
}

std::int32_t node_set_image_file(dg_node_t* node_h, const char* path,
                                 std::uint32_t svg_tint_argb) {
  LiveNode live = resolve_image_target(node_h);
  if (live.status != DG_ERR_OK) {
    return live.status;
  }
  if (path == nullptr) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  return show_image(live, live.impl->app.images().decode_file(path, svg_options(svg_tint_argb)),
                    "dg_node_set_image_file");
}

std::uint32_t timer_add(dg_app_t* app, std::int32_t delay_ms, std::int32_t repeat) {
  AppImpl* impl = resolve_app(app);
  if (impl == nullptr) {
    set_last_error("dg_timer_add: invalid app handle");
    return 0;
  }
  const dg::TimerId id = impl->app.add_timer(delay_ms, repeat != 0);
  if (id.value == 0 || id.value > 0x7FFFFFFFU) {
    (void)impl->app.cancel_timer(id);
    set_last_error("dg_timer_add: out of timer ids");
    return 0;
  }
  return static_cast<std::uint32_t>(id.value);
}

std::int32_t timer_cancel(dg_app_t* app, std::uint32_t timer_id) {
  AppImpl* impl = resolve_app(app);
  if (impl == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  return impl->app.cancel_timer(dg::TimerId{timer_id}) ? DG_ERR_OK : DG_ERR_INVALID_ARGUMENT;
}

std::int32_t wake(dg_app_t* app) {
  AppImpl* impl = resolve_app(app);
  if (impl == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  return impl->app.waker().wake() ? DG_ERR_OK : DG_ERR_UNSUPPORTED;
}

std::int32_t post_user_event(dg_app_t* app, std::uint64_t payload) {
  AppImpl* impl = resolve_app(app);
  if (impl == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  return impl->app.waker().post_user_event(payload) ? DG_ERR_OK : DG_ERR_UNSUPPORTED;
}

std::int32_t debug_post_wheel(dg_window_t* window_h, float dx, float dy) {
  WindowImpl* window = resolve_window(window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  manager_of(window_h).post_wheel(window->window->id(), dx, dy);
  return DG_ERR_OK;
}

std::int32_t debug_post_pointer_button_ex(dg_window_t* window_h, std::int32_t down,
                                          std::int32_t x, std::int32_t y, std::int32_t button,
                                          std::int32_t clicks) {
  WindowImpl* window = resolve_window(window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  dg::PointerButton which = dg::PointerButton::kPrimary;
  if (button == DG_POINTER_BUTTON_SECONDARY) {
    which = dg::PointerButton::kSecondary;
  } else if (button == DG_POINTER_BUTTON_MIDDLE) {
    which = dg::PointerButton::kMiddle;
  } else if (button != DG_POINTER_BUTTON_PRIMARY) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  if (clicks < 0) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  manager_of(window_h).post_pointer_button(window->window->id(), down != 0, x, y, which,
                                           clicks);
  return DG_ERR_OK;
}

}  // namespace dg::abi
