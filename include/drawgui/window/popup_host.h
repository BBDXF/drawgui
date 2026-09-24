// PopupHost - the abstraction design.md section 5.2 calls the single most
// critical decision in the whole design, and the want of it is what
// declined Dropdown for.
//
// Widgets never create windows. They ask PopupHost to show content at an
// anchor, and PopupHost appends an overlay node to the CALLER's own
// RenderTree (the parent window's) - it never opens a second OS window.
//
// OVERLAY-ONLY, by decision: a prior slice also supported
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
// LAYOUT SAFETY, CORRECTED: overlay content is appended as ordinary children
// of the caller's own RenderTree, added directly through
// RenderTree::add_child() rather than through the caller's LayoutTree - that
// structural fact stands. What used to be claimed on top of it did not: this
// comment previously told callers they must NOT call layout()/layout_full()
// again while an overlay popup was open, and gave "the nodes vector's size
// only covers what LayoutTree itself created" as the reason. That reason is
// false - LayoutTree::add_child() does `nodes.resize(id.index + 1)`
// (layout_tree.cpp), so the nodes vector routinely has holes once a
// RenderTree-only child (like an overlay container) exists at a higher
// index. The actual safety property is a different one, and it has been
// verified: LayoutTree::Impl::run() only ever walks from `dirty`, and every
// entry ever pushed onto `dirty` is reached by descending LayoutNode::
// children - never by RenderTree::node_count() or any other index-derived
// bound. An overlay container added through RenderTree::add_child() never
// appears in any LayoutNode::children list, so no dirty walk - incremental
// or layout_full()'s "mark everything dirty then still walk from root" -
// can ever reach it. It is exactly as inert to layout as it needs to be.
// Like focus.h's own corrected comment on the focus ring, this is named
// rather than quietly fixed: the removed rule and its false justification
// are worth keeping on record.
//
// CONSEQUENTLY: a caller MAY call layout()/layout_full() freely while any
// number of overlay popups are open. A single PopupHost may also hold any
// number of concurrently-open PopupHandles - each is self-contained (see
// PopupHandle below) and none of them interferes with any other.
//
// A CONSTRAINT FOR FUTURE MAINTAINERS, not for today's callers: the safety
// argument above depends on overlay containers being added through
// RenderTree::add_child(), never LayoutTree::add_child(). If a future
// change routes overlay creation through the latter, the container becomes
// reachable from a dirty walk and the next layout()/layout_full() call will
// silently overwrite the position PopupHost placed it at via apply_bounds().
// Nothing here enforces that against such a change; it is recorded so
// whoever makes it has to reckon with it.

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
// Centred/corner variants are declined by name; a left/right
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
// into it and later route events to it. Self-contained: a PopupHost may have
// any number of these open at once (see this header's own top comment), and
// each carries everything close()/handle_pointer()/handle_key() need for
// itself - nothing is kept in the PopupHost instead.
struct PopupHandle {
  bool open = false;

  // The RenderTree the overlay container was added to (the same one the
  // caller passed to show()). This field EXISTED, was deleted (commit
  // 48c9cd3) on the grounds that it was a redundant echo of a tree the
  // caller already has its own reference to, and is restored here because
  // that reasoning did not survive contact with a second concurrently-open
  // handle: once the tree pointer moved from each handle onto PopupHost
  // itself, opening a second overlay overwrote the first handle's tree
  // pointer, and close() on the first handle silently no-oped (the bug this
  // restoration fixes). Recorded rather than silently re-added, per this
  // codebase's convention of keeping history rather than rewriting it.
  //
  // Validity: set by show(), read by close()/handle_pointer()/handle_key().
  // A PopupHandle does not extend the referenced RenderTree's lifetime -
  // it must still outlive every handle built against it, exactly as before
  // this field existed on PopupHandle.
  RenderTree* tree = nullptr;

  // The window show() placed this popup's content against. Compared against
  // the event's own window id by handle_pointer()/handle_key() (see their
  // own comments below on why that comparison was previously a no-op).
  WindowId window;

  // The clipping container PopupHost added to `tree`, positioned at
  // `content_bounds`'s absolute location. Content is built at LOCAL origin
  // (0, 0) relative to this node.
  NodeId content_root;

  // The popup's resolved position and size, in the parent window's own
  // physical-pixel coordinates, after placement/flip. What close() clips
  // back to empty to remove the popup without erasing the append-only nodes
  // it added.
  PixelRect content_bounds;

  // The flags this popup was shown with, captured once here so callers do
  // not have to remember to pass the same PopupFlags value again to every
  // later handle_pointer()/handle_key() call. Previously that repetition was
  // an unstated obligation on the caller with nothing in the API to enforce
  // or even suggest it.
  PopupFlags flags;
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
  // A PopupHost may have any number of handles open at once, each entirely
  // self-contained (see PopupHandle) - see this header's own top comment.
  [[nodiscard]] Expected<PopupHandle, WindowError> show(
      WindowId parent_window, RenderTree& parent_tree, const PixelRect& anchor_rect,
      PixelSize content_size, PopupPlacement preferred, PopupFlags flags);

  // Tears down the overlay `handle` names and marks it closed. Idempotent:
  // closing an already-closed handle does nothing.
  //
  // Clips the container added by show() to an empty rectangle, which
  // removes it from painting (Overflow::kClip on an empty box) and from hit
  // testing ("a clipping node with no area removes its
  // whole subtree and is itself hittable nowhere") without deleting any
  // node. This is a CHOICE, not something RenderTree forces: RenderTree has
  // supported real node removal since slice 8-5 (RenderTree::remove_child(),
  // render_tree.h) - this comment used to claim "RenderTree is
  // append-only", which stopped being true then. Using remove_child()
  // instead would invalidate content_root and every content node id the
  // caller holds under it, and would require the caller to thread
  // dg::on_node_removed()'s six side-table cleanups
  // (src/render/node_lifecycle.cpp) through afterward - a real decision that
  // needs an actual caller motivated to make it, not a default this type
  // picks for them. So clip-to-empty stays the default, and the cost stays
  // named: repeatedly opening and closing an overlay popup leaks a handful
  // of dead nodes every time, exactly as repeatedly building any other
  // never-removed widget would.
  void close(PopupHandle& handle);

  // Routes one pump() pointer event for dismissal. Returns true when this
  // call closed `handle` (so the caller stops treating it as open and
  // refreshes whatever UI depended on it being there). Uses the PopupFlags
  // `handle` was shown with (see PopupHandle::flags) - callers do not pass
  // flags again here.
  //
  // Click-outside: a kDown on the popup's own window (handle.window) in a
  // position outside content_bounds dismisses it - there is no popup window
  // of its own to distinguish "outside" any other way. A kDown on any OTHER
  // window is outside by definition, and is now actually checked as such:
  // this comment used to make that claim while the event's window id was
  // discarded with `(void)event_window` and only the coordinates were
  // compared, so a kDown on a different window whose coordinates happened
  // to fall inside content_bounds was wrongly treated as inside and left the
  // popup open. This is the plumbing design.md section 5.2 asks for ("all
  // input events carry window_id"), already present on PointerEvent/
  // KeyEvent - nothing new had to be
  // added there.
  [[nodiscard]] bool handle_pointer(PopupHandle& handle, WindowId event_window,
                                    const PointerEvent& event);

  // Escape dismissal. Reaches the popup only when the event's window matches
  // handle.window - this comment used to claim that ("Reaches the popup
  // whenever the parent window has focus") while, like handle_pointer()
  // above, discarding event_window with `(void)` and never checking it, so
  // Escape on any window closed the popup. Now checked for real.
  [[nodiscard]] bool handle_key(PopupHandle& handle, WindowId event_window, const KeyEvent& event);

 private:
  WindowManager* windows_ = nullptr;
};

}  // namespace dg
