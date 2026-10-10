// App-defined shortcuts: App::add_shortcut() (router level 4) and
// Window::add_shortcut() (level 2), kept beside the generated binding table
// and resolved before it at each level (keys.cpp hands both to
// dg::route_key_event()).
//
// Rebinding an app-wide chord replaces it (app.h). A second
// Window::add_shortcut() of the same chord on the same scope node is a
// conflict (kAlreadyAttached) - remove_shortcut() first.

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>

#include "app/internal.h"
#include "drawgui/app/app.h"
#include "drawgui/app/window.h"
#include "drawgui/shortcuts/chord.h"

namespace dg {

Status App::add_shortcut(std::string_view chord, std::uint16_t action_id) {
  const std::optional<Chord> parsed = parse_user_chord(chord);
  if (!parsed.has_value() || action_id < kUserActionBase) {
    return Status::kInvalidArgument;
  }
  std::vector<UserShortcut>& table = state_->user_shortcuts;
  const auto same = std::find_if(table.begin(), table.end(),
                                 [&](const UserShortcut& entry) { return entry.chord == *parsed; });
  if (same != table.end()) {
    same->action_id = action_id;
    return Status::kOk;
  }
  table.push_back(UserShortcut{*parsed, action_id, std::nullopt});
  return Status::kOk;
}

Status App::remove_shortcut(std::string_view chord) {
  const std::optional<Chord> parsed = parse_user_chord(chord);
  if (!parsed.has_value()) {
    return Status::kInvalidArgument;
  }
  const auto matches = [&](const UserShortcut& entry) { return entry.chord == *parsed; };
  std::size_t removed = std::erase_if(state_->user_shortcuts, matches);
  for (const std::unique_ptr<Window>& window : state_->windows) {
    removed += std::erase_if(window->impl().user_shortcuts, matches);
  }
  return removed > 0 ? Status::kOk : Status::kUnknownId;
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static): public API.
Expected<std::string, Status> App::chord_label(std::string_view chord) const {
  const std::optional<Chord> parsed = parse_user_chord(chord);
  if (!parsed.has_value()) {
    return Unexpected(Status::kInvalidArgument);
  }
  return format_chord(*parsed, Platform::kLinux);
}

Status Window::add_shortcut(NodeId scope, std::string_view chord, std::uint16_t action_id) {
  app_detail::WinState& window = *state_;
  if (app_detail::record_of(window, scope) == nullptr) {
    return Status::kInvalidNode;
  }
  const std::optional<Chord> parsed = parse_user_chord(chord);
  if (!parsed.has_value() || action_id < kUserActionBase) {
    return Status::kInvalidArgument;
  }
  const bool taken = std::any_of(
      window.user_shortcuts.begin(), window.user_shortcuts.end(),
      [&](const UserShortcut& entry) { return entry.chord == *parsed && entry.scope == scope; });
  if (taken) {
    return Status::kAlreadyAttached;
  }
  window.user_shortcuts.push_back(UserShortcut{*parsed, action_id, scope});
  return Status::kOk;
}

}  // namespace dg
