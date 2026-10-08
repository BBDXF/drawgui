// Scrollbars on kScrollView and kList: geometry, visibility modes, thumb
// drag, track paging, hover/press, the disabled rule, theme tokens and
// DG_PROP_SCROLLBAR. Expected numbers are derived by hand from the fixture's
// sizes, never read back from the code under test.

#include <optional>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/base/expected.h"
#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/types.h"
#include "drawgui/layout/box.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/props/node_props.h"
#include "drawgui/render/prop_ids.generated.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/shortcuts/keyboard_scroll.h"
#include "drawgui/theme/theme.h"
#include "drawgui/theme/theme_loader.h"
#include "drawgui/theme/token_ids.generated.h"
#include "drawgui/widget/focus.h"
#include "drawgui/widget/interaction.h"
#include "drawgui/widget/widget_set.h"

namespace {

using dg::BoxStyle;
using dg::Color;
using dg::LayoutKind;
using dg::LayoutTree;
using dg::NodeId;
using dg::NodeStyle;
using dg::PixelPoint;
using dg::PixelRect;
using dg::PixelSize;
using dg::ScrollAxis;
using dg::ScrollbarGeometry;
using dg::ScrollbarMode;
using dg::ScrollbarStyle;
using dg::ScrollResult;
using dg::Widget;
using dg::WidgetKind;
using dg::WidgetSet;

// A 100x100 viewport with a 10 px gutter on the bar's side. Ten 40 px items
// make 400 px of content against a 100 px viewport: max offset 300, thumb
// 100 * 100 / 400 = 25 px, travel 75 px.
constexpr int kViewport = 100;
constexpr int kGutter = 10;
constexpr int kItem = 40;
constexpr int kItems = 10;

const Color kTrack = Color::from_argb(0xFF101010);
const Color kThumb = Color::from_argb(0xFF202020);
const Color kThumbHover = Color::from_argb(0xFF303030);
const Color kThumbPressed = Color::from_argb(0xFF404040);
const Color kDisabledFill = Color::from_argb(0xFF505050);
const Color kDisabledOn = Color::from_argb(0xFF606060);

ScrollbarStyle test_style() {
  ScrollbarStyle style;
  style.track = kTrack;
  style.thumb = kThumb;
  style.thumb_hover = kThumbHover;
  style.thumb_pressed = kThumbPressed;
  style.size = kGutter;
  style.min_thumb_length = 24;
  return style;
}

dg::TreeSpec spec() {
  dg::TreeSpec spec;
  spec.viewport = PixelSize{300, 300};
  return spec;
}

struct View {
  LayoutTree tree{spec()};
  WidgetSet widgets;
  NodeId view;
  NodeId content;
  std::vector<NodeId> items;
  NodeId track;
  NodeId thumb;

  View(ScrollAxis axis, int items_count) {
    const bool vertical = axis == ScrollAxis::kVertical;
    BoxStyle root;
    root.kind = LayoutKind::kColumn;
    root.padding = dg::EdgeInsets::all(20);
    tree.set_box(LayoutTree::root(), root);

    BoxStyle box;
    box.kind = LayoutKind::kLeaf;
    box.scroll_axis = axis;
    box.width = kViewport;
    box.height = kViewport;
    if (vertical) {
      box.padding.right = kGutter;
    } else {
      box.padding.bottom = kGutter;
    }
    NodeStyle style;
    style.overflow = dg::Overflow::kClip;
    view = tree.add_child(LayoutTree::root(), box, style);

    BoxStyle column;
    column.kind = vertical ? LayoutKind::kColumn : LayoutKind::kRow;
    content = tree.add_child(view, column, NodeStyle{});
    for (int i = 0; i < items_count; ++i) {
      BoxStyle item;
      item.width = vertical ? kViewport - kGutter : kItem;
      item.height = vertical ? kItem : kViewport - kGutter;
      items.push_back(tree.add_child(content, item, NodeStyle{}));
    }
    track = tree.add_child(view, BoxStyle{}, NodeStyle{});
    thumb = tree.add_child(track, BoxStyle{}, NodeStyle{});

    Widget widget;
    widget.kind = WidgetKind::kScrollView;
    widget.scroll_axis = axis;
    widget.scroll_content = content;
    widgets.attach(view, widget);
    widgets.set_scrollbar_style(test_style());
    widgets.attach_scrollbar(tree.render(), view, track, thumb);
    tree.layout_full();
    sync();
  }

  [[nodiscard]] int extent() const {
    const PixelRect box = tree.content_bounds(view);
    return widgets.at(view).scroll_axis == ScrollAxis::kHorizontal ? box.width : box.height;
  }
  void sync() { widgets.sync_scrollbar(tree.render(), view, extent()); }
  [[nodiscard]] ScrollbarGeometry geometry() const {
    return widgets.scrollbar_geometry(tree.render(), view, extent());
  }
  [[nodiscard]] PixelRect abs(NodeId id) const { return tree.render().absolute_bounds(id); }
  [[nodiscard]] int offset_y() const { return tree.render().scroll_offset(view).y; }
  bool scroll(int dy) {
    return widgets.scroll_by(tree.render(), view, tree.content_bounds(view), 0, dy);
  }
};

}  // namespace

TEST_SUITE("scrollbar geometry") {
  TEST_CASE("thumb is proportional and the track sits in the trailing gutter") {
    View v{ScrollAxis::kVertical, kItems};
    const ScrollbarGeometry g = v.geometry();
    REQUIRE(g.visible);
    CHECK(g.axis == ScrollAxis::kVertical);
    CHECK(g.max_offset == 300);
    CHECK(g.track == PixelRect{kViewport - kGutter, 0, kGutter, kViewport});
    CHECK(g.thumb == PixelRect{0, 0, kGutter, 25});

    const PixelRect view = v.abs(v.view);
    CHECK(v.abs(v.track) == PixelRect{view.x + 90, view.y, 10, 100});
    CHECK(v.abs(v.thumb) == PixelRect{view.x + 90, view.y, 10, 25});
    // The gutter is reserved: no item reaches under the bar.
    CHECK(v.abs(v.items[0]).right() <= v.abs(v.track).x);
  }

  TEST_CASE(
      "the thumb tracks wheel, keyboard and programmatic scrolling; the track stays put") {
    View v{ScrollAxis::kVertical, kItems};
    const PixelRect track_before = v.abs(v.track);
    const PixelRect view = v.abs(v.view);

    REQUIRE(v.scroll(150));  // wheel / programmatic: scroll_by()
    CHECK(v.abs(v.track) == track_before);
    // 75 px of travel * 150 / 300 = 37.5, rounded half away from zero.
    CHECK(v.abs(v.thumb).y == view.y + 38);

    REQUIRE(dg::apply_keyboard_scroll(DG_ACTION_SCROLL_TO_END, v.items[0], v.tree, v.widgets));
    CHECK(v.offset_y() == 300);
    CHECK(v.abs(v.thumb) == PixelRect{view.x + 90, view.y + 75, 10, 25});
    CHECK(v.abs(v.track) == track_before);

    // A raw RenderTree write bypasses scroll_by(); sync_scrollbar() catches up.
    v.tree.render().set_scroll_offset(v.view, PixelPoint{0, 0});
    v.sync();
    CHECK(v.abs(v.thumb).y == view.y);
    CHECK(v.abs(v.track) == track_before);
  }

  TEST_CASE("a very long content clamps the thumb to its minimum length") {
    View v{ScrollAxis::kVertical, 1000};  // 40000 px: 100*100/40000 rounds to 0
    CHECK(v.geometry().thumb.height == 24);
    REQUIRE(dg::apply_keyboard_scroll(DG_ACTION_SCROLL_TO_END, v.items[0], v.tree, v.widgets));
    CHECK(v.abs(v.thumb).bottom() == v.abs(v.track).bottom());
  }

  TEST_CASE("horizontal scroll views get a bottom bar") {
    View v{ScrollAxis::kHorizontal, kItems};
    const ScrollbarGeometry g = v.geometry();
    REQUIRE(g.visible);
    CHECK(g.track == PixelRect{0, kViewport - kGutter, kViewport, kGutter});
    CHECK(g.thumb == PixelRect{0, 0, 25, kGutter});
    REQUIRE(
        v.widgets.scroll_by(v.tree.render(), v.view, v.tree.content_bounds(v.view), 300, 0));
    CHECK(v.abs(v.thumb).x == v.abs(v.view).x + 75);
  }
}

TEST_SUITE("scrollbar visibility") {
  TEST_CASE("auto hides the bar when the content fits; nothing is hit there") {
    View v{ScrollAxis::kVertical, 2};  // 80 px of content in 100 px
    CHECK_FALSE(v.geometry().visible);
    CHECK(v.abs(v.track).is_empty());
    CHECK(v.abs(v.thumb).is_empty());
    const PixelRect view = v.abs(v.view);
    const std::optional<NodeId> hit =
        v.tree.render().hit_test(PixelPoint{view.x + 95, view.y + 5});
    CHECK(hit != std::optional<NodeId>{v.track});
    CHECK(hit != std::optional<NodeId>{v.thumb});
  }

  TEST_CASE("DG_PROP_SCROLLBAR always/never override auto") {
    View fits{ScrollAxis::kVertical, 2};
    REQUIRE(dg::set_prop(fits.tree, fits.view, DG_PROP_SCROLLBAR,
                         dg::PropValue::option(DG_SCROLLBAR_ALWAYS))
                .ok());
    CHECK(fits.tree.render().style(fits.view).scrollbar == ScrollbarMode::kAlways);
    fits.sync();
    REQUIRE(fits.geometry().visible);
    CHECK(fits.abs(fits.thumb).height == kViewport);  // nothing to scroll: full length

    View overflows{ScrollAxis::kVertical, kItems};
    REQUIRE(dg::set_prop(overflows.tree, overflows.view, DG_PROP_SCROLLBAR,
                         dg::PropValue::option(DG_SCROLLBAR_NEVER))
                .ok());
    overflows.sync();
    CHECK_FALSE(overflows.geometry().visible);
    CHECK(overflows.abs(overflows.track).is_empty());
    // Hiding the bar does not stop scrolling.
    CHECK(overflows.scroll(40));
  }

  TEST_CASE("DG_PROP_SCROLLBAR rejects an unknown ordinal") {
    View v{ScrollAxis::kVertical, kItems};
    CHECK(dg::prop_type(DG_PROP_SCROLLBAR) == dg::PropType::k_enum);
    CHECK(DG_SCROLLBAR_AUTO == static_cast<std::uint32_t>(ScrollbarMode::kAuto));
    CHECK(DG_SCROLLBAR_NEVER == static_cast<std::uint32_t>(ScrollbarMode::kNever));
    CHECK_FALSE(dg::set_prop(v.tree, v.view, DG_PROP_SCROLLBAR, dg::PropValue::option(3)).ok());
    CHECK(v.tree.render().style(v.view).scrollbar == ScrollbarMode::kAuto);
  }
}

TEST_SUITE("scrollbar interaction") {
  TEST_CASE("dragging the thumb maps thumb travel onto the scroll range") {
    View v{ScrollAxis::kVertical, kItems};
    const PixelRect thumb = v.abs(v.thumb);
    const PixelPoint grab{thumb.x + 5, thumb.y + 10};
    REQUIRE(v.widgets.owner_of(v.tree.render(), *v.tree.render().hit_test(grab)) == v.thumb);

    // A move before any press drags nothing.
    CHECK_FALSE(v.widgets.scrollbar_drag(v.tree.render(), v.thumb, v.extent(), grab).moved);

    CHECK_FALSE(v.widgets.scrollbar_press(v.tree.render(), v.thumb, v.extent(), grab).moved);
    // 30 px of thumb travel out of 75 -> 30 * 300 / 75 = 120.
    ScrollResult r = v.widgets.scrollbar_drag(v.tree.render(), v.thumb, v.extent(),
                                              PixelPoint{grab.x, grab.y + 30});
    CHECK(r.moved);
    CHECK(v.offset_y() == 120);
    // The grab point stays under the pointer.
    CHECK(v.abs(v.thumb).y == thumb.y + 30);
    // The pointer may leave the bar sideways and run past the end: clamped.
    r = v.widgets.scrollbar_drag(v.tree.render(), v.thumb, v.extent(),
                                 PixelPoint{grab.x - 200, grab.y + 500});
    CHECK(v.offset_y() == 300);
    r = v.widgets.scrollbar_drag(v.tree.render(), v.thumb, v.extent(),
                                 PixelPoint{grab.x, grab.y - 500});
    CHECK(v.offset_y() == 0);

    v.widgets.scrollbar_release();
    CHECK_FALSE(v.widgets
                    .scrollbar_drag(v.tree.render(), v.thumb, v.extent(),
                                    PixelPoint{grab.x, grab.y + 30})
                    .moved);
    CHECK(v.offset_y() == 0);
  }

  TEST_CASE("a press on the track pages one viewport toward the pointer") {
    View v{ScrollAxis::kVertical, kItems};
    const PixelRect track = v.abs(v.track);
    const PixelPoint below{track.x + 5, track.y + 90};
    REQUIRE(v.widgets.owner_of(v.tree.render(), *v.tree.render().hit_test(below)) == v.track);
    CHECK(v.widgets.scrollbar_press(v.tree.render(), v.track, v.extent(), below).moved);
    CHECK(v.offset_y() == 100);
    CHECK(v.widgets.scrollbar_press(v.tree.render(), v.track, v.extent(), below).moved);
    CHECK(v.offset_y() == 200);
    CHECK(v.widgets.scrollbar_press(v.tree.render(), v.track, v.extent(), below).moved);
    CHECK(v.offset_y() == 300);  // clamped, not 400
    // Thumb now at the bottom (75..100): a press above it pages back up.
    const PixelPoint above{track.x + 5, track.y + 10};
    CHECK(v.widgets.scrollbar_press(v.tree.render(), v.track, v.extent(), above).moved);
    CHECK(v.offset_y() == 200);
    // A track press never starts a drag.
    CHECK_FALSE(v.widgets.scrollbar_drag(v.tree.render(), v.track, v.extent(), below).moved);
  }

  TEST_CASE("hover and press paint the thumb's states; the track keeps its colour") {
    View v{ScrollAxis::kVertical, kItems};
    const dg::RenderTree& render = v.tree.render();
    CHECK(render.style(v.track).fill == kTrack);
    CHECK(render.style(v.thumb).fill == kThumb);

    dg::Interaction interaction;
    (void)interaction.moved_over(v.thumb);
    v.widgets.refresh(v.tree.render(), v.thumb, interaction.state_of(v.thumb));
    CHECK(render.style(v.thumb).fill == kThumbHover);
    (void)interaction.pressed_on(v.thumb);
    v.widgets.refresh(v.tree.render(), v.thumb, interaction.state_of(v.thumb));
    CHECK(render.style(v.thumb).fill == kThumbPressed);

    v.widgets.refresh(v.tree.render(), v.track, dg::PointerState{true, true});
    CHECK(render.style(v.track).fill == kTrack);
  }

  TEST_CASE("scrollbar parts never take keyboard focus") {
    CHECK_FALSE(dg::is_focusable(WidgetKind::kScrollbar));
    View v{ScrollAxis::kVertical, kItems};
    CHECK(v.widgets.scrollbar_owner_of(v.thumb) == v.view);
    CHECK(v.widgets.scrollbar_owner_of(v.track) == v.view);
    CHECK_FALSE(v.widgets.scrollbar_owner_of(v.view).has_value());
    CHECK(v.widgets.is_scrollbar_thumb(v.thumb));
    CHECK_FALSE(v.widgets.is_scrollbar_thumb(v.track));
    dg::Focus focus;
    const dg::FocusChange change =
        focus.focus_next(v.tree.render(), v.widgets, LayoutTree::root());
    CHECK_FALSE(change.focused.has_value());
  }
}

TEST_SUITE("scrollbar disabled") {
  TEST_CASE("a disabled scroll view shows its bar but it cannot be dragged or paged") {
    View v{ScrollAxis::kVertical, kItems};
    v.widgets.set_disabled_palette(dg::DisabledPalette{kDisabledFill, kDisabledOn});
    REQUIRE(dg::set_prop(v.tree, v.view, DG_PROP_DISABLED, dg::PropValue::boolean(true)).ok());
    dg::Interaction interaction;
    v.widgets.refresh_subtree(v.tree.render(), v.view, interaction);

    CHECK(v.geometry().visible);
    CHECK(v.tree.render().style(v.track).fill == kDisabledFill);
    CHECK(v.tree.render().style(v.thumb).fill == kDisabledOn);

    const PixelRect thumb = v.abs(v.thumb);
    const PixelPoint grab{thumb.x + 5, thumb.y + 5};
    CHECK_FALSE(v.widgets.owner_of(v.tree.render(), v.thumb).has_value());
    (void)v.widgets.scrollbar_press(v.tree.render(), v.thumb, v.extent(), grab);
    CHECK_FALSE(v.widgets
                    .scrollbar_drag(v.tree.render(), v.thumb, v.extent(),
                                    PixelPoint{grab.x, grab.y + 30})
                    .moved);
    const PixelRect track = v.abs(v.track);
    CHECK_FALSE(v.widgets
                    .scrollbar_press(v.tree.render(), v.track, v.extent(),
                                     PixelPoint{track.x + 5, track.y + 90})
                    .moved);
    CHECK(v.offset_y() == 0);

    // The wheel still scrolls a disabled view, and the thumb follows.
    CHECK(v.scroll(300));
    CHECK(v.abs(v.thumb).bottom() == v.abs(v.track).bottom());

    REQUIRE(dg::set_prop(v.tree, v.view, DG_PROP_DISABLED, dg::PropValue::boolean(false)).ok());
    v.widgets.refresh_subtree(v.tree.render(), v.view, interaction);
    CHECK(v.tree.render().style(v.track).fill == kTrack);
    CHECK(v.tree.render().style(v.thumb).fill == kThumb);
  }
}

TEST_SUITE("scrollbar on a list") {
  TEST_CASE("dragging a list's thumb recycles the pool to the new window") {
    constexpr int kExtent = 20;
    constexpr int kCount = 1000;
    constexpr int kPool = 7;
    dg::RenderTree tree{spec()};
    WidgetSet widgets;
    widgets.set_scrollbar_style(test_style());
    const NodeId list =
        tree.add_child(dg::RenderTree::root(), PixelRect{10, 10, 60, 100}, NodeStyle{});
    Widget widget;
    widget.kind = WidgetKind::kList;
    widget.list_axis = ScrollAxis::kVertical;
    widget.list_item_count = kCount;
    widget.list_item_extent = kExtent;
    for (int i = 0; i < kPool; ++i) {
      widget.list_pool.push_back(
          tree.add_child(list, PixelRect{0, 0, 50, kExtent}, NodeStyle{}));
    }
    widgets.attach(list, widget);
    const NodeId track = tree.add_child(list, PixelRect{}, NodeStyle{});
    const NodeId thumb = tree.add_child(track, PixelRect{}, NodeStyle{});
    widgets.attach_scrollbar(tree, list, track, thumb);
    (void)widgets.list_sync(tree, list, 0);
    widgets.sync_scrollbar(tree, list, 100);

    // 20000 px of content in 100: thumb clamps to 24, travel 76, max 19900.
    const ScrollbarGeometry g = widgets.scrollbar_geometry(tree, list, 100);
    REQUIRE(g.visible);
    CHECK(g.max_offset == 19900);
    CHECK(g.thumb.height == 24);
    CHECK(tree.absolute_bounds(track) == PixelRect{60, 10, 10, 100});

    const PixelRect at = tree.absolute_bounds(thumb);
    const PixelPoint grab{at.x + 5, at.y + 5};
    (void)widgets.scrollbar_press(tree, thumb, 100, grab);
    // 38 of 76 px -> exactly half: 9950.
    const ScrollResult r =
        widgets.scrollbar_drag(tree, thumb, 100, PixelPoint{grab.x, grab.y + 38});
    REQUIRE(r.moved);
    CHECK(tree.scroll_offset(list).y == 9950);
    CHECK(tree.absolute_bounds(track) == PixelRect{60, 10, 10, 100});
    CHECK(tree.absolute_bounds(thumb).y == 10 + 38);

    // Every slot now shows an item of the visible window 497..503, and the
    // reassigned ones were reported.
    const int top = 9950 / kExtent;
    CHECK(r.slots.size() == static_cast<std::size_t>(kPool));
    for (const dg::ListSlot& slot : r.slots) {
      CHECK(slot.logical_index >= top);
      CHECK(slot.logical_index < top + kPool);
      CHECK(tree.local_bounds(slot.node).y == slot.logical_index * kExtent);
    }
  }
}

TEST_SUITE("scrollbar theme") {
  TEST_CASE("the builtin theme defines every scrollbar token in both variants") {
    const dg::Expected<dg::Theme, dg::ThemeLoadError> theme = dg::load_builtin_theme();
    REQUIRE(theme.has_value());
    CHECK(theme.value().int_value(DG_TOKEN_SIZE_SCROLLBAR).value_or(0) > 0);
    for (const dg::ThemeVariant variant : {dg::ThemeVariant::kLight, dg::ThemeVariant::kDark}) {
      CHECK(theme.value().color_value(DG_TOKEN_COLOR_SCROLLBAR_TRACK, variant).has_value());
      CHECK(theme.value().color_value(DG_TOKEN_COLOR_SCROLLBAR_THUMB, variant).has_value());
      CHECK(
          theme.value().color_value(DG_TOKEN_COLOR_SCROLLBAR_THUMB_HOVER, variant).has_value());
      CHECK(theme.value()
                .color_value(DG_TOKEN_COLOR_SCROLLBAR_THUMB_PRESSED, variant)
                .has_value());
    }
    CHECK(theme.value().color_value(DG_TOKEN_COLOR_SCROLLBAR_THUMB, dg::ThemeVariant::kLight) !=
          theme.value().color_value(DG_TOKEN_COLOR_SCROLLBAR_THUMB, dg::ThemeVariant::kDark));
  }

  TEST_CASE("a theme switch is set_scrollbar_style() + refresh_subtree()") {
    View v{ScrollAxis::kVertical, kItems};
    ScrollbarStyle next = test_style();
    next.track = Color::from_argb(0xFFAA0000);
    next.thumb = Color::from_argb(0xFF00AA00);
    v.widgets.set_scrollbar_style(next);
    CHECK(v.widgets.scrollbar_style() == next);
    dg::Interaction interaction;
    v.widgets.refresh_subtree(v.tree.render(), LayoutTree::root(), interaction);
    CHECK(v.tree.render().style(v.track).fill == *next.track);
    CHECK(v.tree.render().style(v.thumb).fill == *next.thumb);

    next.size = 6;  // a thinner bar needs a re-sync
    v.widgets.set_scrollbar_style(next);
    v.sync();
    CHECK(v.abs(v.track).width == 6);
    CHECK(v.abs(v.track).right() == v.abs(v.view).right());
  }
}
