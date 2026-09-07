// The scene examples/06_font_fallback puts on screen, built without a window.
//
// Two claims live here, and each is arranged so that a reader can see it fail:
//
//   ONE STYLE, MANY SCRIPTS. Every sample row below names the SAME font - one
//   ordinary Latin family - and carries text in a script that family has never
//   heard of. Before this slice each of those rows drew nothing at all unless
//   the caller looked up and named a family per script.
//
//   THE LANGUAGE TAG SELECTS THE FACE. The two Han panels contain the SAME
//   FOUR CODEPOINTS and differ only in their BCP 47 tag. If the tag stopped
//   reaching the chain they would be pixel-identical, which --verify-fallback
//   checks.
//
// The honest caveat, repeated on screen because it matters: this machine has
// no Japanese font. The ja panel therefore draws Japanese text in a Chinese
// face, and the scene says so. What is proven here is the ROUTING, not the
// typography - doc/font-fallback.md states exactly what is still missing.
//
// No SDL, no Skia, no display. The demo and the headless check compile this
// same file, for the reason sub-steps 1 to 3 all established: the verification
// that is worth anything runs the scene a human is actually looking at.

#pragma once

#include <string>
#include <vector>

#include "drawgui/base/expected.h"
#include "drawgui/base/pixel_geometry.h"
#include "drawgui/render/font_catalog.h"
#include "drawgui/render/render_tree.h"

namespace font_scene {

struct Options {
  dg::PixelSize viewport{1040, 856};
  std::string font_dir = "/usr/share/fonts";

  // One Latin family, named once, for every row. The point of the scene is
  // that nothing else is ever named.
  std::string primary_family = "DejaVu Sans";
};

// One row: the script's name, and a string in it.
struct Sample {
  std::string script;
  std::string text;
};

struct Scene {
  dg::RenderTree tree;
  dg::FontCatalog catalog;

  // The single family every sample row asks for.
  dg::FontId primary;

  std::vector<Sample> samples;

  // The nodes carrying the identical Han text under two language tags.
  dg::NodeId hans_panel;
  dg::NodeId ja_panel;

  // Which family each language chain actually chose on THIS machine. Reported
  // rather than assumed, because on a machine with no Japanese font the two
  // are the same and that has to be visible instead of quietly passing.
  std::string hans_family;
  std::string ja_family;
};

[[nodiscard]] dg::Expected<Scene, dg::FontError> build(const Options& options);

// The four Han characters the two panels share. design.md section 5.13.5 names
// these as the typical Han-unification cases.
[[nodiscard]] std::string han_sample();

}  // namespace font_scene
