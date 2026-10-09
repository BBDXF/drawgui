// --verify-showcase: drives every tab of the showcase with injected input.

#pragma once

#include <ostream>

#include "drawgui/app/app.h"

namespace showcase {

int verify(dg::App& app, std::ostream& out);

}  // namespace showcase
