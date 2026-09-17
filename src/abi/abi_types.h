// The C++-side state behind every opaque ABI handle.
//
// Every dg_*_t the host holds is a pointer to a tiny wrapper - {app, index},
// or just {index} for dg_app_t itself - allocated once by the function that
// hands it out and NEVER FREED. That is the whole of the handle-validation
// design (doc/abi.md section 4): dereferencing a stale handle always reads
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
// layer down, and doc/theme.md's ThemeBindings comment already worked out
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
#include "drawgui/anim/animation_engine.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/shortcuts/action_scopes.h"
#include "drawgui/theme/theme.h"
#include "drawgui/theme/theme_bindings.h"
#include "drawgui/theme/theme_package.h"
#include "drawgui/widget/focus.h"
#include "drawgui/widget/interaction.h"
#include "drawgui/widget/widget_set.h"
#include "drawgui/window/window_manager.h"

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
// dg_node_remove() is called - see node_props.h's PropWrite/set_prop() for
// why a LIVE node is required before dg_node_set_prop() can do anything: a
// property write needs a real LayoutTree& and NodeId, and a pending node has
// neither yet.
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

// One open window: the SDL-backed id window_manager.h already owns, and the
// layout/render/widget/interaction state this slice's ABI drives through it
// - the same four objects examples/11_form_controls's own Runner and Scene
// hold, generalised from "one example, one window" to "one app, N windows".
class WindowImpl {
 public:
  explicit WindowImpl(const dg::TreeSpec& spec) : tree(spec) {}

  dg::WindowId sdl_window;
  dg::LayoutTree tree;
  dg::WidgetSet widgets;
  dg::Interaction interaction;
  std::optional<dg::RasterSurface> surface;
  bool closed = false;

  // Set once by dg_window_set_root(); a second call is DG_ERR_ALREADY_ATTACHED.
  std::optional<dg::NodeId> root;

  // 7-6: this window's own $token binding side table (doc/theme.md's
  // ThemeBindings, unchanged) - one per window, mirroring the fact that a
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
  // one anywhere else blurs) - process_pointer_event() below is this ABI's
  // one caller of that rule. `action_scopes` backs dg_node_scope_action().
  dg::Focus focus;
  dg::ActionScopes action_scopes;

  // 8-5: this window's own AnimationEngine, present for exactly one
  // reason - dg_node_remove()'s on_node_removed() call requires one as a
  // NON-optional reference parameter (src/render/node_lifecycle.h's own
  // header comment explains why cleanup takes every side table this way).
  // There is still no animation ABI (section 6 of doc/abi.md declines
  // dg_animate/dg_node_set_transition by name, unchanged by this slice),
  // so nothing else in this file ever calls animate()/set_transition() on
  // this instance - it only ever receives cancel_all_for() calls, which
  // are no-ops against an engine with no slots.
  dg::AnimationEngine animation;

  dg_window_t* self_handle = nullptr;
  std::size_t self_index = 0;
};

// 7-6: one loaded theme, and (if it came from dg_theme_load_dir(), or from
// dg_theme_load_memory() with a non-null base_dir) the untrusted package
// directory it can still read resources from.
struct ThemeImpl {
  dg::Theme theme;
  dg::ThemeVariant variant = dg::ThemeVariant::kLight;
  std::optional<dg::ThemePackage> package;

  dg_theme_t* self_handle = nullptr;
};

// One dg_app_t: the window manager every one of its windows shares (SDL3's
// own multi-window design already supports this - examples/01_sdl3_multi_window
// is the precedent), and the two append-only arenas this ABI's handles index
// into.
class AppImpl {
 public:
  explicit AppImpl(dg::WindowManager manager) : wm(std::move(manager)) {}

  dg::WindowManager wm;
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
  // dequeue mode. Filled by wait_events()'s internal pump, drained by
  // poll_events(); never delivered any other way (dg_set_event_callback is
  // this slice's own declined item - see abi/drawgui.def.toml's own header).
  std::vector<dg_event> pending_events;
};

}  // namespace dg::abi
