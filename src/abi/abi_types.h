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
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/render_tree.h"
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

  dg_window_t* self_handle = nullptr;
  std::size_t self_index = 0;
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

  // dg_poll_events()'s queue - design.md section 5.8 decision 2's mandatory
  // dequeue mode. Filled by wait_events()'s internal pump, drained by
  // poll_events(); never delivered any other way (dg_set_event_callback is
  // this slice's own declined item - see abi/drawgui.def.toml's header).
  std::vector<dg_event> pending_events;
};

}  // namespace dg::abi
