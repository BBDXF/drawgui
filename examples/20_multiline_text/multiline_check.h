// The headless half of examples/20_multiline_text.
//
// Three claims, against the exact scene the demo window draws:
//
//   1. every wrapping panel's declared height equals an INDEPENDENTLY
//      rebuilt dg::Paragraph::build() at the same width - not read back
//      from the scene, computed a second time from the panel's own text.
//   2. the CJK panel (no spaces at all) and the latin panel (ordinary word
//      breaks) both wrap to more than one line, and the ellipsized panel's
//      max_lines=2 truncates and reports it - the multi-line counterpart of
//      the single-line TextField's ellipsize() assertion.
//   3. the exactly-once-layout verdict, reported as LayoutStats numbers: a
//      single dg::LayoutTree::layout_full() call visits every node exactly
//      once (doc/text-layout.md section 2's answer), and a second, no-op
//      layout() call relays out nothing.

#pragma once

#include <iosfwd>

namespace multiline_check {

int run(std::ostream& out);

}  // namespace multiline_check
