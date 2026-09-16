#include "multiline_check.h"

#include <ostream>
#include <string>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/paragraph.h"
#include "drawgui/render/render_tree.h"

#include "multiline_scene.h"

namespace multiline_check {
namespace {

using dg::LayoutStats;
using dg::Paragraph;
using dg::PixelRect;
using dg::TextStyle;

bool check_height(const dg::FontCatalog& fonts, const multiline_scene::Scene& scene,
                  dg::NodeId panel, const TextStyle& text, const char* name, std::ostream& out,
                  bool& ok) {
  dg::Expected<Paragraph, dg::FontError> rebuilt =
      Paragraph::build(fonts, text,
                        static_cast<float>(multiline_scene::kPanelWidth -
                                           (2 * multiline_scene::kPanelPadding)));
  if (!rebuilt.has_value()) {
    out << "  FAIL " << name << ": independent Paragraph::build() failed\n";
    ok = false;
    return false;
  }
  const int expected_height =
      rebuilt.value().metrics().height + (2 * multiline_scene::kPanelPadding);
  const PixelRect box = scene.tree.bounds(panel);
  if (box.height != expected_height) {
    out << "  FAIL " << name << ": panel height " << box.height
        << " != independently rebuilt height " << expected_height << "\n";
    ok = false;
    return false;
  }
  return true;
}

}  // namespace

int run(std::ostream& out) {
  out << "MULTILINE TEXT: SkParagraph wrap/CJK/BiDi against an independently rebuilt\n"
         "  height oracle, the line-count claims, and the exactly-once-layout verdict.\n\n";

  dg::TreeSpec spec;
  spec.viewport = multiline_scene::kDemoViewport;
  spec.background.fill = dg::Color::from_argb(0xFF14171C);
  multiline_scene::Scene scene = multiline_scene::build(spec);

  if (!scene.tree.diagnostics().empty()) {
    out << "  FAIL: the demo scene produced a layout diagnostic:\n";
    for (const std::string& line : scene.tree.diagnostics()) {
      out << "    " << line << "\n";
    }
    return 1;
  }

  bool ok = true;

  TextStyle latin_style;
  latin_style.text = multiline_scene::kLatinText;
  latin_style.font = scene.primary;
  latin_style.size = multiline_scene::kFontSize;
  latin_style.wrap = true;
  check_height(scene.fonts, scene, scene.handles.latin_panel, latin_style, "latin", out, ok);

  TextStyle cjk_style = latin_style;
  cjk_style.text = multiline_scene::kCjkText;
  cjk_style.language = "zh-Hans";
  check_height(scene.fonts, scene, scene.handles.cjk_panel, cjk_style, "cjk", out, ok);

  TextStyle mixed_style = latin_style;
  mixed_style.text = multiline_scene::kMixedText;
  mixed_style.language = "zh-Hans";
  check_height(scene.fonts, scene, scene.handles.mixed_panel, mixed_style, "mixed", out, ok);

  TextStyle bidi_style = latin_style;
  bidi_style.text = multiline_scene::kBidiText;
  check_height(scene.fonts, scene, scene.handles.bidi_panel, bidi_style, "bidi", out, ok);

  TextStyle ellipsized_style = latin_style;
  ellipsized_style.max_lines = 2;
  check_height(scene.fonts, scene, scene.handles.ellipsized_panel, ellipsized_style, "ellipsized",
              out, ok);

  out << "  height oracle: " << (ok ? "PASS" : "FAIL") << "\n";

  // Line-count claims, rebuilt independently rather than trusted from the
  // scene: the CJK string carries not one space, so ANY wrap it shows is
  // UAX#14's rule table firing, not whitespace - the "16 unspaced Han
  // characters" smoke-test claim, now consumed by a real paragraph.
  bool lines_ok = true;
  {
    dg::Expected<Paragraph, dg::FontError> latin_built = Paragraph::build(
        scene.fonts, latin_style,
        static_cast<float>(multiline_scene::kPanelWidth - (2 * multiline_scene::kPanelPadding)));
    dg::Expected<Paragraph, dg::FontError> cjk_built = Paragraph::build(
        scene.fonts, cjk_style,
        static_cast<float>(multiline_scene::kPanelWidth - (2 * multiline_scene::kPanelPadding)));
    dg::Expected<Paragraph, dg::FontError> ellipsized_built = Paragraph::build(
        scene.fonts, ellipsized_style,
        static_cast<float>(multiline_scene::kPanelWidth - (2 * multiline_scene::kPanelPadding)));
    if (!latin_built.has_value() || !cjk_built.has_value() || !ellipsized_built.has_value()) {
      out << "  FAIL: could not rebuild a paragraph for the line-count claims\n";
      lines_ok = false;
    } else {
      const dg::ParagraphMetrics latin_metrics = latin_built.value().metrics();
      const dg::ParagraphMetrics cjk_metrics = cjk_built.value().metrics();
      const dg::ParagraphMetrics ellipsized_metrics = ellipsized_built.value().metrics();
      out << "  latin: " << latin_metrics.line_count << " line(s); cjk (no spaces): "
          << cjk_metrics.line_count << " line(s); ellipsized (max_lines=2): "
          << ellipsized_metrics.line_count << " line(s), exceeded="
          << (ellipsized_metrics.exceeded_max_lines ? "true" : "false") << "\n";
      lines_ok = latin_metrics.line_count > 1 && cjk_metrics.line_count > 1 &&
                ellipsized_metrics.line_count == 2 && ellipsized_metrics.exceeded_max_lines;
    }
  }
  out << "  line-count claims: " << (lines_ok ? "PASS" : "FAIL") << "\n";
  ok = ok && lines_ok;

  // The exactly-once-layout verdict, doc/text-layout.md section 2's own
  // question, answered with LayoutStats numbers rather than an argument.
  const LayoutStats built = scene.stats_after_build;
  out << "  after building the scene (one layout_full() call): nodes_total="
      << built.nodes_total << " nodes_visited=" << built.nodes_visited
      << " nodes_relaid_out=" << built.nodes_relaid_out << "\n";
  const bool built_ok = built.nodes_visited == built.nodes_total &&
                       built.nodes_relaid_out == built.nodes_total;

  const LayoutStats second = scene.tree.layout();
  out << "  a second, no-op layout() call: nodes_visited=" << second.nodes_visited
      << " nodes_relaid_out=" << second.nodes_relaid_out << "\n";
  const bool second_ok = second.nodes_visited == 0 && second.nodes_relaid_out == 0;

  const bool layout_ok = built_ok && second_ok;
  out << "  exactly-once-layout verdict: " << (layout_ok ? "PASS" : "FAIL")
      << " (LayoutTree never measured a byte of text - the height was declared "
         "before the node existed)\n";
  ok = ok && layout_ok;

  out << (ok ? "\nPASS\n" : "\nFAIL\n");
  return ok ? 0 : 1;
}

}  // namespace multiline_check
