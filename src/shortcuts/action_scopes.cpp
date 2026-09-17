#include "drawgui/shortcuts/action_scopes.h"

#include <algorithm>

namespace dg {

void ActionScopes::scope(NodeId node, dg_action_id action_id) {
  const std::size_t index = node.index;
  if (by_node_.size() <= index) {
    by_node_.resize(index + 1);
  }
  std::vector<dg_action_id>& actions = by_node_[index];
  if (std::find(actions.begin(), actions.end(), action_id) != actions.end()) {
    return;
  }
  actions.push_back(action_id);
}

bool ActionScopes::consumes(NodeId node, dg_action_id action_id) const {
  if (node.index >= by_node_.size()) {
    return false;
  }
  const std::vector<dg_action_id>& actions = by_node_[node.index];
  return std::find(actions.begin(), actions.end(), action_id) != actions.end();
}

void ActionScopes::forget(NodeId node) {
  if (node.index >= by_node_.size()) {
    return;
  }
  by_node_[node.index].clear();
}

}  // namespace dg
