// NodeKind - what dg::Window::create() builds.
//
// The enumerator values ARE the C ABI's DG_NODE_TYPE_* numbers (abi/
// drawgui.def.toml); src/abi static_asserts every one, so a C handle's
// type_id converts with a plain cast.

#pragma once

#include <cstdint>

namespace dg {

enum class NodeKind : std::uint8_t {
  kBox = 1,
  kButton = 2,
  kLabel = 3,
  kCheckbox = 4,
  kTextField = 5,
  kTextArea = 6,
  kScrollView = 7,
  kList = 8,
  kImage = 9,
  kDropdown = 10,
  kProgress = 11,
  kTabs = 12,
  kTree = 13,
  kTable = 14,
  kSlider = 15,
  kRadio = 16,
  // A horizontal bar of drop-down menus (Window::menu_bar()).
  kMenuBar = 17,
};

inline constexpr std::uint16_t kNodeKindMin = 1;
inline constexpr std::uint16_t kNodeKindMax = 17;

[[nodiscard]] constexpr bool is_node_kind(std::uint16_t value) {
  return value >= kNodeKindMin && value <= kNodeKindMax;
}

// The src/ui-backed kinds (dropdown, progress bar, tabs, tree, table).
[[nodiscard]] constexpr bool is_component_kind(NodeKind kind) {
  return kind == NodeKind::kDropdown || kind == NodeKind::kProgress ||
         kind == NodeKind::kTabs || kind == NodeKind::kTree || kind == NodeKind::kTable;
}

// Whether Window::create()/move() accept `kind` as a parent: a list's rows
// come from Window::list_set_items(), a slider's only child is its thumb,
// and a component composes its own children (tabs take content through
// Window::tabs_add()'s panels).
[[nodiscard]] constexpr bool accepts_children(NodeKind kind) {
  return kind != NodeKind::kList && kind != NodeKind::kSlider && kind != NodeKind::kMenuBar &&
         !is_component_kind(kind);
}

[[nodiscard]] constexpr bool is_editor_kind(NodeKind kind) {
  return kind == NodeKind::kTextField || kind == NodeKind::kTextArea;
}

}  // namespace dg
