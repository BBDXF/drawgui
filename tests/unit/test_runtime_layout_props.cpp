// DG_PROP_VISIBLE (display:none), DG_PROP_ROW_HEIGHT on tables/trees, and
// kButton centring its children.

#include <cstddef>
#include <optional>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/app/prop.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/props/node_props.h"
#include "drawgui/render/prop_ids.generated.h"
#include "drawgui/render/render_tree.h"

#include "runtime_fixture.h"

namespace {

using dg::LayoutTree;
using dg::NodeId;
using dg::PixelRect;
using runtime_test::Rt;

std::vector<PixelRect> all_bounds(const LayoutTree& tree, const std::vector<NodeId>& ids) {
  std::vector<PixelRect> out;
  out.reserve(ids.size());
  for (const NodeId id : ids) {
    out.push_back(tree.render().absolute_bounds(id));
  }
  return out;
}

dg::BoxStyle sized(int width, int height) {
  dg::BoxStyle box;
  box.width = width;
  box.height = height;
  return box;
}

void set_visible(LayoutTree& tree, NodeId id, bool shown) {
  REQUIRE(dg::set_prop(tree, id, DG_PROP_VISIBLE, dg::PropValue::boolean(shown)).ok());
}

int count_with_height(const Rt& t, NodeId under, int height) {
  int n = 0;
  for (const NodeId id : dg::subtree_of(t.win->layout().render(), under)) {
    const PixelRect r = t.bounds(id);
    n += r.height == height && r.width > 0 ? 1 : 0;
  }
  return n;
}

}  // namespace

TEST_SUITE("runtime layout props") {
  TEST_CASE("visible=false takes no space, gap included, and incremental == full") {
    dg::TreeSpec spec;
    spec.viewport = dg::PixelSize{300, 300};
    LayoutTree tree(spec);
    dg::BoxStyle column;
    column.kind = dg::LayoutKind::kColumn;
    column.gap = 10;
    const NodeId col = tree.add_child(LayoutTree::root(), column, dg::NodeStyle{});
    const NodeId a = tree.add_child(col, sized(50, 20), dg::NodeStyle{});
    dg::BoxStyle middle = sized(60, 30);
    middle.kind = dg::LayoutKind::kRow;
    const NodeId b = tree.add_child(col, middle, dg::NodeStyle{});
    const NodeId inner = tree.add_child(b, sized(15, 15), dg::NodeStyle{});
    const NodeId c = tree.add_child(col, sized(40, 20), dg::NodeStyle{});
    const std::vector<NodeId> ids{col, a, b, inner, c};
    (void)tree.layout();
    CHECK(tree.render().absolute_bounds(c).y == 20 + 10 + 30 + 10);

    set_visible(tree, b, false);
    (void)tree.layout();
    CHECK(tree.render().absolute_bounds(c).y == 20 + 10);
    CHECK(tree.render().absolute_bounds(b).is_empty());
    CHECK(tree.render().absolute_bounds(col).height == 20 + 10 + 20);
    const std::vector<PixelRect> incremental = all_bounds(tree, ids);
    (void)tree.layout_full();
    CHECK(all_bounds(tree, ids) == incremental);

    // A change inside the hidden subtree is inert, and showing it again
    // lays it out from scratch at its new size.
    REQUIRE(dg::set_prop(tree, inner, DG_PROP_WIDTH, dg::PropValue::length(25.0F)).ok());
    (void)tree.layout();
    CHECK(all_bounds(tree, ids) == incremental);
    set_visible(tree, b, true);
    (void)tree.layout();
    CHECK(tree.render().absolute_bounds(c).y == 20 + 10 + 30 + 10);
    CHECK(tree.render().absolute_bounds(inner).width == 25);
    const std::vector<PixelRect> shown = all_bounds(tree, ids);
    (void)tree.layout_full();
    CHECK(all_bounds(tree, ids) == shown);
  }

  TEST_CASE("a hidden subtree is not hit-tested") {
    dg::TreeSpec spec;
    spec.viewport = dg::PixelSize{200, 200};
    LayoutTree tree(spec);
    dg::BoxStyle stack;
    stack.kind = dg::LayoutKind::kAbsolute;
    const NodeId layer = tree.add_child(LayoutTree::root(), stack, dg::NodeStyle{});
    dg::BoxStyle pinned = sized(100, 100);
    pinned.left = 0;
    pinned.top = 0;
    const NodeId under = tree.add_child(layer, pinned, dg::NodeStyle{});
    const NodeId over = tree.add_child(layer, pinned, dg::NodeStyle{});
    const NodeId child = tree.add_child(over, sized(50, 50), dg::NodeStyle{});
    (void)tree.layout();
    CHECK(tree.render().hit_test(dg::PixelPoint{10, 10}) == child);
    set_visible(tree, over, false);
    (void)tree.layout();
    CHECK(tree.render().hit_test(dg::PixelPoint{10, 10}) == under);
  }

  TEST_CASE("hiding a focused widget's ancestor blurs it and it cannot be focused") {
    Rt t;
    const NodeId group = t.add(dg::NodeKind::kBox);
    const NodeId button = t.add(dg::NodeKind::kButton, group);
    (void)t.pump();
    REQUIRE(t.win->focus(button) == dg::Status::kOk);
    REQUIRE(t.win->focused() == button);
    REQUIRE(t.win->set(group, dg::prop::visible(false)) == dg::Status::kOk);
    (void)t.pump();
    CHECK_FALSE(t.win->focused().has_value());
    CHECK(t.win->focus(button) == dg::Status::kUnsupported);
    CHECK(t.bounds(group).is_empty());
    REQUIRE(t.win->set(group, dg::prop::visible(true)) == dg::Status::kOk);
    (void)t.pump();
    CHECK(t.win->focus(button) == dg::Status::kOk);
    CHECK_FALSE(t.bounds(button).is_empty());
  }

  TEST_CASE("table and tree row height") {
    Rt t;
    const NodeId table_node = t.add(dg::NodeKind::kTable);
    dg::TableRef table = t.win->table(table_node).value();
    REQUIRE(table.set_columns({dg::TableColumnSpec{"A", std::nullopt, 100.0F, false}}) ==
            dg::Status::kOk);
    std::vector<std::vector<std::string>> rows(50, std::vector<std::string>{"x"});
    REQUIRE(table.set_rows(rows) == dg::Status::kOk);
    REQUIRE(table.select(40) == dg::Status::kOk);
    (void)t.pump();
    CHECK(count_with_height(t, table_node, 41) == 0);
    CHECK(table.set_row_height(0) == dg::Status::kValueOutOfRange);
    CHECK(table.set_row_height(5000) == dg::Status::kValueOutOfRange);
    CHECK(table.set_row_height(41) == dg::Status::kOk);
    (void)t.pump();
    CHECK(count_with_height(t, table_node, 41) > 0);
    CHECK(table.selected().value() == 40);
    CHECK(t.win->set(table_node, dg::prop::row_height(41.5F)) == dg::Status::kValueOutOfRange);

    const NodeId tree_node = t.add(dg::NodeKind::kTree);
    dg::TreeRef tree = t.win->tree(tree_node).value();
    REQUIRE(tree.set_items({dg::TreeEntry{"a"}, dg::TreeEntry{"b"}}) == dg::Status::kOk);
    (void)t.pump();
    REQUIRE(t.win->set(tree_node, dg::prop::row_height(37)) == dg::Status::kOk);
    (void)t.pump();
    CHECK(count_with_height(t, tree_node, 37) > 0);

    // Any other kind refuses it.
    const NodeId box = t.add(dg::NodeKind::kBox);
    CHECK(t.win->set(box, dg::prop::row_height(30)) != dg::Status::kOk);
  }

  TEST_CASE("a button centres its children, and justify/align move them") {
    Rt t;
    const NodeId button = t.add(dg::NodeKind::kButton);
    const NodeId icon = t.add(dg::NodeKind::kBox, button);
    REQUIRE(t.win->set(icon, {dg::prop::width(24), dg::prop::height(24)}) == dg::Status::kOk);
    (void)t.pump();
    const PixelRect outer = t.bounds(button);
    const PixelRect inner = t.bounds(icon);
    CHECK(inner.x - outer.x == (outer.width - 24) / 2);
    CHECK(inner.y - outer.y == (outer.height - 24) / 2);

    t.set(button, DG_PROP_JUSTIFY, dg::PropValue::option(DG_JUSTIFY_START));
    t.set(button, DG_PROP_ALIGN, dg::PropValue::option(DG_ALIGN_END));
    (void)t.pump();
    CHECK(t.bounds(icon).x == outer.x);
    CHECK(t.bounds(icon).y == outer.y + outer.height - 24);
  }
}
