#include "theme_scene.h"

#include <utility>

#include "drawgui/layout/box.h"
#include "drawgui/render/prop_ids.generated.h"
#include "drawgui/theme/theme_loader.h"

namespace theme_scene {
namespace {

using dg::BoxStyle;
using dg::EdgeInsets;
using dg::LayoutKind;
using dg::NodeStyle;

BoxStyle panel_box(int width, int height) {
  BoxStyle box;
  box.width = width;
  box.height = height;
  return box;
}

}  // namespace

Scene build(const dg::TreeSpec& spec) {
  dg::LayoutTree tree{spec};
  Handles handles;

  const dg::Expected<dg::Theme, dg::ThemeLoadError> loaded = dg::load_builtin_theme();
  // The builtin theme is embedded and verified by tools/check_consistency.py
  // at CI time (design.md section 5.7.7); a load failure here would mean
  // the SHIPPED binary's own theme does not parse, which is exactly the
  // "CI 在构建期校验内置主题，保证发布版不会因主题解析失败而起不来" guarantee
  // design.md section 5.7.4 asks for - so this demo asserts it rather than
  // handling a case that should be structurally impossible by the time a
  // binary exists at all.
  dg::Theme theme = loaded.value();

  dg::ThemeBindings bindings;

  const dg::NodeId root = dg::LayoutTree::root();
  BoxStyle row;
  row.kind = LayoutKind::kRow;
  row.padding = EdgeInsets::all(16);
  row.gap = theme.int_value(DG_TOKEN_SPACE_MD).value_or(8);
  handles.row = tree.add_child(root, row, NodeStyle{});
  // The row's gap is READ from space.md once, here - a plain literal, NOT
  // a $token binding - deliberately, so that THIS demo's own light/dark
  // switch measures a pure colour-token case (see doc/theme.md section 5's
  // "colour-only" LayoutStats row). Binding `gap` to a token too is real
  // and supported (dg::bind_token(tree, bindings, row, DG_PROP_GAP, ...)
  // works exactly like every other binding in this file), but
  // ThemeBindings::apply() re-resolves and re-writes EVERY binding
  // unconditionally on every switch, with no before/after value diff - so
  // a switch that includes even one int-token binding costs a relayout
  // regardless of whether that token's value actually moved between the
  // two variants, which the shipped theme's own `base` never does (design.md
  // section 5.7.4: int tokens are variant-INDEPENDENT). Keeping this demo's
  // switch colour-only is what makes ITS OWN LayoutStats row honestly zero;
  // tests/unit/test_theme.cpp's "an int-token switch DOES relayout" case is
  // where the OTHER half of this slice's own measurement requirement lives,
  // built with two Theme instances that genuinely disagree on `space.md` so
  // the relayout is caused by a real value change, not by apply()'s
  // unconditional-rewrite cost alone.

  handles.surface_panel = tree.add_child(handles.row, panel_box(140, 140), NodeStyle{});
  (void)dg::bind_token(tree, bindings, handles.surface_panel, DG_PROP_BACKGROUND_COLOR, theme,
                       dg::ThemeVariant::kLight, DG_TOKEN_COLOR_SURFACE);
  (void)dg::bind_token(tree, bindings, handles.surface_panel, DG_PROP_BORDER_COLOR, theme,
                       dg::ThemeVariant::kLight, DG_TOKEN_COLOR_BORDER);
  {
    dg::PropWrite border_width = dg::set_prop(
        tree, handles.surface_panel, DG_PROP_BORDER_WIDTH_L, dg::PropValue::length(2.0F));
    (void)border_width;
    for (dg_prop_id side :
         {DG_PROP_BORDER_WIDTH_T, DG_PROP_BORDER_WIDTH_R, DG_PROP_BORDER_WIDTH_B}) {
      (void)dg::set_prop(tree, handles.surface_panel, side, dg::PropValue::length(2.0F));
    }
  }

  handles.border_panel = tree.add_child(handles.row, panel_box(140, 140), NodeStyle{});
  (void)dg::bind_token(tree, bindings, handles.border_panel, DG_PROP_BACKGROUND_COLOR, theme,
                       dg::ThemeVariant::kLight, DG_TOKEN_COLOR_PRIMARY);

  handles.primary_panel = tree.add_child(handles.row, panel_box(70, 140), NodeStyle{});
  (void)dg::bind_token(tree, bindings, handles.primary_panel, DG_PROP_BACKGROUND_COLOR, theme,
                       dg::ThemeVariant::kLight, DG_TOKEN_COLOR_PRIMARY_HOVER);

  handles.radius_panel = tree.add_child(handles.row, panel_box(140, 140), NodeStyle{});
  (void)dg::bind_token(tree, bindings, handles.radius_panel, DG_PROP_BACKGROUND_COLOR, theme,
                       dg::ThemeVariant::kLight, DG_TOKEN_COLOR_SURFACE);
  (void)dg::bind_token(tree, bindings, handles.radius_panel, DG_PROP_BORDER_COLOR, theme,
                       dg::ThemeVariant::kLight, DG_TOKEN_COLOR_BORDER);
  for (dg_prop_id side : {DG_PROP_BORDER_WIDTH_L, DG_PROP_BORDER_WIDTH_T,
                          DG_PROP_BORDER_WIDTH_R, DG_PROP_BORDER_WIDTH_B}) {
    (void)dg::set_prop(tree, handles.radius_panel, side, dg::PropValue::length(2.0F));
  }
  // The int-token client - border_radius_tl/tr/br/bl are NodeStyle fields
  // (node_props.cpp's style_radius()), so binding one to radius.md proves
  // an int token can drive a PAINT property too, not only a layout one -
  // the schema's color/int dichotomy (design.md section 5.7.2) is about a
  // token's VALUE TYPE, not about which struct a bound property happens to
  // live in.
  for (dg_prop_id corner : {DG_PROP_BORDER_RADIUS_TL, DG_PROP_BORDER_RADIUS_TR,
                            DG_PROP_BORDER_RADIUS_BR, DG_PROP_BORDER_RADIUS_BL}) {
    (void)dg::bind_token(tree, bindings, handles.radius_panel, corner, theme,
                         dg::ThemeVariant::kLight, DG_TOKEN_RADIUS_MD);
  }

  tree.layout_full();
  return Scene{std::move(tree), std::move(theme), std::move(bindings), handles,
               dg::ThemeVariant::kLight};
}

std::size_t switch_variant(Scene& scene) {
  scene.variant = scene.variant == dg::ThemeVariant::kLight ? dg::ThemeVariant::kDark
                                                            : dg::ThemeVariant::kLight;
  return scene.bindings.apply(scene.tree, scene.theme, scene.variant);
}

}  // namespace theme_scene
