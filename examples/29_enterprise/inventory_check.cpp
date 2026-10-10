// The headless check of examples/29_enterprise (CTest enterprise.verify):
// every claim is driven by injected platform input - pointer presses, keys
// and committed text on the real SDL event queue - and read back from the
// app's model and the runtime.

#include <algorithm>
#include <ostream>
#include <string>
#include <vector>

#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/node_lifecycle.h"
#include "drawgui/render/render_tree.h"
#include "example_app.h"
#include "inventory.h"
#include "probe.h"

namespace inventory {

std::vector<dg::Event> Probe::pump(int rounds) {
  std::vector<dg::Event> all;
  for (int i = 0; i < rounds; ++i) {
    for (const dg::Event& e : app_.pump(5)) {
      inv_.on_event(e);
      all.push_back(e);
    }
  }
  return all;
}

std::vector<dg::Event> Probe::click(int x, int y, dg::PointerButton button, int clicks) {
  dg::WindowManager& wm = app_.window_manager();
  wm.warp_pointer(w_.id(), x, y);
  wm.post_pointer_button(w_.id(), true, x, y, button, clicks);
  wm.post_pointer_button(w_.id(), false, x, y, button, clicks);
  return pump();
}

std::vector<dg::Event> Probe::click(dg::NodeId node, dg::PointerButton button, int clicks) {
  const dg::PixelRect r = bounds(node);
  return click(r.x + (r.width / 2), r.y + (r.height / 2), button, clicks);
}

std::vector<dg::Event> Probe::chord(dg::LogicalKey key, dg::Modifier mods) {
  app_.window_manager().post_logical_key(w_.id(), true, key, mods);
  app_.window_manager().post_logical_key(w_.id(), false, key, mods);
  return pump();
}

std::vector<dg::Event> Probe::key(dg::Key key, bool shift) {
  app_.window_manager().post_key(w_.id(), true, key, shift);
  app_.window_manager().post_key(w_.id(), false, key, shift);
  return pump();
}

std::vector<dg::Event> Probe::type(const std::string& text) {
  app_.window_manager().post_text_input(w_.id(), text);
  return pump();
}

dg::PixelRect Probe::bounds(dg::NodeId node) const {
  return w_.layout().render().absolute_bounds(node);
}

bool Probe::under(dg::NodeId ancestor, dg::NodeId node) const {
  const dg::RenderTree& tree = w_.layout().render();
  for (dg::NodeId at = node;; at = tree.parent(at)) {
    if (at == ancestor) {
      return true;
    }
    if (tree.parent(at) == at) {
      return false;
    }
  }
}

// The on-screen node showing exactly `text` (inside `within`, when given);
// the last one in tree order, so an open popup wins over the page.
std::optional<dg::NodeId> Probe::find_text(const std::string& text,
                                           std::optional<dg::NodeId> within) const {
  const dg::RenderTree& tree = w_.layout().render();
  const dg::PixelRect clip =
      within.has_value() ? bounds(*within) : dg::PixelRect{0, 0, kWidth, kHeight};
  std::optional<dg::NodeId> found;
  for (const dg::NodeId id : dg::subtree_of(tree, dg::RenderTree::root())) {
    if (tree.style(id).text.text != text) {
      continue;
    }
    if (within.has_value() && !under(*within, id)) {
      continue;
    }
    const dg::PixelRect r = bounds(id);
    const int cx = r.x + (r.width / 2);
    const int cy = r.y + (r.height / 2);
    if (r.width > 0 && r.height > 0 && cx >= clip.x && cx < clip.x + clip.width &&
        cy >= clip.y && cy < clip.y + clip.height) {
      found = id;
    }
  }
  return found;
}

std::optional<dg::NodeId> Probe::name_cell(std::uint64_t key) const {
  const Asset* a = inv_.find(key);
  if (a == nullptr) {
    return std::nullopt;
  }
  return find_text(a->name, inv_.nodes().table);
}

std::vector<dg::Event> Probe::click_text(const std::string& text,
                                         std::optional<dg::NodeId> within,
                                         dg::PointerButton button) {
  const std::optional<dg::NodeId> node = find_text(text, within);
  if (!node.has_value()) {
    return {};
  }
  return click(*node, button);
}

std::vector<dg::Event> Probe::click_row(std::uint64_t key, dg::PointerButton button,
                                        int clicks) {
  const std::optional<dg::NodeId> cell = name_cell(key);
  if (!cell.has_value()) {
    return {};
  }
  return click(*cell, button, clicks);
}

int verify(dg::App& app, std::ostream& out) {
  dg::Window* window = open_window(app);
  if (window == nullptr) {
    return 1;
  }
  dg::Window& w = *window;
  Inventory inv(app, w, dg::ThemeVariant::kLight);
  Probe p(app, w, inv);
  const Inventory::Nodes& n = inv.nodes();
  p.pump(4);
  bool all = true;
  const auto check = [&](bool ok, const std::string& what) {
    return example::check(ok, what, out, all);
  };
  const auto table_selected_key = [&]() -> std::uint64_t {
    dg::TableRef t = w.table(n.table).value();
    const int row = t.selected().value_or(-1);
    return row < 0 ? 0 : t.key_of(row).value_or(0);
  };
  const auto escape = [&] { return p.key(dg::Key::kEscape); };

  out << "enterprise: asset inventory\n";
  check(inv.assets().size() == 18 && inv.visible().size() == 18, "18 assets listed at start");

  // -- Sorting + right-click -> 删除 acts on the clicked row, by key. ---------
  p.click_text("价值", n.table);
  p.click_text("价值 \u25B2", n.table);  // second press: descending
  const std::vector<std::uint64_t> sorted = inv.visible();
  bool descending = true;
  for (std::size_t i = 1; i < sorted.size(); ++i) {
    descending = descending && inv.find(sorted[i - 1])->value >= inv.find(sorted[i])->value;
  }
  check(descending, "header clicks sort by 价值 (app sorts, descending on the second click)");
  p.click_row(sorted[0]);
  const std::uint64_t first = inv.selected_key();
  check(first == sorted[0], "clicking row 0 selects it");
  const std::uint64_t row3 = sorted[3];
  std::vector<dg::Event> got = p.click_row(row3, dg::PointerButton::kSecondary);
  const std::vector<dg::events::ContextClickEvent> ctx =
      example::events_of<dg::events::ContextClickEvent>(got);
  check(ctx.size() == 1 && ctx[0].item_key == row3,
        "right-click on row 3 reports that row's key");
  check(p.find_text("删除…").has_value(), "the row menu is open");
  p.click_text("删除…");
  check(inv.dialog_kind() == DialogKind::kDelete, "删除… asks for confirmation");
  p.click_text("删除");
  check(inv.find(row3) == nullptr && inv.find(first) != nullptr && inv.assets().size() == 17,
        "删除 removed row 3 (by key), not the earlier selection");
  check(inv.dirty(), "the deletion marks the inventory unsaved");

  // -- Keyboard context menu (Shift+F10 / Menu) on the selected row. -----------
  p.click_row(inv.visible()[1]);
  const std::uint64_t picked = inv.selected_key();
  got = p.chord(dg::LogicalKey::kF10, dg::Modifier::kShift);
  const auto kctx = example::events_of<dg::events::ContextClickEvent>(got);
  check(kctx.size() == 1 && kctx[0].node == n.table && p.find_text("编辑…").has_value(),
        "Shift+F10 in the table opens the row menu");
  const bool was_in_use = inv.find(picked)->in_use;
  p.click_text(was_in_use ? "标记为闲置" : "标记为在用");
  check(inv.find(picked)->in_use != was_in_use, "the keyboard menu acts on the selected row");
  p.chord(dg::LogicalKey::kMenu);
  check(p.find_text("编辑…").has_value(), "the Menu key opens it too");
  escape();
  check(!p.find_text("编辑…").has_value(), "Escape closes it");

  // -- F2 / Delete are table-scoped; Escape returns focus to the table. --------
  p.click_row(picked);
  check(inv.focus_owner() == n.table, "clicking a row focuses the table");
  p.chord(dg::LogicalKey::kF2);
  check(inv.dialog_kind() == DialogKind::kRename, "F2 in the table opens 重命名");
  escape();
  check(inv.dialog_kind() == DialogKind::kNone && !w.has_dialog(), "Escape cancels the dialog");
  check(inv.focus_owner() == n.table, "focus returns to the table after Escape");
  p.chord(dg::LogicalKey::kDelete);
  check(inv.dialog_kind() == DialogKind::kDelete, "Delete in the table asks to delete");
  escape();
  check(inv.find(picked) != nullptr, "cancelling keeps the asset");

  // -- F2 / Delete inside a text field only edit text. --------------------------
  p.chord(dg::LogicalKey::kF, dg::Modifier::kMod);
  check(inv.focus_owner() == n.search, "Ctrl+F focuses the search field");
  p.type("xyz");
  p.key(dg::Key::kHome);
  const std::size_t count = inv.assets().size();
  p.chord(dg::LogicalKey::kF2);
  p.key(dg::Key::kDelete);
  check(
      !w.has_dialog() && inv.assets().size() == count && w.text(n.search).value_or("") == "yz",
      "F2 / Delete in the search field edit text, never the table");
  p.chord(dg::LogicalKey::kA, dg::Modifier::kMod);
  p.key(dg::Key::kBackspace);

  // -- Search filters. ------------------------------------------------------------
  p.type("macbook");
  bool all_match = !inv.visible().empty();
  for (const std::uint64_t k : inv.visible()) {
    all_match = all_match && inv.find(k)->name.find("MacBook") != std::string::npos;
  }
  const auto macs = std::count_if(inv.assets().begin(), inv.assets().end(), [](const Asset& a) {
    return a.name.find("MacBook") != std::string::npos;
  });
  check(static_cast<long>(inv.visible().size()) == macs && macs > 0 && all_match,
        "typing \"macbook\" filters the table to the MacBook rows");
  p.chord(dg::LogicalKey::kA, dg::Modifier::kMod);
  p.key(dg::Key::kBackspace);
  check(inv.visible().size() == inv.assets().size(), "clearing the search shows every row");

  // -- Tree selection filters; its context menu creates there. --------------------
  p.click_text("上海分部", n.tree);
  bool shanghai = !inv.visible().empty();
  for (const std::uint64_t k : inv.visible()) {
    const int d = inv.find(k)->dept;
    shanghai = shanghai && (d == 4 || d == 5);
  }
  check(shanghai && inv.visible().size() < inv.assets().size(),
        "selecting 上海分部 in the tree filters the table");
  p.click_text("销售部", n.tree, dg::PointerButton::kSecondary);
  check(p.find_text("在此新建资产…").has_value(), "right-click on a tree item opens its menu");
  p.click_text("在此新建资产…");
  check(inv.dialog_kind() == DialogKind::kNew && p.find_text("登记到：深圳办事处 / 销售部"),
        "在此新建资产 opens 新建 for that department");
  escape();
  p.click_text("全部资产", n.tree);
  check(inv.visible().size() == inv.assets().size(), "全部资产 shows every row again");

  // -- Ctrl+S from the keyboard and from the menu reach one command. ---------------
  const int saves = inv.saves();
  p.chord(dg::LogicalKey::kS, dg::Modifier::kMod);
  check(inv.saves() == saves + 1 && !inv.dirty(), "Ctrl+S saves");
  p.click_text("文件", n.menu_bar);
  check(p.find_text("Ctrl+S").has_value(), "文件 menu shows the Ctrl+S label");
  p.click_text("保存");
  check(inv.saves() == saves + 2, "文件 → 保存 runs the same save command");

  // -- F10 → Right → Down opens 编辑. ---------------------------------------------
  p.click_row(inv.visible()[0]);
  p.chord(dg::LogicalKey::kF10);
  p.key(dg::Key::kRight);
  p.key(dg::Key::kDown);
  check(p.find_text("查找").has_value() && p.find_text("重命名…").has_value(),
        "F10 → Right → Down opens the 编辑 menu");
  escape();
  escape();
  check(!p.find_text("查找").has_value(), "Escape closes it");

  // -- The icon inside a toolbar button clicks the button; OK waits for a name. -----
  got = p.click(n.icon_new);
  check(inv.dialog_kind() == DialogKind::kNew,
        "clicking the + icon inside 新建 opens 新建资产");
  p.key(dg::Key::kEnter);
  p.click_text("创建");
  check(inv.dialog_kind() == DialogKind::kNew && inv.assets().size() == count,
        "创建 stays disabled while the name is empty (Enter and click)");
  // Pressing the disabled button leaves nothing focused; click into the field.
  p.click(*n.dlg_name);
  p.type("测试工作站");
  p.key(dg::Key::kEnter);
  const bool created = !w.has_dialog() && inv.assets().size() == count + 1 &&
                       inv.assets().back().name == "测试工作站";
  check(created, "after typing a name, Enter creates the asset");
  check(inv.selected_key() == inv.assets().back().id &&
            table_selected_key() == inv.selected_key(),
        "the new asset is selected");
  p.chord(dg::LogicalKey::kN, dg::Modifier::kMod);
  check(inv.dialog_kind() == DialogKind::kNew, "Ctrl+N opens 新建资产 too");
  escape();

  // -- The 在用 check toggles without moving the selection. --------------------------
  p.click_row(inv.visible()[0]);
  const std::uint64_t keep = inv.selected_key();
  const std::uint64_t other = inv.visible()[2];
  const bool before = inv.find(other)->in_use;
  const dg::RenderTree& tree = w.layout().render();
  const std::optional<dg::NodeId> cell = p.name_cell(other);
  bool toggled = false;
  if (cell.has_value()) {
    const std::vector<dg::NodeId> cells = tree.children(tree.parent(*cell));
    if (cells.size() > 2 && !tree.children(cells[2]).empty()) {
      got = p.click(tree.children(cells[2])[0]);
      toggled = !example::events_of<dg::events::RowPartEvent>(got).empty();
    }
  }
  check(toggled && inv.find(other)->in_use != before, "clicking a row's 在用 box toggles it");
  check(inv.selected_key() == keep && table_selected_key() == keep,
        "...and leaves the selection where it was");

  // -- The history row action undoes without changing the selection. -----------------
  p.click_row(1);
  p.click_text("历史", n.tabs);
  const std::size_t entries = inv.history().size();
  const std::optional<dg::NodeId> top = p.find_text("价值改为 ¥12,800", n.history);
  bool undone = false;
  if (top.has_value()) {
    const dg::PixelRect list = p.bounds(n.history);
    const dg::PixelRect row = p.bounds(*top);
    got = p.click(list.x + list.width - 26, row.y + (row.height / 2));
    undone = !example::events_of<dg::events::RowPartEvent>(got).empty();
  }
  check(undone && inv.history().size() == entries - 1, "a history row's 撤销 button undoes it");
  check(inv.selected_key() == 1 && table_selected_key() == 1 &&
            w.tabs(n.tabs).value().selected().value_or(-1) == 1,
        "...without changing the selected asset or the tab");

  // -- Tags: user_data maps the row's buttons to the tag. ----------------------------
  p.click_text("标签", n.tabs);
  const std::size_t tags = inv.find(1)->tags.size();
  if (!n.tag_rows.empty()) {
    const std::vector<dg::NodeId> parts = tree.children(n.tag_rows[0]);
    p.click(parts.back());
  }
  check(inv.find(1)->tags.size() + 1 == tags, "a tag row's remove button removes that tag");

  // -- Quit with unsaved changes asks first. -----------------------------------------
  p.chord(dg::LogicalKey::kQ, dg::Modifier::kMod);
  check(inv.dialog_kind() == DialogKind::kQuit, "Ctrl+Q with unsaved changes asks to save");
  escape();
  check(!inv.quit_requested() && !w.has_dialog(), "Escape keeps the app running");

  // -- Node count stays bounded. ----------------------------------------------------
  p.click_text("常规", n.tabs);
  p.click_row(inv.visible()[0]);
  p.pump(2);
  const int baseline = w.node_count();
  for (int i = 0; i < 6; ++i) {
    p.click_row(inv.visible()[1], dg::PointerButton::kSecondary);
    escape();
    p.chord(dg::LogicalKey::kF2);
    escape();
    p.chord(dg::LogicalKey::kN, dg::Modifier::kMod);
    escape();
    p.chord(dg::LogicalKey::kF10);
    p.key(dg::Key::kDown);
    escape();
    escape();
    p.click_row(inv.visible()[static_cast<std::size_t>(i % 2)]);
  }
  p.click_row(inv.visible()[0]);
  p.pump(2);
  check(w.node_count() == baseline, "node count is unchanged after 24 menus/dialogs (" +
                                        std::to_string(baseline) + " -> " +
                                        std::to_string(w.node_count()) + ")");

  out << (all ? "enterprise: all checks passed\n" : "enterprise: FAILED\n");
  return all ? 0 : 1;
}

}  // namespace inventory
