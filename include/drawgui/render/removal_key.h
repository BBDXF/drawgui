// RemovalKey - the passkey that makes dg::remove_subtree() (node_lifecycle.h)
// the ONLY way to detach a node. RenderTree::remove_child() and
// LayoutTree::remove_child() each take one, and only the two remove_subtree()
// overloads can construct one, so a raw detach that skips the six NodeId-keyed
// side tables (doc/contracts.md section 8) does not compile. Kept in its own
// header, with everything it names forward-declared, so render_tree.h and
// layout_tree.h can both see it without an include cycle.

#pragma once

namespace dg {

struct NodeId;
class RenderTree;
class LayoutTree;
struct NodeSideTables;

class RemovalKey {
 private:
  RemovalKey() = default;

  friend bool remove_subtree(LayoutTree& tree, NodeId id, const NodeSideTables& t);
  friend bool remove_subtree(RenderTree& tree, NodeId id, const NodeSideTables& t);
};

}  // namespace dg
