// App-defined shortcuts: App::add_shortcut() (router level 4) and
// Window::add_shortcut() (level 2). Contract stubs.

#include <string>
#include <string_view>

#include "app/internal.h"
#include "drawgui/app/app.h"
#include "drawgui/app/window.h"

namespace dg {

Status App::add_shortcut(std::string_view /*chord*/, std::uint16_t /*action_id*/) {
  return Status::kUnsupported;
}

Status App::remove_shortcut(std::string_view /*chord*/) {
  return Status::kUnsupported;
}

Expected<std::string, Status> App::chord_label(std::string_view /*chord*/) const {
  return Unexpected(Status::kUnsupported);
}

Status Window::add_shortcut(NodeId /*scope*/, std::string_view /*chord*/,
                            std::uint16_t /*action_id*/) {
  return Status::kUnsupported;
}

}  // namespace dg
