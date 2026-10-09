// Pointer events: press/hover/click through dg::Interaction, focus on press,
// scrollbars, wheel scrolling, list rows, editors' carets and context
// clicks. Components and popups see every event first (route_pointer()).

#include <algorithm>
#include <optional>
#include <vector>

#include "app/components/components.h"
#include "app/internal.h"
#include "drawgui/widget/cursor.h"

namespace dg::app_detail {
namespace {

// One wheel notch, in device pixels, for every wheel-scrolled node type.
constexpr float kWheelStepPx = 40.0F;

// Every transition dg::Interaction reports, refreshed the same way every
// prior C++ example's own dispatch()/react() pair already does (examples/
// 05_widgets/widget_scene.cpp) - asking dg::Interaction::state_of() for each
// touched widget rather than deriving an appearance from the event kind.
void refresh_touched(WinState& window, const dg::InteractionChange& change) {
  const std::optional<dg::NodeId> touched[] = {change.left, change.entered, change.pressed,
                                               change.released, change.clicked};
  for (const std::optional<dg::NodeId>& id : touched) {
    if (id.has_value()) {
      window.widgets.refresh(window.tree.render(), *id, window.interaction.state_of(*id));
    }
  }
}

// A drag that started on a focused text area extends its selection; one that
// started on a scrollbar thumb scrolls its owner.
void drag_held(WinState& window, dg::PixelPoint at) {
  AppState& app = window.app;
  const std::optional<dg::NodeId> held = window.interaction.holding();
  if (!held.has_value()) {
    return;
  }
  const std::optional<dg::NodeId> bar_owner = window.widgets.scrollbar_owner_of(*held);
  if (bar_owner.has_value() && is_scroller(window, *bar_owner)) {
    apply_scroll_result(window, *bar_owner,
                        window.widgets.scrollbar_drag(window.tree.render(), *held,
                                                      viewport_extent(window, *bar_owner), at));
  } else if (app.fonts.has_value() && is_text_area(window, *held) &&
             window.focus.current() == held) {
    window.widgets.text_area_click(window.tree.render(), *app.fonts, *held, at,
                                   /*extend_selection=*/true);
    follow_caret(window, *held);
  }
}

// A press on a scroll view's, list's or text area's own scrollbar scrolls
// it without moving focus or the caret. True when `hit` was such a part.
bool press_scrollbar(WinState& window, std::optional<dg::NodeId> hit, dg::PixelPoint at) {
  const std::optional<dg::NodeId> bar_owner =
      hit.has_value() ? window.widgets.scrollbar_owner_of(*hit) : std::nullopt;
  if (!bar_owner.has_value() || !is_scroller(window, *bar_owner)) {
    return false;
  }
  apply_scroll_result(window, *bar_owner,
                      window.widgets.scrollbar_press(window.tree.render(), *hit,
                                                     viewport_extent(window, *bar_owner), at));
  return true;
}

// Every press on a focused editor places its caret, not only the press that
// focused it. `clicks` is the press's PointerEvent::clicks.
void place_editor_caret(WinState& window, std::optional<dg::NodeId> hit, dg::PixelPoint at,
                        int clicks) {
  AppState& app = window.app;
  if (!app.fonts.has_value() || !hit.has_value() || !is_text_editor(window, *hit) ||
      window.focus.current() != hit) {
    return;
  }
  clicks = std::max(clicks, 1);
  if (is_text_area(window, *hit)) {
    window.widgets.text_area_click(window.tree.render(), *app.fonts, *hit, at,
                                   /*extend_selection=*/false, clicks);
    follow_caret(window, *hit);
  } else {
    window.widgets.text_field_click(window.tree.render(), *app.fonts, *hit, at.x,
                                    /*extend_selection=*/false, clicks);
  }
}

bool is_ancestor_or_self(const dg::RenderTree& tree, dg::NodeId ancestor, dg::NodeId id) {
  dg::NodeId current = id;
  while (true) {
    if (current == ancestor) {
      return true;
    }
    const dg::NodeId parent = tree.parent(current);
    if (parent == current) {
      return false;
    }
    current = parent;
  }
}

// The wheel scrolls the nearest list, scroll view or text area under the
// pointer; a text area's caret stays where it is.
void wheel_scroll(WinState& window, const dg::PointerEvent& event) {
  const dg::PixelPoint at{event.x, event.y};
  dg::RenderTree& tree = window.tree.render();
  const std::optional<dg::NodeId> hit = tree.hit_test(at);
  if (!hit.has_value()) {
    return;
  }
  const int dx = static_cast<int>(-event.wheel_x * kWheelStepPx);
  const int dy = static_cast<int>(-event.wheel_y * kWheelStepPx);
  // The innermost of the two climbs wins: a list inside a scroll view
  // scrolls itself.
  const std::optional<dg::NodeId> list = window.widgets.list_owner_of(tree, *hit);
  const std::optional<dg::NodeId> view = window.widgets.scrollable_owner_of(tree, *hit);
  const bool list_inside_view =
      list.has_value() && view.has_value() && is_ancestor_or_self(tree, *view, *list);
  if (list.has_value() && (!view.has_value() || list_inside_view)) {
    if (dg::is_disabled(tree, *list)) {
      return;
    }
    const std::vector<dg::ListSlot> rows =
        window.widgets.list_scroll_by(tree, *list, viewport_extent(window, *list), dx, dy);
    apply_scroll_result(window, *list, dg::ScrollResult{true, rows});
    return;
  }
  if (!view.has_value() || dg::is_disabled(tree, *view)) {
    return;
  }
  if (is_text_area(window, *view)) {
    const dg::PixelRect bounds = tree.local_bounds(*view);
    window.widgets.scroll_by(tree, *view, dg::PixelRect{0, 0, bounds.width, bounds.height}, 0,
                             dy);
  } else {
    window.widgets.scroll_by(tree, *view, window.tree.content_bounds(*view), dx, dy);
  }
}

// A secondary-button press: a ContextClickEvent on the nearest created node
// under the pointer. Nothing is pressed or focused.
void context_click(WinState& window, const dg::PointerEvent& event) {
  const dg::PixelPoint at{event.x, event.y};
  const std::optional<dg::NodeId> hit = window.tree.render().hit_test(at);
  if (hit.has_value() && dg::is_disabled(window.tree.render(), *hit)) {
    return;
  }
  emit(window.app,
       dg::events::ContextClickEvent{
           window.sdl_window, hit.has_value() ? nearest_node(window, *hit) : std::nullopt,
           event.x, event.y, event.clicks});
}

// PointerEvent::clicks (WindowManager's count), restarted at 1 when a key
// was pressed since the previous press.
int effective_clicks(WinState& window, int clicks) {
  clicks = std::max(clicks, 1);
  if (clicks == 1) {
    window.click_offset = 0;
  } else if (window.key_since_press) {
    window.click_offset = clicks - 1;
  }
  window.key_since_press = false;
  return std::max(clicks - window.click_offset, 1);
}

}  // namespace

// The scroll-view/list/text-area kinds that own a scroll offset and a
// scrollbar.
bool is_scroller(const WinState& window, dg::NodeId id) {
  if (!window.widgets.has(id)) {
    return false;
  }
  const dg::WidgetKind kind = window.widgets.at(id).kind;
  return kind == dg::WidgetKind::kScrollView || kind == dg::WidgetKind::kList ||
         kind == dg::WidgetKind::kTextArea;
}

// The visible length along `owner`'s scroll axis - what scroll_by()/
// list_scroll_by()/the scrollbar functions call `viewport_extent`.
int viewport_extent(const WinState& window, dg::NodeId owner) {
  const dg::Widget& widget = window.widgets.at(owner);
  if (widget.kind == dg::WidgetKind::kTextArea) {
    return window.tree.render().local_bounds(owner).height;
  }
  const dg::PixelRect content = window.tree.content_bounds(owner);
  const dg::ScrollAxis axis =
      widget.kind == dg::WidgetKind::kList ? widget.list_axis : widget.scroll_axis;
  return axis == dg::ScrollAxis::kHorizontal ? content.width : content.height;
}

void process_pointer_event(WinState& window, const dg::PointerEvent& event) {
  // ABI 0.2 phase B: an open dialog/menu, dropdowns, tabs, trees and tables
  // see the event first.
  if (components::route_pointer(window, event)) {
    return;
  }
  const dg::PixelPoint at{event.x, event.y};
  const bool is_button =
      event.action == dg::PointerAction::kDown || event.action == dg::PointerAction::kUp;
  if (is_button && event.button != dg::PointerButton::kPrimary) {
    // ABI 0.2: only the primary button presses/clicks/focuses; a secondary
    // press is a context click, the middle button does nothing.
    if (event.action == dg::PointerAction::kDown &&
        event.button == dg::PointerButton::kSecondary) {
      context_click(window, event);
    }
    return;
  }
  dg::InteractionChange change;
  switch (event.action) {
    case dg::PointerAction::kMove: {
      change =
          window.interaction.moved_over(window.widgets.widget_at(window.tree.render(), at));
      drag_held(window, at);
      break;
    }
    case dg::PointerAction::kDown: {
      window.last_press_clicks = effective_clicks(window, event.clicks);
      const std::optional<dg::NodeId> hit = window.widgets.widget_at(window.tree.render(), at);
      change = window.interaction.pressed_on(hit);
      // 8-3d: a press on a focusable widget focuses it (blurring whatever
      // held focus before), a press anywhere else blurs - the identical
      // rule examples/21_focus/focus_scene.cpp's own dispatch_pointer()
      // already applies to its own Scene. This is what gives
      // Window::scope_action() a real `focused` node to bubble FROM
      // (design.md section 5.5.2 level 2).
      const bool hit_is_focusable = hit.has_value() && window.widgets.has(*hit) &&
                                    dg::is_focusable(window.widgets.at(*hit).kind);
      if (press_scrollbar(window, hit, at)) {
        break;
      }
      if (!hit.has_value() && press_list_row(window, at, window.last_press_clicks)) {
        break;
      }
      const std::optional<dg::NodeId> new_focus = hit_is_focusable ? hit : std::nullopt;
      // The border recolour + display re-derivation for editors, and
      // the FocusChangedEvent, all in apply_focus_change().
      apply_focus_change(window, window.focus.set(new_focus));
      place_editor_caret(window, hit, at, window.last_press_clicks);
      break;
    }
    case dg::PointerAction::kUp:
      change =
          window.interaction.released_on(window.widgets.widget_at(window.tree.render(), at));
      window.widgets.scrollbar_release();
      break;
    case dg::PointerAction::kLeave:
      change = window.interaction.left_window();
      break;
    case dg::PointerAction::kWheel:
      wheel_scroll(window, event);
      return;
  }
  refresh_touched(window, change);
  // ABI 0.2: the cursor follows the pointer. WindowManager only calls the
  // platform when the shape changes, so this is cheap on every motion.
  if (event.action != dg::PointerAction::kLeave) {
    window.app.wm.set_cursor(
        window.sdl_window,
        dg::cursor_at(window.tree.render(), window.widgets, window.interaction, at));
  }
  if (change.clicked.has_value() && window.widgets.has(*change.clicked) &&
      window.widgets.at(*change.clicked).kind == dg::WidgetKind::kCheckbox) {
    // P6 sub-step a2: a completed click on a checkbox flips its checked bit
    // immediately, before the ClickEvent is queued - "receive CLICK, then
    // dg_node_get_checked() to ask" (abi/drawgui.def.toml's comment on
    // dg_node_get_checked()). toggle() cannot repaint, so refresh() follows.
    window.widgets.toggle(*change.clicked);
    window.widgets.refresh(window.tree.render(), *change.clicked,
                           window.interaction.state_of(*change.clicked));
  }
  if (change.clicked.has_value()) {
    // The release carries no count of its own; the press that started this
    // click did, and Interaction only clicks on a release after a press.
    emit(window.app,
         dg::events::ClickEvent{window.sdl_window, exact_node(window, *change.clicked), event.x,
                                event.y, window.last_press_clicks});
  }
}

}  // namespace dg::app_detail
