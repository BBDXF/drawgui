// The theme demo: a scene themed entirely through $token bindings, proving
// design.md section 5.7's core distinction end to end - schema tokens
// (themes/schema.toml, compile-time) resolved against the SHIPPED theme.json
// (themes/builtin/theme.json, runtime data) - and P3's own acceptance bar,
// a runtime light/dark switch that touches no widget tree structure at all.
//
// Deliberately RE-THEMES an existing shape rather than inventing a fresh
// demo scene: three framed panels (surface fill, on-surface text colour,
// border colour) plus a fourth panel whose CORNER RADIUS is also
// token-bound (radius.md) - one property from each of the schema's two
// token types (color, int), which is what lets the demo prove BOTH
// halves of this slice's own measurement requirement: a colour-only
// variant switch costs zero relayout, and the radius panel's switch (this
// scene deliberately varies radius.md BETWEEN a "compact" and "spacious"
// instance of the SAME theme to make an int-token switch observable at
// all, since the shipped light/dark theme itself declares `radius.md`
// variant-INDEPENDENT per design.md section 5.7.4's own "base" shape)
// costs a real one - both measured with LayoutStats, not assumed.

#pragma once

#include <cstdint>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/theme/theme.h"
#include "drawgui/theme/theme_bindings.h"

namespace theme_scene {

inline constexpr dg::PixelSize kDemoViewport{600, 200};

struct Handles {
  dg::NodeId row;
  dg::NodeId surface_panel;
  dg::NodeId border_panel;
  dg::NodeId primary_panel;
  dg::NodeId radius_panel;
};

struct Scene {
  dg::LayoutTree tree;
  dg::Theme theme;
  dg::ThemeBindings bindings;
  Handles handles;
  dg::ThemeVariant variant = dg::ThemeVariant::kLight;
};

// Builds the scene and binds every token-driven property through
// dg::bind_token() - never by writing NodeStyle/BoxStyle fields with a
// literal colour or radius, because the thing this demo exists to show is
// that the BINDING reaches the same paint path a literal write already
// does, exactly the argument examples/16_complex_properties already made
// for its own dedicated-setter channel.
Scene build(const dg::TreeSpec& spec);

// Re-resolves every binding against `theme`'s OTHER variant and switches
// `scene.variant` - the whole of "switch the theme at runtime". Returns
// how many bindings had no value (see ThemeBindings::apply).
std::size_t switch_variant(Scene& scene);

}  // namespace theme_scene
