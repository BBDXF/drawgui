#include "theme_package_scene.h"

#include <utility>

#include "drawgui/layout/box.h"
#include "drawgui/render/prop_ids.generated.h"
#include "drawgui/theme/theme_loader.h"

namespace theme_package_scene {
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

[[nodiscard]] bool bind_panel_colors(dg::LayoutTree& tree, dg::ThemeBindings& bindings,
                                     dg::NodeId panel, const dg::Theme& theme,
                                     dg::ThemeVariant variant, dg_token_id fill_token,
                                     std::string& error_out) {
  dg::PropWrite fill = dg::bind_token(tree, bindings, panel, DG_PROP_BACKGROUND_COLOR, theme,
                                     variant, fill_token);
  if (!fill.ok()) {
    error_out = fill.message;
    return false;
  }
  dg::PropWrite border = dg::bind_token(tree, bindings, panel, DG_PROP_BORDER_COLOR, theme,
                                       variant, DG_TOKEN_COLOR_BORDER);
  if (!border.ok()) {
    error_out = border.message;
    return false;
  }
  for (dg_prop_id side : {DG_PROP_BORDER_WIDTH_L, DG_PROP_BORDER_WIDTH_T, DG_PROP_BORDER_WIDTH_R,
                          DG_PROP_BORDER_WIDTH_B}) {
    (void)dg::set_prop(tree, panel, side, dg::PropValue::length(2.0F));
  }
  return true;
}

}  // namespace

std::optional<Scene> build(const dg::TreeSpec& spec, const std::string& package_dir,
                           std::string& error_out, bool bind_gap_to_token) {
  dg::Expected<dg::ThemePackage, dg::ThemeLoadError> opened = dg::ThemePackage::open(package_dir);
  if (!opened) {
    error_out = opened.error().message;
    return std::nullopt;
  }
  dg::Expected<dg::Theme, dg::ThemeLoadError> loaded = opened.value().load_theme_json();
  if (!loaded) {
    error_out = loaded.error().message;
    return std::nullopt;
  }

  dg::LayoutTree tree{spec};
  dg::Theme theme = std::move(loaded).value();
  dg::ThemeBindings bindings;
  Handles handles;

  const dg::NodeId root = dg::LayoutTree::root();
  BoxStyle row;
  row.kind = LayoutKind::kRow;
  row.padding = EdgeInsets::all(16);
  handles.row = tree.add_child(root, row, NodeStyle{});
  // Whether `gap` is a $token binding or a plain literal is the caller's
  // choice (bind_gap_to_token) - see this file's own header comment for
  // why measuring the two halves of the hot-reload cost split needs two
  // different scene instances, not two edits of the same one.
  if (bind_gap_to_token) {
    dg::PropWrite gap_write =
        dg::bind_token(tree, bindings, handles.row, DG_PROP_GAP, theme,
                       dg::ThemeVariant::kLight, DG_TOKEN_SPACE_MD);
    if (!gap_write.ok()) {
      error_out = gap_write.message;
      return std::nullopt;
    }
  } else {
    (void)dg::set_prop(tree, handles.row, DG_PROP_GAP,
                       dg::PropValue::length(static_cast<float>(
                           theme.int_value(DG_TOKEN_SPACE_MD).value_or(8))));
  }

  handles.surface_panel = tree.add_child(handles.row, panel_box(200, 160), NodeStyle{});
  if (!bind_panel_colors(tree, bindings, handles.surface_panel, theme, dg::ThemeVariant::kLight,
                        DG_TOKEN_COLOR_SURFACE, error_out)) {
    return std::nullopt;
  }

  handles.primary_panel = tree.add_child(handles.row, panel_box(200, 160), NodeStyle{});
  if (!bind_panel_colors(tree, bindings, handles.primary_panel, theme, dg::ThemeVariant::kLight,
                        DG_TOKEN_COLOR_PRIMARY, error_out)) {
    return std::nullopt;
  }

  tree.layout_full();
  return Scene{std::move(tree), std::move(opened).value(), std::move(theme), std::move(bindings),
              handles, dg::ThemeVariant::kLight};
}

bool reload(Scene& scene, std::string& error_out) {
  dg::Expected<dg::Theme, dg::ThemeLoadError> reloaded =
      dg::reload_theme_package(scene.package);
  if (!reloaded) {
    error_out = reloaded.error().message;
    return false;
  }
  scene.theme = std::move(reloaded).value();
  scene.bindings.apply(scene.tree, scene.theme, scene.variant);
  return true;
}

}  // namespace theme_package_scene
