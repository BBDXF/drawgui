#include "clip_scene.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "drawgui/graphics/types.h"
#include "drawgui/layout/box.h"

namespace clip_scene {
namespace {

using dg::BoxStyle;
using dg::Color;
using dg::CrossAlign;
using dg::EdgeInsets;
using dg::LayoutKind;
using dg::NodeId;
using dg::NodeStyle;
using dg::Overflow;

constexpr float kRoundRadius = 18.0F;

NodeStyle flat(std::uint32_t argb) {
  NodeStyle style;
  style.fill = Color::from_argb(argb);
  return style;
}

NodeStyle container(std::uint32_t fill, Overflow overflow, float radius) {
  NodeStyle style;
  style.fill = Color::from_argb(fill);
  style.border_color = Color::from_argb(0xFF5A6472);
  style.border_width = dg::BorderWidths::all(1.0F);
  style.overflow = overflow;
  style.radii = dg::Radii::all(radius);
  return style;
}

BoxStyle sized(int width, int height) {
  BoxStyle box;
  box.width = width;
  box.height = height;
  return box;
}

BoxStyle row(int gap, const EdgeInsets& padding) {
  BoxStyle box;
  box.kind = LayoutKind::kRow;
  box.gap = gap;
  box.padding = padding;
  box.cross_align = CrossAlign::kStretch;
  return box;
}

// Every panel is a flexible, stretched child of one row, so each is exactly a
// fifth of the window and each one's width follows the window's. That is what
// makes the amount its children overrun it a function of the window size, and
// the resize a real test rather than a redraw.
BoxStyle panel_box(int padding = 10) {
  BoxStyle box;
  box.kind = LayoutKind::kRow;
  box.grow = 1;
  box.padding = EdgeInsets::all(padding);
  box.gap = 8;
  return box;
}

// HOW THE OVERFLOW IS PRODUCED, which took a correction to get right.
//
// The first version gave one child a fixed width larger than the panel, and
// nothing overran at all: `limits_for` clamps a definite width to the
// constraint it arrives under, and that constraint IS the panel's content
// box, so a child asking for 420 inside a 215-wide panel simply becomes 215.
// doc/wrapping.md records the same discovery for the wrapping arrangement,
// and the demo's own check is what found it here - the "distinct overflow
// amounts" guard reported one value, and that value was negative.
//
// The two routes that DO overrun are the two the layout tree reports as
// diagnostics rather than hiding:
//
//   a ROW whose children's total main extent exceeds it. Four chips of 140
//   plus three gaps of 8 is 584, against a panel that is a fifth of the
//   window - so the overrun is large and it changes with every resize.
//
//   a MARGIN, which is subtracted from the child's constraint and then added
//   back to the space it occupies, so a margin larger than the container
//   pushes a child clean out of it. That is what produces the vertical
//   overflow and the child that is outside its panel entirely.
constexpr int kChips = 4;
constexpr int kChipWidth = 140;
constexpr int kChipHeight = 150;
constexpr int kHangBelow = 430;

}  // namespace

Scene build(const dg::TreeSpec& spec) {
  dg::LayoutTree tree{spec};
  Handles handles;

  const NodeId root = dg::LayoutTree::root();
  // The gaps between the panels and the space under them are WIDE, and that
  // is the demonstration rather than the styling: the unclipped control's
  // children overrun into that space and are visible there, while the clipped
  // panel's stop at its edge and the space beside it stays background. With a
  // tight gap the neighbouring panel simply paints over the overflow and the
  // control and the clipped panel look identical - which the first screenshot
  // of this demo showed, and which would have made the whole picture prove
  // nothing.
  const NodeId strip =
      tree.add_child(root, row(30, EdgeInsets{16, 14, 16, 46}), flat(0xFF14171C));

  // A ROUNDED panel gets no padding, so its children run right into the
  // corner and the arc has something to cut. With padding the chips stop
  // short of the curve and the screenshot shows a rounded box next to a
  // square child rather than a child following the curve - which the first
  // capture of this demo did, and which demonstrates nothing.
  const auto add_panel = [&tree, strip](Overflow overflow, float radius, std::uint32_t fill) {
    return tree.add_child(strip, panel_box(radius > 0.0F ? 0 : 10),
                          container(fill, overflow, radius));
  };
  // Four chips that together overrun the panel on the main axis, the last of
  // them pushed down far enough by a margin to overrun on the cross axis as
  // well. A clip that only worked on one axis would show here.
  const auto add_children = [&tree](NodeId parent, std::uint32_t fill) {
    NodeId last;
    for (int index = 0; index < kChips; ++index) {
      BoxStyle chip = sized(kChipWidth, kChipHeight);
      if (index == kChips - 1) {
        chip.margin = EdgeInsets{0, kHangBelow, 0, 0};
      }
      last =
          tree.add_child(parent, chip, flat(fill ^ (static_cast<std::uint32_t>(index) << 4U)));
    }
    return last;
  };

  handles.open = add_panel(Overflow::kVisible, 0.0F, 0xFF20262F);
  handles.open_child = add_children(handles.open, 0xFF3C78D8);

  handles.cut = add_panel(Overflow::kClip, 0.0F, 0xFF20262F);
  handles.cut_child = add_children(handles.cut, 0xFF3C78D8);

  handles.round = add_panel(Overflow::kClip, kRoundRadius, 0xFF2A2030);
  handles.round_child = add_children(handles.round, 0xFFE8B45A);

  handles.nest_outer = add_panel(Overflow::kClip, 0.0F, 0xFF1C2A24);
  BoxStyle inner = panel_box(0);
  inner.margin = EdgeInsets{40, 20, 0, 0};
  handles.nest_inner =
      tree.add_child(handles.nest_outer, inner, container(0xFF27453A, Overflow::kClip, 10.0F));
  handles.nest_child = add_children(handles.nest_inner, 0xFF6AA84F);

  handles.gone = add_panel(Overflow::kClip, 0.0F, 0xFF2E1E1E);
  BoxStyle away = sized(80, 60);
  away.margin = EdgeInsets{500, 0, 0, 0};
  handles.gone_child = tree.add_child(handles.gone, away, flat(0xFFCC4125));

  handles.clipped = {handles.open_child, handles.cut_child,  handles.round_child,
                     handles.nest_child, handles.gone_child, handles.nest_inner};

  tree.layout();
  return Scene{std::move(tree), handles};
}

int overflow_amount(const Scene& scene) {
  const dg::PixelRect panel = scene.tree.bounds(scene.handles.cut);
  const dg::PixelRect child = scene.tree.bounds(scene.handles.cut_child);
  return child.right() - panel.right();
}

// The chip a hover is most likely to land on, which is the first one rather
// than the last - the last hangs below the panel and, in the clipped panels,
// is not on screen at all.

namespace {

// The name of a node the scene declared, if it declared one. A chip is not
// one of these - there are four per panel and naming them individually would
// say nothing a reader wants to know.
std::string named(const Handles& handles, NodeId id) {
  if (id == handles.open) {
    return "unclipped control panel";
  }
  if (id == handles.cut) {
    return "square-clipped panel";
  }
  if (id == handles.round) {
    return "rounded-clipped panel";
  }
  if (id == handles.nest_outer) {
    return "nesting outer panel";
  }
  if (id == handles.nest_inner) {
    return "nesting inner panel";
  }
  if (id == handles.gone) {
    return "panel whose child is outside it";
  }
  if (id == dg::LayoutTree::root()) {
    return "background";
  }
  return {};
}

}  // namespace

// Climbs to the nearest named ancestor, so hovering a chip reports which
// panel's chip it is. That is the sentence the demo is trying to produce: the
// name changes at exactly the column the picture is cut at, and the chips a
// clip removed are never named at all.
std::string describe(const Scene& scene, NodeId id) {
  std::string own = named(scene.handles, id);
  if (!own.empty()) {
    return own;
  }
  NodeId walk = id;
  while (walk != dg::LayoutTree::root()) {
    walk = scene.tree.render().parent(walk);
    std::string owner = named(scene.handles, walk);
    if (owner == "background") {
      return owner;
    }
    if (!owner.empty()) {
      return "a chip of the " + owner;
    }
  }
  return "node #" + std::to_string(id.index);
}

}  // namespace clip_scene
