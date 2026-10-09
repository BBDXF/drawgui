// NodeId on its own, so headers that only name nodes (drawgui/app/events.h)
// need not pull in the whole render tree.

#pragma once

#include <cstdint>

namespace dg {

// Identifies one node in one tree. A struct rather than a bare index so it
// cannot be passed where a count or a coordinate was meant.
//
// `{index, generation}`, not a bare index - a design choice recorded from
// years before removal existed: "a generation counter in NodeId
// ... the one that also catches use-after-remove in the C ABI, where a host
// can hold an id indefinitely". `AnimHandle{index, generation}`
// (animation_engine.h) is the working precedent this mirrors exactly, and
// for the identical reason: a removed node's slot IS reused (RenderTree
// never compacts, but it DOES recycle a tombstoned index for a future
// add_child(), the same free-list shape AnimationEngine's own
// allocate_slot()/free_slot() already have), which reopens the ABA problem
// a bare index cannot detect. `index` is the field's name on purpose,
// RENAMED from the field's previous name (`value`) rather than added
// alongside it: the rename is what forces every existing `.value` access on
// a NodeId to fail to compile, walking a reviewer through every call site
// this change touches instead of letting a stale one compile silently
// against two fields where it only meant one.
struct NodeId {
  std::uint32_t index = 0;
  std::uint32_t generation = 0;

  friend bool operator==(NodeId, NodeId) = default;
};

}  // namespace dg
