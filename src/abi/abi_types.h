// The C++-side state behind every opaque ABI handle.
//
// Every dg_*_t the host holds is a pointer to a tiny wrapper - {app, index},
// or just {index} for dg_app_t itself - allocated once by the function that
// hands it out and NEVER FREED. That is the whole of the handle-validation
// design: dereferencing a stale handle always reads
// two ordinary, still-allocated integers, never freed memory, so a caller
// that keeps using a handle after dg_node_remove()/dg_app_destroy() gets an
// ordinary DG_ERR_INVALID_HANDLE rather than undefined behaviour. The real
// resource (AppImpl/WindowImpl/NodeSlot) lives in an arena indexed by that
// same integer and is what actually gets torn down or marked removed.
//
// THIS IS NOT 6-1's AnimHandle{index, generation} even though the shape
// looks similar, and the difference is deliberate, not an oversight: an
// AnimHandle needs a generation counter because animation SLOTS are reused
// (a finished slot's storage is reclaimed for the next animate() call,
// which reopens the classic ABA problem 6-1's own header documents at
// length). Nothing here is ever reused. dg_node_create()/dg_window_create()/
// dg_app_create() only ever APPEND to their arena - removal marks an entry
// dead, exactly like WidgetSet's `std::optional<Widget>` already does one
// layer down, and the ThemeBindings comment already worked out
// which of the two precedents applies to an append-only table: "there is no
// reuse, so there is no ABA problem to guard against". That argument
// transfers here verbatim - a handle's index is claimed once, forever, so a
// generation field would be answering a question this arena's own data
// never asks. The cost this shares with the engine underneath it: an arena
// that never reclaims an index grows without bound over a long enough
// session, exactly like RenderTree/LayoutTree already do - an ABI cannot
// promise memory behaviour the engine underneath does not have.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "drawgui/abi/drawgui.h"
#include "drawgui/app/app.h"
#include "drawgui/app/window.h"
#include "drawgui/theme/theme.h"
#include "drawgui/theme/theme_package.h"

// The wrapper structs the opaque `typedef struct dg_xxx_s dg_xxx_t;` forward
// declarations in drawgui.h name. Deliberately trivial and deliberately
// never `delete`d - see the file header.
struct dg_app_s {
  std::uint32_t index = 0;
};

struct dg_window_s {
  dg_app_t* app = nullptr;
  std::uint32_t index = 0;
};

struct dg_node_s {
  dg_app_t* app = nullptr;
  std::uint32_t index = 0;
};

// 7-6: a loaded dg::Theme belongs to exactly one app (dg_theme_load_dir()'s
// own first parameter), the same one-owner shape dg_window_s/dg_node_s
// already have - see AppImpl::themes below for why the arena, not this
// wrapper, is what a handle actually resolves through.
struct dg_theme_s {
  dg_app_t* app = nullptr;
  std::uint32_t index = 0;
};

namespace dg::abi {

// One dg_node_t's real state. PENDING until attached (dg_window_set_root()
// or dg_node_insert_before() as a child), LIVE afterwards, REMOVED once
// dg_node_remove() is called. A pending node is only a type: every write
// needs a live node (DG_ERR_NO_WINDOW otherwise), so there is nothing to
// stage - attaching it is one dg::Window::create().
struct NodeSlot {
  enum class State : std::uint8_t { kPending, kLive, kRemoved };

  State state = State::kPending;
  std::uint16_t type_id = 0;

  // Meaningful only when state == kLive.
  std::size_t window_index = 0;
  dg::NodeId node_id{};

  // The SAME pointer dg_node_create() returned, reused verbatim by every
  // dg_event naming this node - a fresh wrapper here would still compare
  // equal in content but not in address, and a host is entitled to compare
  // handles by pointer identity.
  dg_node_t* self_handle = nullptr;
};

// One dg_window_t: the runtime window (owned by the app's dg::App) and the
// handle every dg_event naming it carries.
struct WindowImpl {
  dg::Window* window = nullptr;
  dg_window_t* self_handle = nullptr;
  std::size_t self_index = 0;
};

// 7-6: one loaded theme, and (if it came from dg_theme_load_dir(), or from
// dg_theme_load_memory() with a non-null base_dir) the untrusted package
// directory it can still read resources from. Shared with the runtime while
// active; dg_theme_set_variant()/dg_theme_override() on the active theme
// hand it to dg::App::set_theme() again, which re-applies it.
struct ThemeImpl {
  std::shared_ptr<dg::Theme> theme;
  dg::ThemeVariant variant = dg::ThemeVariant::kLight;
  std::optional<dg::ThemePackage> package;

  dg_theme_t* self_handle = nullptr;
};

// One dg_app_t: the runtime dg::App, and the append-only arenas this ABI's
// handles index into.
class AppImpl {
 public:
  explicit AppImpl(dg::App runtime) : app(std::move(runtime)) {}

  dg::App app;
  std::vector<std::unique_ptr<WindowImpl>> windows;
  std::vector<NodeSlot> nodes;

  // 7-6: every dg_theme_t this app has ever loaded (append-only, the same
  // shape `nodes`/`windows` already are), and which one (if any) is
  // currently active - dg_app_set_theme()'s own state. A theme that is
  // never made active still lives here; nothing about loading one commits
  // an app to using it.
  std::vector<std::unique_ptr<ThemeImpl>> themes;
  ThemeImpl* active_theme = nullptr;

  // dg_poll_events()'s queue - design.md section 5.8 decision 2's mandatory
  // dequeue mode: every dg::Event the runtime queued, converted to a
  // dg_event (drain_events()), drained by poll_events(); never delivered
  // any other way (dg_set_event_callback is this ABI's own declined item -
  // see abi/drawgui.def.toml's own header).
  std::vector<dg_event> pending_events;
};

}  // namespace dg::abi
