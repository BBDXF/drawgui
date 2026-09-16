// PopupHost - the abstraction design.md section 5.2 calls the single most
// critical decision in the whole design, and doc/form-controls.md section 2
// declined Dropdown for the want of.
//
// Widgets never create windows. They ask PopupHost to show content at an
// anchor, and PopupHost decides the real form:
//
//   PopupHost::show(...) -> caps.native_popup ? a real Popup window
//                                              : an overlay node in the
//                                                caller's own RenderTree
//
// BOTH BRANCHES ARE REAL, on purpose. design.md's argument for the whole
// abstraction is that it survives the desktop/mobile split; a branch that has
// never run has never been falsified, which is the same "twelve speculative
// platform headers" trap this project's own README already names. `caps` is
// therefore a VALUE passed in, not queried internally - WindowManager::
// platform_caps() answers truthfully for this SDL3/Linux backend
// (native_popup == true), and a caller that wants to exercise the overlay
// branch on this same desktop constructs its own PlatformCaps{false} instead
// of asking the platform. That is how examples/14_popup drives both branches
// on one machine without a second platform or a fake one.
//
// NO VIRTUAL, matching the whole codebase (grep -rn virtual src/ include/
// finds only comments explaining its absence). The platform branch here is
// an ordinary runtime if/else on a bool field, exactly the technique
// WindowManager's own SDL3 backend already uses to keep a single concrete
// class free of an abstract base nothing has contradicted yet.
//
// THE SECOND-ROOT QUESTION, decided: a popup shown through the native branch
// gets its own RenderTree instance - PopupHost owns one per open native
// popup, because a second OS window needs a second SkSurface/RasterSurface
// and RenderTree::root() is always NodeId{0}, so two independent surfaces
// cannot share one RenderTree. This is a second INSTANCE of the existing
// class, never a new tree TYPE: WidgetSet's two-tree invariant (layout +
// render, no third) is unaffected, because nothing here adds a node kind, a
// RenderObject, or a table WidgetSet would need to grow a case for.
//
// The overlay branch needs no second instance at all: its content is
// appended as ordinary children of the CALLER's own RenderTree (the parent
// window's), added directly through RenderTree::add_child() rather than
// through the caller's LayoutTree - verified safe because LayoutTree::layout
// walks LayoutTree::Impl's own node vector (sized to what IT created), never
// RenderTree::node_count(), so a RenderTree-only child sits outside layout's
// bookkeeping without corrupting it. The caller's LayoutTree must not be
// asked to lay out again while a popup added this way is open, because doing
// so has never been exercised and the reason it happens to be safe today
// (index parity by construction, not by a checked invariant) is fragile
// enough to name rather than rely on silently - see doc/popup.md.

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "drawgui/base/expected.h"
#include "drawgui/base/pixel_geometry.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/window/window_manager.h"

namespace dg {

// Where the popup sits relative to `anchor_rect`. Two values, not the full
// CSS placement vocabulary (design.md never specifies one for PopupHost):
// this is the minimum the task can prove with a hand-derived pixel oracle -
// the flip from one to the other when the preferred side would not fit.
// Left/right flipping and centred/corner variants are declined by name in
// doc/popup.md.
enum class PopupPlacement : std::uint8_t {
  kBelow,
  kAbove,
};

struct PopupFlags {
  bool dismiss_on_click_outside = true;
  bool dismiss_on_escape = true;
};

// The below/above flip math PopupHost::show() applies, exposed standalone so
// a headless check can pin it directly against hand-derived rectangles
// without needing a WindowManager at all. `parent_height` is the parent
// window's current drawable height; the popup keeps `anchor`'s left edge and
// `content`'s own size in every case - only the vertical side flips.
[[nodiscard]] PixelRect resolve_popup_placement(const PixelRect& anchor, PixelSize content,
                                                PopupPlacement preferred, int parent_height);

// What show() actually did, and everything a caller needs to build content
// into it and later route events to it.
struct PopupHandle {
  bool open = false;
  bool is_native = false;

  // Native only. Where the popup actually lives, for close_popup() and for
  // telling one window's events apart from another's in pump()'s results.
  WindowId window;

  // The tree to build content into - PopupHost's own owned tree for the
  // native branch, or the caller's tree (the one passed to show()) for the
  // overlay branch. Never null while `open` is true.
  RenderTree* tree = nullptr;

  // The node under which content was built. For the native branch this is
  // always RenderTree::root() of the popup's own tree - content occupies
  // the whole popup window, at LOCAL origin (0, 0). For the overlay branch
  // this is a clipping container PopupHost added to the caller's tree,
  // positioned at `content_bounds`'s absolute location; content is still
  // built at LOCAL origin (0, 0) relative to `content_root`, which is what
  // makes the same content-building code produce the same local geometry in
  // both branches - the equivalence examples/14_popup's oracle checks.
  NodeId content_root;

  // The popup's own size, in both branches - what a caller needing to build
  // content sized to it reads, and (overlay only) what close() clips back to
  // empty to remove the popup without erasing the append-only nodes it
  // added. Native: (0, 0, width, height). Overlay: the resolved position in
  // the parent window's own coordinates, after placement/flip.
  PixelRect content_bounds;
};

class PopupHost {
 public:
  explicit PopupHost(WindowManager& windows);

  // Computes placement (kBelow, flipped to kAbove if it would run past
  // `parent_window`'s current drawable height), then dispatches on
  // `caps.native_popup`:
  //
  //   native   opens a real popup window via WindowManager::open_popup(),
  //            and owns a fresh RenderTree for it. Repainting and
  //            presenting that window is the caller's job, through its own
  //            RasterSurface, exactly the same shape every other window in
  //            this codebase is already drawn through (PopupHandle::window
  //            plus PopupHandle::tree is everything that loop needs).
  //   overlay  appends a clipping container node to `parent_tree`, confined
  //            to `parent_window`'s own surface - it cannot escape the
  //            parent's bounds, which is the accepted mobile-shaped
  //            limitation design.md names for this branch.
  //
  // `anchor_rect` and the returned handle's `content_bounds` are both in
  // `parent_window`'s own physical-pixel coordinates.
  //
  // `kind` defaults to kMenu (SDL_WINDOW_POPUP_MENU on the native branch) -
  // every existing caller of show() keeps getting exactly the window it
  // always has, unchanged. kTooltip (SDL_WINDOW_TOOLTIP) is 7-5b's own
  // addition (doc/menus.md section 6.2): it accepts NO input at all on the
  // native branch, which is why a Tooltip's own lifecycle is NOT driven
  // through handle_pointer()/handle_key() below - a window that receives no
  // input can never produce the click/Escape those two read, so a caller
  // showing a tooltip manages show/hide itself (from hover-timer state,
  // include/drawgui/widget/tooltip.h) and calls close() directly. The
  // overlay branch reads `kind` for nothing today - an appended RenderTree
  // node has no SDL flag of its own to choose - so the two branches are not
  // symmetrical here on purpose: there is nothing yet for the overlay side
  // to differ ON.
  [[nodiscard]] Expected<PopupHandle, WindowError> show(
      WindowId parent_window, RenderTree& parent_tree, PlatformCaps caps,
      const PixelRect& anchor_rect, PixelSize content_size, PopupPlacement preferred,
      PopupFlags flags, PopupWindowKind kind = PopupWindowKind::kMenu);

  // Tears down whichever branch `handle` names and marks it closed.
  // Idempotent: closing an already-closed handle does nothing.
  //
  // Native: destroys the real OS window and drops its owned RenderTree - a
  // genuine, complete teardown.
  //
  // Overlay: clips the container added by show() to an empty rectangle,
  // which removes it from painting (Overflow::kClip on an empty box) and
  // from hit testing (doc/clipping.md: "a clipping node with no area removes
  // its whole subtree and is itself hittable nowhere") without deleting any
  // node - RenderTree is append-only (doc/widgets.md), so the content nodes
  // this popup added are never reclaimed. That is a real, permanent
  // per-open cost this slice accepts and names rather than hides; repeatedly
  // opening and closing an overlay popup leaks a handful of dead nodes every
  // time, exactly as repeatedly building any other never-removed widget
  // would.
  void close(PopupHandle& handle);

  // Routes one pump() pointer event for dismissal. Returns true when this
  // call closed `handle` (so the caller stops treating it as open and
  // refreshes whatever UI depended on it being there).
  //
  // Click-outside: a kDown on any window OTHER than the popup's own (native)
  // or the popup's parent window in a position outside content_bounds
  // (overlay - since the popup has no window of its own to distinguish
  // "outside" that way) dismisses it. This is the plumbing design.md section
  // 5.2 asks for ("all input events carry window_id") already present on
  // PointerEvent/KeyEvent before this slice - see doc/popup.md for the
  // finding that nothing new had to be added there.
  [[nodiscard]] bool handle_pointer(PopupHandle& handle, WindowId event_window,
                                    const PointerEvent& event, PopupFlags flags);

  // Escape dismissal. Native: only reaches the popup while it holds keyboard
  // focus (SDL_WINDOW_POPUP_MENU can gain it - see open_popup()'s own
  // comment). Overlay: reaches it whenever the parent window has focus,
  // since there is no second window to route through.
  [[nodiscard]] bool handle_key(PopupHandle& handle, WindowId event_window,
                                const KeyEvent& event, PopupFlags flags);

 private:
  struct NativePopup {
    WindowId window;
    std::unique_ptr<RenderTree> tree;
  };

  WindowManager* windows_ = nullptr;
  std::vector<NativePopup> native_popups_;

  [[nodiscard]] NativePopup* find_native(WindowId window);
};

}  // namespace dg
