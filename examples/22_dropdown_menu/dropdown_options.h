// The dropdown's own popup content: one interactive kButton row per option
// string, stacked in a plain kColumn - doc/form-controls.md section 2.3's
// own settled finding ("a dropdown's open list is a kColumn of plain rows
// inside a container... exactly what examples/10_scrolling already draws")
// applied literally, except these rows ARE attached kButton widgets (like
// examples/21_focus's popup_menu.cpp, not examples/14_popup's flat colour
// blocks) because keyboard Up/Down needs real Tab stops to move
// dg::Focus::focus_next()/focus_previous() through.
//
// kList (doc/list.md) was evaluated and NOT reused here: its pool nodes
// carry no attached Widget at all (5-3's own recycled slots are plain
// content-bearing children, never interactive), so a kList-backed row could
// not be focused, clicked, or highlighted without a second, parallel
// interaction layer this slice would have had to invent on top of it -
// exactly the "new machinery this slice's acceptance criteria did not ask
// for" shape doc/form-controls.md section 8 already declined once for a
// slider's hover glow. A handful of ordinary kButton rows is also simply
// the right tool at a dropdown's usual size: doc/list.md's own measured
// virtualization threshold is "somewhere past 100,000 items" on this host,
// six orders of magnitude past what an options list has here or in any
// realistic dropdown.
//
// build() takes the WidgetSet to attach into as an explicit parameter for
// the identical reason examples/21_focus's popup_menu.h already states:
// the overlay branch appends into the HOST's own RenderTree, and
// dg::focus_order() walks that tree against ONE WidgetSet, so overlay rows
// must land in the host's own table or Tab/Up/Down would never see them;
// the native branch owns a genuinely separate RenderTree and therefore a
// genuinely separate WidgetSet of its own.

#pragma once

#include <string>
#include <vector>

#include "drawgui/graphics/types.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/widget/widget_set.h"

namespace dropdown_options {

inline constexpr int kRowHeight = 28;
inline constexpr int kRowPad = 6;

// The popup's own content size for `count` options at `width` device
// pixels wide - what a caller passes to PopupHost::show() BEFORE content
// exists, matching every other PopupHost client's own "know the size up
// front" shape (doc/popup.md).
[[nodiscard]] dg::PixelSize size_for(int count, int width);

// Builds `options.size()` rows under `parent`, each `kRowHeight` px tall
// and `width` px wide, in option order - which IS Tab/Up-Down order,
// because RenderTree::children()/dg::focus_order() both walk in add
// order, the same fact 7-4's own focus_order() already rests its DOM-shaped
// default on. Returns the row NodeIds, one per option, in that same order.
// Takes no FontCatalog: a row's own text child is a plain `NodeStyle::text`
// assignment (`font`/`font_size` passed straight through), not a measured
// paragraph - the same "content is the caller's job" boundary kList's own
// item nodes already have.
std::vector<dg::NodeId> build(dg::RenderTree& tree, dg::WidgetSet& widgets, dg::NodeId parent,
                              const std::vector<std::string>& options, int width,
                              dg::FontId font, float font_size);

}  // namespace dropdown_options
