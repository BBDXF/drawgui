// Dropdown: the 9th WidgetKind, direct WidgetSet-level tests over
// dropdown_set_options()/dropdown_select() - the arithmetic
// examples/22_dropdown_menu's own headless check (dropdown_check.cpp)
// exercises through a real scene/PopupHost; this file pins it in isolation,
// matching test_form_controls.cpp's own precedent exactly. Unlike
// dropdown_check.cpp (which sets options at construction time, the same way
// examples/11_form_controls's sliders declare min/max/step once),
// dropdown_set_options() itself had NO test coverage anywhere before this
// file - a real gap found while defect-injecting this slice's own new logic.

#include <optional>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/types.h"
#include "drawgui/render/font_catalog.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/widget/focus.h"
#include "drawgui/widget/widget_set.h"

namespace {

using dg::Color;
using dg::FontCatalog;
using dg::FontId;
using dg::NodeId;
using dg::NodeStyle;
using dg::PixelRect;
using dg::PixelSize;
using dg::RenderTree;
using dg::TreeSpec;
using dg::Widget;
using dg::WidgetKind;
using dg::WidgetSet;

TreeSpec spec_for(PixelSize size = PixelSize{400, 300}) {
  TreeSpec spec;
  spec.viewport = size;
  return spec;
}

FontCatalog test_fonts() {
  const dg::Expected<FontCatalog, dg::FontError> scanned = FontCatalog::scan(DG_TEST_FONT_DIR);
  REQUIRE(scanned.has_value());
  return scanned.value();
}

struct DropdownScene {
  RenderTree tree;
  WidgetSet widgets;
  FontCatalog fonts;
  NodeId anchor;
  NodeId label;
};

DropdownScene build(std::vector<std::string> options) {
  RenderTree tree{spec_for()};
  WidgetSet widgets;
  FontCatalog fonts = test_fonts();
  const dg::Expected<FontId, dg::FontError> font_id = fonts.add("DgTest Latin", false);
  REQUIRE(font_id.has_value());

  const NodeId anchor =
      tree.add_child(RenderTree::root(), PixelRect{0, 0, 160, 32}, NodeStyle{});
  NodeStyle label_style;
  label_style.text.font = font_id.value();
  label_style.text.size = 14.0F;
  label_style.text.color = Color::from_argb(0xFFFFFFFF);
  const NodeId label = tree.add_child(anchor, PixelRect{0, 0, 160, 32}, label_style);

  Widget widget;
  widget.kind = WidgetKind::kDropdown;
  widget.label = label;
  widgets.attach(anchor, widget);
  widgets.dropdown_set_options(tree, fonts, anchor, std::move(options));

  return DropdownScene{std::move(tree), std::move(widgets), std::move(fonts), anchor, label};
}

TEST_CASE("dropdown: is a real WidgetKind, focusable and pointer-accepting") {
  DropdownScene scene = build({"Apple", "Banana", "Cherry"});
  CHECK(scene.widgets.at(scene.anchor).kind == WidgetKind::kDropdown);
  CHECK(dg::is_focusable(WidgetKind::kDropdown));
  CHECK(scene.widgets.accepts_pointer(scene.anchor));
}

TEST_CASE("dropdown: no selection until dropdown_select() is called") {
  DropdownScene scene = build({"Apple", "Banana", "Cherry"});
  CHECK_FALSE(scene.widgets.dropdown_selected_index(scene.anchor).has_value());
  CHECK(scene.tree.style(scene.label).text.text.empty());
}

TEST_CASE(
    "dropdown: selecting the first, a middle, and the last option updates both the "
    "stored index and the anchor's own label") {
  DropdownScene scene = build({"Apple", "Banana", "Cherry", "Date", "Elderberry"});

  CHECK(scene.widgets.dropdown_select(scene.tree, scene.fonts, scene.anchor, 0));
  CHECK(scene.widgets.dropdown_selected_index(scene.anchor) == 0);
  CHECK(scene.tree.style(scene.label).text.text == "Apple");

  CHECK(scene.widgets.dropdown_select(scene.tree, scene.fonts, scene.anchor, 2));
  CHECK(scene.widgets.dropdown_selected_index(scene.anchor) == 2);
  CHECK(scene.tree.style(scene.label).text.text == "Cherry");

  CHECK(scene.widgets.dropdown_select(scene.tree, scene.fonts, scene.anchor, 4));
  CHECK(scene.widgets.dropdown_selected_index(scene.anchor) == 4);
  CHECK(scene.tree.style(scene.label).text.text == "Elderberry");
}

TEST_CASE(
    "dropdown: dropdown_select() rejects out-of-range indices, in both directions, "
    "and reports no change") {
  DropdownScene scene = build({"Apple", "Banana"});
  CHECK_FALSE(scene.widgets.dropdown_select(scene.tree, scene.fonts, scene.anchor, -1));
  CHECK_FALSE(scene.widgets.dropdown_selected_index(scene.anchor).has_value());
  CHECK_FALSE(scene.widgets.dropdown_select(scene.tree, scene.fonts, scene.anchor, 2));
  CHECK_FALSE(scene.widgets.dropdown_selected_index(scene.anchor).has_value());
}

TEST_CASE(
    "dropdown: re-selecting the already-selected index is a no-op, matching "
    "set_slider_value()'s identical \"was this worth a repaint\" signal") {
  DropdownScene scene = build({"Apple", "Banana"});
  CHECK(scene.widgets.dropdown_select(scene.tree, scene.fonts, scene.anchor, 1));
  CHECK_FALSE(scene.widgets.dropdown_select(scene.tree, scene.fonts, scene.anchor, 1));
}

TEST_CASE(
    "dropdown: dropdown_set_options() clears a selected_index that no longer fits a "
    "shorter replacement list, rather than leaving it dangling against the new one") {
  DropdownScene scene = build({"Apple", "Banana", "Cherry"});
  REQUIRE(scene.widgets.dropdown_select(scene.tree, scene.fonts, scene.anchor, 2));
  REQUIRE(scene.widgets.dropdown_selected_index(scene.anchor) == 2);

  scene.widgets.dropdown_set_options(scene.tree, scene.fonts, scene.anchor, {"Only one"});
  CHECK_FALSE(scene.widgets.dropdown_selected_index(scene.anchor).has_value());
  CHECK(scene.widgets.dropdown_options(scene.anchor).size() == 1);
}

TEST_CASE(
    "dropdown: dropdown_set_options() clears a selected_index that is exactly ONE past "
    "the end of the new list - the off-by-one boundary a `>` instead of `>=` bound check "
    "would miss") {
  DropdownScene scene = build({"Apple", "Banana", "Cherry"});
  REQUIRE(scene.widgets.dropdown_select(scene.tree, scene.fonts, scene.anchor, 2));

  scene.widgets.dropdown_set_options(scene.tree, scene.fonts, scene.anchor, {"Kiwi", "Lime"});
  CHECK_FALSE(scene.widgets.dropdown_selected_index(scene.anchor).has_value());
}

TEST_CASE("dropdown: dropdown_set_options() keeps a selected_index that still fits") {
  DropdownScene scene = build({"Apple", "Banana", "Cherry"});
  REQUIRE(scene.widgets.dropdown_select(scene.tree, scene.fonts, scene.anchor, 1));

  scene.widgets.dropdown_set_options(scene.tree, scene.fonts, scene.anchor,
                                     {"Apricot", "Blueberry", "Coconut"});
  CHECK(scene.widgets.dropdown_selected_index(scene.anchor) == 1);
}

}  // namespace
