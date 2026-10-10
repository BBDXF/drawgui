// Rich rows (List/Tree/Table check boxes, icons, secondary text, trailing
// actions, row keys), row-aware click events and toggle buttons, driven
// through dg::Window's refs and the window manager's injection.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "runtime_fixture.h"

using runtime_test::of;
using runtime_test::Rt;
namespace ev = dg::events;

namespace {

dg::ImageId load_icon(dg::App& app) {
  const std::string svg =
      "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16'>"
      "<rect width='16' height='16' fill='red'/></svg>";
  const auto image = app.load_image(reinterpret_cast<const std::uint8_t*>(svg.data()), svg.size());
  REQUIRE(image.has_value());
  return image.value();
}

dg::PixelPoint centre(const dg::PixelRect& r) {
  return dg::PixelPoint{r.x + (r.width / 2), r.y + (r.height / 2)};
}

// The absolute centre of the `part`-th child (0 = check box, 3 + k = action
// k) of the list pool row now showing "row <index>".
dg::PixelPoint list_part(Rt& t, dg::NodeId list, int index, std::size_t part) {
  const dg::RenderTree& tree = t.win->layout().render();
  const std::string text = "row " + std::to_string(index);
  for (const dg::NodeId row : tree.children(list)) {
    const std::vector<dg::NodeId> kids = tree.children(row);
    if (kids.size() > part && tree.style(row).text.text == text) {
      return centre(tree.absolute_bounds(kids[part]));
    }
  }
  FAIL("no such row part");
  return {};
}

int row_y(Rt& t, dg::NodeId list, int index, int height) {
  return t.bounds(list).y + 1 + (index * height) + (height / 2);
}

std::vector<dg::RowItem> rows(int n, dg::ImageId icon, std::uint8_t actions = 0) {
  std::vector<dg::RowItem> out;
  for (int i = 0; i < n; ++i) {
    dg::RowItem row;
    row.text = "row " + std::to_string(i);
    row.secondary = "sub";
    row.icon = icon;
    row.check = (i % 2) == 0;
    row.trailing_actions = actions;
    row.key = 1000U + static_cast<std::uint64_t>(i);
    out.push_back(row);
  }
  return out;
}

template <class E>
std::ptrdiff_t first_index(const std::vector<dg::Event>& events) {
  for (std::size_t i = 0; i < events.size(); ++i) {
    if (dg::event_as<E>(events[i]) != nullptr) {
      return static_cast<std::ptrdiff_t>(i);
    }
  }
  return -1;
}

constexpr int kRow = 28;

}  // namespace

TEST_SUITE("runtime rows") {
  TEST_CASE("list: rich rows keep the pool constant for 10k rows") {
    Rt t;
    const dg::ImageId icon = load_icon(t.app);
    const dg::NodeId node = t.add(dg::NodeKind::kList);
    dg::ListRef list = t.win->list(node).value();
    REQUIRE(list.set_row_actions({{icon, "Edit"}, {icon, "Delete"}}) == dg::Status::kOk);
    REQUIRE(list.set_rows(rows(10, icon, 2), kRow) == dg::Status::kOk);
    (void)t.pump();
    const int small = t.count();
    REQUIRE(list.set_rows(rows(10000, icon, 2), kRow) == dg::Status::kOk);
    (void)t.pump();
    CHECK(t.count() == small);
    t.wm().warp_pointer(t.win->id(), 100, 100);
    t.wm().post_wheel(t.win->id(), 0.0F, -50.0F);
    (void)t.pump();
    CHECK(t.count() == small);
    CHECK(list.set_row_actions(std::vector<dg::RowAction>(9)) == dg::Status::kValueOutOfRange);
    std::vector<dg::RowItem> bad = rows(1, icon);
    bad[0].trailing_actions = 9;
    CHECK(list.set_rows(bad) == dg::Status::kValueOutOfRange);
    CHECK(list.row_checked(1).value() == false);
    CHECK(list.row_checked(10000).error() == dg::Status::kValueOutOfRange);
  }

  TEST_CASE("list: the check box toggles without selecting; Click names the row") {
    Rt t;
    const dg::ImageId icon = load_icon(t.app);
    const dg::NodeId node = t.add(dg::NodeKind::kList);
    dg::ListRef list = t.win->list(node).value();
    REQUIRE(list.set_rows(rows(20, icon), kRow) == dg::Status::kOk);
    (void)t.pump();
    const dg::PixelPoint check = list_part(t, node, 2, 0);
    t.press(check.x, check.y);
    std::vector<dg::Event> got = t.pump();
    const auto parts = of<ev::RowPartEvent>(got);
    REQUIRE(parts.size() == 1);
    CHECK(parts[0].part == ev::RowPartEvent::Part::kCheck);
    CHECK(parts[0].item == 2);
    CHECK(parts[0].item_key == 1002U);
    CHECK(list.row_checked(2).value() == false);
    CHECK(list.selected().value() == -1);
    CHECK(of<ev::ListSelectEvent>(got).empty());

    t.press(150, row_y(t, node, 3, kRow));
    got = t.pump();
    const auto clicks = of<ev::ClickEvent>(got);
    REQUIRE(clicks.size() == 1);
    CHECK(clicks[0].node == node);
    CHECK(clicks[0].item == 3);
    CHECK(clicks[0].item_key == 1003U);
    CHECK(list.selected().value() == 3);
  }

  TEST_CASE("list: an action button reports its index and keeps the selection") {
    Rt t;
    const dg::ImageId icon = load_icon(t.app);
    const dg::NodeId node = t.add(dg::NodeKind::kList);
    dg::ListRef list = t.win->list(node).value();
    REQUIRE(list.set_row_actions({{icon, "Edit"}, {icon, "Delete"}}) == dg::Status::kOk);
    REQUIRE(list.set_rows(rows(5, icon, 2), kRow) == dg::Status::kOk);
    REQUIRE(list.select(0) == dg::Status::kOk);
    (void)t.pump();
    const dg::PixelPoint action = list_part(t, node, 1, 4);
    t.press(action.x, action.y);
    const std::vector<dg::Event> got = t.pump();
    const auto parts = of<ev::RowPartEvent>(got);
    REQUIRE(parts.size() == 1);
    CHECK(parts[0].part == ev::RowPartEvent::Part::kAction);
    CHECK(parts[0].action == 1);
    CHECK(parts[0].item == 1);
    CHECK(list.selected().value() == 0);
    CHECK(of<ev::ListSelectEvent>(got).empty());
    CHECK(of<ev::ClickEvent>(got).empty());
  }

  TEST_CASE("list: hovering an action button shows its tooltip") {
    Rt t;
    REQUIRE(t.app.set_tooltip_delay(0) == dg::Status::kOk);
    const dg::ImageId icon = load_icon(t.app);
    const dg::NodeId node = t.add(dg::NodeKind::kList);
    dg::ListRef list = t.win->list(node).value();
    REQUIRE(list.set_row_actions({{icon, "Edit"}}) == dg::Status::kOk);
    REQUIRE(list.set_rows(rows(3, icon, 1), kRow) == dg::Status::kOk);
    (void)t.pump();
    const int before = t.count();
    const dg::PixelPoint action = list_part(t, node, 0, 3);
    t.wm().warp_pointer(t.win->id(), action.x, action.y);
    (void)t.pump(4);
    CHECK(t.count() > before);  // the tooltip popup's nodes
    t.wm().warp_pointer(t.win->id(), 60, row_y(t, node, 0, kRow));
    (void)t.pump(4);
    CHECK(t.count() == before);
  }

  TEST_CASE("list: a right-click selects the row before the context click") {
    Rt t;
    const dg::NodeId node = t.add(dg::NodeKind::kList);
    dg::ListRef list = t.win->list(node).value();
    REQUIRE(list.set_items({"a", "b", "c", "d"}, kRow) == dg::Status::kOk);
    (void)t.pump();
    t.press(100, row_y(t, node, 2, kRow), dg::PointerButton::kSecondary);
    const std::vector<dg::Event> got = t.pump();
    const auto selects = of<ev::ListSelectEvent>(got);
    const auto contexts = of<ev::ContextClickEvent>(got);
    REQUIRE(selects.size() == 1);
    REQUIRE(contexts.size() == 1);
    CHECK(selects[0].index == 2);
    CHECK(contexts[0].item == 2);
    CHECK(contexts[0].node == node);
    CHECK(first_index<ev::ListSelectEvent>(got) < first_index<ev::ContextClickEvent>(got));
    CHECK(list.selected().value() == 2);
  }

  TEST_CASE("list: row keys follow their rows across a reorder") {
    Rt t;
    const dg::NodeId node = t.add(dg::NodeKind::kList);
    dg::ListRef list = t.win->list(node).value();
    std::vector<dg::RowItem> data(3);
    for (int i = 0; i < 3; ++i) {
      data[static_cast<std::size_t>(i)].text = std::to_string(i);
      data[static_cast<std::size_t>(i)].key = 10U * static_cast<std::uint64_t>(i + 1);
    }
    REQUIRE(list.set_rows(data, kRow) == dg::Status::kOk);
    CHECK(list.index_of(10).value() == 0);
    std::reverse(data.begin(), data.end());
    REQUIRE(list.set_rows(data, kRow) == dg::Status::kOk);
    CHECK(list.index_of(10).value() == 2);
    CHECK(list.key_of(0).value() == 30U);
    CHECK(list.index_of(99).error() == dg::Status::kUnknownId);
    REQUIRE(list.select_key(20) == dg::Status::kOk);
    CHECK(list.selected().value() == 1);
    (void)t.pump();
    t.press(100, row_y(t, node, 0, kRow));
    const auto clicks = of<ev::ClickEvent>(t.pump());
    REQUIRE(clicks.size() == 1);
    CHECK(clicks[0].item_key == 30U);
    REQUIRE(list.set_items({"x", "y"}) == dg::Status::kOk);
    REQUIRE(list.set_row_keys({7, 8}) == dg::Status::kOk);
    CHECK(list.index_of(8).value() == 1);
    CHECK(list.set_row_keys({1}) == dg::Status::kInvalidArgument);
  }

  TEST_CASE("tree: icons and check boxes; the check box does not select; Space toggles") {
    Rt t;
    const dg::ImageId icon = load_icon(t.app);
    const dg::NodeId node = t.add(dg::NodeKind::kTree);
    dg::TreeRef tree = t.win->tree(node).value();
    std::vector<dg::TreeEntry> items(3);
    items[0] = {"root", 0, true, icon, "3 items", false, 1};
    items[1] = {"child", 1, false, icon, "", true, 2};
    items[2] = {"plain", 0, false};
    REQUIRE(tree.set_items(items) == dg::Status::kOk);
    (void)t.pump();
    CHECK(tree.row_checked(0).value() == false);
    CHECK(tree.row_checked(2).error() == dg::Status::kNotApplicable);
    // Find the check box of item 0 (row 0): the third child of a pool row.
    const dg::RenderTree& rt = t.win->layout().render();
    dg::PixelPoint check{};
    bool found = false;
    for (const dg::NodeId n : dg::subtree_of(rt, node)) {
      const std::vector<dg::NodeId> kids = rt.children(n);
      if (kids.size() == 5 && rt.absolute_bounds(kids[2]).width > 0 && !found) {
        const dg::PixelRect b = rt.absolute_bounds(n);
        if (b.y <= t.bounds(node).y + 2) {
          check = centre(rt.absolute_bounds(kids[2]));
          found = true;
        }
      }
    }
    REQUIRE(found);
    t.press(check.x, check.y);
    std::vector<dg::Event> got = t.pump();
    auto parts = of<ev::RowPartEvent>(got);
    REQUIRE(parts.size() == 1);
    CHECK(parts[0].item == 0);
    CHECK(parts[0].item_key == 1U);
    CHECK(tree.row_checked(0).value() == true);
    CHECK(tree.selected().value() == -1);
    CHECK(of<ev::TreeEvent>(got).empty());

    // A press on the label selects item 1 and names it; Space flips it.
    const dg::PixelRect frame = t.bounds(node);
    t.press(frame.x + 150, check.y + 26);
    got = t.pump();
    const auto clicks = of<ev::ClickEvent>(got);
    REQUIRE(clicks.size() == 1);
    CHECK(clicks[0].item == 1);
    CHECK(clicks[0].item_key == 2U);
    CHECK(tree.selected().value() == 1);
    t.chord(dg::LogicalKey::kSpace, dg::Modifier::kNone);
    parts = of<ev::RowPartEvent>(t.pump());
    REQUIRE(parts.size() == 1);
    CHECK(parts[0].item == 1);
    CHECK(tree.row_checked(1).value() == false);
    // The check state survives a theme-driven rebuild.
    REQUIRE(tree.set_expanded(0, false) == dg::Status::kOk);
    CHECK(tree.row_checked(1).value() == false);
    CHECK(tree.index_of(2).value() == 1);

    // A right-click selects the row first.
    t.press(frame.x + 150, check.y, dg::PointerButton::kSecondary);
    got = t.pump();
    const auto contexts = of<ev::ContextClickEvent>(got);
    REQUIRE(contexts.size() == 1);
    CHECK(contexts[0].item == 0);
    CHECK(first_index<ev::TreeEvent>(got) < first_index<ev::ContextClickEvent>(got));
  }

  TEST_CASE("table: a cell check box toggles; Click names row and column; Space toggles") {
    Rt t;
    const dg::ImageId icon = load_icon(t.app);
    const dg::NodeId node = t.add(dg::NodeKind::kTable);
    dg::TableRef table = t.win->table(node).value();
    REQUIRE(table.set_columns({{"Done", 80, std::nullopt, false}, {"Name", std::nullopt,
                                                                   std::nullopt, true}}) ==
            dg::Status::kOk);
    std::vector<std::vector<dg::TableCell>> cells;
    for (int i = 0; i < 4; ++i) {
      cells.push_back({{"", {}, i == 0}, {"name " + std::to_string(i), icon, std::nullopt}});
    }
    REQUIRE(table.set_cells(cells) == dg::Status::kOk);
    REQUIRE(table.set_row_keys({11, 22, 33, 44}) == dg::Status::kOk);
    (void)t.pump();
    CHECK(table.cell_checked(0, 0).value() == true);
    CHECK(table.cell_checked(0, 1).error() == dg::Status::kNotApplicable);
    const dg::RenderTree& rt = t.win->layout().render();
    // The check part of the cell at row 1, column 0.
    const dg::PixelRect frame = t.bounds(node);
    const int header = 26;
    const int y1 = frame.y + 1 + header + 26 + 13;
    dg::PixelPoint check{};
    bool found = false;
    for (const dg::NodeId n : dg::subtree_of(rt, node)) {
      const std::vector<dg::NodeId> kids = rt.children(n);
      const dg::PixelRect b = rt.absolute_bounds(n);
      if (!found && kids.size() == 2 && b.y <= y1 && y1 < b.y + b.height &&
          rt.absolute_bounds(kids[0]).width == 14 && b.x == frame.x + 1) {
        check = centre(rt.absolute_bounds(kids[0]));
        found = true;
      }
    }
    REQUIRE(found);
    t.press(check.x, check.y);
    std::vector<dg::Event> got = t.pump();
    auto parts = of<ev::RowPartEvent>(got);
    REQUIRE(parts.size() == 1);
    CHECK(parts[0].item == 1);
    CHECK(parts[0].column == 0);
    CHECK(parts[0].item_key == 22U);
    CHECK(table.cell_checked(1, 0).value() == true);
    CHECK(table.selected().value() == -1);

    t.press(frame.x + 200, check.y);
    got = t.pump();
    const auto clicks = of<ev::ClickEvent>(got);
    REQUIRE(clicks.size() == 1);
    CHECK(clicks[0].item == 1);
    CHECK(clicks[0].column == 1);
    CHECK(clicks[0].item_key == 22U);
    CHECK(table.selected().value() == 1);
    t.chord(dg::LogicalKey::kSpace, dg::Modifier::kNone);
    parts = of<ev::RowPartEvent>(t.pump());
    REQUIRE(parts.size() == 1);
    CHECK(table.cell_checked(1, 0).value() == false);
    CHECK(table.index_of(44).value() == 3);
    REQUIRE(table.select_key(33) == dg::Status::kOk);
    CHECK(table.selected().value() == 2);

    t.press(frame.x + 200, check.y + 26, dg::PointerButton::kSecondary);
    got = t.pump();
    const auto contexts = of<ev::ContextClickEvent>(got);
    REQUIRE(contexts.size() == 1);
    CHECK(contexts[0].item == 2);
    CHECK(contexts[0].column == 1);
    CHECK(contexts[0].item_key == 33U);
  }

  TEST_CASE("toggle button: DG_PROP_CHECKED makes a button flip before its click") {
    Rt t;
    const dg::NodeId plain = t.add(dg::NodeKind::kButton);
    const dg::NodeId toggle = t.add(dg::NodeKind::kButton);
    CHECK(t.win->checked(plain).error() == dg::Status::kNotApplicable);
    REQUIRE(t.win->set_prop(toggle, dg::prop::checked(false).id,
                            dg::prop::checked(false).value) == dg::Status::kOk);
    (void)t.pump();
    const dg::NodeStyle off = t.win->layout().render().style(toggle);
    const dg::PixelPoint at = centre(t.bounds(toggle));
    t.press(at.x, at.y);
    const auto clicks = of<ev::ClickEvent>(t.pump());
    REQUIRE(clicks.size() == 1);
    CHECK(clicks[0].node == toggle);
    CHECK(t.win->checked(toggle).value() == true);
    t.wm().warp_pointer(t.win->id(), 470, 350);
    (void)t.pump();
    CHECK(t.win->layout().render().style(toggle).fill != off.fill);
    t.press(at.x, at.y);
    (void)t.pump();
    CHECK(t.win->checked(toggle).value() == false);
    CHECK(t.win->set_prop(toggle, DG_PROP_CHECKED, dg::PropValue::number(1.0F)) ==
          dg::Status::kTypeMismatch);
  }

  TEST_CASE("an image inside a button clicks the button") {
    Rt t;
    const dg::ImageId icon = load_icon(t.app);
    const dg::NodeId button = t.add(dg::NodeKind::kButton);
    const dg::NodeId image = t.add(dg::NodeKind::kImage, button);
    REQUIRE(t.win->set_image(image, icon) == dg::Status::kOk);
    t.set(image, DG_PROP_WIDTH, dg::PropValue::length(24));
    t.set(image, DG_PROP_HEIGHT, dg::PropValue::length(24));
    (void)t.pump();
    const dg::PixelPoint at = centre(t.bounds(image));
    t.press(at.x, at.y);
    const auto clicks = of<ev::ClickEvent>(t.pump());
    REQUIRE(clicks.size() == 1);
    CHECK(clicks[0].node == button);
  }
}
