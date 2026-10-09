// The size limits the runtime enforces on caller-supplied arrays. The C ABI
// checks the same constants before copying a C array, so both front ends
// refuse exactly the same inputs.

#pragma once

#include <cstddef>

namespace dg::limits {

// ListRef::set_items(): rows.
inline constexpr std::size_t kListMaxItems = std::size_t{1} << 24U;
// Window::open_menu(): entries; DropdownRef::set_items(): options.
inline constexpr std::size_t kMaxMenuItems = 4096;
// Window::open_dialog(): buttons.
inline constexpr std::size_t kMaxDialogButtons = 8;
// TabsRef::add(): tabs per strip.
inline constexpr std::size_t kMaxTabs = 64;
// TableRef::set_columns(): columns.
inline constexpr std::size_t kMaxTableColumns = 64;
// TreeRef::set_items(): items; TableRef::set_rows(): rows x columns cells.
inline constexpr std::size_t kMaxDataItems = std::size_t{1} << 24U;

}  // namespace dg::limits
