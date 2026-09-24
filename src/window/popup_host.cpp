#include "drawgui/window/popup_host.h"

namespace dg {

// The whole of the placement decision: prefer `preferred` vertically and
// left-alignment horizontally, flip either axis to the other side when the
// preferred one would run past the parent's own drawable size, and clamp
// whatever is left over once both flips have been tried. This used to be
// vertical-only, with horizontal placement documented as a declined
// capability (doc/popup.md) - overlay-only made that untenable: a native
// popup window could still be dragged/repositioned by the OS to stay
// on-screen, but an overlay node is confined to the parent window's own
// surface by construction (see popup_host.h's own top comment), so if this
// function does not keep the whole rectangle inside parent_size, nothing
// downstream of it ever will. Public (not file-local) so
// examples/14_popup's headless oracle and tests/unit/test_popup.cpp can pin
// it directly, independent of WindowManager/SDL.
PixelRect resolve_popup_placement(const PixelRect& anchor, PixelSize content,
                                  PopupPlacement preferred, PixelSize parent_size) {
  // --- Vertical: below/above flip, then clamp. ---
  const bool below_fits_content = anchor.bottom() + content.height <= parent_size.height;
  const bool above_fits_content = anchor.top() - content.height >= 0;

  bool use_below = preferred == PopupPlacement::kBelow;
  if (use_below && !below_fits_content && above_fits_content) {
    use_below = false;
  } else if (!use_below && !above_fits_content && below_fits_content) {
    use_below = true;
  }

  int top = use_below ? anchor.bottom() : anchor.top() - content.height;
  if (content.height >= parent_size.height) {
    // Neither edge of the parent leaves room for the whole content: the
    // clamp interval [0, parent_size.height - content.height] would be
    // empty or inverted. Top-align instead of producing a negative or
    // flipped rectangle - the content will overflow the parent's bottom
    // edge either way, and top-align keeps at least its own top in view.
    top = 0;
  } else if (top < 0) {
    top = 0;
  } else if (top + content.height > parent_size.height) {
    top = parent_size.height - content.height;
  }

  // --- Horizontal: left/right-aligned-to-anchor flip, then clamp. Mirrors
  // the vertical shape above on purpose (see this function's own top
  // comment). ---
  const bool left_fits_content = anchor.left() + content.width <= parent_size.width;
  const bool right_fits_content = anchor.right() - content.width >= 0;

  // Left-alignment (left = anchor.left()) is the only preference this type
  // is ever asked for horizontally - PopupPlacement has no left/right value
  // (declined by name in doc/popup.md) - so it is always the starting
  // preference here, unconditionally, unlike the vertical axis where
  // `preferred` picks the starting side.
  bool use_left = true;
  if (use_left && !left_fits_content && right_fits_content) {
    use_left = false;
  } else if (!use_left && !right_fits_content && left_fits_content) {
    use_left = true;
  }

  int left = use_left ? anchor.left() : anchor.right() - content.width;
  if (content.width >= parent_size.width) {
    // Same reasoning as the vertical case: the clamp interval is empty or
    // inverted, so left-align instead of producing a negative left edge.
    left = 0;
  } else if (left < 0) {
    left = 0;
  } else if (left + content.width > parent_size.width) {
    left = parent_size.width - content.width;
  }

  return PixelRect{left, top, content.width, content.height};
}

PopupHost::PopupHost(WindowManager& windows) : windows_(&windows) {}

Expected<PopupHandle, WindowError> PopupHost::show(WindowId parent_window,
                                                   RenderTree& parent_tree,
                                                   const PixelRect& anchor_rect,
                                                   PixelSize content_size,
                                                   PopupPlacement preferred, PopupFlags flags) {
  (void)flags;
  const Expected<PixelSize, WindowError> parent_size = windows_->drawable_size(parent_window);
  if (!parent_size) {
    return Unexpected{parent_size.error()};
  }
  const PixelRect resolved =
      resolve_popup_placement(anchor_rect, content_size, preferred, parent_size.value());

  // One clipping container, appended directly to the parent's RenderTree
  // (never through its LayoutTree - see popup_host.h's own header comment on
  // why that is safe only while at most one overlay is open). Confined to
  // the parent window's own surface by construction: it is a node of that
  // surface's tree, so it is clipped to the window the same way any other
  // node is.
  NodeStyle container_style;
  container_style.overflow = Overflow::kClip;
  const NodeId container = parent_tree.add_child(RenderTree::root(), resolved, container_style);

  PopupHandle handle;
  handle.open = true;
  handle.content_root = container;
  handle.content_bounds = resolved;
  has_open_overlay_ = true;
  open_tree_ = &parent_tree;
  return handle;
}

void PopupHost::close(PopupHandle& handle) {
  if (!handle.open) {
    return;
  }
  // Clip the container to nothing rather than deleting it - RenderTree is
  // append-only. See close()'s own header comment for the accepted cost.
  if (open_tree_ != nullptr) {
    open_tree_->set_local_bounds(handle.content_root, PixelRect{});
  }
  handle.open = false;
  has_open_overlay_ = false;
  open_tree_ = nullptr;
}

bool PopupHost::handle_pointer(PopupHandle& handle, WindowId event_window,
                               const PointerEvent& event, PopupFlags flags) {
  (void)event_window;
  if (!handle.open || !flags.dismiss_on_click_outside || event.action != PointerAction::kDown) {
    return false;
  }

  // Overlay: only the parent window's own events can name a position inside
  // or outside content_bounds. A kDown on any OTHER window (a click on a
  // DIFFERENT top-level window while this one is merely unfocused) is
  // outside by definition.
  const PixelPoint at{event.x, event.y};
  const bool inside = contains(handle.content_bounds, at);
  if (inside) {
    return false;
  }
  close(handle);
  return true;
}

bool PopupHost::handle_key(PopupHandle& handle, WindowId event_window, const KeyEvent& event,
                           PopupFlags flags) {
  (void)event_window;
  if (!handle.open || !flags.dismiss_on_escape || event.action != KeyAction::kDown) {
    return false;
  }
  if (event.key != Key::kEscape) {
    return false;
  }
  close(handle);
  return true;
}

}  // namespace dg
