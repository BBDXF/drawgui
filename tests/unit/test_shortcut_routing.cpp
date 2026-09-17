// The four-level shortcut router (8-3, design.md section 5.5.2):
// ActionScopes, resolve_action() (levels 2 and 4), and route_key_event()'s
// structural IME/editing-key bypass (levels 1 and the text-editing-key
// carve-out) - see include/drawgui/shortcuts/router.h's own header for the
// full routing-order argument and what is declined (level 3) and why.
//
// A NEW FILE, not an extension of test_shortcuts.cpp: that file owns the
// generator family's own identity (action_id/LogicalKey/ActionScope/the
// binding table/Chord composition), and says so in its own header
// ("Routing... [is] still out of scope here (8-3/8-3d)"). This file is
// that routing, over the SAME real generated table (dg::
// all_shortcut_bindings()) rather than a hand-rolled fixture table, so a
// pass here is also a witness that the router works against the actual
// input/shortcuts.toml data, not a table shaped to make the router look
// good.
//
// No window, no surface - dg::RenderTree/dg::WidgetSet built directly
// in-process, the same precedent test_focus.cpp/test_text_input.cpp
// already set for testing a side table or a widget model without a real
// window.

#include <optional>
#include <string>

#include <doctest/doctest.h>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/types.h"
#include "drawgui/layout/box.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/font_catalog.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/shortcuts/action_ids.generated.h"
#include "drawgui/shortcuts/action_scopes.h"
#include "drawgui/shortcuts/chord.h"
#include "drawgui/shortcuts/logical_key.generated.h"
#include "drawgui/shortcuts/router.h"
#include "drawgui/widget/widget_set.h"
#include "drawgui/window/window_manager.h"

namespace {

using dg::ActionScopes;
using dg::Chord;
using dg::Color;
using dg::FontCatalog;
using dg::FontId;
using dg::Key;
using dg::KeyAction;
using dg::KeyEvent;
using dg::KeyRouteOutcome;
using dg::LogicalKey;
using dg::Modifier;
using dg::NodeId;
using dg::NodeStyle;
using dg::PixelRect;
using dg::PixelSize;
using dg::RenderTree;
using dg::ResolvedAction;
using dg::RoutingContext;
using dg::TextAlign;
using dg::TreeSpec;
using dg::Widget;
using dg::WidgetKind;
using dg::WidgetSet;
using dg::WindowId;

constexpr WindowId kWindow{1};

// select_all (DG_ACTION_SELECT_ALL, ActionScope::kTextField, "Mod+A") is
// the real generated binding every "which node's ActionScopes entry wins"
// test below scopes onto a node - the exact chord/action a real TextField
// would register for itself (input/shortcuts.toml's own consumer field:
// "8-4 clipboard + TextField").
constexpr Chord kSelectAll{Modifier::kMod, LogicalKey::kA};

// scroll_to_start (DG_ACTION_SCROLL_TO_START, ActionScope::kApp, "Home") is
// the real generated app-level binding the Home/End collision test (design.
// md section 5.5.2's own named hazard) is built around.
constexpr Chord kHome{Modifier::kNone, LogicalKey::kHome};

// --- A plain three-level tree: root -> parent -> child, no widgets, for
// the pure resolve_action() tests (nearest-node-wins/bubbling/fall-
// through/unbound). ---
struct PlainScene {
  RenderTree tree;
  NodeId parent;
  NodeId child;
};

PlainScene build_plain_scene() {
  TreeSpec spec;
  spec.viewport = PixelSize{200, 200};
  PlainScene scene{RenderTree{spec}, {}, {}};
  scene.parent =
      scene.tree.add_child(RenderTree::root(), PixelRect{0, 0, 200, 200}, NodeStyle{});
  scene.child = scene.tree.add_child(scene.parent, PixelRect{0, 0, 100, 100}, NodeStyle{});
  return scene;
}

// --- A focused, real kTextField (content + caret children, a real font) -
// the Home/End collision test's own fixture, built the same shape test_
// text_input.cpp's build_field() already establishes so text_field_move()
// actually moves a caret rather than only being trusted to. ---
constexpr float kFontSize = 20.0F;

FontCatalog test_fonts() {
  const dg::Expected<FontCatalog, dg::FontError> scanned = FontCatalog::scan(DG_TEST_FONT_DIR);
  REQUIRE(scanned.has_value());
  return scanned.value();
}

struct FieldScene {
  RenderTree tree;
  WidgetSet widgets;
  FontCatalog fonts;
  NodeId field;
};

FieldScene build_field(const std::string& initial) {
  TreeSpec spec;
  spec.viewport = PixelSize{400, 300};
  RenderTree tree{spec};
  WidgetSet widgets;
  FontCatalog fonts = test_fonts();
  const dg::Expected<FontId, dg::FontError> font_id = fonts.add("DgTest Latin", false);
  REQUIRE(font_id.has_value());

  NodeStyle field_style;
  field_style.overflow = dg::Overflow::kClip;
  const NodeId field =
      tree.add_child(RenderTree::root(), PixelRect{0, 0, 200, 30}, field_style);
  const NodeId highlight = tree.add_child(field, PixelRect{0, 0, 0, 30}, NodeStyle{});
  const NodeId underline = tree.add_child(field, PixelRect{0, 0, 0, 30}, NodeStyle{});

  NodeStyle content_style;
  content_style.text.font = font_id.value();
  content_style.text.size = kFontSize;
  content_style.text.color = Color::from_argb(0xFFE8EDF4);
  content_style.text.align = TextAlign::kLeft;
  content_style.text.text = initial;
  const NodeId content = tree.add_child(field, PixelRect{0, 0, 1, 30}, content_style);

  NodeStyle caret_style;
  caret_style.fill = Color::from_argb(0xFFFFFFFF);
  const NodeId caret = tree.add_child(field, PixelRect{0, 0, 2, 30}, caret_style);

  Widget widget;
  widget.kind = WidgetKind::kTextField;
  widget.content = content;
  widget.caret = caret;
  widget.selection_highlight = highlight;
  widget.composition_underline = underline;
  widget.text = initial;
  widget.cursor = static_cast<int>(initial.size());
  widgets.attach(field, widget);

  return FieldScene{std::move(tree), std::move(widgets), std::move(fonts), field};
}

}  // namespace

TEST_SUITE("shortcut routing") {
  TEST_CASE("nearest node wins: a child and its ancestor both scope the same action") {
    PlainScene scene = build_plain_scene();
    ActionScopes scopes;
    scopes.scope(scene.parent, DG_ACTION_SELECT_ALL);
    scopes.scope(scene.child, DG_ACTION_SELECT_ALL);

    const std::optional<ResolvedAction> resolved = dg::resolve_action(
        kSelectAll, kWindow, scene.child, scene.tree, scopes, dg::all_shortcut_bindings());
    REQUIRE(resolved.has_value());
    CHECK(resolved->action_id == DG_ACTION_SELECT_ALL);
    REQUIRE(resolved->target.has_value());
    CHECK(*resolved->target == scene.child);
  }

  TEST_CASE("bubbling reaches an ancestor when only it scopes the action") {
    PlainScene scene = build_plain_scene();
    ActionScopes scopes;
    scopes.scope(scene.parent, DG_ACTION_SELECT_ALL);

    const std::optional<ResolvedAction> resolved = dg::resolve_action(
        kSelectAll, kWindow, scene.child, scene.tree, scopes, dg::all_shortcut_bindings());
    REQUIRE(resolved.has_value());
    CHECK(resolved->action_id == DG_ACTION_SELECT_ALL);
    REQUIRE(resolved->target.has_value());
    CHECK(*resolved->target == scene.parent);
  }

  TEST_CASE("falls through to the app table when no node on the chain consumes it") {
    PlainScene scene = build_plain_scene();
    const ActionScopes scopes;  // nobody scopes anything

    const std::optional<ResolvedAction> resolved = dg::resolve_action(
        kHome, kWindow, scene.child, scene.tree, scopes, dg::all_shortcut_bindings());
    REQUIRE(resolved.has_value());
    CHECK(resolved->action_id == DG_ACTION_SCROLL_TO_START);
    CHECK_FALSE(resolved->target.has_value());  // app level: no target node
  }

  TEST_CASE("an unbound chord resolves to nullopt, never a made-up action") {
    PlainScene scene = build_plain_scene();
    const ActionScopes scopes;
    const Chord unbound{Modifier::kAlt | Modifier::kShift, LogicalKey::kC};

    const std::optional<ResolvedAction> resolved = dg::resolve_action(
        unbound, kWindow, scene.child, scene.tree, scopes, dg::all_shortcut_bindings());
    CHECK_FALSE(resolved.has_value());
  }

  TEST_CASE("resolve_action costs zero relayout") {
    dg::LayoutTree layout{[] {
      TreeSpec spec;
      spec.viewport = PixelSize{200, 200};
      return spec;
    }()};
    dg::BoxStyle box;
    box.width = 50;
    box.height = 50;
    const NodeId child = layout.add_child(RenderTree::root(), box, NodeStyle{});
    layout.layout_full();

    ActionScopes scopes;
    scopes.scope(child, DG_ACTION_SELECT_ALL);

    const std::optional<ResolvedAction> resolved = dg::resolve_action(
        kSelectAll, kWindow, child, layout.render(), scopes, dg::all_shortcut_bindings());
    REQUIRE(resolved.has_value());

    const dg::LayoutStats stats = layout.layout();
    CHECK(stats.nodes_visited == 0);
  }

  TEST_CASE(
      "Home/End collision (design.md 5.5.2): a focused TextField's own line-start beats "
      "nothing, because the router is never consulted for it") {
    FieldScene scene = build_field("hello");
    // Cursor starts at the end (text_field_move requires a real starting
    // position); move right past the end is a no-op, so this simply
    // confirms the fixture, not the behaviour under test.
    REQUIRE(scene.widgets.text_field_cursor(scene.field) == 5);

    const ActionScopes scopes;  // nobody scopes scroll_to_start onto anything
    const KeyEvent event{kWindow, KeyAction::kDown, Key::kHome, Modifier::kNone,
                         LogicalKey::kHome};
    const RoutingContext ctx{scene.tree, scene.widgets, scopes, dg::all_shortcut_bindings()};

    const dg::KeyRouteResult routed = dg::route_key_event(event, kWindow, scene.field, ctx);
    CHECK(routed.outcome == KeyRouteOutcome::kEditingIntent);
    CHECK_FALSE(routed.action.has_value());  // zero actions fired

    // The real caller's job once route_key_event() answers kEditingIntent:
    // dispatch to the widget's own editing intent, exactly like examples/
    // 12_text_input's Runner::handle_key already does for Key::kHome.
    scene.widgets.text_field_move(scene.tree, scene.fonts, scene.field,
                                  dg::TextFieldMove::kLineStart, false);
    CHECK(scene.widgets.text_field_cursor(scene.field) == 0);
  }

  TEST_CASE(
      "Home/End collision, the other direction: nothing (or a non-text-field) focused fires "
      "scroll_to_start exactly once") {
    PlainScene scene = build_plain_scene();
    WidgetSet widgets;  // scene.child names no widget at all
    const ActionScopes scopes;
    const KeyEvent event{kWindow, KeyAction::kDown, Key::kHome, Modifier::kNone,
                         LogicalKey::kHome};

    // Direction A: nothing focused at all.
    {
      const RoutingContext ctx{scene.tree, widgets, scopes, dg::all_shortcut_bindings()};
      const dg::KeyRouteResult routed = dg::route_key_event(event, kWindow, std::nullopt, ctx);
      REQUIRE(routed.outcome == KeyRouteOutcome::kRouted);
      REQUIRE(routed.action.has_value());
      CHECK(routed.action->action_id == DG_ACTION_SCROLL_TO_START);
      CHECK_FALSE(routed.action->target.has_value());
    }

    // Direction B: a focused node that is not a text field (WidgetSet::
    // has() is false for it) - the bypass never applies to a non-text-
    // field, so this also routes and fires exactly once.
    {
      const RoutingContext ctx{scene.tree, widgets, scopes, dg::all_shortcut_bindings()};
      const dg::KeyRouteResult routed = dg::route_key_event(event, kWindow, scene.child, ctx);
      REQUIRE(routed.outcome == KeyRouteOutcome::kRouted);
      REQUIRE(routed.action.has_value());
      CHECK(routed.action->action_id == DG_ACTION_SCROLL_TO_START);
    }
  }
}
