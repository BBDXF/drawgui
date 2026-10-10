// NodeKind::kMenuBar: a row of titles, each dropping down one menu through
// the window's open_menu() machinery (WinState::menu, popups.cpp).
//
// Mouse: a press on a title opens its menu below it (a press on the open
// title closes it); while a bar menu is open, hovering another title
// switches to it. Keyboard: F10, or a bare Alt press and release, puts the
// bar into keyboard mode (focus stays where it was; the highlighted title
// is painted); Left/Right move between titles - or between the open menus;
// Down/Enter open the highlighted one; Escape closes the menu, then the
// bar. No submenus, no mnemonics.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include "app/components/components.h"
#include "app/internal.h"

namespace dg::app_detail::components {
namespace {

constexpr int kBarHeight = 28;
constexpr int kTitlePad = 10;

dg::TextStyle title_text(const WinState& window, const dg::PopupStyle& style,
                         const std::string& text) {
  dg::TextStyle out;
  out.text = text;
  out.font = window.app.default_font;
  out.size = style.font_size;
  out.color = style.on_surface;
  out.align = dg::TextAlign::kCenter;
  return out;
}

bool menu_open_for(const WinState& window, const NodeRecord& record) {
  return window.menu.is_open() && window.menu_bar == record.id;
}

void paint_titles(WinState& window, const NodeRecord& record, const MenuBarState& state) {
  const dg::PopupStyle style = ui(window).style;
  dg::RenderTree& tree = window.tree.render();
  const bool open = menu_open_for(window, record);
  for (std::size_t i = 0; i < state.titles.size(); ++i) {
    const auto index = static_cast<int>(i);
    const bool lit = (open && window.bar_menu == index) ||
                     (!open && state.active && state.highlight == index) ||
                     (!open && !state.active && state.hover == index);
    if (tree.is_valid(state.titles[i])) {
      tree.set_fill(state.titles[i], lit ? style.item_hover : dg::Color{});
    }
  }
}

MenuBarState* bar_state(WinState& window, dg::NodeId bar) {
  NodeRecord* record = record_of(window, bar);
  return record != nullptr ? std::get_if<MenuBarState>(&record->state) : nullptr;
}

bool has_entries(const MenuBarState& state, int index) {
  return index >= 0 && static_cast<std::size_t>(index) < state.menus.size();
}

std::optional<int> title_at(const WinState& window, const MenuBarState& state,
                            dg::PixelPoint at) {
  const dg::RenderTree& tree = window.tree.render();
  for (std::size_t i = 0; i < state.titles.size(); ++i) {
    if (tree.is_valid(state.titles[i]) &&
        dg::contains(tree.absolute_bounds(state.titles[i]), at)) {
      return static_cast<int>(i);
    }
  }
  return std::nullopt;
}

void set_active(WinState& window, NodeRecord& record, MenuBarState& state, bool active,
                int highlight) {
  state.active = active;
  state.highlight = active ? highlight : -1;
  paint_titles(window, record, state);
}

// Opens menu `index` of the bar below its title, replacing (without a
// dismissal event) another menu of the same bar.
void open_bar_menu(WinState& window, NodeRecord& record, MenuBarState& state, int index) {
  if (!has_entries(state, index)) {
    return;
  }
  dg::UiContext ctx = ui(window);
  if (window.menu.is_open()) {
    if (window.menu_bar == record.id) {
      window.menu.close(ctx);
      clear_menu_owner(window);
    } else {
      dismiss_open_menu(window);
    }
  }
  window.tooltip.hide(ctx);
  const std::vector<dg::MenuEntry>& entries = state.menus[static_cast<std::size_t>(index)].entries;
  const bool any_selectable = std::any_of(entries.begin(), entries.end(), [](const auto& e) {
    return !e.separator && !e.disabled;
  });
  if (!any_selectable) {
    // Nothing to open; the title stays highlighted in keyboard mode.
    state.highlight = index;
    paint_titles(window, record, state);
    return;
  }
  dg::MenuSpec spec = menu_spec_of(entries);
  const dg::PixelRect anchor =
      window.tree.render().absolute_bounds(state.titles[static_cast<std::size_t>(index)]);
  if (!window.menu.open(ctx, anchor, std::move(spec), window.focus.current())) {
    return;
  }
  window.menu_id = window.app.next_popup_id++;
  window.menu_anchor = record.id;
  window.menu_commands = commands_of(entries);
  window.menu_bar = record.id;
  window.bar_menu = index;
  state.highlight = index;
  paint_titles(window, record, state);
}

int neighbour(const MenuBarState& state, int from, int step) {
  const auto count = static_cast<int>(state.menus.size());
  if (count == 0) {
    return -1;
  }
  return ((from < 0 ? 0 : from + step) % count + count) % count;
}

// The first enabled bar with menus: what F10 / Alt activate.
NodeRecord* first_bar(WinState& window) {
  for (NodeRecord* record : records_in_order(window)) {
    auto* state = std::get_if<MenuBarState>(&record->state);
    if (state != nullptr && !state->menus.empty() &&
        !dg::is_disabled(window.tree.render(), record->id)) {
      return record;
    }
  }
  return nullptr;
}

NodeRecord* active_bar(WinState& window) {
  for (NodeRecord* record : records_in_order(window)) {
    auto* state = std::get_if<MenuBarState>(&record->state);
    if (state != nullptr && state->active) {
      return record;
    }
  }
  return nullptr;
}

}  // namespace

void menubar_defaults(dg::BoxStyle& box, dg::NodeStyle& /*style*/) {
  box.kind = dg::LayoutKind::kRow;
  box.height = kBarHeight;
  box.cross_align = dg::CrossAlign::kStretch;
}

void build_menubar(WinState& window, NodeRecord& record) {
  MenuBarState& state = std::get<MenuBarState>(record.state);
  for (const dg::NodeId title : state.titles) {
    if (window.tree.render().is_valid(title)) {
      (void)dg::remove_subtree(window.tree, title, tables_of(window));
    }
  }
  state.titles.clear();
  const dg::PopupStyle style = ui(window).style;
  if (!record.user_fill.has_value() && !record.user_fill_token.has_value()) {
    window.tree.render().set_fill(record.id, style.surface);
  }
  for (const dg::MenuBarMenu& menu : state.menus) {
    dg::BoxStyle box;
    box.width = dg::measure_text_width(fonts_of(window.app), window.app.default_font,
                                       style.font_size, menu.title) +
                (2 * kTitlePad);
    dg::NodeStyle title;
    title.text = title_text(window, style, menu.title);
    title.radii = dg::Radii::all(static_cast<float>(std::max(2, style.radius / 2)));
    state.titles.push_back(window.tree.add_child(record.id, box, title));
  }
  paint_titles(window, record, state);
  window.relaid = true;
}

void restyle_state(WinState& window, NodeRecord& record, MenuBarState& /*state*/) {
  build_menubar(window, record);
}

bool control_pointer(WinState& window, NodeRecord& record, MenuBarState& state,
                     const dg::PointerEvent& event) {
  if (event.window != window.sdl_window ||
      dg::is_disabled(window.tree.render(), record.id)) {
    return false;
  }
  const dg::PixelPoint at{event.x, event.y};
  const bool inside = dg::contains(window.tree.render().absolute_bounds(record.id), at);
  const std::optional<int> title = inside ? title_at(window, state, at) : std::nullopt;
  if (event.action == dg::PointerAction::kMove || event.action == dg::PointerAction::kLeave) {
    const std::optional<int> hover =
        event.action == dg::PointerAction::kMove ? title : std::nullopt;
    if (hover != state.hover) {
      state.hover = hover;
      paint_titles(window, record, state);
    }
    return false;
  }
  if (event.action != dg::PointerAction::kDown) {
    return inside;
  }
  if (!inside) {
    if (state.active) {
      set_active(window, record, state, false, -1);
    }
    return false;
  }
  if (event.button == dg::PointerButton::kPrimary && title.has_value()) {
    state.active = false;
    open_bar_menu(window, record, state, *title);
  }
  return true;
}

bool menubar_menu_pointer(WinState& window, const dg::PointerEvent& event) {
  if (!window.menu_bar.has_value() || event.window != window.sdl_window) {
    return false;
  }
  NodeRecord* record = record_of(window, *window.menu_bar);
  MenuBarState* state = bar_state(window, *window.menu_bar);
  if (record == nullptr || state == nullptr) {
    return false;
  }
  const std::optional<int> title = title_at(window, *state, dg::PixelPoint{event.x, event.y});
  if (!title.has_value()) {
    return false;
  }
  if (event.action == dg::PointerAction::kMove) {
    if (*title != window.bar_menu) {
      open_bar_menu(window, *record, *state, *title);
    }
    return true;
  }
  if (event.action == dg::PointerAction::kDown && event.button == dg::PointerButton::kPrimary) {
    if (*title == window.bar_menu) {
      // A press on the open title closes its menu.
      dismiss_open_menu(window);
      set_active(window, *record, *state, false, -1);
    } else {
      open_bar_menu(window, *record, *state, *title);
    }
    return true;
  }
  return event.action == dg::PointerAction::kUp;
}

bool menubar_menu_key(WinState& window, const dg::KeyEvent& event) {
  if (!window.menu_bar.has_value() || event.action != dg::KeyAction::kDown ||
      (event.key != dg::Key::kLeft && event.key != dg::Key::kRight)) {
    return false;
  }
  NodeRecord* record = record_of(window, *window.menu_bar);
  MenuBarState* state = bar_state(window, *window.menu_bar);
  if (record == nullptr || state == nullptr) {
    return false;
  }
  const int next = neighbour(*state, window.bar_menu, event.key == dg::Key::kLeft ? -1 : 1);
  state->active = true;
  open_bar_menu(window, *record, *state, next);
  return true;
}

void menubar_menu_closed(WinState& window, dg::NodeId bar, bool keep_active) {
  NodeRecord* record = record_of(window, bar);
  MenuBarState* state = bar_state(window, bar);
  if (record == nullptr || state == nullptr) {
    return;
  }
  const int index = window.bar_menu;
  clear_menu_owner(window);
  set_active(window, *record, *state, keep_active, index);
}

bool menubar_route_key(WinState& window, const dg::KeyEvent& event) {
  const bool alt_key = event.logical_key == dg::LogicalKey::kAltKey;
  if (alt_key && event.action == dg::KeyAction::kDown) {
    window.alt_armed = true;
    return false;
  }
  if (alt_key && event.action == dg::KeyAction::kUp) {
    const bool armed = window.alt_armed;
    window.alt_armed = false;
    if (!armed) {
      return false;
    }
    if (NodeRecord* active = active_bar(window)) {
      set_active(window, *active, std::get<MenuBarState>(active->state), false, -1);
      return true;
    }
    if (NodeRecord* bar = first_bar(window)) {
      set_active(window, *bar, std::get<MenuBarState>(bar->state), true, 0);
      return true;
    }
    return false;
  }
  if (event.action != dg::KeyAction::kDown) {
    return false;
  }
  window.alt_armed = false;
  const bool f10 = event.logical_key == dg::LogicalKey::kF10 && event.mods == dg::Modifier::kNone;
  NodeRecord* active = active_bar(window);
  if (active == nullptr) {
    if (f10) {
      if (NodeRecord* bar = first_bar(window)) {
        set_active(window, *bar, std::get<MenuBarState>(bar->state), true, 0);
        return true;
      }
    }
    return false;
  }
  MenuBarState& state = std::get<MenuBarState>(active->state);
  switch (event.key) {
    case dg::Key::kLeft:
    case dg::Key::kRight:
      set_active(window, *active, state, true,
                 neighbour(state, state.highlight, event.key == dg::Key::kLeft ? -1 : 1));
      return true;
    case dg::Key::kDown:
    case dg::Key::kEnter:
      open_bar_menu(window, *active, state, state.highlight);
      return true;
    case dg::Key::kEscape:
      set_active(window, *active, state, false, -1);
      return true;
    default:
      break;
  }
  if (f10) {
    set_active(window, *active, state, false, -1);
    return true;
  }
  // Any other key leaves keyboard mode and goes on as usual.
  set_active(window, *active, state, false, -1);
  return false;
}

void menubar_forget(WinState& window, NodeRecord& record) {
  if (window.menu_bar == record.id) {
    dg::UiContext ctx = ui(window);
    window.menu.close(ctx);
    clear_menu_owner(window);
  }
}

}  // namespace dg::app_detail::components

namespace dg {

using app_detail::MenuBarState;
using app_detail::components::component_of;

Expected<MenuBarRef, Status> Window::menu_bar(NodeId node) {
  const app_detail::NodeRecord* record = app_detail::record_of(*state_, node);
  if (record == nullptr) {
    return Unexpected(Status::kInvalidNode);
  }
  if (record->kind != NodeKind::kMenuBar) {
    return Unexpected(Status::kUnsupported);
  }
  return MenuBarRef(state_.get(), node);
}

Status MenuBarRef::set_menus(std::vector<MenuBarMenu> menus) {
  app_detail::WinState& win = window();
  const auto found = component_of<MenuBarState>(win, node());
  if (found.status != Status::kOk) {
    return found.status;
  }
  for (const MenuBarMenu& menu : menus) {
    if (menu.entries.size() > limits::kMaxMenuItems) {
      return Status::kValueOutOfRange;
    }
  }
  app_detail::components::menubar_forget(win, *found.record);
  found.state->menus = std::move(menus);
  found.state->active = false;
  found.state->highlight = -1;
  found.state->hover.reset();
  app_detail::components::build_menubar(win, *found.record);
  return Status::kOk;
}

Status MenuBarRef::set_entry(int menu, int item, MenuEntry entry) {
  app_detail::WinState& win = window();
  const auto found = component_of<MenuBarState>(win, node());
  if (found.status != Status::kOk) {
    return found.status;
  }
  std::vector<MenuBarMenu>& menus = found.state->menus;
  if (menu < 0 || static_cast<std::size_t>(menu) >= menus.size() || item < 0 ||
      static_cast<std::size_t>(item) >= menus[static_cast<std::size_t>(menu)].entries.size()) {
    return Status::kValueOutOfRange;
  }
  menus[static_cast<std::size_t>(menu)].entries[static_cast<std::size_t>(item)] =
      std::move(entry);
  // An open copy of that menu closes; the next open shows the new entry.
  if (win.menu_bar == found.record->id && win.bar_menu == menu) {
    app_detail::components::menubar_forget(win, *found.record);
    found.state->active = false;
    found.state->highlight = -1;
    app_detail::components::build_menubar(win, *found.record);
  }
  return Status::kOk;
}

}  // namespace dg
