// The content examples/14_popup shows through PopupHost - a three-row menu,
// built as flat colour blocks rather than text.
//
// Deliberately textless: the point of this demo is PopupHost itself (the
// window-kind/overlay split, placement, dismissal), not a widget, and a
// second FontCatalog per native popup window would be one more moving part
// the equivalence oracle would have to hold equal for no reason - a future
// slice building the Dropdown widget on top of PopupHost is where text
// belongs. `doc/popup.md` records this scoping decision by name.
//
// build_menu_content() is called with THE SAME LOCAL geometry by both
// branches - RenderTree::add_child()'s bounds are always relative to the
// parent it is given, so calling this against a fresh tree's root() (native)
// or against an overlay container appended into the host's own tree
// (overlay) produces byte-identical local rectangles either way. That
// equality is the whole of what examples/14_popup's oracle checks.

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
