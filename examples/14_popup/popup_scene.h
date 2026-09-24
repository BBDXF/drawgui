// The content examples/14_popup shows through PopupHost - a three-row menu,
// built as flat colour blocks rather than text.
//
// Deliberately textless: the point of this demo is PopupHost itself
// (placement, dismissal), not a widget - a future slice building the
// Dropdown widget on top of PopupHost is where text belongs.
// This scoping decision is by name.
//
// PopupHost is overlay-only: there is no
// second (native) branch left to hold this content's geometry equal
// against, so build_menu_content() is called exactly once per popup open,
// against whichever RenderTree/content_root PopupHost::show() handed back -
// content is always built at LOCAL origin (0, 0) relative to `parent`,
// which is what makes this call site identical regardless of where in the
// host's own tree that container ended up.

#pragma once

#include "drawgui/graphics/types.h"
#include "drawgui/render/render_tree.h"

namespace popup_scene {

inline constexpr dg::PixelSize kContentSize{160, 104};
inline constexpr int kRowHeight = 28;
inline constexpr int kRowGap = 4;
inline constexpr int kPadding = 6;

inline constexpr dg::Color kBackground = dg::Color::from_argb(0xFF2B303B);
inline constexpr dg::Color kRowColors[3] = {
    dg::Color::rgba(0xE7, 0x4C, 0x3C),
    dg::Color::rgba(0x2E, 0xCC, 0x71),
    dg::Color::rgba(0x2E, 0x86, 0xDE),
};

// Adds a background panel plus three coloured rows under `parent`, at LOCAL
// bounds starting at (0, 0) - never at `parent`'s absolute position, which
// `tree` supplies automatically through add_child()'s own parent-relative
// contract.
void build_menu_content(dg::RenderTree& tree, dg::NodeId parent, dg::PixelSize size);

}  // namespace popup_scene
