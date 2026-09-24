// PopupHost - the abstraction design.md section 5.2 calls the single most
// critical decision in the whole design, and doc/form-controls.md section 2
// declined Dropdown for the want of.
//
// Widgets never create windows. They ask PopupHost to show content at an
// anchor, and PopupHost appends an overlay node to the CALLER's own
// RenderTree (the parent window's) - it never opens a second OS window.
//
// OVERLAY-ONLY, by decision (see doc/popup.md): a prior slice also supported
// a native-OS-popup branch (SDL_CreatePopupWindow, escaping the parent
// window's own bounds), but nothing in this codebase's own placement logic
// ever used the one capability that branch had and the overlay branch does
// not (resolve_popup_placement() below's own fit test is against the parent
// window's drawable size, never the screen's - true before and after this
// function grew a horizontal flip/clamp to match its vertical one), the
// native branch could not be exercised at all under the dummy video driver
// headless tests run under, and every defect this round
// found (font inheritance, expose repaint, focus-ring overflow) existed only
// in that branch. It has been deleted rather than fixed. A caller that later
// needs content to escape its own parent window's bounds is a new,
// independently-justified feature, not a resurrection of this one.
//
// NO VIRTUAL, matching the whole codebase (grep -rn virtual src/ include/
// finds only comments explaining its absence).
//
// A STRUCTURAL SAFETY CONSTRAINT THIS TYPE NOW GUARDS: overlay content is
// appended as ordinary children of the caller's own RenderTree, added
// directly through RenderTree::add_child() rather than through the caller's
// LayoutTree - verified safe because LayoutTree::layout walks LayoutTree::
// Impl's own node vector (sized to what IT created), never
// RenderTree::node_count(), so a RenderTree-only child sits outside
// layout's bookkeeping without corrupting it - PROVIDED the caller's
// LayoutTree is never asked to lay out again while an overlay popup added
// this way is open (doing so has never been exercised, and the reason it
// happens to be safe today - index parity by construction, not a checked
// invariant - is fragile enough to name rather than rely on silently).
// Overlay-only makes this the ONLY way content ever reaches PopupHost, so
// PopupHost now tracks whether it currently has an overlay open
// (has_open_overlay(), maintained by show()/close()) so a caller CAN check
// before calling layout()/layout_full() again. This is a checkable fact a
// caller can consult, not an enforced assertion inside LayoutTree/RenderTree
// itself - doing that would mean changing either class's public API, which
// is out of scope here. TODO: consider having LayoutTree::layout() itself
// refuse (or PopupHost proactively close) rather than leaving this as a
// contract the caller must uphold.

#pragma once

#include <cstdint>
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
// Centred/corner variants are declined by name in doc/popup.md; a left/right
// PLACEMENT PREFERENCE is too (the popup always starts left-aligned to the
// anchor), but that is unrelated to resolve_popup_placement() itself now
// flipping/clamping horizontally when left-alignment would not fit - see
// this header's own comment on that function.
enum class PopupPlacement : std::uint8_t {
  kBelow,
  kAbove,
};

struct PopupFlags {
  bool dismiss_on_click_outside = true;
  bool dismiss_on_escape = true;
};

// The placement math PopupHost::show() applies, exposed standalone so a
// headless check can pin it directly against hand-derived rectangles
// without needing a WindowManager at all. `parent_size` is the parent
// window's current drawable size. Vertically: prefers `preferred`, flips to
// the other side when that would not fit, then clamps into
// [0, parent_size.height - content.height] if even the flipped side runs
// over (top-aligns instead if content is taller than the parent itself, see
// this function's own definition comment). Horizontally: prefers
// `anchor`'s left edge, flips to align with `anchor`'s right edge when that
// would not fit, then clamps/left-aligns by the same rule. `content`'s own
// size is unchanged in every case - only its position moves.
[[nodiscard]] PixelRect resolve_popup_placement(const PixelRect& anchor, PixelSize content,
                                                PopupPlacement preferred, PixelSize parent_size);

// What show() actually did, and everything a caller needs to build content
// into it and later route events to it. Overlay-only: there is no second
// RenderTree here and no window id - build content into `content_root` using
// the same RenderTree you passed to show().
struct PopupHandle {
  bool open = false;

  // The clipping container PopupHost added to the caller's own RenderTree
  // (the one passed to show()), positioned at `content_bounds`'s absolute
  // location. Content is built at LOCAL origin (0, 0) relative to this node.
  NodeId content_root;

  // The popup's resolved position and size, in the parent window's own
  // physical-pixel coordinates, after placement/flip. What close() clips
  // back to empty to remove the popup without erasing the append-only nodes
  // it added.
  PixelRect content_bounds;
};

class PopupHost {
 public:
  explicit PopupHost(WindowManager& windows);

  // Computes placement (kBelow, flipped to kAbove if it would run past
  // `parent_window`'s current drawable height), then appends a clipping
  // container node to `parent_tree`, confined to `parent_window`'s own
  // surface - it cannot escape the parent's bounds, which is the accepted
  // limitation this overlay-only design keeps (see this header's own top
  // comment).
  //
  // `anchor_rect` and the returned handle's `content_bounds` are both in
  // `parent_window`'s own physical-pixel coordinates.
  //
  // Only one overlay popup may be open through this PopupHost at a time
  // while the caller's LayoutTree is not re-laid-out - see this header's own
  // top comment on the LayoutTree safety constraint. has_open_overlay()
  // lets a caller check before calling layout()/layout_full() again.
  [[nodiscard]] Expected<PopupHandle, WindowError> show(
      WindowId parent_window, RenderTree& parent_tree, const PixelRect& anchor_rect,
      PixelSize content_size, PopupPlacement preferred, PopupFlags flags);

  // Tears down the overlay `handle` names and marks it closed. Idempotent:
  // closing an already-closed handle does nothing.
  //
  // Clips the container added by show() to an empty rectangle, which
  // removes it from painting (Overflow::kClip on an empty box) and from hit
  // testing (doc/clipping.md: "a clipping node with no area removes its
  // whole subtree and is itself hittable nowhere") without deleting any
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
  // Click-outside: a kDown on the popup's parent window in a position
  // outside content_bounds dismisses it - there is no popup window of its
  // own to distinguish "outside" any other way. This is the plumbing
  // design.md section 5.2 asks for ("all input events carry window_id"),
  // already present on PointerEvent/KeyEvent - see doc/popup.md for the
  // finding that nothing new had to be added there.
  [[nodiscard]] bool handle_pointer(PopupHandle& handle, WindowId event_window,
                                    const PointerEvent& event, PopupFlags flags);

  // Escape dismissal. Reaches the popup whenever the parent window has
  // focus, since there is no second window to route through.
  [[nodiscard]] bool handle_key(PopupHandle& handle, WindowId event_window,
                                const KeyEvent& event, PopupFlags flags);

  // Whether this PopupHost currently has an overlay popup open - the check
  // a caller should make before calling layout()/layout_full() again on the
  // RenderTree an open overlay's content was appended to (see this header's
  // own top comment on why that call is unsafe while one is open).
  [[nodiscard]] bool has_open_overlay() const { return has_open_overlay_; }

 private:
  WindowManager* windows_ = nullptr;
  bool has_open_overlay_ = false;

  // The RenderTree the currently-open overlay's container was added to -
  // PopupHandle itself carries no RenderTree pointer (see this header's own
  // top comment: a handle that just echoes back the tree its own caller
  // passed in is a redundant field), so close() reaches the container
  // through this instead. Valid only while has_open_overlay_ is true; this
  // is also what enforces "at most one overlay open at a time" as an actual
  // fact about this type rather than only a documented caller obligation.
  RenderTree* open_tree_ = nullptr;
};

}  // namespace dg
