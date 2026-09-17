#include "render/node_lifecycle.h"

#include <utility>

namespace dg {
namespace {

void collect_subtree(const RenderTree& tree, NodeId id, std::vector<NodeId>& out) {
  out.push_back(id);
  for (const NodeId child : tree.children(id)) {
    collect_subtree(tree, child, out);
  }
}

}  // namespace

std::vector<NodeId> subtree_of(const RenderTree& tree, NodeId id) {
  std::vector<NodeId> out;
  collect_subtree(tree, id, out);
  return out;
}

bool on_node_removed(LayoutTree& tree, NodeId id, WidgetSet& widgets,
                     ThemeBindings& theme_bindings, Focus& focus, AnimationEngine& animation,
                     ActionScopes& action_scopes, Interaction& interaction) {
  // The whole subtree has to be gathered BEFORE anything is detached:
  // RenderTree::children() walks live parent/child links, which
  // remove_child() clears the instant it tombstones a node.
  const std::vector<NodeId> subtree = subtree_of(tree.render(), id);

  if (!tree.remove_child(id)) {
    return false;
  }

  // Focus is checked once against the WHOLE subtree (blur_if_any_of()
  // already takes a span) rather than once per id - cheaper, and it is the
  // identical call list-recycling already makes for the same reason.
  focus.blur_if_any_of(subtree);

  for (const NodeId victim : subtree) {
    widgets.forget(victim);
    theme_bindings.forget(victim);
    animation.cancel_all_for(victim);
    action_scopes.forget(victim);
    interaction.forget(victim);
  }
  return true;
}

}  // namespace dg
