// The internal frame loop.
//
// Mirrors examples/11_form_controls/form_window.cpp's Runner::resize_if_needed()
// + Runner::draw() exactly, generalised from one window to any window this
// app owns - design.md section 5.16.1's "宿主只声明" applies to painting too:
// a host never gets a canvas, it gets App::pump(), and drawgui paints and
// presents every frame internally.

#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "app/components/components.h"
#include "app/internal.h"

namespace dg::app_detail {
namespace {

WinState* find_window_by_sdl_id(AppState& app, dg::WindowId id) {
  for (const std::unique_ptr<dg::Window>& window : app.windows) {
    if (!window->impl().closed && window->impl().sdl_window == id) {
      return &window->impl();
    }
  }
  return nullptr;
}

void repaint_and_present(WinState& window) {
  if (!ensure_surface_current(window)) {
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
  relayout(window);
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
  (void)window.app.wm.present(window.sdl_window, image, window.tree.render().painted().rects());
}

dg::events::WindowStateEvent::Kind to_state_kind(dg::WindowEventKind kind) {
  using Kind = dg::events::WindowStateEvent::Kind;
  switch (kind) {
    case dg::WindowEventKind::kResized:
      return Kind::kResized;
    case dg::WindowEventKind::kMoved:
      return Kind::kMoved;
    case dg::WindowEventKind::kMinimized:
      return Kind::kMinimized;
    case dg::WindowEventKind::kMaximized:
      return Kind::kMaximized;
    case dg::WindowEventKind::kRestored:
      return Kind::kRestored;
    case dg::WindowEventKind::kFocusGained:
      return Kind::kFocusGained;
    case dg::WindowEventKind::kFocusLost:
      return Kind::kFocusLost;
    case dg::WindowEventKind::kEnteredFullscreen:
      return Kind::kEnteredFullscreen;
    case dg::WindowEventKind::kLeftFullscreen:
      return Kind::kLeftFullscreen;
  }
  return Kind::kResized;
}

// ABI 0.2. A resize needs nothing here beyond the event itself: process_pump()'s
// ensure_surface_current() pass has already rebuilt the surface at the new
// drawable size, the same path every resize always took.
void queue_window_events(AppState& app, const std::vector<dg::WindowEvent>& events) {
  for (const dg::WindowEvent& event : events) {
    const WinState* window = find_window_by_sdl_id(app, event.window);
    if (window == nullptr) {
      continue;
    }
    dg::events::WindowStateEvent out{window->sdl_window, to_state_kind(event.kind)};
    if (event.kind == dg::WindowEventKind::kResized) {
      out.x = event.width;
      out.y = event.height;
    } else if (event.kind == dg::WindowEventKind::kMoved) {
      out.x = event.x;
      out.y = event.y;
    }
    emit(app, out);
  }
}

// ABI 0.2: timers and cross-thread user events. Neither names a window.
void queue_timer_and_user_events(AppState& app, const dg::PumpResult& pumped) {
  for (const dg::TimerId timer : pumped.timers) {
    emit(app, dg::events::TimerEvent{timer});
  }
  for (const std::uint64_t payload : pumped.user_events) {
    emit(app, dg::events::UserEvent{payload});
  }
}

// After layout: text areas re-place their composed children every pump (as
// before); scroll views and lists re-derive their scrollbars, and lists
// their rows, after a pass that actually re-placed nodes.
void resync_widgets(WinState& window) {
  AppState& app = window.app;
  dg::RenderTree& tree = window.tree.render();
  const bool relaid = window.relaid;
  window.relaid = false;
  for (const NodeRecord* record : records_in_order(window)) {
    if (!window.widgets.has(record->id)) {
      continue;
    }
    if (record->kind == NodeKind::kTextArea && app.fonts.has_value()) {
      window.widgets.text_area_refresh(tree, *app.fonts, record->id);
    } else if (relaid && record->kind == NodeKind::kScrollView) {
      window.widgets.sync_scrollbar(tree, record->id, viewport_extent(window, record->id));
    } else if (relaid && record->kind == NodeKind::kList) {
      resync_list(window, *record);
    } else if (relaid && record->kind == NodeKind::kTextField &&
               window.focus.current() != record->id) {
      // An unfocused field's ellipsis depends on its laid-out width.
      refresh_editor_text(window, *record);
    }
    if (is_editor_kind(record->kind)) {
      sync_placeholder(window, *record);
    }
  }
  if (relaid) {
    // A layout pass puts every slider thumb back at its track's origin.
    window.widgets.resync_sliders(tree);
    components::sync(window);
  }
}

// Every runtime window is cancellable_close: the title-bar close arrives
// as a close request, and closes at once unless the app asked to confirm
// it. `closed` still names windows the platform closed by itself.
void process_closes(AppState& app, const dg::PumpResult& pumped) {
  for (const dg::WindowId closed_id : pumped.closed) {
    WinState* window = find_window_by_sdl_id(app, closed_id);
    if (window == nullptr) {
      continue;
    }
    window->closed = true;
    emit(app, dg::events::WindowClosedEvent{window->sdl_window});
  }

  for (const dg::WindowId requested : pumped.close_requested) {
    WinState* window = find_window_by_sdl_id(app, requested);
    if (window == nullptr) {
      continue;
    }
    if (window->close_confirmation) {
      emit(app, dg::events::CloseRequestedEvent{window->sdl_window});
    } else {
      window->self->close();
    }
  }
}

// An IME's not-yet-committed preedit previews inside the focused editor
// without touching its text; an empty preedit ends the preview. The commit
// arrives later as an ordinary TextInputEvent.
void route_text_editing(AppState& app, const dg::PumpResult& pumped) {
  for (const dg::TextEditingEvent& event : pumped.text_editing) {
    WinState* window = find_window_by_sdl_id(app, event.window);
    if (window == nullptr || !app.fonts.has_value()) {
      continue;
    }
    const std::optional<dg::NodeId> focused = window->focus.current();
    if (!focused.has_value() || !is_text_editor(*window, *focused)) {
      continue;
    }
    if (is_text_area(*window, *focused)) {
      window->widgets.text_area_composition_update(window->tree.render(), *app.fonts, *focused,
                                                   event.text, event.start, event.length);
      follow_caret(*window, *focused);
    } else {
      window->widgets.text_field_composition_update(window->tree.render(), *app.fonts,
                                                    *focused, event.text, event.start,
                                                    event.length);
    }
  }
}

}  // namespace

// Every layout pass goes through here, so resync_widgets() knows when the
// composed children layout re-places (scrollbars, list rows) need
// re-deriving.
void relayout(WinState& window) {
  if (window.tree.layout().nodes_relaid_out != 0) {
    window.relaid = true;
  }
}

bool ensure_surface_current(WinState& window) {
  const dg::Expected<dg::PixelSize, dg::WindowError> size =
      window.app.wm.drawable_size(window.sdl_window);
  if (!size) {
    return false;
  }
  if (window.surface.has_value() && window.tree.viewport() == size.value()) {
    return true;
  }
  window.tree.resize(size.value());
  relayout(window);
  window.surface = dg::RasterSurface::create(size.value().width, size.value().height);
  return window.surface.has_value();
}

void process_pump(AppState& app, int timeout_ms) {
  // Tooltip delays and indeterminate progress bars wake the wait by themselves.
  // Running animations wake it every frame.
  const dg::PumpResult pumped =
      app.wm.pump(clamp_animation_timeout(app, components::clamp_timeout(app, timeout_ms)));

  // A node created since the last pump has all-zero bounds() until
  // LayoutTree::layout() has run at least once - LayoutTree::add_child()'s
  // own header comment: "reading absolute_bounds() before then answers with
  // zero". Every still-open window's layout must therefore be brought
  // current BEFORE this pump's own pointer events are hit-tested against
  // it, not only at the end where repaint_and_present() would otherwise be
  // the first thing to call layout() - a click delivered in the very same
  // pump() call that first attached its target's parent must not be
  // hit-tested against a stale, never-laid-out tree.
  for (const std::unique_ptr<dg::Window>& window : app.windows) {
    if (!window->impl().closed) {
      ensure_surface_current(window->impl());
    }
  }

  process_closes(app, pumped);

  queue_window_events(app, pumped.window_events);

  queue_timer_and_user_events(app, pumped);

  for (const dg::PointerEvent& event : pumped.pointer) {
    WinState* window = find_window_by_sdl_id(app, event.window);
    if (window != nullptr) {
      process_pointer_event(*window, event);
    }
  }

  for (const dg::KeyEvent& event : pumped.key) {
    WinState* window = find_window_by_sdl_id(app, event.window);
    if (window != nullptr) {
      process_key_event(*window, event);
    }
  }

  route_text_editing(app, pumped);

  // P6 sub-step a2 (second half): a committed SDL_EVENT_TEXT_INPUT reaches
  // whichever node THIS runtime's own dg::Focus currently names - never
  // SDL's own IME capture state, matching create_window()'s own comment on
  // why start_text_input() runs once, for a window's whole lifetime, rather
  // than being toggled per focus change. A no-op if nothing is focused, or
  // the focused node is not an editor, or this app has no FontCatalog yet
  // (text_field_insert() needs one to re-measure the display projection -
  // the same precondition Window::set_text() already has for labels).
  for (const dg::TextInputEvent& event : pumped.text_input) {
    WinState* window = find_window_by_sdl_id(app, event.window);
    if (window == nullptr || components::route_text(*window, event) || !app.fonts.has_value()) {
      continue;
    }
    const std::optional<dg::NodeId> focused = window->focus.current();
    if (!focused.has_value() || !is_text_editor(*window, *focused)) {
      continue;
    }
    const bool changed = insert_into_editor(*window, *focused, event.text);
    if (changed) {
      emit(app,
           dg::events::TextChangedEvent{window->sdl_window, exact_node(*window, *focused)});
    }
  }

  for (const std::unique_ptr<dg::Window>& handle : app.windows) {
    WinState& window = handle->impl();
    if (!window.closed) {
      report_focus_drift(window);
      tick_animations(window);
      components::tick(window);
      // A relayout re-places a text area's composed children at its content
      // origin (and may have resized it): re-derive them before painting.
      relayout(window);
      resync_widgets(window);
      repaint_and_present(window);
    }
  }
}

}  // namespace dg::app_detail

namespace dg {

std::vector<Event> App::pump(int timeout_ms) {
  app_detail::process_pump(*state_, timeout_ms);
  return std::exchange(state_->pending_events, {});
}

std::vector<Event> App::poll() {
  return std::exchange(state_->pending_events, {});
}

void App::run(const std::function<void(const Event&)>& on_event) {
  app_detail::AppState& app = *state_;
  app.quit_requested = false;
  const auto any_open = [&app] {
    return std::any_of(app.windows.begin(), app.windows.end(),
                       [](const std::unique_ptr<Window>& window) { return !window->closed(); });
  };
  while (!app.quit_requested && any_open()) {
    std::vector<Event> events = pump(-1);
    for (std::size_t i = 0; i < events.size(); ++i) {
      if (on_event) {
        on_event(events[i]);
      }
      if (app.quit_requested) {
        // The rest stay queued for the next pump()/poll().
        app.pending_events.insert(app.pending_events.begin(),
                                  std::make_move_iterator(events.begin() + static_cast<std::ptrdiff_t>(i) + 1),
                                  std::make_move_iterator(events.end()));
        break;
      }
    }
  }
  app.quit_requested = false;
}

void App::quit() {
  state_->quit_requested = true;
}

}  // namespace dg
