// The widget DISABLED state (DG_PROP_DISABLED): the property write itself,
// ancestor inheritance, and every interaction path a disabled widget must
// drop - pointer ownership, press cancel, Tab/click focus, focus loss,
// node-scoped shortcuts and the disabled paint. In-process RenderTree +
// WidgetSet like test_focus.cpp; the C ABI path lives in
// test_abi_tree_ops.cpp.

#include <algorithm>
#include <optional>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/types.h"
#include "drawgui/layout/box.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/props/node_props.h"
#include "drawgui/render/prop_ids.generated.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/shortcuts/action_ids.generated.h"
#include "drawgui/shortcuts/action_scopes.h"
#include "drawgui/shortcuts/chord.h"
#include "drawgui/shortcuts/router.h"
#include "drawgui/widget/focus.h"
#include "drawgui/widget/interaction.h"
#include "drawgui/widget/widget_set.h"

namespace {

using dg::Color;
using dg::Focus;
using dg::Interaction;
using dg::NodeId;
using dg::NodeStyle;
using dg::PixelPoint;
using dg::PixelRect;
using dg::RenderTree;
using dg::Widget;
using dg::WidgetKind;
using dg::WidgetSet;

constexpr Color kNormal = Color::rgba(0x10, 0x20, 0x30);
constexpr Color kHover = Color::rgba(0x11, 0x21, 0x31);
constexpr Color kPressed = Color::rgba(0x12, 0x22, 0x32);
constexpr Color kDisabled = Color::rgba(0x80, 0x80, 0x80);
constexpr Color kOn = Color::rgba(0x00, 0xFF, 0x00);
constexpr Color kOff = Color::rgba(0x00, 0x00, 0x00);
constexpr Color kOnDisabled = Color::rgba(0x60, 0x60, 0x60);

//   root
//    |- panel (kPanel)                 (0, 0, 400, 100)
//    |    |- button   (kButton)         (10, 10, 80, 30)
//    |    |- checkbox (kCheckbox)       (100, 10, 30, 30) + indicator child
//    |    |- radio_a / radio_b (kCheckbox, group 1)  (140/180, 10, 30, 30)
//    |    |- field    (kTextField)      (220, 10, 80, 30)
//    |    |- dropdown (kDropdown)       (310, 10, 80, 30)
//    |- slider (kSlider) + thumb        (10, 110, 100, 20)
//    |- card (kButton)                  (0, 150, 200, 80)
//    |    |- inner (kButton)            (10, 10, 80, 30) -> abs (10, 160)
//    |- list (kList)                    (220, 150, 100, 80)
//         |- row (kButton)              (0, 0, 100, 20) -> abs (220, 150)
dg::TreeSpec spec_of(int width, int height) {
  dg::TreeSpec spec;
  spec.viewport = dg::PixelSize{width, height};
  return spec;
}

struct Scene {
  RenderTree tree{spec_of(400, 300)};
  WidgetSet widgets;
  NodeId panel, button, checkbox, indicator, radio_a, radio_b, field, dropdown;
  NodeId slider, thumb, card, inner, list, row;
};

Widget make(WidgetKind kind) {
  Widget w;
  w.kind = kind;
  w.fill_normal = kNormal;
  w.fill_hover = kHover;
  w.fill_pressed = kPressed;
  w.fill_disabled = kDisabled;
  return w;
}

Scene build() {
  Scene s;
  const NodeId root = RenderTree::root();
  s.panel = s.tree.add_child(root, PixelRect{0, 0, 400, 100}, NodeStyle{});
  s.button = s.tree.add_child(s.panel, PixelRect{10, 10, 80, 30}, NodeStyle{});
  s.checkbox = s.tree.add_child(s.panel, PixelRect{100, 10, 30, 30}, NodeStyle{});
  s.indicator = s.tree.add_child(s.checkbox, PixelRect{5, 5, 20, 20}, NodeStyle{});
  s.radio_a = s.tree.add_child(s.panel, PixelRect{140, 10, 30, 30}, NodeStyle{});
  s.radio_b = s.tree.add_child(s.panel, PixelRect{180, 10, 30, 30}, NodeStyle{});
  s.field = s.tree.add_child(s.panel, PixelRect{220, 10, 80, 30}, NodeStyle{});
  s.dropdown = s.tree.add_child(s.panel, PixelRect{310, 10, 80, 30}, NodeStyle{});
  s.slider = s.tree.add_child(root, PixelRect{10, 110, 100, 20}, NodeStyle{});
  s.thumb = s.tree.add_child(s.slider, PixelRect{0, 0, 10, 20}, NodeStyle{});
  s.card = s.tree.add_child(root, PixelRect{0, 150, 200, 80}, NodeStyle{});
  s.inner = s.tree.add_child(s.card, PixelRect{10, 10, 80, 30}, NodeStyle{});
  s.list = s.tree.add_child(root, PixelRect{220, 150, 100, 80}, NodeStyle{});
  s.row = s.tree.add_child(s.list, PixelRect{0, 0, 100, 20}, NodeStyle{});

  s.widgets.attach(s.panel, make(WidgetKind::kPanel));
  s.widgets.attach(s.button, make(WidgetKind::kButton));
  Widget check = make(WidgetKind::kCheckbox);
  check.indicator = s.indicator;
  check.indicator_on = kOn;
  check.indicator_off = kOff;
  check.indicator_disabled = kOnDisabled;
  s.widgets.attach(s.checkbox, check);
  s.widgets.attach(s.indicator, Widget{});
  Widget radio = make(WidgetKind::kCheckbox);
  radio.group = 1;
  s.widgets.attach(s.radio_a, radio);
  s.widgets.attach(s.radio_b, radio);
  s.widgets.attach(s.field, make(WidgetKind::kTextField));
  s.widgets.attach(s.dropdown, make(WidgetKind::kDropdown));
  Widget slide = make(WidgetKind::kSlider);
  slide.thumb = s.thumb;
  s.widgets.attach(s.slider, slide);
  s.widgets.attach(s.card, make(WidgetKind::kButton));
  s.widgets.attach(s.inner, make(WidgetKind::kButton));
  Widget list = make(WidgetKind::kList);
  list.list_axis = dg::ScrollAxis::kVertical;
  s.widgets.attach(s.list, list);
  s.widgets.attach(s.row, make(WidgetKind::kButton));
  return s;
}

void set_disabled(RenderTree& tree, NodeId id, bool disabled) {
  NodeStyle style = tree.style(id);
  style.disabled = disabled;
  tree.set_style(id, style);
}

PixelPoint centre_of(const RenderTree& tree, NodeId id) {
  const PixelRect r = tree.absolute_bounds(id);
  return PixelPoint{r.x + (r.width / 2), r.y + (r.height / 2)};
}

}  // namespace

TEST_SUITE("disabled") {
  TEST_CASE("DG_PROP_DISABLED is a bool property written through dg::set_prop()") {
    dg::LayoutTree tree{spec_of(100, 100)};
    const NodeId node = tree.add_child(dg::LayoutTree::root(), dg::BoxStyle{}, NodeStyle{});

    REQUIRE(dg::prop_type(DG_PROP_DISABLED) == dg::PropType::k_bool);
    CHECK(dg::set_prop(tree, node, DG_PROP_DISABLED, dg::PropValue::boolean(true)).ok());
    CHECK(tree.render().style(node).disabled);
    CHECK(dg::set_prop(tree, node, DG_PROP_DISABLED, dg::PropValue::boolean(false)).ok());
    CHECK_FALSE(tree.render().style(node).disabled);

    CHECK(dg::set_prop(tree, node, DG_PROP_DISABLED, dg::PropValue::number(1.0F)).status ==
          dg::PropStatus::kTypeMismatch);
    CHECK(dg::set_prop(tree, node, DG_PROP_DISABLED, dg::PropValue::boolean_bits(2)).status ==
          dg::PropStatus::kValueOutOfRange);
    CHECK_FALSE(tree.render().style(node).disabled);
  }

  TEST_CASE("is_disabled() inherits from any ancestor and re-enabling restores the subtree") {
    Scene s = build();
    CHECK_FALSE(dg::is_disabled(s.tree, s.button));
    set_disabled(s.tree, s.panel, true);
    CHECK(dg::is_disabled(s.tree, s.panel));
    CHECK(dg::is_disabled(s.tree, s.button));
    CHECK(dg::is_disabled(s.tree, s.indicator));
    CHECK_FALSE(dg::is_disabled(s.tree, s.slider));

    set_disabled(s.tree, s.button, true);
    set_disabled(s.tree, s.panel, false);
    CHECK(dg::is_disabled(s.tree, s.button));  // its own bit survives
    CHECK_FALSE(dg::is_disabled(s.tree, s.checkbox));
  }

  TEST_CASE("every interactive kind stops owning the pointer when disabled") {
    Scene s = build();
    const std::vector<NodeId> kinds = {s.button, s.checkbox, s.radio_a, s.field, s.dropdown};
    for (const NodeId id : kinds) {
      CHECK(s.widgets.widget_at(s.tree, centre_of(s.tree, id)) == std::optional<NodeId>{id});
      set_disabled(s.tree, id, true);
      CHECK_FALSE(s.widgets.widget_at(s.tree, centre_of(s.tree, id)).has_value());
      set_disabled(s.tree, id, false);
    }
    // The checkbox's indicator child climbs to the disabled checkbox, not past it.
    set_disabled(s.tree, s.checkbox, true);
    CHECK_FALSE(s.widgets.widget_at(s.tree, centre_of(s.tree, s.indicator)).has_value());

    // Slider drag ownership.
    CHECK(s.widgets.slidable_owner_of(s.tree, s.thumb) == std::optional<NodeId>{s.slider});
    set_disabled(s.tree, s.slider, true);
    CHECK_FALSE(s.widgets.slidable_owner_of(s.tree, s.thumb).has_value());
  }

  TEST_CASE("a disabled widget swallows the pointer rather than passing it to its owner") {
    Scene s = build();
    set_disabled(s.tree, s.inner, true);
    CHECK_FALSE(s.widgets.widget_at(s.tree, centre_of(s.tree, s.inner)).has_value());
    // The card itself, outside the inner button, is still live.
    CHECK(s.widgets.widget_at(s.tree, PixelPoint{150, 200}) == std::optional<NodeId>{s.card});
  }

  TEST_CASE("a disabled list disables its rows but still finds its list owner for scrolling") {
    Scene s = build();
    set_disabled(s.tree, s.list, true);
    CHECK_FALSE(s.widgets.widget_at(s.tree, centre_of(s.tree, s.row)).has_value());
    CHECK(s.widgets.list_owner_of(s.tree, s.row) == std::optional<NodeId>{s.list});
  }

  TEST_CASE("disabling during a press cancels it: released, never clicked") {
    Scene s = build();
    Interaction interaction;
    const PixelPoint at = centre_of(s.tree, s.button);
    interaction.moved_over(s.widgets.widget_at(s.tree, at));
    const dg::InteractionChange press = interaction.pressed_on(s.widgets.widget_at(s.tree, at));
    REQUIRE(press.pressed == std::optional<NodeId>{s.button});

    set_disabled(s.tree, s.panel, true);
    const dg::InteractionChange cancel = interaction.cancel_disabled(s.tree);
    CHECK(cancel.released == std::optional<NodeId>{s.button});
    CHECK(cancel.left == std::optional<NodeId>{s.button});
    CHECK_FALSE(cancel.clicked.has_value());
    CHECK_FALSE(interaction.holding().has_value());
    CHECK_FALSE(interaction.hovered().has_value());

    // Re-enabling before the release still produces no click.
    set_disabled(s.tree, s.panel, false);
    const dg::InteractionChange release =
        interaction.released_on(s.widgets.widget_at(s.tree, at));
    CHECK_FALSE(release.clicked.has_value());
  }

  TEST_CASE("a press that is not cancelled explicitly still cannot click a disabled widget") {
    Scene s = build();
    Interaction interaction;
    const PixelPoint at = centre_of(s.tree, s.checkbox);
    interaction.pressed_on(s.widgets.widget_at(s.tree, at));
    set_disabled(s.tree, s.checkbox, true);
    const dg::InteractionChange release =
        interaction.released_on(s.widgets.widget_at(s.tree, at));
    CHECK(release.released == std::optional<NodeId>{s.checkbox});
    CHECK_FALSE(release.clicked.has_value());
  }

  TEST_CASE("cancel_disabled() is a no-op while nothing touched is disabled") {
    Scene s = build();
    Interaction interaction;
    interaction.pressed_on(s.button);
    set_disabled(s.tree, s.slider, true);
    CHECK_FALSE(interaction.cancel_disabled(s.tree).any());
    CHECK(interaction.holding() == std::optional<NodeId>{s.button});
  }

  TEST_CASE("focus_order() skips a disabled widget and a disabled container's subtree") {
    Scene s = build();
    const NodeId root = RenderTree::root();
    std::vector<NodeId> order = dg::focus_order(s.tree, s.widgets, root);
    CHECK(order.size() == 10);

    set_disabled(s.tree, s.button, true);
    order = dg::focus_order(s.tree, s.widgets, root);
    CHECK(std::find(order.begin(), order.end(), s.button) == order.end());
    CHECK(order.size() == 9);

    set_disabled(s.tree, s.panel, true);
    order = dg::focus_order(s.tree, s.widgets, root);
    CHECK(order == std::vector<NodeId>{s.slider, s.card, s.inner, s.row});
    CHECK(dg::focus_order(s.tree, s.widgets, s.panel).empty());

    Focus focus;
    focus.set(s.slider);
    CHECK(focus.focus_previous(s.tree, s.widgets, root).focused ==
          std::optional<NodeId>{s.row});
  }

  TEST_CASE("set_guarded() refuses a disabled target, by its own bit or an ancestor's") {
    Scene s = build();
    Focus focus;
    set_disabled(s.tree, s.field, true);
    CHECK_FALSE(focus.set_guarded(s.tree, s.field).any());
    CHECK_FALSE(focus.current().has_value());
    set_disabled(s.tree, s.field, false);
    set_disabled(s.tree, s.panel, true);
    CHECK_FALSE(focus.set_guarded(s.tree, s.dropdown).any());
    CHECK(focus.set_guarded(s.tree, s.slider).focused == std::optional<NodeId>{s.slider});
  }

  TEST_CASE("disabling the focused widget (or its container) blurs it") {
    Scene s = build();
    Focus focus;
    focus.set(s.field);
    CHECK_FALSE(focus.blur_if_disabled(s.tree).any());
    set_disabled(s.tree, s.panel, true);
    const dg::FocusChange change = focus.blur_if_disabled(s.tree);
    CHECK(change.blurred == std::optional<NodeId>{s.field});
    CHECK_FALSE(change.focused.has_value());
    CHECK_FALSE(focus.current().has_value());
  }

  TEST_CASE("node-scoped shortcuts on a disabled focus never resolve") {
    Scene s = build();
    dg::ActionScopes scopes;
    scopes.scope(s.field, DG_ACTION_SELECT_ALL);
    const dg::Chord select_all{dg::Modifier::kMod, dg::LogicalKey::kA};
    const dg::WindowId window{1};
    CHECK(dg::resolve_action(select_all, window, s.field, s.tree, scopes,
                             dg::all_shortcut_bindings())
              .has_value());
    set_disabled(s.tree, s.field, true);
    const std::optional<dg::ResolvedAction> resolved = dg::resolve_action(
        select_all, window, s.field, s.tree, scopes, dg::all_shortcut_bindings());
    CHECK((!resolved.has_value() || !resolved->target.has_value()));
  }

  TEST_CASE("refresh() paints fill_disabled whatever the pointer state, and back") {
    Scene s = build();
    s.widgets.toggle(s.checkbox);
    s.widgets.refresh(s.tree, s.checkbox, dg::PointerState{true, false});
    CHECK(s.tree.style(s.checkbox).fill == kHover);
    CHECK(s.tree.style(s.indicator).fill == kOn);

    set_disabled(s.tree, s.panel, true);
    Interaction interaction;
    s.widgets.refresh_subtree(s.tree, s.panel, interaction);
    CHECK(s.tree.style(s.checkbox).fill == kDisabled);
    CHECK(s.tree.style(s.indicator).fill == kOnDisabled);
    CHECK(s.tree.style(s.button).fill == kDisabled);
    s.widgets.refresh(s.tree, s.button, dg::PointerState{true, true});
    CHECK(s.tree.style(s.button).fill == kDisabled);

    set_disabled(s.tree, s.panel, false);
    s.widgets.refresh_subtree(s.tree, s.panel, interaction);
    CHECK(s.tree.style(s.checkbox).fill == kNormal);
    CHECK(s.tree.style(s.indicator).fill == kOn);
  }

  TEST_CASE("an unset fill_disabled falls back to fill_normal, ignoring hover") {
    Scene s = build();
    Widget plain = make(WidgetKind::kButton);
    plain.fill_disabled.reset();
    const NodeId id =
        s.tree.add_child(RenderTree::root(), PixelRect{0, 260, 20, 20}, NodeStyle{});
    s.widgets.attach(id, plain);
    set_disabled(s.tree, id, true);
    s.widgets.refresh(s.tree, id, dg::PointerState{true, false});
    CHECK(s.tree.style(id).fill == kNormal);
  }
}
