// Which mouse cursor the pointer should show - DG_PROP_CURSOR resolved
// against the tree.
//
// THE RULE, climbing from the node under the pointer towards the root:
//
//   1. A node whose own NodeStyle::cursor is not kAuto decides. This includes
//      disabled nodes, so an author who wants kNotAllowed over a disabled
//      button sets it explicitly.
//   2. Otherwise a widget whose kind has an opinion of its own decides
//      (widget_default_cursor() != kDefault) - unless the hit node is
//      disabled (is_disabled()), in which case widget kinds have no opinion.
//   3. Nothing decided: kDefault.
//
// WIDGET DEFAULTS follow desktop conventions rather than the web's: only a
// text field changes the cursor (to the I-beam). Native buttons, checkboxes,
// dropdowns, list rows and sliders on Windows, macOS, GNOME and KDE all keep
// the arrow; the hand is a hyperlink cursor. An app that wants the web look
// sets DG_PROP_CURSOR = pointer on its buttons (or on a container of them).
// Disabled widgets show the arrow, not kNotAllowed - again what native
// toolkits do.
//
// Pure functions of the tree, like dg::is_disabled(): nothing is cached, so
// there is no second copy of the answer to go stale.

#pragma once

#include <optional>

#include "drawgui/base/cursor.h"
#include "drawgui/base/pixel_geometry.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/widget/interaction.h"
#include "drawgui/widget/widget_set.h"

namespace dg {

// The cursor a widget kind shows when nothing on its path says otherwise.
[[nodiscard]] Cursor widget_default_cursor(WidgetKind kind);

// The rule above, for `node` (nothing under the pointer: kDefault). Never
// returns kAuto.
[[nodiscard]] Cursor resolve_cursor(const RenderTree& tree, const WidgetSet& widgets,
                                    std::optional<NodeId> node);

// What the pointer at `point` should show, given `interaction`'s state.
//
// While a press is held the HELD widget decides, not whatever is under the
// pointer: dragging a text selection out of a field keeps the I-beam, and
// dragging off a button does not flicker through every neighbour's cursor -
// the same implicit grab Interaction::moved_over() already applies to hover.
[[nodiscard]] Cursor cursor_at(const RenderTree& tree, const WidgetSet& widgets,
                               const Interaction& interaction, PixelPoint point);

}  // namespace dg
