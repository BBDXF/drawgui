// Positional insert and re-parenting: RenderTree/LayoutTree move_child(),
// the primitive dg_node_insert_before() uses for a non-null `ref` and for
// moving an already-live node. A move keeps identity (NodeId unchanged), so
// unlike removal it touches no side table.

#include <optional>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/graphics/raster_surface.h"
#include "drawgui/layout/box.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/render_tree.h"

namespace {

using dg::BoxStyle;
using dg::Color;
using dg::LayoutKind;
using dg::LayoutTree;
using dg::NodeId;
using dg::NodeStyle;

dg::TreeSpec spec_of() {
  dg::TreeSpec spec;
  spec.viewport = dg::PixelSize{200, 200};
  return spec;
}

// root -> column -> {a, b, c}, a -> a_child; `other` is a second column
// beside the first so cross-parent moves have somewhere to go.
struct Scene {
  LayoutTree tree{spec_of()};
  NodeId column;
  NodeId other;
  NodeId a;
  NodeId a_child;
  NodeId b;
  NodeId c;

  Scene() {
    BoxStyle col;
    col.kind = LayoutKind::kColumn;
    col.width = 100;
    col.height = 100;
    NodeStyle style;
    style.fill = Color::rgba(0x10, 0x20, 0x30);
    column = tree.add_child(LayoutTree::root(), col, style);
    other = tree.add_child(LayoutTree::root(), col, style);

    BoxStyle box;
    box.width = 40;
    box.height = 20;
    a = tree.add_child(column, box, style);
    a_child = tree.add_child(a, box, style);
    b = tree.add_child(column, box, style);
    c = tree.add_child(column, box, style);
    tree.layout_full();
  }

  [[nodiscard]] std::vector<NodeId> kids(NodeId id) { return tree.render().children(id); }
};

}  // namespace

TEST_SUITE("node_move") {
  TEST_CASE("move before a sibling reorders and relays out") {
    Scene scene;
    REQUIRE(scene.tree.move_child(scene.c, scene.column, scene.a));
    CHECK(scene.kids(scene.column) == std::vector<NodeId>{scene.c, scene.a, scene.b});
    CHECK(scene.tree.render().is_valid(scene.c));

    scene.tree.layout();
    CHECK(scene.tree.bounds(scene.c).y == 0);
    CHECK(scene.tree.bounds(scene.a).y == 20);
    CHECK(scene.tree.bounds(scene.b).y == 40);
    // Paint/hit testing follow the new order: the top row is now `c`.
    const std::optional<NodeId> hit = scene.tree.render().hit_test(dg::PixelPoint{30, 5});
    REQUIRE(hit.has_value());
    CHECK(*hit == scene.c);
  }

  TEST_CASE("nullopt appends, and a move across parents carries the subtree") {
    Scene scene;
    REQUIRE(scene.tree.move_child(scene.a, scene.other, std::nullopt));
    CHECK(scene.kids(scene.column) == std::vector<NodeId>{scene.b, scene.c});
    CHECK(scene.kids(scene.other) == std::vector<NodeId>{scene.a});
    CHECK(scene.tree.render().parent(scene.a) == scene.other);
    CHECK(scene.kids(scene.a) == std::vector<NodeId>{scene.a_child});

    scene.tree.layout();
    CHECK(scene.tree.bounds(scene.b).y == 0);
    const dg::PixelRect other_abs = scene.tree.render().absolute_bounds(scene.other);
    CHECK(scene.tree.render().absolute_bounds(scene.a).y == other_abs.y);
    CHECK(scene.tree.render().absolute_bounds(scene.a_child).y == other_abs.y);
  }

  TEST_CASE("incremental layout after a move equals a full layout") {
    Scene scene;
    REQUIRE(scene.tree.move_child(scene.b, scene.other, std::nullopt));
    REQUIRE(scene.tree.move_child(scene.c, scene.column, scene.a));
    scene.tree.layout();
    const std::vector<NodeId> ids{scene.a, scene.a_child, scene.b, scene.c};
    std::vector<dg::PixelRect> incremental;
    incremental.reserve(ids.size());
    for (const NodeId id : ids) {
      incremental.push_back(scene.tree.render().absolute_bounds(id));
    }
    scene.tree.layout_full();
    for (std::size_t i = 0; i < ids.size(); ++i) {
      CHECK(scene.tree.render().absolute_bounds(ids[i]) == incremental[i]);
    }
  }

  TEST_CASE("a move damages both the old and the new position") {
    Scene scene;
    dg::RasterSurface surface = dg::RasterSurface::create(200, 200).value();
    scene.tree.render().repaint_full(surface);
    REQUIRE(scene.tree.render().damage().is_empty());

    const dg::PixelRect old_abs = scene.tree.render().absolute_bounds(scene.a);
    REQUIRE(scene.tree.move_child(scene.a, scene.other, std::nullopt));
    scene.tree.layout();
    const dg::PixelRect new_abs = scene.tree.render().absolute_bounds(scene.a);
    REQUIRE(old_abs != new_abs);

    const dg::PixelRect damaged = scene.tree.render().damage().bounds();
    for (const dg::PixelRect& rect : {old_abs, new_abs}) {
      CHECK(damaged.left() <= rect.left());
      CHECK(damaged.top() <= rect.top());
      CHECK(damaged.right() >= rect.right());
      CHECK(damaged.bottom() >= rect.bottom());
    }
  }

  TEST_CASE("cycles, bad refs and the root are refused without changing anything") {
    Scene scene;
    CHECK_FALSE(scene.tree.move_child(scene.a, scene.a, std::nullopt));
    CHECK_FALSE(scene.tree.move_child(scene.a, scene.a_child, std::nullopt));
    CHECK_FALSE(scene.tree.move_child(scene.column, scene.a_child, std::nullopt));
    CHECK_FALSE(scene.tree.move_child(scene.a, scene.column, scene.a));
    CHECK_FALSE(scene.tree.move_child(scene.b, scene.column, scene.a_child));
    CHECK_FALSE(scene.tree.move_child(LayoutTree::root(), scene.column, std::nullopt));
    CHECK(scene.kids(scene.column) == std::vector<NodeId>{scene.a, scene.b, scene.c});
    CHECK(scene.kids(scene.a) == std::vector<NodeId>{scene.a_child});
  }
}
