// The one place a node's removal reaches every side table that keys state
// by NodeId - the ANSWER to doc/widgets.md's own recorded question ("the
// day removal arrives"), and to the six-table map slice 8-5 grounded
// against the working engine rather than assumed complete from a design
// sketch: WidgetSet, ThemeBindings, Focus and ActionScopes were the four
// the initial consultation named; reading interaction.h found a fifth
// (Interaction's hovered_/holding_), and animation_engine.h a sixth that
// additionally had no per-node cancel to call yet (AnimationEngine, whose
// cancel_all_for() this slice built for exactly this call site).
//
// EVERY TABLE IS A REQUIRED, NON-DEFAULTED REFERENCE PARAMETER, and that
// shape IS the guarantee, not merely documentation of one: a seventh
// NodeId-keyed side table arriving later either threads through this
// signature - which the compiler then forces onto every existing call
// site - or it produces a second, visibly incomplete cleanup function next
// to this one, which a reviewer can see and reject on sight. Neither
// outcome lets a table silently go uncleaned the way an optional or
// defaulted parameter would.
//
// doc/showcase.md records the one case this project has already measured
// of two systems not knowing about each other: ThemeBindings and
// AnimationEngine both writing to the same property with neither aware of
// the other, one silently overwriting the other's frame. A NodeId-keyed
// side table added to this project without going through this function
// would be the identical defect one level up - a table simply never told a
// node under it is gone, silently keeping stale state pointed at whatever
// the recycled index becomes next - so the required-parameter shape here
// is this slice's answer to that same class of bug, not a new one.

#pragma once

#include <vector>

#include "drawgui/anim/animation_engine.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/shortcuts/action_scopes.h"
#include "drawgui/theme/theme_bindings.h"
#include "drawgui/widget/focus.h"
#include "drawgui/widget/interaction.h"
#include "drawgui/widget/widget_set.h"

namespace dg {

// Every id in `id`'s subtree (id included), in the order RenderTree's own
// pre-order children() walk visits them - the same enumeration
// dg_dump_layout_tree already relies on, exposed here so a caller/test can
// see exactly which ids on_node_removed() is about to clean up before it
// runs.
[[nodiscard]] std::vector<NodeId> subtree_of(const RenderTree& tree, NodeId id);

// Detaches `id` (LayoutTree::remove_child(), which detaches both trees at
// once and relayouts the former parent exactly once) and tells every one
// of the six side tables above that every node in the removed subtree is
// gone. Returns false, and touches NOTHING - not even a partial cleanup -
// under the identical conditions LayoutTree::remove_child() itself refuses
// (the root, or an id that does not currently name a live node), which is
// what keeps a caller from having to reason about a half-removed tree: the
// removal and every table's cleanup happen together, or none of it does.
bool on_node_removed(LayoutTree& tree, NodeId id, WidgetSet& widgets,
                     ThemeBindings& theme_bindings, Focus& focus, AnimationEngine& animation,
                     ActionScopes& action_scopes, Interaction& interaction);

}  // namespace dg
