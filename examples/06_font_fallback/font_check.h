// The headless half of examples/06_font_fallback.
//
// It checks the scene that is on screen rather than a second scene written to
// be easy to check, which is the rule sub-steps 1 to 3 all settled on. Two
// questions, and the first one is the acceptance criterion for this slice:
//
//   Does every codepoint in the scene get a REAL GLYPH - not merely a non-null
//   typeface, which a tofu box also has - except the one row that is in the
//   scene precisely to show what a missing glyph looks like?
//
//   Do the two Han panels actually differ in PIXELS when the language tags
//   route them to different families? A family name that changed while the
//   pixels did not would mean the tag reached the resolver and stopped there.

#pragma once

#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

#include "font_scene.h"

namespace font_check {

struct Result {
  std::size_t codepoints = 0;
  std::size_t missing = 0;

  // How many of those are the row that exists to show what a missing glyph
  // looks like. Zero means the demonstration is vacuous, which is a FAILURE:
  // the row's original codepoint turned out to be covered by a CJK font here,
  // and the check reported a clean pass over a demo proving nothing.
  std::size_t deliberate_missing = 0;

  // Nodes whose text contains a codepoint nothing on this machine covers,
  // described so a failure names the string rather than a node index.
  std::vector<std::string> nodes_with_missing;

  // Non-background pixels that differ between the two Han panels. Zero when
  // the two language tags selected the same face, which is a legitimate
  // outcome on a machine with one Han family and is reported rather than
  // asserted away.
  std::size_t han_panel_differences = 0;

  bool han_families_distinct = false;
  bool passed = false;
};

[[nodiscard]] Result verify(font_scene::Scene& scene);

void print(const font_scene::Scene& scene, const Result& result, std::ostream& out);

}  // namespace font_check
