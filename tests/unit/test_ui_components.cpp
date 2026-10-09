// The library popup components (include/drawgui/ui/): Menu, Dropdown,
// Tooltip, Dialog - each driven through a real (dummy-driver) WindowManager
// + PopupHost, the way tests/unit/test_popup.cpp drives PopupHost itself.

#include <cstdlib>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/anim/animation_engine.h"
#include "drawgui/render/font_catalog.h"
#include "drawgui/render/node_lifecycle.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/shortcuts/action_scopes.h"
#include "drawgui/theme/theme_bindings.h"
#include "drawgui/ui/dialog.h"
#include "drawgui/ui/dropdown.h"
#include "drawgui/ui/menu.h"
#include "drawgui/ui/tooltip_popup.h"
#include "drawgui/widget/focus.h"
#include "drawgui/widget/interaction.h"
#include "drawgui/widget/widget_set.h"
#include "drawgui/window/popup_host.h"
#include "drawgui/window/window_manager.h"

namespace {

using dg::AnimTime;
using dg::Key;
using dg::KeyAction;
using dg::KeyEvent;
using dg::MenuEvent;
using dg::MenuSpec;
using dg::NodeId;
using dg::NodeStyle;
using dg::PixelRect;
using dg::PixelSize;
using dg::PointerAction;
using dg::PointerEvent;
using dg::RenderTree;

constexpr PixelSize kViewport{400, 300};

struct Fixture {
  dg::WindowManager windows;
  dg::WindowId window;
  RenderTree tree;
  dg::WidgetSet widgets;
  dg::ThemeBindings theme_bindings;
  dg::Focus focus;
  dg::AnimationEngine animation;
  dg::ActionScopes action_scopes;
  dg::Interaction interaction;
  std::optional<dg::FontCatalog> fonts;
  dg::FontId font;
  std::optional<dg::PopupHost> popups;

  NodeId button_a;  // an ordinary focusable widget in the host window
  NodeId button_b;

  static dg::WindowManager make_windows() {
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    dg::Expected<dg::WindowManager, dg::WindowError> made = dg::WindowManager::create();
    REQUIRE(made.has_value());
    return std::move(made.value());
  }

  static dg::TreeSpec tree_spec() {
    dg::TreeSpec spec;
    spec.viewport = kViewport;
    return spec;
  }

  Fixture() : windows(make_windows()), tree(tree_spec()) {
    dg::WindowSpec spec;
    spec.title = "ui components unit test";
    spec.width = kViewport.width;
    spec.height = kViewport.height;
    const dg::Expected<dg::WindowId, dg::WindowError> opened = windows.open(spec);
    REQUIRE(opened.has_value());
    window = opened.value();
    popups.emplace(windows);

    dg::Expected<dg::FontCatalog, dg::FontError> scanned =
        dg::FontCatalog::scan(DG_TEST_FONT_DIR);
    REQUIRE(scanned.has_value());
    fonts = std::move(scanned.value());
    const dg::Expected<dg::FontId, dg::FontError> id = fonts->add("DgTest Latin", false);
    REQUIRE(id.has_value());
    font = id.value();

    button_a = add_button(PixelRect{20, 20, 100, 30});
    button_b = add_button(PixelRect{20, 60, 100, 30});
  }

  NodeId add_button(const PixelRect& bounds) {
    const NodeId node = tree.add_child(RenderTree::root(), bounds, NodeStyle{});
    dg::Widget widget;
    widget.kind = dg::WidgetKind::kButton;
    widgets.attach(node, widget);
    return node;
  }

  dg::UiContext ctx() {
    return dg::UiContext{
        *popups,
        window,
        tree,
        {widgets, theme_bindings, focus, animation, action_scopes, interaction},
        fonts.has_value() ? &*fonts : nullptr,
        font,
        dg::PopupStyle{}};
  }

  [[nodiscard]] std::size_t live_nodes() const {
    return dg::subtree_of(tree, RenderTree::root()).size();
  }

  [[nodiscard]] KeyEvent key(Key k, dg::Modifier mods = dg::Modifier::kNone) const {
    KeyEvent event;
    event.window = window;
    event.action = KeyAction::kDown;
    event.key = k;
    event.mods = mods;
    return event;
  }

  [[nodiscard]] PointerEvent pointer(PointerAction action, int x, int y) const {
    PointerEvent event;
    event.window = window;
    event.action = action;
    event.x = x;
    event.y = y;
    return event;
  }

  [[nodiscard]] PointerEvent pointer_at(PointerAction action, NodeId id) const {
    const PixelRect bounds = tree.absolute_bounds(id);
    return pointer(action, bounds.x + (bounds.width / 2), bounds.y + (bounds.height / 2));
  }
};

MenuSpec mixed_menu() {
  MenuSpec spec;
  spec.items = {dg::menu_item("Alpha"), dg::menu_separator(), dg::menu_item("Bravo", true),
                dg::menu_item("Charlie"), dg::menu_check_item("Delta", true)};
  return spec;
}

// --- Menu --------------------------------------------------------------

TEST_CASE("menu: open/close/activate cycles return the node and widget counts to baseline") {
  Fixture f;
  const std::size_t nodes = f.live_nodes();
  const std::size_t widgets = f.widgets.count();
  dg::Menu menu;
  for (int i = 0; i < 4; ++i) {
    dg::UiContext ctx = f.ctx();
    REQUIRE(menu.open(ctx, PixelRect{20, 50, 0, 0}, mixed_menu(), f.button_a));
    CHECK(menu.is_open());
    CHECK(f.live_nodes() > nodes);
    CHECK(f.widgets.count() == widgets + 4);  // four non-separator items
    if (i % 2 == 0) {
      menu.close(ctx);
    } else {
      CHECK(menu.handle_key(ctx, f.key(Key::kEnter)).event == MenuEvent::kActivated);
    }
    CHECK_FALSE(menu.is_open());
    CHECK(f.live_nodes() == nodes);
    CHECK(f.widgets.count() == widgets);
    CHECK_FALSE(f.focus.current_scope().has_value());
  }
}

TEST_CASE("menu: keyboard nav skips separators and disabled items, wraps, Home/End") {
  Fixture f;
  dg::UiContext ctx = f.ctx();
  dg::Menu menu;
  REQUIRE(menu.open(ctx, PixelRect{20, 50, 0, 0}, mixed_menu(), f.button_a));
  CHECK(menu.highlighted(ctx) == 0);
  CHECK(menu.handle_key(ctx, f.key(Key::kDown)).event == MenuEvent::kHandled);
  CHECK(menu.highlighted(ctx) == 3);  // skips the separator (1) and disabled Bravo (2)
  menu.handle_key(ctx, f.key(Key::kDown));
  CHECK(menu.highlighted(ctx) == 4);
  menu.handle_key(ctx, f.key(Key::kDown));
  CHECK(menu.highlighted(ctx) == 0);  // wraps forward
  menu.handle_key(ctx, f.key(Key::kUp));
  CHECK(menu.highlighted(ctx) == 4);  // wraps backward
  menu.handle_key(ctx, f.key(Key::kHome));
  CHECK(menu.highlighted(ctx) == 0);
  menu.handle_key(ctx, f.key(Key::kEnd));
  CHECK(menu.highlighted(ctx) == 4);
  menu.set_highlight(ctx, 2);  // disabled: refused
  CHECK(menu.highlighted(ctx) == 4);

  // The highlight is painted: the highlighted row, and only it, has the hover fill.
  const dg::PopupStyle style;
  CHECK(f.tree.style(menu.item_nodes()[4]).fill == style.item_hover);
  CHECK(f.tree.style(menu.item_nodes()[0]).fill == style.surface);

  // Type-ahead: "c" jumps to Charlie, "b" finds nothing enabled.
  dg::TextInputEvent text;
  text.window = f.window;
  text.text = "c";
  CHECK(menu.handle_text(ctx, text).event == MenuEvent::kHandled);
  CHECK(menu.highlighted(ctx) == 3);
  text.text = "B";
  CHECK(menu.handle_text(ctx, text).event == MenuEvent::kIgnored);
  CHECK(menu.highlighted(ctx) == 3);
}

TEST_CASE("menu: initial highlight on a disabled item falls back to the first enabled one") {
  Fixture f;
  dg::UiContext ctx = f.ctx();
  dg::Menu menu;
  MenuSpec spec = mixed_menu();
  spec.initial_highlight = 2;
  REQUIRE(menu.open(ctx, PixelRect{20, 50, 0, 0}, spec, std::nullopt));
  CHECK(menu.highlighted(ctx) == 0);
  spec.initial_highlight = 3;
  REQUIRE(menu.open(ctx, PixelRect{20, 50, 0, 0}, spec, std::nullopt));
  CHECK(menu.highlighted(ctx) == 3);
}

TEST_CASE("menu: Enter activates the highlighted item and restores focus to the anchor") {
  Fixture f;
  dg::UiContext ctx = f.ctx();
  f.focus.set(f.button_a);
  dg::Menu menu;
  REQUIRE(menu.open(ctx, PixelRect{20, 50, 0, 0}, mixed_menu(), f.button_a));
  CHECK(f.focus.current() != f.button_a);
  menu.handle_key(ctx, f.key(Key::kDown));
  const dg::MenuResult result = menu.handle_key(ctx, f.key(Key::kEnter));
  CHECK(result.event == MenuEvent::kActivated);
  CHECK(result.index == 3);
  CHECK(result.closed());
  CHECK(f.focus.current() == f.button_a);
}

TEST_CASE("menu: Escape dismisses and restores focus to the anchor") {
  Fixture f;
  dg::UiContext ctx = f.ctx();
  const std::size_t nodes = f.live_nodes();
  dg::Menu menu;
  REQUIRE(menu.open(ctx, PixelRect{20, 50, 0, 0}, mixed_menu(), f.button_b));
  const dg::MenuResult result = menu.handle_key(ctx, f.key(Key::kEscape));
  CHECK(result.event == MenuEvent::kDismissed);
  CHECK_FALSE(menu.is_open());
  CHECK(f.focus.current() == f.button_b);
  CHECK(f.live_nodes() == nodes);
  // Closed: further events are not the menu's.
  CHECK(menu.handle_key(ctx, f.key(Key::kDown)).event == MenuEvent::kIgnored);
}

TEST_CASE(
    "menu: pointer - hover highlights, disabled/separator clicks do nothing, outside "
    "dismisses") {
  Fixture f;
  dg::UiContext ctx = f.ctx();
  dg::Menu menu;
  REQUIRE(menu.open(ctx, PixelRect{200, 20, 0, 0}, mixed_menu(), f.button_a));
  const std::vector<NodeId> rows = menu.item_nodes();

  CHECK(menu.handle_pointer(ctx, f.pointer_at(PointerAction::kMove, rows[3])).event ==
        MenuEvent::kHandled);
  CHECK(menu.highlighted(ctx) == 3);
  CHECK(menu.handle_pointer(ctx, f.pointer_at(PointerAction::kDown, rows[2])).event ==
        MenuEvent::kHandled);  // disabled
  CHECK(menu.handle_pointer(ctx, f.pointer_at(PointerAction::kDown, rows[1])).event ==
        MenuEvent::kHandled);  // separator
  CHECK(menu.is_open());

  const dg::MenuResult outside =
      menu.handle_pointer(ctx, f.pointer(PointerAction::kDown, 5, 290));
  CHECK(outside.event == MenuEvent::kDismissed);
  CHECK_FALSE(menu.is_open());
  CHECK(f.focus.current() == f.button_a);

  REQUIRE(menu.open(ctx, PixelRect{200, 20, 0, 0}, mixed_menu(), f.button_a));
  const dg::MenuResult clicked =
      menu.handle_pointer(ctx, f.pointer_at(PointerAction::kDown, menu.item_nodes()[4]));
  CHECK(clicked.event == MenuEvent::kActivated);
  CHECK(clicked.index == 4);
}

TEST_CASE("menu: width grows to fit labels and honours min_width") {
  Fixture f;
  dg::UiContext ctx = f.ctx();
  dg::Menu menu;
  MenuSpec spec = mixed_menu();
  spec.min_width = 300;
  REQUIRE(menu.open(ctx, PixelRect{0, 0, 0, 0}, spec, std::nullopt));
  CHECK(menu.handle()->content_bounds.width == 300);
  spec.min_width = 0;
  REQUIRE(menu.open(ctx, PixelRect{0, 0, 0, 0}, spec, std::nullopt));
  CHECK(menu.handle()->content_bounds.width > 0);
  CHECK(menu.handle()->content_bounds.width < 300);
}

// --- Dropdown ------------------------------------------------------------

NodeId add_dropdown(Fixture& f, std::vector<std::string> options) {
  const NodeId anchor =
      f.tree.add_child(RenderTree::root(), PixelRect{200, 20, 150, 30}, NodeStyle{});
  NodeStyle label_style;
  label_style.text.font = f.font;
  label_style.text.size = 14.0F;
  const NodeId label = f.tree.add_child(anchor, PixelRect{0, 0, 150, 30}, label_style);
  dg::Widget widget;
  widget.kind = dg::WidgetKind::kDropdown;
  widget.label = label;
  f.widgets.attach(anchor, widget);
  f.widgets.dropdown_set_options(f.tree, *f.fonts, anchor, std::move(options));
  return anchor;
}

TEST_CASE("dropdown: selection updates the kDropdown widget and reports the change") {
  Fixture f;
  const NodeId anchor = add_dropdown(f, {"Red", "Green", "Blue"});
  const std::size_t nodes = f.live_nodes();
  dg::UiContext ctx = f.ctx();
  dg::Dropdown dropdown{anchor};

  // Closed: Down on an unfocused anchor is not a trigger; focused, it opens.
  CHECK(dropdown.handle_key(ctx, f.key(Key::kDown)).event == dg::DropdownEvent::kIgnored);
  f.focus.set(anchor);
  CHECK(dropdown.handle_key(ctx, f.key(Key::kDown)).event == dg::DropdownEvent::kOpened);
  CHECK(dropdown.menu().highlighted(ctx) == 0);
  CHECK(dropdown.menu().handle()->content_bounds.width >= 150);

  dropdown.handle_key(ctx, f.key(Key::kDown));
  dropdown.handle_key(ctx, f.key(Key::kDown));
  const dg::DropdownResult changed = dropdown.handle_key(ctx, f.key(Key::kEnter));
  CHECK(changed.event == dg::DropdownEvent::kChanged);
  CHECK(changed.selected == 2);
  CHECK_FALSE(changed.previous.has_value());
  CHECK(f.widgets.dropdown_selected_index(anchor) == 2);
  CHECK(f.tree.style(f.tree.children(anchor).front()).text.text == "Blue");
  CHECK(f.focus.current() == anchor);
  CHECK(f.live_nodes() == nodes);

  // Reopen by click: highlight seeds at the selection, which is checked.
  CHECK(dropdown.handle_pointer(ctx, f.pointer_at(PointerAction::kDown, anchor)).event ==
        dg::DropdownEvent::kOpened);
  CHECK(dropdown.menu().highlighted(ctx) == 2);
  CHECK(dropdown.menu().spec().items[2].checked == true);
  CHECK(dropdown.menu().spec().items[0].checked == false);

  // Escape: closed, no change.
  const dg::DropdownResult escaped = dropdown.handle_key(ctx, f.key(Key::kEscape));
  CHECK(escaped.event == dg::DropdownEvent::kClosed);
  CHECK(escaped.selected == 2);
  CHECK(f.widgets.dropdown_selected_index(anchor) == 2);

  // Clicking a row commits it; previous is reported.
  dropdown.open(ctx);
  const NodeId green = dropdown.menu().item_nodes()[1];
  const dg::DropdownResult clicked =
      dropdown.handle_pointer(ctx, f.pointer_at(PointerAction::kDown, green));
  CHECK(clicked.event == dg::DropdownEvent::kChanged);
  CHECK(clicked.selected == 1);
  CHECK(clicked.previous == 2);

  // Choosing the already-selected option closes without a change.
  dropdown.open(ctx);
  CHECK(dropdown.handle_key(ctx, f.key(Key::kEnter)).event == dg::DropdownEvent::kClosed);
  CHECK(f.live_nodes() == nodes);
}

// --- Tooltip ----------------------------------------------------------------

TEST_CASE("tooltip: shows after the delay via the clock seam, hides on leave") {
  Fixture f;
  dg::UiContext ctx = f.ctx();
  const std::size_t nodes = f.live_nodes();
  dg::Tooltip tooltip{500};
  tooltip.bind(f.button_a, "Hello");

  CHECK(tooltip.tick(ctx, f.button_a, AnimTime{1000}) == dg::TooltipEvent::kNone);
  CHECK(tooltip.next_deadline() == AnimTime{1500});
  CHECK(tooltip.tick(ctx, f.button_a, AnimTime{1499}) == dg::TooltipEvent::kNone);
  CHECK_FALSE(tooltip.is_open());
  CHECK(tooltip.tick(ctx, f.button_a, AnimTime{1500}) == dg::TooltipEvent::kShown);
  CHECK(tooltip.is_open());
  CHECK(tooltip.shown_for() == f.button_a);
  // Placed below the target.
  CHECK(tooltip.handle()->content_bounds.y >= f.tree.absolute_bounds(f.button_a).bottom());

  CHECK(tooltip.tick(ctx, std::nullopt, AnimTime{1600}) == dg::TooltipEvent::kHidden);
  CHECK_FALSE(tooltip.is_open());
  CHECK(f.live_nodes() == nodes);

  // An unbound widget never gets one; re-entering restarts the delay.
  CHECK(tooltip.tick(ctx, f.button_b, AnimTime{2000}) == dg::TooltipEvent::kNone);
  CHECK(tooltip.tick(ctx, f.button_b, AnimTime{9000}) == dg::TooltipEvent::kNone);
  CHECK(tooltip.tick(ctx, f.button_a, AnimTime{9000}) == dg::TooltipEvent::kNone);
  CHECK(tooltip.tick(ctx, f.button_a, AnimTime{9499}) == dg::TooltipEvent::kNone);
  CHECK(tooltip.tick(ctx, f.button_a, AnimTime{9500}) == dg::TooltipEvent::kShown);
}

TEST_CASE("tooltip: a press hides it and suppresses it until the pointer leaves") {
  Fixture f;
  dg::UiContext ctx = f.ctx();
  const std::size_t nodes = f.live_nodes();
  dg::Tooltip tooltip{100};
  tooltip.bind(f.button_a, "Hello");
  tooltip.tick(ctx, f.button_a, AnimTime{0});
  REQUIRE(tooltip.tick(ctx, f.button_a, AnimTime{100}) == dg::TooltipEvent::kShown);

  CHECK(tooltip.handle_pointer(ctx, f.pointer_at(PointerAction::kDown, f.button_a)) ==
        dg::TooltipEvent::kHidden);
  CHECK(f.live_nodes() == nodes);
  CHECK(tooltip.tick(ctx, f.button_a, AnimTime{5000}) == dg::TooltipEvent::kNone);
  CHECK_FALSE(tooltip.next_deadline().has_value());

  // Leave and come back: eligible again after a fresh delay.
  tooltip.tick(ctx, std::nullopt, AnimTime{5100});
  tooltip.tick(ctx, f.button_a, AnimTime{5200});
  CHECK(tooltip.tick(ctx, f.button_a, AnimTime{5300}) == dg::TooltipEvent::kShown);

  // A wheel scroll hides it too.
  CHECK(tooltip.handle_pointer(ctx, f.pointer_at(PointerAction::kWheel, f.button_a)) ==
        dg::TooltipEvent::kHidden);
  // A press before the delay elapsed cancels the pending bubble.
  tooltip.tick(ctx, std::nullopt, AnimTime{6000});
  tooltip.tick(ctx, f.button_a, AnimTime{6000});
  tooltip.handle_pointer(ctx, f.pointer_at(PointerAction::kDown, f.button_a));
  CHECK(tooltip.tick(ctx, f.button_a, AnimTime{7000}) == dg::TooltipEvent::kNone);
}

// --- Dialog ------------------------------------------------------------------

dg::DialogSpec confirm_spec() {
  dg::DialogSpec spec;
  spec.title = "Delete file?";
  spec.body = "This cannot be undone.";
  spec.buttons = {"Cancel", "Delete"};
  spec.default_button = 1;
  spec.cancel_button = 0;
  return spec;
}

TEST_CASE("dialog: focus trap - Tab cycles the buttons, outside targets are refused") {
  Fixture f;
  dg::UiContext ctx = f.ctx();
  const std::size_t nodes = f.live_nodes();
  f.focus.set(f.button_a);
  dg::Dialog dialog;
  REQUIRE(dialog.open(ctx, confirm_spec(), f.button_a));
  REQUIRE(dialog.buttons().size() == 2);
  CHECK(f.focus.current() == dialog.buttons()[1]);  // the default button
  CHECK(f.focus.scope_is_modal());
  CHECK(dialog.handle()->content_bounds == PixelRect{0, 0, kViewport.width, kViewport.height});

  CHECK(dialog.handle_key(ctx, f.key(Key::kTab)).event == dg::DialogEvent::kHandled);
  CHECK(f.focus.current() == dialog.buttons()[0]);
  dialog.handle_key(ctx, f.key(Key::kTab));
  CHECK(f.focus.current() == dialog.buttons()[1]);
  dialog.handle_key(ctx, f.key(Key::kTab, dg::Modifier::kShift));
  CHECK(f.focus.current() == dialog.buttons()[0]);

  // Clicking the host button behind the scrim does not reach it.
  const dg::DialogResult behind =
      dialog.handle_pointer(ctx, f.pointer(PointerAction::kDown, 30, 30));
  CHECK(behind.event == dg::DialogEvent::kHandled);
  CHECK_FALSE(behind.target.has_value());
  CHECK(dialog.is_open());
  CHECK(f.focus.set_guarded(f.tree, f.button_b).any() == false);

  // Escape reports the cancel button and restores focus.
  const dg::DialogResult escaped = dialog.handle_key(ctx, f.key(Key::kEscape));
  CHECK(escaped.event == dg::DialogEvent::kClosed);
  CHECK(escaped.button == 0);
  CHECK_FALSE(dialog.is_open());
  CHECK(f.focus.current() == f.button_a);
  CHECK_FALSE(f.focus.current_scope().has_value());
  CHECK(f.live_nodes() == nodes);
}

TEST_CASE("dialog: Enter activates the focused button, else the default; clicks close") {
  Fixture f;
  dg::UiContext ctx = f.ctx();
  dg::Dialog dialog;
  REQUIRE(dialog.open(ctx, confirm_spec(), std::nullopt));
  dialog.handle_key(ctx, f.key(Key::kTab));  // focus Cancel
  CHECK(dialog.handle_key(ctx, f.key(Key::kEnter)).button == 0);

  // Body content: a widget the caller adds under body(); focus there, Enter
  // falls back to the default button.
  REQUIRE(dialog.open(ctx, confirm_spec(), std::nullopt));
  const NodeId extra = f.tree.add_child(
      dialog.body(), PixelRect{0, dialog.body_content_top(), 100, 20}, NodeStyle{});
  dg::Widget widget;
  widget.kind = dg::WidgetKind::kButton;
  f.widgets.attach(extra, widget);
  const dg::DialogResult clicked =
      dialog.handle_pointer(ctx, f.pointer_at(PointerAction::kDown, extra));
  CHECK(clicked.event == dg::DialogEvent::kHandled);
  CHECK(clicked.target == extra);
  CHECK(f.focus.current() == extra);
  CHECK(dialog.handle_key(ctx, f.key(Key::kEnter)).button == 1);

  REQUIRE(dialog.open(ctx, confirm_spec(), std::nullopt));
  const dg::DialogResult button =
      dialog.handle_pointer(ctx, f.pointer_at(PointerAction::kDown, dialog.buttons()[0]));
  CHECK(button.event == dg::DialogEvent::kClosed);
  CHECK(button.button == 0);

  // No cancel button: Escape closes with no button.
  dg::DialogSpec plain;
  plain.title = "Note";
  REQUIRE(dialog.open(ctx, plain, std::nullopt));
  const dg::DialogResult escaped = dialog.handle_key(ctx, f.key(Key::kEscape));
  CHECK(escaped.event == dg::DialogEvent::kClosed);
  CHECK_FALSE(escaped.button.has_value());
}

TEST_CASE("dialog: repeated open/close returns the node count to baseline") {
  Fixture f;
  dg::UiContext ctx = f.ctx();
  const std::size_t nodes = f.live_nodes();
  const std::size_t widgets = f.widgets.count();
  dg::Dialog dialog;
  for (int i = 0; i < 3; ++i) {
    REQUIRE(dialog.open(ctx, confirm_spec(), std::nullopt));
    dialog.close(ctx);
    CHECK(f.live_nodes() == nodes);
    CHECK(f.widgets.count() == widgets);
  }
}

}  // namespace
