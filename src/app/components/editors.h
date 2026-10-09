// The text editors' palette, shared by editors.cpp, focus.cpp and list.cpp.

#pragma once

#include <cstdint>

#include "app/state.h"
#include "drawgui/graphics/types.h"
#include "drawgui/render/render_tree.h"

namespace dg::app_detail {

// NodeKind::kTextField / kTextArea colours come from the theme
// (editor_palette_for()), so both editors follow light/dark and token
// overrides. These are only the fallbacks for a theme lacking a token - the
// editors' original dark palette.
inline constexpr std::uint32_t kTextFieldFillArgb = 0xFF161C24;
inline constexpr std::uint32_t kTextFieldBorderUnfocusedArgb = 0xFF2C3644;
inline constexpr std::uint32_t kTextFieldBorderFocusedArgb = 0xFF2E86DE;
inline constexpr std::uint32_t kTextFieldTextColorArgb = 0xFFE8EDF4;
// Selection = the accent at this opacity, so text stays readable on top.
inline constexpr std::uint32_t kTextFieldSelectionAlpha = 0x8A;

// Both editors' colours, resolved from the active (else builtin dark) theme:
// box = color.surface, border = color.border, focused border / caret =
// color.primary, text and IME underline = color.on-surface, selection =
// color.primary made translucent.
struct EditorPalette {
  dg::Color fill = dg::Color::from_argb(kTextFieldFillArgb);
  dg::Color border = dg::Color::from_argb(kTextFieldBorderUnfocusedArgb);
  dg::Color border_focused = dg::Color::from_argb(kTextFieldBorderFocusedArgb);
  dg::Color text = dg::Color::from_argb(kTextFieldTextColorArgb);
  dg::Color caret = dg::Color::from_argb(kTextFieldBorderFocusedArgb);
  dg::Color selection = dg::Color::from_argb((kTextFieldSelectionAlpha << 24U) |
                                             (kTextFieldBorderFocusedArgb & 0xFFFFFFU));
  dg::Color underline = dg::Color::from_argb(kTextFieldTextColorArgb);
};

[[nodiscard]] EditorPalette editor_palette_for(const AppState& app);
[[nodiscard]] dg::NodeStyle text_field_box_style(const EditorPalette& palette, bool focused);
// Re-applies text_field_box_style() without clobbering DG_PROP_DISABLED,
// which lives in the same NodeStyle this replaces wholesale.
void set_text_field_box_style(const EditorPalette& palette, dg::RenderTree& tree, dg::NodeId id,
                              bool focused);

}  // namespace dg::app_detail
