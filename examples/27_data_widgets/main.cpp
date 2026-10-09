// examples/27_data_widgets: progress bars, tabs, a 10,000-item tree and a
// 10,000-row table on dg::App / dg::Window.
//
// The runtime builds, lays out, syncs and themes every component; this file
// only creates the nodes, fills them with data, sorts the table when the
// user asks for it, and shows the last event on a status line.
//
//   drawgui_data_widgets                 interactive window
//   drawgui_data_widgets --run-ms N      ... closing itself after N ms
//   drawgui_data_widgets --dump-png FILE one frame to PNG (no display)
//   drawgui_data_widgets --verify        headless self-check (CTest)
//   --font-dir DIR                       where "DejaVu Sans" is looked for

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "drawgui/app/app.h"
#include "drawgui/app/prop.h"
#include "drawgui/app/window.h"
#include "drawgui/render/prop_ids.generated.h"
#include "drawgui/theme/theme_loader.h"
#include "drawgui/theme/token_ids.generated.h"
#include "example_app.h"

namespace {

using dg::NodeId;
using dg::NodeKind;

constexpr int kWidth = 900;
constexpr int kHeight = 620;
constexpr int kTreeItems = 10000;
constexpr int kTableRows = 10000;
constexpr int kTableWidth = 500;

enum class Mode : std::uint8_t { kWindow, kVerify, kDumpPng };

struct Options {
  Mode mode = Mode::kWindow;
  std::string font_dir = "/usr/share/fonts";
  std::string png_path;
  int run_ms = 0;
};

std::vector<dg::TreeEntry> tree_items() {
  // 1000 folders of 9 files: 10,000 items; the first two start expanded.
  std::vector<dg::TreeEntry> items;
  items.reserve(kTreeItems);
  for (int folder = 0; folder < kTreeItems / 10; ++folder) {
    items.push_back({"folder " + std::to_string(folder), 0, folder < 2});
    for (int file = 0; file < 9; ++file) {
      items.push_back({"file " + std::to_string(folder) + "." + std::to_string(file), 1, false});
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

void padding(dg::Window& w, NodeId node, float px) {
  for (const std::uint16_t id :
       {DG_PROP_PADDING_L, DG_PROP_PADDING_T, DG_PROP_PADDING_R, DG_PROP_PADDING_B}) {
    (void)w.set_prop(node, id, dg::PropValue::number(px));
  }
}

NodeId label(dg::Window& w, NodeId parent, const std::string& text, float width) {
  const NodeId node = w.create(NodeKind::kLabel, parent).value();
  (void)w.set(node, {dg::prop::width(width), dg::prop::height(22)});
  (void)w.set_text(node, DG_PROP_TEXT, text);
  (void)w.bind_token(node, DG_PROP_TEXT_COLOR, DG_TOKEN_COLOR_ON_SURFACE);
  (void)w.set_prop(node, DG_PROP_TEXT_ALIGN, dg::PropValue::option(DG_TEXT_ALIGN_LEFT));
  return node;
}

struct Demo {
  dg::Window* w = nullptr;
  NodeId root;
  NodeId determinate;
  NodeId indeterminate;
  NodeId tabs;
  std::vector<NodeId> panels;
  NodeId tree;
  NodeId table;
  NodeId status;
  // The table's source data, which the app sorts on a sort request.
  std::vector<std::vector<std::string>> rows;
  int sort_column = -1;
  bool sort_ascending = true;

  void set_status(const std::string& text) const { (void)w->set_text(status, DG_PROP_TEXT, text); }

  void sort_table(int column) {
    sort_ascending = sort_column == column ? !sort_ascending : true;
    sort_column = column;
    const auto c = static_cast<std::size_t>(column);
    const bool numeric = column == 1;
    const bool ascending = sort_ascending;
    std::stable_sort(rows.begin(), rows.end(), [c, numeric, ascending](const auto& a, const auto& b) {
      const bool less = numeric ? example::parse_int(a[c]) < example::parse_int(b[c]) : a[c] < b[c];
      const bool greater = numeric ? example::parse_int(b[c]) < example::parse_int(a[c]) : b[c] < a[c];
      return ascending ? less : greater;
    });
    dg::TableRef ref = w->table(table).value();
    (void)ref.set_rows(rows);
    (void)ref.set_sort_indicator(column, ascending);
  }

  void on_event(const dg::Event& event) {
    if (const auto* tab = dg::event_as<dg::events::TabSelectEvent>(event)) {
      set_status("tab " + std::to_string(tab->index));
    } else if (const auto* tree_event = dg::event_as<dg::events::TreeEvent>(event)) {
      set_status("tree item " + std::to_string(tree_event->item));
    } else if (const auto* table_event = dg::event_as<dg::events::TableEvent>(event)) {
      if (table_event->kind == dg::events::TableEvent::Kind::kSort) {
        sort_table(table_event->index);
        set_status("sorted by column " + std::to_string(table_event->index));
      } else {
        set_status("table row " + std::to_string(table_event->index));
      }
    }
  }
};

Demo build(dg::Window& w) {
  Demo d;
  d.w = &w;
  d.root = w.create(NodeKind::kBox).value();
  (void)w.set(d.root, {dg::prop::width(kWidth), dg::prop::height(kHeight), dg::prop::column(),
                       dg::prop::gap(12)});
  padding(w, d.root, 20);

  label(w, d.root, "Progress (determinate 0.42 / indeterminate)", 500);
  d.determinate = w.create(NodeKind::kProgress, d.root).value();
  (void)w.set(d.determinate, {dg::prop::width(400), dg::prop::height(8)});
  (void)w.progress(d.determinate).value().set_value(0.42F);
  d.indeterminate = w.create(NodeKind::kProgress, d.root).value();
  (void)w.set(d.indeterminate, {dg::prop::width(400), dg::prop::height(8)});
  (void)w.progress(d.indeterminate).value().set_indeterminate(true);

  const NodeId columns = w.create(NodeKind::kBox, d.root).value();
  // A plain box starts 240x120; this row takes the width and the height
  // left over.
  (void)w.set(columns, {dg::prop::row(), dg::prop::gap(20), dg::prop::width_percent(100),
                        dg::prop::grow(1)});

  d.tabs = w.create(NodeKind::kTabs, columns).value();
  (void)w.set(d.tabs, {dg::prop::width(320), dg::prop::height(440)});
  dg::TabsRef tabs = w.tabs(d.tabs).value();
  for (const auto& [text, disabled] : std::vector<std::pair<std::string, bool>>{
           {"Tree", false}, {"About", false}, {"Locked", true}, {"Notes", false}}) {
    const NodeId panel = tabs.add(text, disabled).value();
    (void)w.set(panel, {dg::prop::column(), dg::prop::gap(6)});
    padding(w, panel, 8);
    d.panels.push_back(panel);
  }
  d.tree = w.create(NodeKind::kTree, d.panels[0]).value();
  (void)w.set(d.tree, {dg::prop::width(300), dg::prop::height(380)});
  (void)w.tree(d.tree).value().set_items(tree_items());
  label(w, d.panels[1], "drawgui data controls", 280);
  label(w, d.panels[1], "ProgressBar, Tabs, TreeView, Table", 280);
  label(w, d.panels[3], "Left/Right switch tabs", 280);

  d.rows = table_rows();
  d.table = w.create(NodeKind::kTable, columns).value();
  (void)w.set(d.table, {dg::prop::width(kTableWidth), dg::prop::height(400)});
  dg::TableRef table = w.table(d.table).value();
  (void)table.set_columns({{"Name", std::nullopt, 45.0F, true},
                           {"Size", 90, std::nullopt, true},
                           {"Flag", std::nullopt, std::nullopt, false}});
  (void)table.set_rows(d.rows);

  d.status = label(w, d.root, "ready", 800);
  return d;
}

dg::Window* open(dg::App& app) {
  dg::WindowOptions options;
  options.title = "drawgui data widgets";
  options.width = kWidth;
  options.height = kHeight;
  options.background = dg::Color::from_argb(0xFF1B1F27);
  dg::Expected<dg::Window*, dg::AppError> window = app.create_window(options);
  if (!window) {
    std::cerr << "create_window() failed: " << window.error().message << "\n";
    return nullptr;
  }
  return window.value();
}

// The Tab-order stops, in order, starting from whatever is focused now.
std::vector<NodeId> tab_cycle(dg::Window& w, example::Driver& drive) {
  std::vector<NodeId> stops;
  for (int i = 0; i < 8; ++i) {
    drive.key(dg::Key::kTab);
    const std::optional<NodeId> now = w.focused();
    if (!now.has_value() || std::find(stops.begin(), stops.end(), *now) != stops.end()) {
      break;
    }
    stops.push_back(*now);
  }
  return stops;
}

int verify(dg::App& app, std::ostream& out) {
  dg::Window* w = open(app);
  if (w == nullptr) {
    return 1;
  }
  Demo d = build(*w);
  example::Driver drive(app, *w);
  drive.pump();
  bool ok = true;
  auto check = [&](bool pass, const std::string& what) { example::check(pass, what, out, ok); };
  out << "data widgets verify\n";

  // 10,000 items each, yet only a viewport's worth of rows exists.
  const int nodes = w->node_count();
  check(nodes < 400, "tree and table pools are sized by the viewport, not the data (" +
                         std::to_string(nodes) + " nodes)");

  // Tab order: progress bars are never focusable.
  w->clear_focus();
  check(tab_cycle(*w, drive).size() == 3, "Tab order: selected tab, tree, table");

  // Tree keyboard navigation.
  dg::TreeRef tree = w->tree(d.tree).value();
  (void)w->focus(d.tree);
  drive.key(dg::Key::kDown);
  drive.key(dg::Key::kEnd);
  check(tree.selected().value() == kTreeItems - 10,
        "End selects the last visible item (folder 999)");
  drive.key(dg::Key::kHome);
  std::vector<dg::events::TreeEvent> changed =
      example::events_of<dg::events::TreeEvent>(drive.key(dg::Key::kLeft));
  check(!changed.empty() && changed.back().kind == dg::events::TreeEvent::Kind::kCollapse &&
            changed.back().item == 0,
        "Left collapses folder 0");
  changed = example::events_of<dg::events::TreeEvent>(drive.key(dg::Key::kRight));
  check(!changed.empty() && changed.back().kind == dg::events::TreeEvent::Kind::kExpand &&
            changed.back().item == 0,
        "Right expands it again");
  check(w->node_count() == nodes, "expand/collapse/scroll add no nodes");

  // Table: a header click requests a sort, the app sorts, row 0 changes.
  const dg::PixelRect table = drive.bounds(d.table);
  const int size_header_x = table.x + (kTableWidth * 45 / 100) + 45;
  std::vector<dg::events::TableEvent> sort =
      example::events_of<dg::events::TableEvent>(drive.click(size_header_x, table.y + 10));
  check(!sort.empty() && sort.back().kind == dg::events::TableEvent::Kind::kSort &&
            sort.back().index == 1,
        "header click requests a sort");
  for (const dg::events::TableEvent& e : sort) {
    d.on_event(e);
  }
  check(d.rows.front()[1] == "0", "the app sorted by size ascending");
  (void)w->focus(d.table);
  drive.key(dg::Key::kDown);
  drive.key(dg::Key::kPageDown);
  check(w->table(d.table).value().selected().value() > 1, "PageDown moves the table selection");
  check(w->node_count() == nodes, "table data replacement adds no nodes");

  // Tabs: Right skips the disabled tab; the tree's panel hides.
  (void)w->focus(d.tabs);
  drive.key(dg::Key::kRight);
  drive.key(dg::Key::kRight);
  check(w->tabs(d.tabs).value().selected().value() == 3,
        "Right twice lands on Notes, skipping Locked");
  // The runtime parks each caller panel inside the strip's own panel area;
  // a hidden tab collapses that area.
  const dg::RenderTree& render = w->layout().render();
  check(render.local_bounds(render.parent(d.panels[0])).is_empty(), "the tree's panel is hidden");
  w->clear_focus();
  const std::vector<NodeId> hidden_order = tab_cycle(*w, drive);
  check(std::find(hidden_order.begin(), hidden_order.end(), d.tree) == hidden_order.end(),
        "a hidden panel's tree leaves Tab order");
  w->invalidate(d.root);
  drive.pump();
  check(render.local_bounds(render.parent(d.panels[0])).is_empty(), "still hidden after relayout");

  check(w->progress(d.indeterminate).value().indeterminate().value(),
        "the indeterminate bar animates on the runtime's clock");

  // Teardown: removing the tabs takes the tree in its panel with it.
  (void)w->remove(d.tabs);
  check(!w->kind_of(d.tree).has_value(), "the tree went with its tab panel");

  out << (ok ? "PASS\n" : "FAIL\n");
  return ok ? 0 : 1;
}

int dump_png(dg::App& app, const std::string& path, std::ostream& out) {
  dg::Window* w = open(app);
  if (w == nullptr) {
    return 1;
  }
  const Demo d = build(*w);
  (void)w->tree(d.tree).value().select(3);
  (void)w->table(d.table).value().select(2);
  (void)w->focus(d.table);
  example::Driver(app, *w).pump();
  return example::write_png(*w, path, out);
}

int run_window(dg::App& app, int run_ms) {
  dg::Window* w = open(app);
  if (w == nullptr) {
    return 1;
  }
  Demo d = build(*w);
  example::close_after(app, run_ms);
  app.run([&](const dg::Event& event) {
    if (dg::event_as<dg::events::TimerEvent>(event) != nullptr) {
      app.quit();
    }
    d.on_event(event);
  });
  return 0;
}

std::optional<Options> parse(int argc, char** argv) {
  Options options;
  const std::vector<std::string> args{argv + 1, argv + argc};
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string& arg = args[i];
    const bool has_value = i + 1 < args.size();
    if (arg == "--verify") {
      options.mode = Mode::kVerify;
    } else if (arg == "--dump-png" && has_value) {
      options.mode = Mode::kDumpPng;
      options.png_path = args[++i];
    } else if (arg == "--font-dir" && has_value) {
      options.font_dir = args[++i];
    } else if (arg == "--run-ms" && has_value) {
      options.run_ms = example::parse_int(args[++i]).value_or(0);
    } else {
      std::cerr << "unrecognized argument: " << arg << "\n";
      return std::nullopt;
    }
  }
  return options;
}

}  // namespace

int main(int argc, char** argv) {
  const std::optional<Options> options = parse(argc, argv);
  if (!options.has_value()) {
    return 2;
  }
  if (options->mode != Mode::kWindow) {
    example::use_headless_driver();
  }
  dg::Expected<dg::App, dg::AppError> app =
      example::make_app(options->font_dir, "DejaVu Sans", std::cout);
  if (!app) {
    return 1;
  }
  dg::Expected<dg::Theme, dg::ThemeLoadError> theme = dg::load_builtin_theme();
  if (theme) {
    app.value().set_theme(std::make_shared<const dg::Theme>(std::move(theme).value()),
                          dg::ThemeVariant::kDark);
  }
  switch (options->mode) {
    case Mode::kVerify:
      return verify(app.value(), std::cout);
    case Mode::kDumpPng:
      return dump_png(app.value(), options->png_path, std::cout);
    case Mode::kWindow:
      break;
  }
  return run_window(app.value(), options->run_ms);
}
