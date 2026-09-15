// Theme tokens: the schema/data split (design.md section 5.7.1), the
// $token live-reference binding side table, and the light/dark runtime
// switch - P3's own acceptance bar for this slice.
//
// Four independent things, four independent techniques:
//
//   THE TOKEN REGISTRY (token_type()/token_name()/token_id_for_name()) is
//   checked the way prop_ids.generated.h's own constants already are:
//   plain forward/reverse lookups against the generated table.
//
//   THE JSON LOADER is checked against BOTH failure modes design.md
//   section 5.7.5 names by name - unknown token name, type mismatch - and
//   against the parser's own defensive limits (deep nesting, malformed
//   input), matching this slice's own instruction to feed it deliberately
//   malformed input rather than only well-formed cases.
//
//   THE BINDING SIDE TABLE (ThemeBindings/bind_token) is checked the way
//   node_props.cpp's own dedicated-setter channel already is: a rejection
//   must be observable, must name the node, and must change nothing.
//
//   THE RUNTIME SWITCH is checked against LayoutStats, matching every
//   prior slice's own "measured, not assumed" discipline
//   (doc/scrolling.md section 4, doc/list.md section 6): a colour-token
//   switch must relayout zero nodes, and an int-token (radius/spacing)
//   switch must relayout exactly the nodes bound to it - both measured on
//   a real LayoutTree, not argued from the code shape alone.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/layout/box.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/props/node_props.h"
#include "drawgui/render/prop_ids.generated.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/theme/theme.h"
#include "drawgui/theme/theme_bindings.h"
#include "drawgui/theme/theme_loader.h"

namespace {

using dg::BoxStyle;
using dg::Color;
using dg::LayoutKind;
using dg::LayoutStats;
using dg::LayoutTree;
using dg::NodeId;
using dg::NodeStyle;
using dg::PixelSize;
using dg::PropStatus;
using dg::PropWrite;
using dg::Theme;
using dg::ThemeBindings;
using dg::ThemeLoadStatus;
using dg::ThemeVariant;
using dg::TokenType;

dg::TreeSpec spec_for(PixelSize size) {
  dg::TreeSpec spec;
  spec.viewport = size;
  return spec;
}

// Asserts `actual` equals the theme's resolved value for (token, variant),
// failing loudly if the theme has no such value at all - factored out so
// the switch tests below stay under clang-tidy's cognitive-complexity
// threshold rather than repeating this has_value()/CHECK/FAIL shape twice.
void check_bound_color(const Theme& theme, dg_token_id token, ThemeVariant variant,
                       Color actual, const char* what) {
  const std::optional<Color> expected = theme.color_value(token, variant);
  if (expected.has_value()) {
    CHECK(actual == expected.value());
  } else {
    FAIL(what << ": theme has no value for this token/variant");
  }
}

}  // namespace

TEST_CASE("token registry: forward and reverse lookups agree with the generated table") {
  CHECK(dg::token_type(DG_TOKEN_COLOR_SURFACE) == TokenType::k_color);
  CHECK(dg::token_type(DG_TOKEN_RADIUS_MD) == TokenType::k_int);
  CHECK_FALSE(dg::token_type(DG_TOKEN_INVALID).has_value());
  CHECK_FALSE(dg::token_type(dg_token_id{60000}).has_value());

  CHECK(dg::token_name(DG_TOKEN_COLOR_SURFACE) == "color.surface");
  CHECK(dg::token_id_for_name("color.surface") == DG_TOKEN_COLOR_SURFACE);
  CHECK(dg::token_id_for_name("radius.md") == DG_TOKEN_RADIUS_MD);
  CHECK_FALSE(dg::token_id_for_name("no.such.token").has_value());
}

TEST_CASE("Theme storage: unset slots answer nullopt, set slots round-trip") {
  Theme theme;
  CHECK_FALSE(theme.int_value(DG_TOKEN_RADIUS_MD).has_value());
  CHECK_FALSE(theme.color_value(DG_TOKEN_COLOR_SURFACE, ThemeVariant::kLight).has_value());

  theme.set_int(DG_TOKEN_RADIUS_MD, 8);
  CHECK(theme.int_value(DG_TOKEN_RADIUS_MD) == 8);

  theme.set_color(DG_TOKEN_COLOR_SURFACE, ThemeVariant::kLight, Color::rgba(0xFF, 0xFF, 0xFF));
  theme.set_color(DG_TOKEN_COLOR_SURFACE, ThemeVariant::kDark, Color::rgba(0x1E, 0x1E, 0x1E));
  CHECK(theme.color_value(DG_TOKEN_COLOR_SURFACE, ThemeVariant::kLight) ==
        Color::rgba(0xFF, 0xFF, 0xFF));
  CHECK(theme.color_value(DG_TOKEN_COLOR_SURFACE, ThemeVariant::kDark) ==
        Color::rgba(0x1E, 0x1E, 0x1E));
  // The variant not set is still unanswered - the two colour tables are
  // genuinely independent, not one falling back to the other.
  CHECK_FALSE(theme.color_value(DG_TOKEN_COLOR_BORDER, ThemeVariant::kLight).has_value());
}

TEST_CASE("load_builtin_theme: the embedded theme.json parses and covers this test's tokens") {
  const dg::Expected<Theme, dg::ThemeLoadError> loaded = dg::load_builtin_theme();
  REQUIRE(loaded.has_value());
  const Theme& theme = loaded.value();
  CHECK(theme.name() == "drawgui-builtin");
  CHECK(theme.int_value(DG_TOKEN_RADIUS_MD) == 8);
  CHECK(theme.color_value(DG_TOKEN_COLOR_SURFACE, ThemeVariant::kLight) ==
        Color::rgba(0xFF, 0xFF, 0xFF, 0xFF));
  CHECK(theme.color_value(DG_TOKEN_COLOR_SURFACE, ThemeVariant::kDark) ==
        Color::rgba(0x1E, 0x1E, 0x1E, 0xFF));
}

TEST_CASE("load_theme: an unknown token name fails and reports its location") {
  const std::string json = R"({
    "schema_version": 1,
    "base": {"radius.md": 8, "radius.sm": 4, "space.sm": 4, "space.md": 8},
    "variants": {
      "light": {"color.surface": "#FFFFFFFF", "color.mystery-token": "#000000FF",
                "color.on-surface": "#000000FF", "color.border": "#000000FF",
                "color.primary": "#000000FF", "color.on-primary": "#000000FF",
                "color.primary-hover": "#000000FF", "color.primary-pressed": "#000000FF"},
      "dark": {"color.surface": "#000000FF", "color.on-surface": "#FFFFFFFF",
               "color.border": "#FFFFFFFF", "color.primary": "#FFFFFFFF",
               "color.on-primary": "#000000FF", "color.primary-hover": "#FFFFFFFF",
               "color.primary-pressed": "#FFFFFFFF"}
    }
  })";
  const dg::Expected<Theme, dg::ThemeLoadError> loaded = dg::load_theme(json);
  REQUIRE_FALSE(loaded.has_value());
  CHECK(loaded.error().status == ThemeLoadStatus::kUnknownToken);
  CHECK(loaded.error().message.find("variants.light.color.mystery-token") != std::string::npos);
}

TEST_CASE("load_theme: a type mismatch (int token under variants) fails and names it") {
  const std::string json = R"({
    "schema_version": 1,
    "base": {"radius.md": 8, "radius.sm": 4, "space.sm": 4, "space.md": 8},
    "variants": {
      "light": {"color.surface": "#FFFFFFFF", "radius.md": "#000000FF",
                "color.on-surface": "#000000FF", "color.border": "#000000FF",
                "color.primary": "#000000FF", "color.on-primary": "#000000FF",
                "color.primary-hover": "#000000FF", "color.primary-pressed": "#000000FF"},
      "dark": {"color.surface": "#000000FF", "color.on-surface": "#FFFFFFFF",
               "color.border": "#FFFFFFFF", "color.primary": "#FFFFFFFF",
               "color.on-primary": "#000000FF", "color.primary-hover": "#FFFFFFFF",
               "color.primary-pressed": "#FFFFFFFF"}
    }
  })";
  const dg::Expected<Theme, dg::ThemeLoadError> loaded = dg::load_theme(json);
  REQUIRE_FALSE(loaded.has_value());
  CHECK(loaded.error().status == ThemeLoadStatus::kTypeMismatch);
  CHECK(loaded.error().message.find("variants.light.radius.md") != std::string::npos);
}

TEST_CASE("load_theme: a malformed colour string is a type mismatch, not a crash") {
  const std::string json = R"({
    "schema_version": 1,
    "base": {"radius.md": 8, "radius.sm": 4, "space.sm": 4, "space.md": 8},
    "variants": {
      "light": {"color.surface": "not-a-colour",
                "color.on-surface": "#000000FF", "color.border": "#000000FF",
                "color.primary": "#000000FF", "color.on-primary": "#000000FF",
                "color.primary-hover": "#000000FF", "color.primary-pressed": "#000000FF"},
      "dark": {"color.surface": "#000000FF", "color.on-surface": "#FFFFFFFF",
               "color.border": "#FFFFFFFF", "color.primary": "#FFFFFFFF",
               "color.on-primary": "#000000FF", "color.primary-hover": "#FFFFFFFF",
               "color.primary-pressed": "#FFFFFFFF"}
    }
  })";
  const dg::Expected<Theme, dg::ThemeLoadError> loaded = dg::load_theme(json);
  REQUIRE_FALSE(loaded.has_value());
  CHECK(loaded.error().status == ThemeLoadStatus::kTypeMismatch);
}

TEST_CASE("load_theme: malformed JSON fails with a parse error, never crashes") {
  const std::vector<std::string> malformed = {
      "",
      "{",
      "not json at all",
      R"({"schema_version": 1, "base": {,}, "variants": {}})",
      R"({"schema_version": 1 "base": {}})",      // missing comma
      std::string(5, '[') + std::string(5, ']'),  // shallow but unmatched-context array
      "\"" + std::string(200000, 'x') + "\"",     // a long string, still under the byte cap
  };
  for (const std::string& text : malformed) {
    const dg::Expected<Theme, dg::ThemeLoadError> loaded = dg::load_theme(text);
    CHECK_FALSE(loaded.has_value());
  }
}

TEST_CASE("load_theme: a deeply nested document is rejected, not a stack overflow") {
  std::string bomb;
  for (int i = 0; i < 100000; ++i) {
    bomb += "[";
  }
  const dg::Expected<Theme, dg::ThemeLoadError> loaded = dg::load_theme(bomb);
  REQUIRE_FALSE(loaded.has_value());
  CHECK(loaded.error().status == ThemeLoadStatus::kParseError);
}

TEST_CASE("load_theme: an oversized document is rejected before it is parsed") {
  const std::string huge(2 << 20, ' ');
  const dg::Expected<Theme, dg::ThemeLoadError> loaded = dg::load_theme(huge);
  REQUIRE_FALSE(loaded.has_value());
  CHECK(loaded.error().status == ThemeLoadStatus::kParseError);
}

TEST_CASE("bind_token: rejects an unknown prop_id or token_id, and changes nothing") {
  LayoutTree tree{spec_for(PixelSize{100, 100})};
  const NodeId node = tree.add_child(LayoutTree::root(), BoxStyle{}, NodeStyle{});
  ThemeBindings bindings;
  const Theme theme = dg::load_builtin_theme().value();

  const Color before_fill = tree.render().style(node).fill;

  const PropWrite bad_prop = dg::bind_token(tree, bindings, node, dg_prop_id{60000}, theme,
                                            ThemeVariant::kLight, DG_TOKEN_COLOR_SURFACE);
  CHECK(bad_prop.status == PropStatus::kUnknownId);

  const PropWrite bad_token = dg::bind_token(tree, bindings, node, DG_PROP_BACKGROUND_COLOR,
                                             theme, ThemeVariant::kLight, dg_token_id{60000});
  CHECK(bad_token.status == PropStatus::kUnknownId);

  const PropWrite mismatched = dg::bind_token(tree, bindings, node, DG_PROP_BACKGROUND_COLOR,
                                              theme, ThemeVariant::kLight, DG_TOKEN_RADIUS_MD);
  CHECK(mismatched.status == PropStatus::kTypeMismatch);

  CHECK(tree.render().style(node).fill == before_fill);
  CHECK_FALSE(bindings.token_for(node, DG_PROP_BACKGROUND_COLOR).has_value());
}

TEST_CASE("bind_token: a successful bind writes the CURRENT variant's value immediately") {
  LayoutTree tree{spec_for(PixelSize{100, 100})};
  const NodeId node = tree.add_child(LayoutTree::root(), BoxStyle{}, NodeStyle{});
  ThemeBindings bindings;
  const Theme theme = dg::load_builtin_theme().value();

  const PropWrite result = dg::bind_token(tree, bindings, node, DG_PROP_BACKGROUND_COLOR, theme,
                                          ThemeVariant::kLight, DG_TOKEN_COLOR_SURFACE);
  REQUIRE(result.ok());
  check_bound_color(theme, DG_TOKEN_COLOR_SURFACE, ThemeVariant::kLight,
                    tree.render().style(node).fill, "surface_panel");
  CHECK(bindings.token_for(node, DG_PROP_BACKGROUND_COLOR) == DG_TOKEN_COLOR_SURFACE);
}

TEST_CASE(
    "ThemeBindings::apply: switching light->dark updates every bound node, "
    "color-only costs zero relayout") {
  LayoutTree tree{spec_for(PixelSize{200, 200})};
  BoxStyle box;
  box.width = 40;
  box.height = 40;
  const NodeId a = tree.add_child(LayoutTree::root(), box, NodeStyle{});
  const NodeId b = tree.add_child(LayoutTree::root(), box, NodeStyle{});
  ThemeBindings bindings;
  const Theme theme = dg::load_builtin_theme().value();

  REQUIRE(dg::bind_token(tree, bindings, a, DG_PROP_BACKGROUND_COLOR, theme,
                         ThemeVariant::kLight, DG_TOKEN_COLOR_SURFACE)
              .ok());
  REQUIRE(dg::bind_token(tree, bindings, b, DG_PROP_BORDER_COLOR, theme, ThemeVariant::kLight,
                         DG_TOKEN_COLOR_BORDER)
              .ok());
  tree.layout_full();

  const std::size_t unresolved = bindings.apply(tree, theme, ThemeVariant::kDark);
  CHECK(unresolved == 0);
  check_bound_color(theme, DG_TOKEN_COLOR_SURFACE, ThemeVariant::kDark,
                    tree.render().style(a).fill, "surface_panel (a)");
  check_bound_color(theme, DG_TOKEN_COLOR_BORDER, ThemeVariant::kDark,
                    tree.render().style(b).border_color, "border_panel (b)");

  // THE MEASUREMENT this slice's task asks for: a colour-only theme switch
  // through the SAME dg::set_prop() door an ordinary NodeStyle write already
  // uses is a paint-only change (node_props.cpp's own dispatch table routes
  // background_color/border_color into NodeStyle, never BoxStyle) - so the
  // next layout() pass, with nothing else dirtied, must visit and relay out
  // zero nodes, extending doc/scrolling.md's/doc/list.md's own
  // nodes_visited == 0 finding to a theme switch.
  const LayoutStats stats = tree.layout();
  CHECK(stats.nodes_visited == 0);
  CHECK(stats.nodes_relaid_out == 0);
}

TEST_CASE(
    "ThemeBindings::apply: an int-token (radius/spacing) switch DOES relayout the "
    "bound nodes - measured, not assumed") {
  // A theme whose light/dark variants differ ONLY in `space.md`'s BASE
  // value - deliberately not a real light/dark instance, built to isolate
  // the one case this slice's task asks to be measured separately from the
  // colour-only case: space.md/radius.md are declared variant-INDEPENDENT
  // by themes/schema.toml (design.md section 5.7.4's own "base" shape), so
  // this test exercises the same resolve-and-write path with a DIFFERENT
  // Theme object standing in for "the value this token would carry", since
  // apply()'s job is "re-resolve every binding", independent of whether a
  // real theme.json ever varies an int token by variant.
  Theme narrow;
  narrow.set_int(DG_TOKEN_SPACE_MD, 8);
  Theme wide;
  wide.set_int(DG_TOKEN_SPACE_MD, 40);

  LayoutTree tree{spec_for(PixelSize{200, 200})};
  BoxStyle row;
  row.kind = LayoutKind::kRow;
  const NodeId container = tree.add_child(LayoutTree::root(), row, NodeStyle{});
  BoxStyle leaf_box;
  leaf_box.width = 20;
  leaf_box.height = 20;
  tree.add_child(container, leaf_box, NodeStyle{});
  tree.add_child(container, leaf_box, NodeStyle{});

  ThemeBindings bindings;
  REQUIRE(dg::bind_token(tree, bindings, container, DG_PROP_GAP, narrow, ThemeVariant::kLight,
                         DG_TOKEN_SPACE_MD)
              .ok());
  tree.layout_full();
  CHECK(tree.box(container).gap == 8);

  bindings.apply(tree, wide, ThemeVariant::kLight);
  CHECK(tree.box(container).gap == 40);

  // gap is a BoxStyle field (node_props.cpp's apply_gap), so re-resolving
  // it calls LayoutTree::set_box() - which marks the node dirty - and the
  // NEXT layout() pass must relay it (and its children, whose positions
  // depend on the gap) out for real. This is the deliberate OTHER HALF of
  // the previous test's zero: which invalidation a token switch costs is
  // decided by which struct the resolved property lands in, exactly as
  // node_props.h already documents for an ordinary literal write - a
  // theme switch inherits that rule for free rather than needing a new one.
  const LayoutStats stats = tree.layout();
  CHECK(stats.nodes_visited > 0);
  CHECK(stats.nodes_relaid_out > 0);
}
