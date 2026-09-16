// examples/24_theme_package's headless oracle: loads the external
// `fixtures/mytheme` package (copied to a writable temp directory first,
// so the checked-in fixture is never mutated by a test run), and proves,
// in order: (1) the package loads and renders correctly; (2) editing
// theme.json and reloading changes a bound colour with ZERO relayout;
// (3) editing a different token (space.md, an int token) and reloading
// DOES relayout the bound row - design.md section 12's hot-reload
// granularity question, settled and measured, not assumed; (4) a hostile
// resource path is rejected by dg::ThemePackage::read_resource(), proving
// design.md section 5.7.5's restriction in the SAME process that just
// proved the legitimate path works.
#pragma once

#include <ostream>

namespace theme_package_check {

int run(std::ostream& out);

}  // namespace theme_package_check
