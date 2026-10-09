// Runtime gaps closed for the showcase: auto sizes, theme-bound buttons and
// toggles, editor placeholders and insets, label wrapping and padding,
// paint-time image tints, data views that grow, and the full-width tab strip.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "app/runtime_debug.h"
#include "drawgui/app/prop.h"
#include "drawgui/render/image_catalog.h"
#include "drawgui/theme/theme_loader.h"
#include "drawgui/theme/token_ids.generated.h"
#include "runtime_fixture.h"

using runtime_test::Rt;

namespace {

std::shared_ptr<dg::Theme> builtin_theme() {
  dg::Expected<dg::Theme, dg::ThemeLoadError> loaded = dg::load_builtin_theme();
  REQUIRE(loaded.has_value());
  return std::make_shared<dg::Theme>(std::move(loaded).value());
}

dg::Color token(const dg::Theme& theme, dg_token_id id, dg::ThemeVariant variant) {
  const std::optional<dg::Color> c = theme.color_value(id, variant);
  REQUIRE(c.has_value());
  return *c;
}

// The first node under `root` (inclusive) painting exactly `text`.
std::optional<dg::NodeId> node_with_text(const dg::RenderTree& tree, dg::NodeId root,
                                         const std::string& text) {
  for (const dg::NodeId id : dg::subtree_of(tree, root)) {
    if (tree.style(id).text.text == text) {
      return id;
    }
  }
  return std::nullopt;
}

// BGRA of the painted frame at (x, y).
std::uint32_t pixel(const dg::Window& window, int x, int y) {
  const dg::RasterSurface* surface = dg::app_detail::painted_surface(window);
  REQUIRE(surface != nullptr);
  const dg::PixelView view = surface->peek_pixels();
  const std::uint8_t* px =
      view.pixels + (static_cast<std::size_t>(y) * view.row_bytes) + (static_cast<std::size_t>(x) * 4);
  return (static_cast<std::uint32_t>(px[3]) << 24U) | (static_cast<std::uint32_t>(px[2]) << 16U) |
         (static_cast<std::uint32_t>(px[1]) << 8U) | px[0];
}

}  // namespace

TEST_SUITE("runtime polish") {
  TEST_CASE("auto width/height: a box drops its 240x120 default and hugs its content") {
    Rt t;
    const dg::NodeId box = t.add(dg::NodeKind::kBox);
    REQUIRE(t.win->set(box, {dg::prop::row(), dg::prop::gap(0)}) == dg::Status::kOk);
    (void)t.pump(1);
    CHECK(t.bounds(box).height == 120);
    REQUIRE(t.win->set(box, {dg::prop::width_auto(), dg::prop::height_auto()}) ==
            dg::Status::kOk);
    const dg::NodeId chip = t.add(dg::NodeKind::kBox, box);
    REQUIRE(t.win->set(chip, {dg::prop::width(30), dg::prop::height(20)}) == dg::Status::kOk);
    (void)t.pump(1);
    CHECK(t.bounds(box).height == 20);
    CHECK(t.bounds(box).width == 30);
    // A percentage, then auto: auto clears the percentage too.
    REQUIRE(t.win->set(box, dg::prop::width_percent(50)) == dg::Status::kOk);
    (void)t.pump(1);
    CHECK(t.bounds(box).width == 240);
    REQUIRE(t.win->set(box, dg::prop::width_auto()) == dg::Status::kOk);
    (void)t.pump(1);
    CHECK(t.bounds(box).width == 30);
    // Only the six size properties take the auto unit.
    CHECK(t.win->set_prop(box, DG_PROP_GAP, dg::PropValue::auto_length()) ==
          dg::Status::kTypeMismatch);
  }

  TEST_CASE("buttons, checkboxes and radios take their colours from the theme") {
    Rt t;
    const std::shared_ptr<dg::Theme> theme = builtin_theme();
    t.app.set_theme(theme, dg::ThemeVariant::kLight);
    const dg::NodeId button = t.add(dg::NodeKind::kButton);
    const dg::NodeId check = t.add(dg::NodeKind::kCheckbox);
    const dg::NodeId radio = t.add(dg::NodeKind::kRadio);
    (void)t.pump(1);
    const dg::RenderTree& tree = t.win->layout().render();
    const auto light = dg::ThemeVariant::kLight;
    CHECK(t.win->widgets().at(button).fill_normal ==
          token(*theme, DG_TOKEN_COLOR_PRIMARY, light));
    CHECK(t.win->widgets().at(button).fill_hover ==
          token(*theme, DG_TOKEN_COLOR_PRIMARY_HOVER, light));
    CHECK(tree.style(button).text.color == token(*theme, DG_TOKEN_COLOR_ON_PRIMARY, light));
    CHECK(tree.style(check).fill == token(*theme, DG_TOKEN_COLOR_SURFACE, light));
    CHECK(tree.style(check).border_color == token(*theme, DG_TOKEN_COLOR_BORDER, light));
    const dg::NodeId indicator = t.win->widgets().at(check).indicator;
    CHECK(tree.style(indicator).radii.top_left > 0.0F);
    CHECK(t.win->widgets().at(check).indicator_on ==
          token(*theme, DG_TOKEN_COLOR_PRIMARY, light));

    // A caller's own background colour wins and survives a switch; the
    // themed ones follow it.
    const dg::Color mine = dg::Color::from_argb(0xFF336699);
    REQUIRE(t.win->set(radio, dg::prop::background(mine)) == dg::Status::kOk);
    REQUIRE(t.app.set_theme_variant(dg::ThemeVariant::kDark) == dg::Status::kOk);
    (void)t.pump(1);
    const auto dark = dg::ThemeVariant::kDark;
    CHECK(t.win->widgets().at(button).fill_normal ==
          token(*theme, DG_TOKEN_COLOR_PRIMARY, dark));
    CHECK(tree.style(check).fill == token(*theme, DG_TOKEN_COLOR_SURFACE, dark));
    CHECK(t.win->widgets().at(radio).fill_normal == mine);
    CHECK(t.win->widgets().at(radio).fill_hover != mine);

    // A token-bound background follows the switch through the widget fills.
    REQUIRE(t.win->bind_token(button, DG_PROP_BACKGROUND_COLOR, DG_TOKEN_COLOR_BORDER) ==
            dg::Status::kOk);
    CHECK(t.win->widgets().at(button).fill_normal == token(*theme, DG_TOKEN_COLOR_BORDER, dark));
    REQUIRE(t.app.set_theme_variant(dg::ThemeVariant::kLight) == dg::Status::kOk);
    CHECK(t.win->widgets().at(button).fill_normal ==
          token(*theme, DG_TOKEN_COLOR_BORDER, light));
  }

  TEST_CASE("placeholder: shown muted while the editor is empty, hidden once it has text") {
    Rt t;
    const dg::NodeId field = t.add(dg::NodeKind::kTextField);
    const dg::NodeId area = t.add(dg::NodeKind::kTextArea);
    REQUIRE(t.win->set_text(field, DG_PROP_PLACEHOLDER, "Your name") == dg::Status::kOk);
    REQUIRE(t.win->set_text(area, DG_PROP_PLACEHOLDER, "Notes") == dg::Status::kOk);
    const dg::NodeId label = t.add(dg::NodeKind::kLabel);
    CHECK(t.win->set_text(label, DG_PROP_PLACEHOLDER, "x") == dg::Status::kNotApplicable);
    (void)t.pump(1);
    const dg::RenderTree& tree = t.win->layout().render();
    const std::optional<dg::NodeId> hint = node_with_text(tree, field, "Your name");
    REQUIRE(hint.has_value());
    const dg::NodeId content = t.win->widgets().at(field).content;
    CHECK(tree.style(*hint).text.color.alpha() < tree.style(content).text.color.alpha());
    CHECK(tree.local_bounds(*hint).x == 10);
    CHECK(node_with_text(tree, area, "Notes").has_value());
    CHECK(tree.style(*node_with_text(tree, area, "Notes")).text.wrap);

    REQUIRE(t.win->focus(field) == dg::Status::kOk);
    t.wm().post_text_input(t.win->id(), "Ann");
    (void)t.pump(1);
    CHECK(!node_with_text(tree, field, "Your name").has_value());
    REQUIRE(t.win->set_text(field, DG_PROP_TEXT, "") == dg::Status::kOk);
    CHECK(node_with_text(tree, field, "Your name").has_value());
  }

  TEST_CASE("text field: a 10px default text inset, moved by the padding properties") {
    Rt t;
    const dg::NodeId field = t.add(dg::NodeKind::kTextField);
    REQUIRE(t.win->set_text(field, DG_PROP_TEXT, "hello") == dg::Status::kOk);
    (void)t.pump(1);
    const dg::RenderTree& tree = t.win->layout().render();
    const dg::NodeId content = t.win->widgets().at(field).content;
    CHECK(tree.local_bounds(content).x == 10);
    REQUIRE(t.win->set_prop(field, DG_PROP_PADDING_L, dg::PropValue::number(4)) ==
            dg::Status::kOk);
    (void)t.pump(1);
    CHECK(tree.local_bounds(content).x == 4);
    // A click left of the text, inside the inset, puts the caret at 0.
    REQUIRE(t.win->focus(field) == dg::Status::kOk);
    const dg::PixelRect box = t.bounds(field);
    t.press(box.x + 2, box.y + (box.height / 2));
    (void)t.pump(1);
    CHECK(tree.local_bounds(t.win->widgets().at(field).caret).x == 4);
  }

  TEST_CASE("labels: wrap_text wraps, padding moves the text") {
    Rt t;
    const dg::NodeId label = t.add(dg::NodeKind::kLabel);
    REQUIRE(t.win->set(label, dg::prop::wrap_text()) == dg::Status::kOk);
    REQUIRE(t.win->set_prop(label, DG_PROP_PADDING_L, dg::PropValue::number(12)) ==
            dg::Status::kOk);
    REQUIRE(t.win->set_prop(label, DG_PROP_PADDING_T, dg::PropValue::number(6)) ==
            dg::Status::kOk);
    const dg::RenderTree& tree = t.win->layout().render();
    CHECK(tree.style(label).text.wrap);
    CHECK(tree.style(label).text.pad_left == 12);
    CHECK(tree.style(label).text.pad_top == 6);
    CHECK(t.win->layout().box(label).padding.left == 12);
    REQUIRE(t.win->set(label, dg::prop::wrap_text(false)) == dg::Status::kOk);
    CHECK(!tree.style(label).text.wrap);
  }

  TEST_CASE("image_tint recolours one decoded SVG at paint time") {
    Rt t;
    const std::string svg =
        R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 10 10" width="10" height="10">)"
        R"(<rect width="10" height="10" fill="#000"/></svg>)";
    const dg::Expected<dg::ImageId, dg::ImageError> id = t.app.images().decode_svg(
        reinterpret_cast<const std::uint8_t*>(svg.data()), svg.size(), dg::SvgOptions{});
    REQUIRE(id.has_value());
    const dg::NodeId a = t.add(dg::NodeKind::kImage);
    const dg::NodeId b = t.add(dg::NodeKind::kImage);
    REQUIRE(t.win->set_image(a, id.value()) == dg::Status::kOk);
    REQUIRE(t.win->set_image(b, id.value()) == dg::Status::kOk);
    REQUIRE(t.win->set(a, dg::prop::image_tint(dg::Color::from_argb(0xFFFF0000))) ==
            dg::Status::kOk);
    (void)t.pump(2);
    const dg::PixelRect ra = t.bounds(a);
    const dg::PixelRect rb = t.bounds(b);
    CHECK(pixel(*t.win, ra.x + (ra.width / 2), ra.y + (ra.height / 2)) == 0xFFFF0000U);
    CHECK(pixel(*t.win, rb.x + (rb.width / 2), rb.y + (rb.height / 2)) == 0xFF000000U);
    REQUIRE(t.win->set(a, dg::prop::image_tint(dg::Color::from_argb(0xFF00FF00))) ==
            dg::Status::kOk);
    (void)t.pump(2);
    CHECK(pixel(*t.win, ra.x + (ra.width / 2), ra.y + (ra.height / 2)) == 0xFF00FF00U);
  }

  TEST_CASE("list, tree and table grow with their container") {
    Rt t;
    const dg::NodeId row = t.add(dg::NodeKind::kBox);
    REQUIRE(t.win->set(row, {dg::prop::row(), dg::prop::gap(0), dg::prop::width_percent(100),
                             dg::prop::grow(1)}) == dg::Status::kOk);
    REQUIRE(t.win->set_prop(row, DG_PROP_ALIGN, dg::PropValue::option(DG_ALIGN_STRETCH)) ==
            dg::Status::kOk);
    const dg::NodeId list = t.add(dg::NodeKind::kList, row);
    const dg::NodeId tree_node = t.add(dg::NodeKind::kTree, row);
    const dg::NodeId table = t.add(dg::NodeKind::kTable, row);
    for (const dg::NodeId node : {list, tree_node, table}) {
      REQUIRE(t.win->set(node, {dg::prop::width_auto(), dg::prop::height_auto(),
                                dg::prop::grow(1)}) == dg::Status::kOk);
    }
    REQUIRE(t.win->tree(tree_node).value().set_items({{"a", 0, false}}) == dg::Status::kOk);
    REQUIRE(t.win->table(table).value().set_columns({{"c", std::nullopt, std::nullopt, false}}) ==
            dg::Status::kOk);
    (void)t.pump(3);
    const dg::RenderTree& tree = t.win->layout().render();
    CHECK(t.bounds(row).height == 360);
    for (const dg::NodeId node : {list, tree_node, table}) {
      CHECK(t.bounds(node).height == 360);
      CHECK(t.bounds(node).width == 160);
    }
    // The composed views fill the grown node, not their 240px default.
    CHECK(t.bounds(tree.children(tree_node).front()).height == 360);
    CHECK(t.bounds(tree.children(table).front()).height == 360);
    CHECK(t.bounds(tree.children(tree_node).front()).width == 160);
  }

  TEST_CASE("tabs: the strip's background spans the whole tabs node") {
    Rt t;
    const dg::NodeId tabs = t.add(dg::NodeKind::kTabs);
    REQUIRE(t.win->set(tabs, dg::prop::width_percent(100)) == dg::Status::kOk);
    REQUIRE(t.win->tabs(tabs).value().add("One").has_value());
    (void)t.pump(1);
    const dg::RenderTree& tree = t.win->layout().render();
    const dg::NodeId strip = tree.children(tree.children(tabs).front()).front();
    CHECK(t.bounds(strip).width == t.bounds(tabs).width);
    CHECK(tree.style(strip).fill.alpha() == 0xFF);
  }
}
