#include "drawgui/render/node_lifecycle.h"

#include <cassert>
#include <utility>

namespace dg {
namespace {

void collect_subtree(const RenderTree& tree, NodeId id, std::vector<NodeId>& out) {
  out.push_back(id);
  for (const NodeId child : tree.children(id)) {
    collect_subtree(tree, child, out);
  }
}

void forget_everywhere(const std::vector<NodeId>& subtree, const NodeSideTables& t) {
  // Focus is checked once against the WHOLE subtree (blur_if_any_of()
  // already takes a span) rather than once per id - cheaper, and it is the
  // identical call list-recycling already makes for the same reason.
  t.focus.blur_if_any_of(subtree);

  for (const NodeId victim : subtree) {
    t.widgets.forget(victim);
    t.theme_bindings.forget(victim);
    t.animation.cancel_all_for(victim);
    t.action_scopes.forget(victim);
    t.interaction.forget(victim);
  }

#ifndef NDEBUG
  // Debug check: no queryable table still names a removed id.
  // (AnimationEngine and ActionScopes expose no per-node query; their
  // cleanup is pinned by tests/unit/test_node_removal.cpp instead.)
  for (const NodeId victim : subtree) {
    assert(!t.widgets.has(victim));
    assert(t.theme_bindings.bindings_for(victim).empty());
    assert(!t.focus.is_focused(victim));
    assert(t.interaction.hovered() != victim);
    assert(t.interaction.holding() != victim);
  }
#endif
}

}  // namespace

std::vector<NodeId> subtree_of(const RenderTree& tree, NodeId id) {
  std::vector<NodeId> out;
  collect_subtree(tree, id, out);
  return out;
}

bool remove_subtree(LayoutTree& tree, NodeId id, const NodeSideTables& t) {
  // The whole subtree has to be gathered BEFORE anything is detached:
  // RenderTree::children() walks live parent/child links, which
  // remove_child() clears the instant it tombstones a node.
  const std::vector<NodeId> subtree = subtree_of(tree.render(), id);
  if (!tree.remove_child(id, RemovalKey{})) {
    return false;
  }
  forget_everywhere(subtree, t);
  return true;
}

bool remove_subtree(RenderTree& tree, NodeId id, const NodeSideTables& t) {
  if (!tree.is_valid(id)) {
    return false;
  }
  const std::vector<NodeId> subtree = subtree_of(tree, id);
  if (!tree.remove_child(id, RemovalKey{})) {
    return false;
  }
  forget_everywhere(subtree, t);
  return true;
}

}  // namespace dg
