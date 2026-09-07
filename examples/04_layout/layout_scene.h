// The layout examples/04_layout arranges, and the mutations it applies to it.
//
// It lives here rather than inside main.cpp because tests/unit compiles this
// same file, for the reason sub-step 1 established: an incremental-equals-full
// check is worth much more against the tree that is actually on screen than
// against a second tree written to be easy to verify. It needs no window and
// no SDL, only the layout tree, which is what lets that verification be a
// CTest entry on a headless machine.
//
// The five mutations are not a sample of what a layout can do. They are one
// per class of thing incremental layout can get wrong, and each one is named
// after the property it is there to pin down.

#pragma once

#include <cstdint>

#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/render_tree.h"

namespace layout_scene {

// One class of change, and what it must cost.
enum class Mutation : std::uint8_t {
  // A leaf grows and shrinks inside a shrink-to-fit parent, so the parent
  // resizes and every later sibling of the parent shifts. The case with the
  // widest blast radius, and the one where forgetting to damage the vacated
  // box leaves a trail.
  kLeafResizesParent,

  // A leaf changes height inside a card whose own size is pinned by its
  // parent. Nothing outside the card may move, and nothing outside the card
  // may even be laid out again.
  kContainedResize,

  // A change two levels down a row nested in a column. Same containment
  // claim, but reached through a different arrangement, because a boundary
  // that works for a column and not for a row is a boundary that works by
  // accident.
  kNestedRowInColumn,

  // A box assigned the value it already had. It marks dirty, so the
  // incremental path does real work; it must still move nothing and damage
  // nothing. This is the case that catches a pass which writes bounds
  // unconditionally.
  kNoOp,

  // A fill colour, which layout has no opinion about. Zero nodes re-laid-out
  // is the only acceptable answer, and it is the case that catches layout
  // invalidation leaking out of the render tree's.
  kPaintOnly,
};

// The nodes the script touches. Everything else is arranged once and never
// moves, which is what an application's chrome does and what makes the
// re-layout scope number mean something.
struct Handles {
  // Inside `chip_group`, which has no size of its own and therefore takes the
  // chip's. The toolbar's later children move when this does.
  dg::NodeId chip;
  dg::NodeId chip_group;
  dg::NodeId toolbar;

  // A row of fixed-size tags after the group. Their absolute positions are
  // the evidence that a parent resize really did shift the siblings.
  dg::NodeId first_tag;

  // The title strip of one card. The card is a flexible, stretched child of a
  // row, so it is constrained tightly on both axes and is a relayout
  // boundary; this is the change that must stop there.
  dg::NodeId card_title;
  dg::NodeId card;

  // A leaf two levels inside the sidebar: column > row > leaf.
  dg::NodeId sidebar_label;
  dg::NodeId sidebar_item;

  // Colour only, never a size.
  dg::NodeId sidebar_dot;

  // Pinned by two opposite edges inside an absolute layer, so its constraints
  // are tight and its size is the container's business rather than its own.
  dg::NodeId stretched_bar;
  dg::NodeId badge_layer;

  // The strip the demo prints its readout onto. A tree node rather than
  // something painted beside the tree, so that it takes part in damage like
  // anything else; square-cornered, and therefore safe to clip anywhere.
  dg::NodeId readout;

  dg::NodeId body;
};

struct Scene {
  dg::LayoutTree tree;
  Handles handles;
};

struct Options {
  dg::TreeSpec spec;

  // Rounds the CONTAINERS rather than only the accents. Sub-step 1 measured
  // this exact switch at 492,822 versus 16,250 damaged pixels per frame on a
  // comparable scene - 30x, from one radius - because an anti-aliased
  // rounded node has to be repainted whole. Layout decides node bounds, so
  // layout decides how much that rule costs; the demo makes it a key press
  // rather than a paragraph in a design document.
  bool rounded_containers = false;

  // How tall the readout strip is. It is part of the tree at whatever height
  // it is given, so the offscreen verification and the window run the same
  // arrangement rather than two that differ by one node.
  int readout_height = 150;
};

[[nodiscard]] Scene build(const Options& options);

// Applies one mutation, parameterised by frame so that the same frame number
// always produces the same tree. Nothing here reads a clock or a random
// number: the verification replays the script twice and compares bounds
// exactly, and anything time-dependent would turn that into a coin toss.
void apply(dg::LayoutTree& tree, const Handles& handles, Mutation mutation, int frame);

// Every mutation, on its own schedule, so that most frames change one thing
// and the occasional frame changes several.
void apply_frame(dg::LayoutTree& tree, const Handles& handles, int frame);

[[nodiscard]] const char* name_of(Mutation mutation);

// The five in declaration order, for a test or a report that wants to walk
// them all without repeating the list and getting it wrong.
[[nodiscard]] const Mutation* all_mutations(int& count);

}  // namespace layout_scene
