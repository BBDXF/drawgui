// The in-window data controls (include/drawgui/ui/): ProgressBar, Tabs,
// TreeView, Table - built into a LayoutTree with no window and no fonts
// (text widths fall back to the estimate).

#include <optional>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/anim/animation_engine.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/node_lifecycle.h"
#include "drawgui/shortcuts/action_scopes.h"
#include "drawgui/theme/theme_bindings.h"
#include "drawgui/ui/progress_bar.h"
#include "drawgui/ui/table.h"
#include "drawgui/ui/tabs.h"
#include "drawgui/ui/tree_view.h"
#include "drawgui/widget/focus.h"
#include "drawgui/widget/interaction.h"
#include "drawgui/widget/widget_set.h"

namespace {

using dg::AnimTime;
using dg::BoxStyle;
using dg::Key;
using dg::KeyEvent;
using dg::LayoutTree;
using dg::NodeId;
using dg::PixelPoint;
using dg::PixelRect;
using dg::PointerAction;
using dg::PointerEvent;

struct Fixture {
  LayoutTree layout;
  dg::WidgetSet widgets;
  dg::ThemeBindings theme_bindings;
  dg::Focus focus;
  dg::AnimationEngine animation;
  dg::ActionScopes action_scopes;
  dg::Interaction interaction;

  static dg::TreeSpec spec() {
    dg::TreeSpec s;
    s.viewport = dg::PixelSize{800, 600};
    return s;
  }

  Fixture() : layout(spec()) {
    BoxStyle root;
    root.kind = dg::LayoutKind::kColumn;
    root.cross_align = dg::CrossAlign::kStart;
    layout.set_box(LayoutTree::root(), root);
  }

  dg::ControlContext ctx() {
    return dg::ControlContext{layout,
                              dg::NodeSideTables{widgets, theme_bindings, focus, animation,
                                                 action_scopes, interaction},
                              nullptr, dg::FontId{}, dg::ControlStyle{}};
  }

  dg::RenderTree& tree() { return layout.render(); }

  [[nodiscard]] std::size_t live_nodes() const {
    return dg::subtree_of(layout.render(), dg::RenderTree::root()).size();
  }
};

KeyEvent key(Key k) {
  KeyEvent event;
  event.key = k;
  return event;
}

PointerEvent down_at(PixelPoint p) {
  PointerEvent event;
  event.action = PointerAction::kDown;
  event.x = p.x;
  event.y = p.y;
  return event;
}

PixelPoint center(const PixelRect& r) {
  return PixelPoint{r.x + (r.width / 2), r.y + (r.height / 2)};
}

std::vector<dg::TreeItem> big_tree(int roots, int children) {
  std::vector<dg::TreeItem> items;
  for (int r = 0; r < roots; ++r) {
    dg::TreeItem root;
    root.label = "root " + std::to_string(r);
    items.push_back(root);
    for (int c = 0; c < children; ++c) {
      dg::TreeItem child;
      child.label = "child " + std::to_string(c);
      child.depth = 1;
      items.push_back(child);
    }
  }
  return items;
}

}  // namespace

TEST_CASE("ProgressBar: determinate fill, deterministic indeterminate frames, no focus") {
  Fixture f;
  dg::ControlContext ctx = f.ctx();
  const std::size_t baseline = f.live_nodes();
  dg::ProgressBar bar;
  dg::ProgressBarSpec spec;
  spec.box.width = 200;
  spec.box.height = 8;
  spec.value = 0.25F;
  bar.build(ctx, LayoutTree::root(), spec);
  f.layout.layout_full();
  bar.sync(ctx);
  CHECK(f.tree().local_bounds(bar.fill()).width == 50);
  CHECK(bar.set_value(ctx, 2.0F));
  CHECK(bar.value() == 1.0F);
  CHECK(f.tree().local_bounds(bar.fill()).width == 200);
  CHECK_FALSE(bar.set_value(ctx, 1.5F));
  CHECK(dg::focus_order(f.tree(), f.widgets, LayoutTree::root()).empty());
  CHECK_FALSE(f.widgets.has(bar.track()));

  bar.set_indeterminate(ctx, true, AnimTime{1000});
  CHECK(bar.animating(ctx));
  const int period = ctx.style.indeterminate_period_ms;
  bar.tick(ctx, AnimTime{1000});
  const PixelRect first = f.tree().local_bounds(bar.fill());
  CHECK(first.width == 60);
  CHECK(first.x == -60);  // starts fully off the left edge
  CHECK(bar.tick(ctx, AnimTime{1000 + (period / 2)}));
  const PixelRect half = f.tree().local_bounds(bar.fill());
  CHECK(half.x == ((260 / 2) - 60));
  bar.tick(ctx, AnimTime{1000 + period});
  CHECK(f.tree().local_bounds(bar.fill()) == first);  // periodic
  bar.tick(ctx, AnimTime{1000 + (period / 2)});
  CHECK(f.tree().local_bounds(bar.fill()) == half);  // a pure function of time

  f.animation.set_reduced_motion(true);
  CHECK_FALSE(bar.animating(ctx));
  bar.tick(ctx, AnimTime{1234});
  CHECK(f.tree().local_bounds(bar.fill()).x == 70);

  bar.destroy(ctx);
  CHECK(f.live_nodes() == baseline);
}

TEST_CASE("Tabs: roving focus skips disabled tabs, only the active panel is live") {
  Fixture f;
  dg::ControlContext ctx = f.ctx();
  const std::size_t baseline = f.live_nodes();
  const std::size_t widgets_before = f.widgets.count();
  dg::Tabs tabs;
  dg::TabsSpec spec;
  dg::TabItem a;
  a.label = "Alpha";
  dg::TabItem b;
  b.label = "Beta";
  b.disabled = true;
  dg::TabItem c;
  c.label = "Gamma";
  spec.tabs = {a, b, c};
  spec.box.height = 300;
  tabs.build(ctx, LayoutTree::root(), spec);

  // One focusable button in each panel.
  std::vector<NodeId> buttons;
  for (int i = 0; i < 3; ++i) {
    BoxStyle box;
    box.width = 80;
    box.height = 30;
    const NodeId button = f.layout.add_child(tabs.panel(i), box, dg::NodeStyle{});
    dg::Widget widget;
    widget.kind = dg::WidgetKind::kButton;
    f.widgets.attach(button, widget);
    buttons.push_back(button);
  }
  f.layout.layout_full();
  tabs.sync(ctx);

  CHECK(tabs.selected() == 0);
  CHECK(f.tree().local_bounds(tabs.panel(1)).is_empty());
  CHECK(f.tree().local_bounds(tabs.panel(2)).is_empty());
  CHECK_FALSE(f.tree().local_bounds(tabs.panel(0)).is_empty());
  // Tab order: the selected tab and the visible panel's button only.
  const std::vector<NodeId> order = dg::focus_order(f.tree(), f.widgets, LayoutTree::root());
  REQUIRE(order.size() == 2);
  CHECK(order[0] == tabs.tab_nodes()[0]);
  CHECK(order[1] == buttons[0]);
  // A hidden panel's content is not hittable where it would be.
  const PixelPoint button_point = center(f.tree().absolute_bounds(buttons[0]));
  CHECK(f.widgets.widget_at(f.tree(), button_point) == buttons[0]);

  // Keyboard: nothing happens without focus on a tab.
  CHECK(tabs.handle_key(ctx, key(Key::kRight)).event == dg::TabsEvent::kIgnored);
  f.focus.set(tabs.tab_nodes()[0]);
  dg::TabsResult result = tabs.handle_key(ctx, key(Key::kRight));
  CHECK(result.event == dg::TabsEvent::kSelected);
  CHECK(result.index == 2);  // Beta is disabled
  CHECK(f.focus.current() == tabs.tab_nodes()[2]);
  CHECK(f.tree().local_bounds(tabs.panel(0)).is_empty());
  CHECK_FALSE(f.tree().local_bounds(tabs.panel(2)).is_empty());
  CHECK(f.widgets.widget_at(f.tree(), button_point) != buttons[0]);
  CHECK(tabs.handle_key(ctx, key(Key::kRight)).index == 0);  // wraps, skipping Beta
  CHECK(tabs.handle_key(ctx, key(Key::kEnd)).index == 2);
  CHECK(tabs.handle_key(ctx, key(Key::kHome)).index == 0);
  CHECK(tabs.handle_key(ctx, key(Key::kEnter)).event == dg::TabsEvent::kHandled);
  CHECK_FALSE(tabs.select(ctx, 1));

  // Focus inside a panel that gets hidden is dropped.
  f.focus.set(buttons[0]);
  CHECK(tabs.select(ctx, 2));
  CHECK_FALSE(f.focus.current().has_value());

  // Pointer: a click on a disabled tab does nothing, on an enabled one selects.
  const PixelPoint beta = center(f.tree().absolute_bounds(tabs.tab_nodes()[1]));
  CHECK(tabs.handle_pointer(ctx, down_at(beta)).event == dg::TabsEvent::kIgnored);
  const PixelPoint alpha = center(f.tree().absolute_bounds(tabs.tab_nodes()[0]));
  result = tabs.handle_pointer(ctx, down_at(alpha));
  CHECK(result.event == dg::TabsEvent::kSelected);
  CHECK(result.index == 0);

  // Relayout restores every panel; sync() hides them again.
  f.layout.layout_full();
  tabs.sync(ctx);
  CHECK(f.tree().local_bounds(tabs.panel(2)).is_empty());

  tabs.destroy(ctx);
  CHECK(f.live_nodes() == baseline);
  CHECK(f.widgets.count() == widgets_before);
}

TEST_CASE("TreeView: virtualized over 10k items, expand/collapse and keyboard navigation") {
  Fixture f;
  dg::ControlContext ctx = f.ctx();
  const std::size_t baseline = f.live_nodes();
  const std::size_t widgets_before = f.widgets.count();
  dg::TreeView view;
  dg::TreeViewSpec spec;
  spec.items = big_tree(1000, 9);  // 10,000 items
  spec.viewport_height = 260;
  spec.box.width = 300;
  view.build(ctx, LayoutTree::root(), spec);
  f.layout.layout_full();
  view.sync(ctx);

  const int pool = (260 / ctx.style.row_height) + 1;
  CHECK(view.pool().size() == static_cast<std::size_t>(pool));
  const std::size_t built_nodes = f.live_nodes();
  CHECK(view.visible_rows().size() == 1000);

  // Expanding everything changes the row count, never the node count.
  for (int r = 0; r < 1000; ++r) {
    view.set_expanded(ctx, r * 10, true);
  }
  CHECK(view.visible_rows().size() == 10000);
  CHECK(f.live_nodes() == built_nodes);
  view.set_expanded(ctx, 0, true);

  // Keyboard needs focus on the control.
  CHECK(view.handle_key(ctx, key(Key::kDown)).event == dg::TreeEvent::kIgnored);
  f.focus.set(view.root());
  dg::TreeResult result = view.handle_key(ctx, key(Key::kDown));
  CHECK(result.event == dg::TreeEvent::kSelected);
  CHECK(result.item == 0);
  CHECK(view.handle_key(ctx, key(Key::kDown)).item == 1);
  // Left on a leaf goes to the parent; Left again collapses it.
  CHECK(view.handle_key(ctx, key(Key::kLeft)).item == 0);
  result = view.handle_key(ctx, key(Key::kLeft));
  CHECK(result.event == dg::TreeEvent::kCollapsed);
  CHECK(view.visible_rows().size() == 9991);
  CHECK(view.handle_key(ctx, key(Key::kDown)).item == 10);  // the next root
  result = view.handle_key(ctx, key(Key::kRight));          // already expanded
  CHECK(result.event == dg::TreeEvent::kSelected);
  CHECK(result.item == 11);  // first child

  // End scrolls to the last row; the pool still only covers the viewport.
  result = view.handle_key(ctx, key(Key::kEnd));
  CHECK(result.item == 9999);
  CHECK(f.tree().scroll_offset(view.list()).y == (9991 * ctx.style.row_height) - 260);
  CHECK(f.live_nodes() == built_nodes);
  view.handle_key(ctx, key(Key::kHome));

  // Pointer: a click on the disclosure of row 0 toggles, on the label selects.
  const PixelRect list = f.tree().absolute_bounds(view.list());
  const int row_mid = list.y + (ctx.style.row_height / 2);
  result = view.handle_pointer(ctx, down_at(PixelPoint{list.x + 8, row_mid}));
  CHECK(result.event == dg::TreeEvent::kExpanded);
  CHECK(result.item == 0);
  result = view.handle_pointer(
      ctx, down_at(PixelPoint{list.x + 100, row_mid + ctx.style.row_height}));
  CHECK(result.event == dg::TreeEvent::kSelected);
  CHECK(result.item == 1);
  // Collapsing the parent of the selection moves the selection to it.
  view.set_expanded(ctx, 0, false);
  CHECK(view.selected() == 0);

  view.destroy(ctx);
  CHECK(f.live_nodes() == baseline);
  CHECK(f.widgets.count() == widgets_before);
}

TEST_CASE("Table: 10k rows on a fixed pool, selection, sort requests, column widths") {
  Fixture f;
  dg::ControlContext ctx = f.ctx();
  const std::size_t baseline = f.live_nodes();
  const std::size_t widgets_before = f.widgets.count();
  dg::Table table;
  dg::TableSpec spec;
  dg::TableColumn name;
  name.title = "Name";
  name.width_percent = 50.0F;
  dg::TableColumn size;
  size.title = "Size";
  size.width_px = 80;
  dg::TableColumn note;
  note.title = "Note";
  note.sortable = false;
  spec.columns = {name, size, note};
  for (int i = 0; i < 10000; ++i) {
    spec.rows.push_back({"file " + std::to_string(i), std::to_string(i * 3), "x"});
  }
  spec.viewport_height = 200;
  spec.box.width = 400;
  table.build(ctx, LayoutTree::root(), spec);
  f.layout.layout_full();
  table.sync(ctx);

  const auto pool =
      static_cast<std::size_t>(((200 + ctx.style.row_height - 1) / ctx.style.row_height) + 1);
  CHECK(table.pool().size() == pool);
  const std::size_t built_nodes = f.live_nodes();

  // Column widths: the row is 400 - 2 (frame) - gutter wide.
  const int row_width = 398 - f.widgets.scrollbar_style().size;
  const std::vector<NodeId> cells = f.tree().children(table.pool()[0]);
  REQUIRE(cells.size() == 3);
  CHECK(f.tree().local_bounds(cells[0]).width == row_width / 2);
  CHECK(f.tree().local_bounds(cells[1]).width == 80);
  CHECK(f.tree().local_bounds(cells[2]).width == row_width - (row_width / 2) - 80);
  CHECK(f.tree().style(cells[0]).text.text == "file 0");

  // Header clicks: sortable -> request, not sortable -> nothing.
  dg::TableResult result = table.handle_pointer(
      ctx, down_at(center(f.tree().absolute_bounds(table.header_cells()[1]))));
  CHECK(result.event == dg::TableEvent::kSortRequested);
  CHECK(result.column == 1);
  result = table.handle_pointer(
      ctx, down_at(center(f.tree().absolute_bounds(table.header_cells()[2]))));
  CHECK(result.event != dg::TableEvent::kSortRequested);
  // Header cells never enter Tab order; the table root does.
  const std::vector<NodeId> order = dg::focus_order(f.tree(), f.widgets, LayoutTree::root());
  REQUIRE(order.size() == 1);
  CHECK(order[0] == table.root());

  // Row click selects and focuses.
  const PixelRect list = f.tree().absolute_bounds(table.list());
  result = table.handle_pointer(
      ctx, down_at(PixelPoint{list.x + 20, list.y + (2 * ctx.style.row_height) + 3}));
  CHECK(result.event == dg::TableEvent::kSelected);
  CHECK(result.row == 2);
  CHECK(f.focus.is_focused(table.root()));
  CHECK(table.handle_key(ctx, key(Key::kDown)).row == 3);
  CHECK(table.handle_key(ctx, key(Key::kEnd)).row == 9999);
  CHECK(f.live_nodes() == built_nodes);
  // The selected last row is painted on a recycled slot.
  bool found = false;
  for (const NodeId row : table.pool()) {
    if (f.tree().style(f.tree().children(row)[0]).text.text == "file 9999") {
      found = f.tree().style(row).fill != dg::Color{};  // soft selection tint
    }
  }
  CHECK(found);

  // Wheel scrolls without changing the selection.
  PointerEvent wheel;
  wheel.action = PointerAction::kWheel;
  wheel.wheel_y = 1.0F;
  wheel.x = list.x + 20;
  wheel.y = list.y + 20;
  const int before = f.tree().scroll_offset(table.list()).y;
  CHECK(table.handle_pointer(ctx, wheel).event == dg::TableEvent::kHandled);
  CHECK(f.tree().scroll_offset(table.list()).y < before);
  CHECK(table.selected() == 9999);

  // App-side sort: new data, fewer rows, the offset clamps.
  table.set_rows(ctx, {{"b", "1", ""}, {"a", "2", ""}});
  table.set_sort_indicator(ctx, 0, true);
  CHECK(f.tree().style(table.header_cells()[0]).text.text == "Name \u25B2");
  CHECK(f.tree().scroll_offset(table.list()).y == 0);
  CHECK_FALSE(table.selected().has_value());

  table.destroy(ctx);
  CHECK(f.live_nodes() == baseline);
  CHECK(f.widgets.count() == widgets_before);
}
