#include "drawgui/window/popup_host.h"

#include <algorithm>
#include <utility>

namespace dg {

// The whole of the placement decision: prefer `preferred`, flip to the other
// side when the preferred one would run past parent_height. Horizontal
// clamping/flipping is declined - see popup_host.h's own PopupPlacement
// comment. Public (not file-local) so examples/14_popup's headless oracle
// and tests/unit/test_popup.cpp can pin it directly, independent of
// WindowManager/SDL.
PixelRect resolve_popup_placement(const PixelRect& anchor, PixelSize content,
                                  PopupPlacement preferred, int parent_height) {
  const bool below_fits_content = anchor.bottom() + content.height <= parent_height;
  const bool above_fits_content = anchor.top() - content.height >= 0;

  bool use_below = preferred == PopupPlacement::kBelow;
  if (use_below && !below_fits_content && above_fits_content) {
    use_below = false;
  } else if (!use_below && !above_fits_content && below_fits_content) {
    use_below = true;
  }

  const int top = use_below ? anchor.bottom() : anchor.top() - content.height;
  return PixelRect{anchor.left(), top, content.width, content.height};
}

PopupHost::PopupHost(WindowManager& windows) : windows_(&windows) {}

PopupHost::NativePopup* PopupHost::find_native(WindowId window) {
  const auto entry =
      std::find_if(native_popups_.begin(), native_popups_.end(),
                   [window](const NativePopup& popup) { return popup.window == window; });
  return entry == native_popups_.end() ? nullptr : &*entry;
}

Expected<PopupHandle, WindowError> PopupHost::show(WindowId parent_window,
                                                   RenderTree& parent_tree, PlatformCaps caps,
                                                   const PixelRect& anchor_rect,
                                                   PixelSize content_size,
                                                   PopupPlacement preferred, PopupFlags flags,
                                                   PopupWindowKind kind) {
  (void)flags;
  const Expected<PixelSize, WindowError> parent_size = windows_->drawable_size(parent_window);
  if (!parent_size) {
    return Unexpected{parent_size.error()};
  }
  const PixelRect resolved =
      resolve_popup_placement(anchor_rect, content_size, preferred, parent_size.value().height);

  if (caps.native_popup) {
    const Expected<WindowId, WindowError> popup_window =
        windows_->open_popup(parent_window, resolved.left(), resolved.top(), content_size.width,
                             content_size.height, kind);
    if (!popup_window) {
      return Unexpected{popup_window.error()};
    }

    TreeSpec spec;
    spec.viewport = content_size;
    native_popups_.push_back(
        NativePopup{popup_window.value(), std::make_unique<RenderTree>(spec)});

    PopupHandle handle;
    handle.open = true;
    handle.is_native = true;
    handle.window = popup_window.value();
    handle.tree = native_popups_.back().tree.get();
    handle.content_root = RenderTree::root();
    handle.content_bounds = PixelRect{0, 0, content_size.width, content_size.height};
    return handle;
  }

  // Overlay branch: one clipping container, appended directly to the
  // parent's RenderTree (never through its LayoutTree - see popup_host.h's
  // own header comment on why that is safe). Confined to the parent
  // window's own surface by construction: it is a node of that surface's
  // tree, so it is clipped to the window the same way any other node is.
  NodeStyle container_style;
  container_style.overflow = Overflow::kClip;
  const NodeId container = parent_tree.add_child(RenderTree::root(), resolved, container_style);

  PopupHandle handle;
  handle.open = true;
  handle.is_native = false;
  handle.tree = &parent_tree;
  handle.content_root = container;
  handle.content_bounds = resolved;
  return handle;
}

void PopupHost::close(PopupHandle& handle) {
  if (!handle.open) {
    return;
  }
  if (handle.is_native) {
    windows_->close_popup(handle.window);
    const auto entry = std::find_if(
        native_popups_.begin(), native_popups_.end(),
        [&handle](const NativePopup& popup) { return popup.window == handle.window; });
    if (entry != native_popups_.end()) {
      native_popups_.erase(entry);
    }
  } else {
    // Clip the container to nothing rather than deleting it - RenderTree is
    // append-only. See close()'s own header comment for the accepted cost.
    handle.tree->set_local_bounds(handle.content_root, PixelRect{});
  }
  handle.open = false;
  handle.tree = nullptr;
}

bool PopupHost::handle_pointer(PopupHandle& handle, WindowId event_window,
                               const PointerEvent& event, PopupFlags flags) {
  if (!handle.open || !flags.dismiss_on_click_outside || event.action != PointerAction::kDown) {
    return false;
  }

  if (handle.is_native) {
    if (event_window == handle.window) {
      return false;
    }
    close(handle);
    return true;
  }

  // Overlay: only the parent window's own events can name a position inside
  // or outside content_bounds. A kDown on any OTHER window (impossible for
  // an overlay, which has none of its own, but a click on a DIFFERENT
  // top-level window while this one is merely unfocused) is outside by
  // definition.
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
  if (!handle.open || !flags.dismiss_on_escape || event.action != KeyAction::kDown) {
    return false;
  }
  if (event.key != Key::kEscape) {
    return false;
  }
  if (handle.is_native && event_window != handle.window) {
    return false;
  }
  close(handle);
  return true;
}

}  // namespace dg
