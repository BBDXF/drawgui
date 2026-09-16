// examples/20_multiline_text: 7-2's display substrate on screen.
//
// FIVE PANELS, each `TextStyle::wrap = true` at the same declared width, and
// each panel's HEIGHT computed by `dg::Paragraph::build()` BEFORE the node is
// constructed - the same "know your size before your content is resolved"
// discipline design.md section 5.10.3 already forces on a decoded image
// (doc/image.md), extended to wrapped text (doc/text-layout.md section 2).
// LayoutTree itself never measures a byte of text; it only ever sees a
// declared `height` like any other box.
//
//   latin       a long English sentence, wrapped at ordinary space breaks -
//               the baseline case every text layout engine handles.
//   cjk         16+ UNSPACED Chinese characters - UAX#14 line breaking is
//               the ONLY thing that can wrap this at all, and 7-1's smoke
//               test already proved libgrapheme's rule table fires more
//               than 4 times on this exact string. This is the owner's
//               stated priority, drawn first among the CJK-bearing panels.
//   mixed       Latin + Chinese + a colour emoji in ONE run of text -
//               6-1's font-fallback chain (one family, thirteen scripts)
//               consumed by SkParagraph rather than by the plain SkFont
//               path, proving the "zero fontconfig" chain still drives
//               text layout's font selection.
//   bidi        Arabic embedded in Latin ("hello <arabic> world") - BiDi
//               reorders the Arabic run right-to-left WITHIN the text,
//               while the panel itself (and every other panel, and the
//               whole window) stays left-to-right - design.md section
//               5.13.7's "BiDi yes, UI mirroring no", drawn rather than
//               merely asserted.
//   ellipsized  the same long sentence as `latin`, `max_lines = 2` - the
//               multi-line counterpart of the single-line TextField's
//               ellipsize() (doc/text-input.md section 1.4), now built on
//               SkParagraph's own truncation rather than a hand-rolled one.
//
// Every panel uses a REAL SYSTEM FONT directory (`/usr/share/fonts`, the
// same default examples/06_font_fallback already scans), not the generated
// test fonts test_paragraph.cpp uses - because the point of this demo is to
// show what this project's own font-fallback chain draws on a real machine,
// the same honesty argument doc/font-fallback.md's "this machine has no
// Japanese font" caveat already established for a static screenshot.

#pragma once

#include <string>
#include <vector>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/font_catalog.h"
#include "drawgui/render/render_tree.h"

namespace multiline_scene {

inline constexpr int kPanelWidth = 260;
inline constexpr int kPanelPadding = 12;
inline constexpr int kPanelGap = 16;
inline constexpr float kFontSize = 18.0F;

// The five strings, named so a check can rebuild the same
// dg::Paragraph::build() call the scene did and compare against the SAME
// hand nothing but arithmetic on Skia's own reported metrics - never a
// screenshot.
inline const std::string kLatinText =
    "Multi-line paragraph layout replaces a fixed rule with a real one: "
    "SkParagraph wraps this English sentence at ordinary word breaks, the "
    "same way any text editor does.";
inline const std::string kCjkText =
    "\u591a\u884c\u6587\u672c\u6392\u7248\u73b0\u5728\u7531\u771f\u5b9e\u7684"
    "\u89c4\u5219\u63a5\u7ba1\uff0c\u800c\u4e0d\u662f\u56fa\u5b9a\u89c4\u5219"
    "\uff1a\u6ca1\u6709\u7a7a\u683c\u7684\u4e2d\u6587\u53ea\u80fd\u9760"
    "\u65ad\u884c\u89c4\u5219\u8868\u624d\u80fd\u6362\u884c\u3002";
inline const std::string kMixedText =
    "Mixed script: \u4e2d\u6587 + Latin + \U0001F600 in one run, one family "
    "chain, zero fontconfig.";
inline const std::string kBidiText =
    "hello \u0645\u0631\u062D\u0628\u0627 world, BiDi "
    "reorders the Arabic run without mirroring the UI.";

struct Handles {
  dg::NodeId column;
  dg::NodeId latin_panel;
  dg::NodeId cjk_panel;
  dg::NodeId mixed_panel;
  dg::NodeId bidi_panel;
  dg::NodeId ellipsized_panel;
};

struct Scene {
  dg::LayoutTree tree;
  dg::FontCatalog fonts;
  dg::FontId primary;
  Handles handles;

  // The layout pass count needed for the exactly-once verdict: the FIRST
  // call is `layout_full()` building the scene, so `stats_after_build`
  // records what a single full pass over the whole tree cost - doc/text-
  // layout.md section 2's answer, measured rather than assumed.
  dg::LayoutStats stats_after_build;
};

// `spec.fonts` is overwritten with a catalog scanned from `font_dir` and one
// family added under `primary_family` - a caller only supplies the viewport,
// background and (optionally) which font directory/family to use.
Scene build(dg::TreeSpec spec, const std::string& font_dir = "/usr/share/fonts",
            const std::string& primary_family = "DejaVu Sans");

inline constexpr dg::PixelSize kDemoViewport{620, 900};

}  // namespace multiline_scene
