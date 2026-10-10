// NodeKind::kMenuBar: a row of titles dropping down menus. Contract stub -
// the node composes like a plain box until the runtime lands.

#include <utility>
#include <vector>

#include "app/components/components.h"
#include "app/internal.h"

namespace dg {

Expected<MenuBarRef, Status> Window::menu_bar(NodeId node) {
  const app_detail::NodeRecord* record = app_detail::record_of(*state_, node);
  if (record == nullptr) {
    return Unexpected(Status::kInvalidNode);
  }
  if (record->kind != NodeKind::kMenuBar) {
    return Unexpected(Status::kUnsupported);
  }
  return MenuBarRef(state_.get(), node);
}

Status MenuBarRef::set_menus(std::vector<MenuBarMenu> /*menus*/) {
  return Status::kUnsupported;
}

Status MenuBarRef::set_entry(int /*menu*/, int /*item*/, MenuEntry /*entry*/) {
  return Status::kUnsupported;
}

}  // namespace dg
