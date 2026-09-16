// examples/24_theme_package: an EXTERNAL theme package directory, loaded
// through dg::ThemePackage (P7 slice 7-6) rather than the compiled-in
// builtin theme examples/18_theme uses. Same $token binding mechanism
// (dg::bind_token/dg::ThemeBindings), same demo shape (panels bound to
// colour tokens) - the only thing this slice adds is WHERE the theme.json
// bytes come from, and that they are untrusted (design.md section 5.7.5).
//
// One property, deliberately, is bound to an INT token (`gap`, BoxStyle,
// space.md) rather than only colour tokens - unlike 18_theme's own scene,
// which keeps `gap` a plain literal specifically so ITS switch measures
// pure colour-only cost. This scene's whole point is the OTHER half of the
// same measurement: reloading a package that changed an int token must
// relayout the nodes that read it, and reloading one that changed only a
// colour token must not - doc/theme-packages.md's own hot-reload
// granularity measurement.
#pragma once

#include <string>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/theme/theme.h"
#include "drawgui/theme/theme_bindings.h"
#include "drawgui/theme/theme_package.h"

namespace theme_package_scene {

inline constexpr dg::PixelSize kDemoViewport{560, 220};

struct Handles {
  dg::NodeId row;
  dg::NodeId surface_panel;
  dg::NodeId primary_panel;
};

struct Scene {
  dg::LayoutTree tree;
  dg::ThemePackage package;
  dg::Theme theme;
  dg::ThemeBindings bindings;
  Handles handles;
  dg::ThemeVariant variant = dg::ThemeVariant::kLight;
};

// Opens `package_dir` as a dg::ThemePackage, loads its theme.json, builds
// the scene and binds every colour-driven property (background_color/
// border_color on both panels) through dg::bind_token(). `bind_gap_to_token`
// controls whether the row's `gap` (BoxStyle, space.md - an INT token) is
// ALSO a $token binding, or a plain literal read once at build time.
//
// This matters because of a finding doc/theme.md already recorded and this
// slice re-confirms rather than re-discovers: ThemeBindings::apply()
// re-resolves and re-writes EVERY recorded binding unconditionally, with no
// before/after value diff - so a reload that touches ANY bound int
// property costs a relayout regardless of whether that property's value
// actually moved. Measuring "a colour-only EDIT costs zero relayout"
// therefore requires a scene with NO int-token binding at all
// (`bind_gap_to_token = false`, the default, matching examples/18_theme's
// own scene); measuring "an int-token EDIT costs a real one" requires a
// SEPARATE scene instance that has one (`bind_gap_to_token = true`) - see
// theme_package_check.cpp for why this demo builds both rather than
// reusing one scene for both halves of the measurement.
[[nodiscard]] std::optional<Scene> build(const dg::TreeSpec& spec,
                                         const std::string& package_dir, std::string& error_out,
                                         bool bind_gap_to_token = false);

// Re-reads theme.json from `scene.package` (design.md section 5.7.6's hot
// reload, in full - see dg::reload_theme_package()) and re-applies every
// binding against the CURRENT variant. Returns false (leaving `scene`
// untouched) if the edited file no longer parses/validates - a hot reload
// is not exempt from schema validation merely because it is a reload.
[[nodiscard]] bool reload(Scene& scene, std::string& error_out);

}  // namespace theme_package_scene
