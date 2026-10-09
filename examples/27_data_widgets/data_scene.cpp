#include "data_scene.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <utility>

#include "drawgui/graphics/raster_surface.h"
#include "drawgui/render/node_lifecycle.h"
#include "drawgui/theme/theme.h"
#include "drawgui/theme/theme_loader.h"
#include "drawgui/theme/token_ids.generated.h"

namespace data_scene {
namespace {

using dg::BoxStyle;
using dg::Color;
using dg::EdgeInsets;
using dg::Key;
using dg::LayoutKind;
using dg::LayoutTree;
using dg::NodeId;
using dg::NodeStyle;

constexpr std::uint32_t kBodyFill = 0xFF1B1F27;

std::vector<dg::TreeItem> tree_items() {
  // 1000 folders of 9 files: 10,000 items.
  std::vector<dg::TreeItem> items;
  items.reserve(kTreeItems);
  for (int folder = 0; folder < kTreeItems / 10; ++folder) {
    dg::TreeItem dir;
    dir.label = "folder " + std::to_string(folder);
    dir.expanded = folder < 2;
    items.push_back(dir);
    for (int file = 0; file < 9; ++file) {
      dg::TreeItem leaf;
      leaf.label = "file " + std::to_string(folder) + "." + std::to_string(file);
      leaf.depth = 1;
      items.push_back(leaf);
    }
  }
  return items;
}

std::vector<std::vector<std::string>> table_rows() {
  std::vector<std::vector<std::string>> rows;
  rows.reserve(kTableRows);
  for (int i = 0; i < kTableRows; ++i) {
    const int size = (i * 7919) % 10007;
    rows.push_back(
        {"record " + std::to_string(i), std::to_string(size), i % 3 == 0 ? "yes" : "no"});
  }
  return rows;
}

NodeId add_label(Scene& scene, NodeId parent, const std::string& text, int width) {
  BoxStyle box;
  box.width = width;
  box.height = 22;
  NodeStyle style;
  style.text.text = text;
  style.text.font = scene.font;
  style.text.size = 14.0F;
  style.text.color = scene.style.on_surface;
  style.text.align = dg::TextAlign::kLeft;
  return scene.layout.add_child(parent, box, style);
}

void load_theme(Scene& scene) {
  const dg::Expected<dg::Theme, dg::ThemeLoadError> theme = dg::load_builtin_theme();
  if (!theme.has_value()) {
    return;
  }
  const dg::Theme& t = theme.value();
  const dg::ThemeVariant dark = dg::ThemeVariant::kDark;
  scene.style = dg::control_style_from_theme(t, dark);
  dg::ScrollbarStyle bar;
  bar.track = t.color_value(DG_TOKEN_COLOR_SCROLLBAR_TRACK, dark);
  bar.thumb = t.color_value(DG_TOKEN_COLOR_SCROLLBAR_THUMB, dark);
  bar.thumb_hover = t.color_value(DG_TOKEN_COLOR_SCROLLBAR_THUMB_HOVER, dark);
  bar.thumb_pressed = t.color_value(DG_TOKEN_COLOR_SCROLLBAR_THUMB_PRESSED, dark);
  bar.size = t.int_value(DG_TOKEN_SIZE_SCROLLBAR).value_or(bar.size);
  scene.widgets.set_scrollbar_style(bar);
  dg::DisabledPalette palette;
  palette.fill = t.color_value(DG_TOKEN_COLOR_DISABLED, dark);
  palette.on = t.color_value(DG_TOKEN_COLOR_ON_DISABLED, dark);
  scene.widgets.set_disabled_palette(palette);
  scene.ring_color =
      t.color_value(DG_TOKEN_COLOR_FOCUS_RING, dark).value_or(Color::from_argb(0xFFFFA733));
}

bool check(bool ok, const std::string& what, std::ostream& out, bool& all) {
  out << (ok ? "  ok   " : "  FAIL ") << what << "\n";
  all = all && ok;
  return ok;
}

dg::KeyEvent key(Key k) {
  dg::KeyEvent event;
  event.key = k;
  return event;
}

dg::PointerEvent down_at(dg::PixelPoint p) {
  dg::PointerEvent event;
  event.action = dg::PointerAction::kDown;
  event.x = p.x;
  event.y = p.y;
  return event;
}

dg::PixelPoint center(const dg::PixelRect& r) {
  return dg::PixelPoint{r.x + (r.width / 2), r.y + (r.height / 2)};
}

std::size_t live_nodes(const Scene& scene) {
  return dg::subtree_of(scene.layout.render(), dg::RenderTree::root()).size();
}

}  // namespace

dg::ControlContext Scene::ctx() {
  return dg::ControlContext{
      layout,
      dg::NodeSideTables{widgets, theme_bindings, focus, animation, action_scopes, interaction},
      fonts.has_value() ? &*fonts : nullptr, font, style};
}

std::unique_ptr<Scene> build(const std::string& font_dir) {
  dg::TreeSpec spec;
  spec.viewport = dg::PixelSize{kWidth, kHeight};
  spec.background.fill = Color::from_argb(kBodyFill);
  std::optional<dg::FontCatalog> fonts;
  dg::FontId font;
  dg::Expected<dg::FontCatalog, dg::FontError> scanned = dg::FontCatalog::scan(font_dir);
  if (scanned) {
    dg::FontCatalog catalog = std::move(scanned).value();
    if (const auto id = catalog.add("DejaVu Sans", false)) {
      font = id.value();
    }
    spec.fonts = catalog;
    fonts = std::move(catalog);
  }
  auto scene = std::make_unique<Scene>(spec);
  scene->fonts = std::move(fonts);
  scene->font = font;
  load_theme(*scene);
  dg::ControlContext ctx = scene->ctx();

  BoxStyle body;
  body.kind = LayoutKind::kColumn;
  body.gap = 12;
  body.padding = EdgeInsets::all(20);
  scene->layout.set_box(LayoutTree::root(), body);

  add_label(*scene, LayoutTree::root(), "Progress (determinate 0.42 / indeterminate)", 500);
  dg::ProgressBarSpec determinate;
  determinate.box.width = 400;
  determinate.box.height = 8;
  determinate.value = 0.42F;
  scene->determinate.build(ctx, LayoutTree::root(), determinate);
  dg::ProgressBarSpec indeterminate = determinate;
  indeterminate.indeterminate = true;
  scene->indeterminate.build(ctx, LayoutTree::root(), indeterminate);

  BoxStyle row;
  row.kind = LayoutKind::kRow;
  row.gap = 20;
  const NodeId columns = scene->layout.add_child(LayoutTree::root(), row, NodeStyle{});

  dg::TabsSpec tabs;
  dg::TabItem tree_tab;
  tree_tab.label = "Tree";
  dg::TabItem about_tab;
  about_tab.label = "About";
  dg::TabItem locked_tab;
  locked_tab.label = "Locked";
  locked_tab.disabled = true;
  dg::TabItem notes_tab;
  notes_tab.label = "Notes";
  tabs.tabs = {tree_tab, about_tab, locked_tab, notes_tab};
  tabs.box.width = 320;
  tabs.box.height = 440;
  scene->tabs.build(ctx, columns, tabs);

  BoxStyle panel_box;
  panel_box.kind = LayoutKind::kColumn;
  panel_box.padding = EdgeInsets::all(8);
  panel_box.gap = 6;
  scene->layout.set_box(scene->tabs.panel(0), panel_box);
  dg::TreeViewSpec tree_spec;
  tree_spec.items = tree_items();
  tree_spec.viewport_height = 380;
  tree_spec.box.width = 300;
  scene->tree_view.build(ctx, scene->tabs.panel(0), std::move(tree_spec));
  for (int i = 1; i < 4; ++i) {
    scene->layout.set_box(scene->tabs.panel(i), panel_box);
  }
  add_label(*scene, scene->tabs.panel(1), "drawgui data controls", 280);
  add_label(*scene, scene->tabs.panel(1), "ProgressBar, Tabs, TreeView, Table", 280);
  add_label(*scene, scene->tabs.panel(3), "Left/Right switch tabs", 280);

  scene->rows = table_rows();
  dg::TableSpec table;
  dg::TableColumn name;
  name.title = "Name";
  name.width_percent = 45.0F;
  dg::TableColumn size;
  size.title = "Size";
  size.width_px = 90;
  dg::TableColumn flag;
  flag.title = "Flag";
  flag.sortable = false;
  table.columns = {name, size, flag};
  table.rows = scene->rows;
  table.viewport_height = 400;
  table.box.width = 500;
  scene->table.build(ctx, columns, std::move(table));

  scene->status = add_label(*scene, LayoutTree::root(), "ready", 800);
  relayout(*scene);
  return scene;
}

void relayout(Scene& scene) {
  scene.layout.layout();
  dg::ControlContext ctx = scene.ctx();
  scene.determinate.sync(ctx);
  scene.indeterminate.sync(ctx);
  scene.tabs.sync(ctx);
  scene.tree_view.sync(ctx);
  scene.table.sync(ctx);
  update_ring(scene);
}

void set_status(Scene& scene, const std::string& text) {
  dg::TextStyle style = scene.tree().style(scene.status).text;
  style.text = text;
  scene.tree().set_text(scene.status, style);
}

void update_ring(Scene& scene) {
  dg::update_focus_ring(scene.tree(), LayoutTree::root(), scene.ring, scene.focus.current(),
                        scene.ring_color);
}

void sort_table(Scene& scene, int column) {
  scene.sort_ascending = scene.sort_column == column ? !scene.sort_ascending : true;
  scene.sort_column = column;
  const auto c = static_cast<std::size_t>(column);
  const bool numeric = column == 1;
  const bool ascending = scene.sort_ascending;
  std::stable_sort(
      scene.rows.begin(), scene.rows.end(),
      [c, numeric, ascending](const auto& a, const auto& b) {
        const bool less = numeric ? std::stoi(a[c]) < std::stoi(b[c]) : a[c] < b[c];
        const bool greater = numeric ? std::stoi(b[c]) < std::stoi(a[c]) : b[c] < a[c];
        return ascending ? less : greater;
      });
  dg::ControlContext ctx = scene.ctx();
  scene.table.set_rows(ctx, scene.rows);
  scene.table.set_sort_indicator(ctx, column, ascending);
}

void handle_pointer(Scene& scene, const dg::PointerEvent& event) {
  dg::ControlContext ctx = scene.ctx();
  const dg::TabsResult tabs = scene.tabs.handle_pointer(ctx, event);
  if (tabs.event == dg::TabsEvent::kSelected) {
    set_status(scene, "tab " + std::to_string(tabs.index));
  }
  const dg::TreeResult tree = scene.tree_view.handle_pointer(ctx, event);
  if (tree.event == dg::TreeEvent::kSelected || tree.event == dg::TreeEvent::kExpanded ||
      tree.event == dg::TreeEvent::kCollapsed) {
    set_status(scene, "tree item " + std::to_string(tree.item));
  }
  const dg::TableResult table = scene.table.handle_pointer(ctx, event);
  if (table.event == dg::TableEvent::kSortRequested) {
    sort_table(scene, table.column);
    set_status(scene, "sorted by column " + std::to_string(table.column));
  } else if (table.event == dg::TableEvent::kSelected) {
    set_status(scene, "table row " + std::to_string(table.row));
  }
  update_ring(scene);
}

void handle_key(Scene& scene, const dg::KeyEvent& event) {
  dg::ControlContext ctx = scene.ctx();
  if (event.action == dg::KeyAction::kDown && event.key == Key::kTab) {
    if (dg::has(event.mods, dg::Modifier::kShift)) {
      scene.focus.focus_previous(scene.tree(), scene.widgets, LayoutTree::root());
    } else {
      scene.focus.focus_next(scene.tree(), scene.widgets, LayoutTree::root());
    }
    update_ring(scene);
    return;
  }
  const dg::TabsResult tabs = scene.tabs.handle_key(ctx, event);
  if (tabs.event == dg::TabsEvent::kSelected) {
    set_status(scene, "tab " + std::to_string(tabs.index));
  }
  const dg::TreeResult tree = scene.tree_view.handle_key(ctx, event);
  if (tree.event != dg::TreeEvent::kIgnored && tree.item >= 0) {
    set_status(scene, "tree item " + std::to_string(tree.item));
  }
  const dg::TableResult table = scene.table.handle_key(ctx, event);
  if (table.event == dg::TableEvent::kSelected) {
    set_status(scene, "table row " + std::to_string(table.row));
  }
  update_ring(scene);
}

int verify(const std::string& font_dir, std::ostream& out) {
  std::unique_ptr<Scene> scene = build(font_dir);
  Scene& s = *scene;
  dg::ControlContext ctx = s.ctx();
  bool ok = true;
  out << "data widgets verify\n";

  const int row = s.style.row_height;
  const int tree_rows = ((380 + row - 1) / row) + 1;
  const auto tree_pool = static_cast<std::size_t>(tree_rows);
  const int table_rows = ((400 + row - 1) / row) + 1;
  const auto table_pool = static_cast<std::size_t>(table_rows);
  check(s.tree_view.pool().size() == tree_pool, "tree pool is sized by the viewport", out, ok);
  check(s.table.pool().size() == table_pool, "table pool is sized by the viewport", out, ok);
  check(s.tree_view.visible_rows().size() == 1018, "tree starts with two folders expanded", out,
        ok);
  const std::size_t nodes = live_nodes(s);

  // Tab order: progress bars are never focusable.
  const std::vector<NodeId> order = dg::focus_order(s.tree(), s.widgets, LayoutTree::root());
  check(order.size() == 3, "Tab order: selected tab, tree, table", out, ok);

  // Tree keyboard navigation.
  s.focus.set(s.tree_view.root());
  handle_key(s, key(Key::kDown));
  handle_key(s, key(Key::kEnd));
  check(s.tree_view.selected() == kTreeItems - 10,
        "End selects the last visible item (folder 999)", out, ok);
  handle_key(s, key(Key::kHome));
  handle_key(s, key(Key::kLeft));
  check(s.tree_view.visible_rows().size() == 1009, "Left collapses folder 0", out, ok);
  handle_key(s, key(Key::kRight));
  check(s.tree_view.visible_rows().size() == 1018, "Right expands it again", out, ok);
  check(live_nodes(s) == nodes, "expand/collapse/scroll add no nodes", out, ok);

  // Table: sort request, the app sorts, row 0 changes.
  const dg::TableResult sort = s.table.handle_pointer(
      ctx, down_at(center(s.tree().absolute_bounds(s.table.header_cells()[1]))));
  check(sort.event == dg::TableEvent::kSortRequested && sort.column == 1,
        "header click requests a sort", out, ok);
  sort_table(s, sort.column);
  check(s.rows.front()[1] == "0", "the app sorted by size ascending", out, ok);
  s.focus.set(s.table.root());
  handle_key(s, key(Key::kDown));
  handle_key(s, key(Key::kPageDown));
  check(s.table.selected().has_value() && *s.table.selected() > 1,
        "PageDown moves the table selection", out, ok);
  check(live_nodes(s) == nodes, "table data replacement adds no nodes", out, ok);

  // Tabs: Right skips the disabled tab; the tree's panel hides.
  s.focus.set(s.tabs.tab_nodes()[0]);
  handle_key(s, key(Key::kRight));
  handle_key(s, key(Key::kRight));
  check(s.tabs.selected() == 3, "Right twice lands on Notes, skipping Locked", out, ok);
  check(s.tree().local_bounds(s.tabs.panel(0)).is_empty(), "the tree's panel is hidden", out,
        ok);
  const std::vector<NodeId> hidden_order =
      dg::focus_order(s.tree(), s.widgets, LayoutTree::root());
  check(std::find(hidden_order.begin(), hidden_order.end(), s.tree_view.root()) ==
            hidden_order.end(),
        "a hidden panel's tree leaves Tab order", out, ok);
  relayout(s);
  check(s.tree().local_bounds(s.tabs.panel(0)).is_empty(), "still hidden after relayout", out,
        ok);

  // Indeterminate progress is a pure function of time.
  s.indeterminate.tick(ctx, dg::AnimTime{300});
  const dg::PixelRect a = s.tree().local_bounds(s.indeterminate.fill());
  s.indeterminate.tick(ctx, dg::AnimTime{700});
  s.indeterminate.tick(ctx, dg::AnimTime{300 + s.style.indeterminate_period_ms});
  check(s.tree().local_bounds(s.indeterminate.fill()) == a,
        "indeterminate frames repeat each period", out, ok);

  // Teardown returns to the root alone.
  s.table.destroy(ctx);
  s.tabs.destroy(ctx);
  s.determinate.destroy(ctx);
  s.indeterminate.destroy(ctx);
  check(!s.tree().is_valid(s.tree_view.root()), "the tree went with its tab panel", out, ok);

  out << (ok ? "PASS\n" : "FAIL\n");
  return ok ? 0 : 1;
}

int dump_png(const std::string& font_dir, const std::string& path, std::ostream& out) {
  std::unique_ptr<Scene> scene = build(font_dir);
  Scene& s = *scene;
  dg::ControlContext ctx = s.ctx();
  s.indeterminate.tick(ctx, dg::AnimTime{400});
  s.tree_view.select(ctx, 3);
  s.table.select(ctx, 2);
  s.focus.set(s.table.root());
  update_ring(s);
  std::optional<dg::RasterSurface> surface = dg::RasterSurface::create(kWidth, kHeight);
  if (!surface.has_value()) {
    out << "could not create a surface\n";
    return 1;
  }
  s.tree().repaint_full(*surface);
  const std::vector<std::uint8_t> png = surface->encode_png();
  std::ofstream file(path, std::ios::binary);
  file.write(reinterpret_cast<const char*>(png.data()),
             static_cast<std::streamsize>(png.size()));
  if (!file) {
    out << "could not write " << path << "\n";
    return 1;
  }
  out << "wrote " << path << "\n";
  return 0;
}

}  // namespace data_scene
