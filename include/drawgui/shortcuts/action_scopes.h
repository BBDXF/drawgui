// ActionScopes: the fourth `NodeId`-keyed side table, after WidgetSet,
// ThemeBindings and Focus (design.md section 5.5.3's `dg_node_scope_action`).
//
// THE SHAPE IS ThemeBindings' OWN SHAPE, DELIBERATELY: a
// `std::vector<std::vector<dg_action_id>>` indexed by `NodeId`, growing the
// same way `ThemeBindings::bind()`/`WidgetSet::ensure()` already do, for the
// identical reason theme_bindings.h's own header argues at length - a node
// may scope more than one action_id (a TextField scoping both select_all
// and paste), so the per-node slot is a small vector rather than one, and
// this is a fact about WHICH NODE consumes an intent, not a fact any
// painter/hit-tester/layout reader needs to learn a second meaning for, so
// it belongs beside WidgetSet rather than inside NodeStyle or Widget.
//
// APPEND-ONLY IN THE SAME SENSE THE OTHER THREE SIDE TABLES ARE: scoping the
// same (node, action_id) pair twice is a no-op (checked before inserting),
// never a growing duplicate list - the same de-duplication ThemeBindings::
// bind() performs for a re-bound (node, prop_id) pair, adapted to a set
// rather than a map because one node scoping the same action twice has
// nothing to REPLACE (there is no second field to overwrite, unlike a
// TokenBinding's token_id).
//
// `forget(NodeId)` IS INCLUDED NOW, NOT DEFERRED, even though nothing calls
// it yet: 8-5's plan entry (drawgui-p0.md) already commits to ONE
// `on_node_removed()` taking EVERY side table as a required reference
// parameter - WidgetSet, ThemeBindings, Focus and this one - so a fourth
// side table with no removal hook would be a known gap in a signature 8-5
// is going to write regardless, not a speculative addition ahead of a
// caller the way a hypothetical `unbind()` would be.

#pragma once

#include <cstddef>
#include <vector>

#include "drawgui/render/render_tree.h"
#include "drawgui/shortcuts/action_ids.generated.h"

namespace dg {

class ActionScopes {
 public:
  // Records that `node` consumes `action_id` - design.md section 5.5.3's
  // `dg_node_scope_action`. A no-op if `node` already scopes `action_id`.
  // Grows the backing vector to cover `node`, the same way
  // ThemeBindings::bind() does.
  void scope(NodeId node, dg_action_id action_id);

  [[nodiscard]] bool consumes(NodeId node, dg_action_id action_id) const;

  // 8-5's `on_node_removed()` will call this on every side table by name
  // (drawgui-p0.md's own slice list) - clears whatever `node` scoped, so a
  // recycled/removed node's stale scoping cannot resurface against whatever
  // reoccupies its NodeId. A no-op for a node that never scoped anything.
  void forget(NodeId node);

 private:
  std::vector<std::vector<dg_action_id>> by_node_;
};

}  // namespace dg
