#include "layout_scene.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "drawgui/graphics/types.h"
#include "drawgui/layout/box.h"

namespace layout_scene {
namespace {

using dg::BoxStyle;
using dg::Color;
using dg::CrossAlign;
using dg::EdgeInsets;
using dg::LayoutKind;
using dg::NodeId;
using dg::NodeStyle;

constexpr int kHeaderTicks = 8;
constexpr int kSidebarItems = 7;
constexpr int kToolbarTags = 3;
constexpr int kStatusPips = 4;
constexpr int kGridRows = 2;
constexpr int kGridColumns = 3;
constexpr int kCardAccents = 3;

constexpr float kContainerRadius = 6.0F;
constexpr float kAccentRadius = 4.0F;

NodeStyle flat(std::uint32_t argb) {
  NodeStyle style;
  style.fill = Color::from_argb(argb);
  return style;
}

NodeStyle panel(std::uint32_t fill, std::uint32_t border, bool rounded) {
  NodeStyle style;
  style.fill = Color::from_argb(fill);
  style.border_color = Color::from_argb(border);
  style.border_width = 1.0F;
  if (rounded) {
    style.radii = dg::Radii::all(kContainerRadius);
  }
  return style;
}

// Rounded whatever the container switch says. These are the small things that
// move, and sub-step 1 measured that rounding the things that move is cheap
// while rounding the things that contain them is 30x - a rounded node is
// repainted whole, so a rounded CARD makes its own area the smallest unit of
// damage anything inside it can produce.
NodeStyle accent(std::uint32_t fill, std::uint32_t border) {
  NodeStyle style;
  style.fill = Color::from_argb(fill);
  style.border_color = Color::from_argb(border);
  style.border_width = 1.0F;
  style.radii = dg::Radii::all(kAccentRadius);
  return style;
}

BoxStyle leaf(int width, int height) {
  BoxStyle box;
  if (width > 0) {
    box.width = width;
  }
  if (height > 0) {
    box.height = height;
  }
  return box;
}

BoxStyle flexible(int height) {
  BoxStyle box;
  box.grow = 1;
  if (height > 0) {
    box.height = height;
  }
  return box;
}

BoxStyle stack(LayoutKind kind, int gap, CrossAlign cross) {
  BoxStyle box;
  box.kind = kind;
  box.gap = gap;
  box.cross_align = cross;
  return box;
}

// A triangle wave, so a swept value reverses instead of snapping back. The
// discontinuity of a sawtooth would still be laid out correctly but would
// hide a one-frame stale-pixel artifact inside the jump.
int triangle(int frame, int period) {
  const int phase = ((frame % period) + period) % period;
  const int half = period / 2;
  return phase < half ? phase : period - phase;
}

std::uint8_t mix_channel(std::uint8_t from, std::uint8_t to, int numerator, int denominator) {
  const int span = static_cast<int>(to) - static_cast<int>(from);
  return static_cast<std::uint8_t>(static_cast<int>(from) + ((span * numerator) / denominator));
}

// Integer arithmetic on purpose: the verification replays this script twice
// and compares the results exactly, so every value has to be reproducible
// rather than merely close.
Color mix(std::uint32_t from, std::uint32_t to, int numerator, int denominator) {
  const Color a = Color::from_argb(from);
  const Color b = Color::from_argb(to);
  return Color::rgba(mix_channel(a.red(), b.red(), numerator, denominator),
                     mix_channel(a.green(), b.green(), numerator, denominator),
                     mix_channel(a.blue(), b.blue(), numerator, denominator), 0xFF);
}

constexpr Mutation kAllMutations[] = {
    Mutation::kLeafResizesParent, Mutation::kContainedResize,
    Mutation::kNestedRowInColumn, Mutation::kNoOp,
    Mutation::kPaintOnly,
};

void build_header(dg::LayoutTree& tree, NodeId root, bool rounded) {
  BoxStyle header_box = stack(LayoutKind::kRow, 10, CrossAlign::kCenter);
  header_box.height = 48;
  header_box.padding = EdgeInsets::symmetric(14, 0);
  const NodeId header = tree.add_child(root, header_box, flat(0xFF1B2028));

  tree.add_child(header, leaf(150, 20), panel(0xFF232A34, 0xFF2E3742, rounded));
  for (int i = 0; i < kHeaderTicks; ++i) {
    tree.add_child(header, leaf(26, 10), flat(i % 3 == 0 ? 0xFF3A4657 : 0xFF2A323D));
  }
  tree.add_child(header, flexible(6), flat(0xFF20262F));
}

void build_sidebar(dg::LayoutTree& tree, NodeId body, Handles& handles, bool rounded) {
  BoxStyle sidebar_box = stack(LayoutKind::kColumn, 6, CrossAlign::kStretch);
  sidebar_box.width = 210;
  sidebar_box.padding = EdgeInsets::all(10);
  const NodeId sidebar = tree.add_child(body, sidebar_box, flat(0xFF191E26));

  for (int i = 0; i < kSidebarItems; ++i) {
    BoxStyle item_box = stack(LayoutKind::kRow, 8, CrossAlign::kCenter);
    item_box.height = 28;
    item_box.padding = EdgeInsets::symmetric(8, 0);
    const NodeId item =
        tree.add_child(sidebar, item_box, panel(0xFF232A34, 0xFF2E3742, rounded));

    const NodeId dot = tree.add_child(item, leaf(10, 10), accent(0xFF3FA9F5, 0xFF8FC8FF));
    const NodeId label = tree.add_child(item, flexible(8), flat(0xFF39424F));

    // The middle item, so that the ones above and below it are evidence that
    // a contained change really was contained.
    if (i == kSidebarItems / 2) {
      handles.sidebar_item = item;
      handles.sidebar_dot = dot;
      handles.sidebar_label = label;
    }
  }
}

void build_toolbar(dg::LayoutTree& tree, NodeId content, Handles& handles, bool rounded) {
  BoxStyle toolbar_box = stack(LayoutKind::kRow, 10, CrossAlign::kCenter);
  toolbar_box.height = 40;
  toolbar_box.padding = EdgeInsets::symmetric(10, 0);
  handles.toolbar =
      tree.add_child(content, toolbar_box, panel(0xFF1A1F27, 0xFF2A323D, rounded));

  // No width and no grow, so this column is whatever its widest child is.
  // That is what makes the chip's change reach outward: the group resizes,
  // and every later child of the toolbar moves.
  handles.chip_group = tree.add_child(
      handles.toolbar, stack(LayoutKind::kColumn, 4, CrossAlign::kStart), flat(0xFF222A35));
  handles.chip =
      tree.add_child(handles.chip_group, leaf(90, 18), accent(0xFF2E86DE, 0xFF8FC8FF));
  tree.add_child(handles.chip_group, leaf(64, 6), flat(0xFF39424F));

  for (int i = 0; i < kToolbarTags; ++i) {
    const NodeId tag =
        tree.add_child(handles.toolbar, leaf(46, 20), panel(0xFF232A34, 0xFF2E3742, rounded));
    if (i == 0) {
      handles.first_tag = tag;
    }
  }
  tree.add_child(handles.toolbar, flexible(20), flat(0xFF20262F));
}

void build_card(dg::LayoutTree& tree, NodeId row, Handles& handles, bool rounded,
                bool tracked) {
  BoxStyle card_box = stack(LayoutKind::kColumn, 8, CrossAlign::kStretch);
  card_box.grow = 1;
  card_box.padding = EdgeInsets::all(10);
  const NodeId card = tree.add_child(row, card_box, panel(0xFF222A35, 0xFF313B49, rounded));

  const NodeId title = tree.add_child(card, leaf(0, 18), flat(0xFF2E3846));
  tree.add_child(card, flexible(0), flat(0xFF1C232C));

  BoxStyle foot_box = stack(LayoutKind::kRow, 6, CrossAlign::kCenter);
  foot_box.height = 14;
  const NodeId foot = tree.add_child(card, foot_box, flat(0xFF1E252F));
  for (int i = 0; i < kCardAccents; ++i) {
    tree.add_child(foot, leaf(18, 8), accent(0xFF3B4757, 0xFF4C5B6E));
  }

  if (tracked) {
    handles.card = card;
    handles.card_title = title;
  }
}

// The absolute-positioning demonstration, and every row of design.md section
// 5.4.2's table in one node: a corner anchored by two near edges, a corner
// anchored by two far edges, and a bar pinned by both horizontal edges - which
// is the case that produces a tight constraint, and therefore a relayout
// boundary, without anyone declaring one.
void build_badge_layer(dg::LayoutTree& tree, NodeId row, Handles& handles, bool rounded) {
  BoxStyle layer_box;
  layer_box.kind = LayoutKind::kAbsolute;
  layer_box.grow = 1;
  layer_box.padding = EdgeInsets::all(8);
  handles.badge_layer = tree.add_child(row, layer_box, panel(0xFF222A35, 0xFF313B49, rounded));

  BoxStyle top_left = leaf(44, 14);
  top_left.left = 6;
  top_left.top = 6;
  tree.add_child(handles.badge_layer, top_left, accent(0xFFE74C3C, 0xFFFF9C8F));

  BoxStyle bottom_right = leaf(34, 12);
  bottom_right.right = 6;
  bottom_right.bottom = 6;
  tree.add_child(handles.badge_layer, bottom_right, accent(0xFF27AE60, 0xFF6FE0A0));

  BoxStyle bar = leaf(0, 6);
  bar.left = 6;
  bar.right = 6;
  bar.bottom = 26;
  handles.stretched_bar =
      tree.add_child(handles.badge_layer, bar, accent(0xFFF6C445, 0xFFFFE08A));
}

void build_status(dg::LayoutTree& tree, NodeId content, bool rounded) {
  BoxStyle status_box = stack(LayoutKind::kRow, 8, CrossAlign::kCenter);
  status_box.height = 24;
  status_box.padding = EdgeInsets::symmetric(10, 0);
  status_box.main_align = dg::MainAlign::kSpaceBetween;
  const NodeId status =
      tree.add_child(content, status_box, panel(0xFF1A1F27, 0xFF2A323D, rounded));
  for (int i = 0; i < kStatusPips; ++i) {
    tree.add_child(status, leaf(40, 10), flat(0xFF313B49));
  }
}

void set_box_copy(dg::LayoutTree& tree, NodeId id, BoxStyle box) {
  tree.set_box(id, box);
}

}  // namespace

Scene build(const Options& options) {
  dg::LayoutTree tree{options.spec};
  const bool rounded = options.rounded_containers;

  BoxStyle root_box = stack(LayoutKind::kColumn, 0, CrossAlign::kStretch);
  set_box_copy(tree, dg::LayoutTree::root(), root_box);

  Handles handles;
  const NodeId root = dg::LayoutTree::root();
  build_header(tree, root, rounded);

  BoxStyle body_box = stack(LayoutKind::kRow, 0, CrossAlign::kStretch);
  body_box.grow = 1;
  handles.body = tree.add_child(root, body_box, flat(0xFF14171C));

  build_sidebar(tree, handles.body, handles, rounded);

  BoxStyle content_box = stack(LayoutKind::kColumn, 12, CrossAlign::kStretch);
  content_box.grow = 1;
  content_box.padding = dg::EdgeInsets::all(12);
  const NodeId content = tree.add_child(handles.body, content_box, flat(0xFF14171C));

  build_toolbar(tree, content, handles, rounded);

  BoxStyle grid_box = stack(LayoutKind::kColumn, 12, CrossAlign::kStretch);
  grid_box.grow = 1;
  const NodeId grid = tree.add_child(content, grid_box, flat(0xFF14171C));
  for (int row_index = 0; row_index < kGridRows; ++row_index) {
    BoxStyle row_box = stack(LayoutKind::kRow, 12, CrossAlign::kStretch);
    row_box.grow = 1;
    const NodeId row = tree.add_child(grid, row_box, flat(0xFF14171C));
    for (int column = 0; column < kGridColumns; ++column) {
      const bool last = row_index == kGridRows - 1 && column == kGridColumns - 1;
      if (last) {
        build_badge_layer(tree, row, handles, rounded);
      } else {
        build_card(tree, row, handles, rounded, row_index == 0 && column == 1);
      }
    }
  }

  build_status(tree, content, rounded);

  handles.readout = tree.add_child(root, leaf(0, options.readout_height), flat(0xFF0E1116));

  tree.layout_full();
  return Scene{std::move(tree), handles};
}

void apply(dg::LayoutTree& tree, const Handles& handles, Mutation mutation, int frame) {
  switch (mutation) {
    case Mutation::kLeafResizesParent: {
      BoxStyle box = tree.box(handles.chip);
      box.width = 70 + triangle(frame * 2, 120);
      set_box_copy(tree, handles.chip, box);
      return;
    }
    case Mutation::kContainedResize: {
      BoxStyle box = tree.box(handles.card_title);
      box.height = 14 + triangle(frame, 24);
      set_box_copy(tree, handles.card_title, box);
      return;
    }
    case Mutation::kNestedRowInColumn: {
      BoxStyle box = tree.box(handles.sidebar_label);
      box.margin.right = triangle(frame * 3, 40);
      set_box_copy(tree, handles.sidebar_label, box);
      return;
    }
    case Mutation::kNoOp: {
      // Assigned the value it already holds. The dirty mark is real and the
      // boundary is laid out again; nothing may move.
      const BoxStyle box = tree.box(handles.stretched_bar);
      set_box_copy(tree, handles.stretched_bar, box);
      return;
    }
    case Mutation::kPaintOnly:
      tree.render().set_fill(handles.sidebar_dot,
                             mix(0xFF264257, 0xFF3FA9F5, triangle(frame, 96), 48));
      return;
  }
}

void apply_frame(dg::LayoutTree& tree, const Handles& handles, int frame) {
  apply(tree, handles, Mutation::kLeafResizesParent, frame);
  apply(tree, handles, Mutation::kPaintOnly, frame);
  if (frame % 3 == 0) {
    apply(tree, handles, Mutation::kContainedResize, frame);
  }
  if (frame % 5 == 0) {
    apply(tree, handles, Mutation::kNestedRowInColumn, frame);
  }
  if (frame % 7 == 0) {
    apply(tree, handles, Mutation::kNoOp, frame);
  }
}

const char* name_of(Mutation mutation) {
  switch (mutation) {
    case Mutation::kLeafResizesParent:
      return "leaf resizes its parent, siblings shift";
    case Mutation::kContainedResize:
      return "leaf resizes inside a pinned card";
    case Mutation::kNestedRowInColumn:
      return "leaf inside a row inside a column";
    case Mutation::kNoOp:
      return "box reassigned its own value";
    case Mutation::kPaintOnly:
      return "fill colour only, no geometry";
  }
  return "?";
}

const Mutation* all_mutations(int& count) {
  count = static_cast<int>(std::size(kAllMutations));
  return static_cast<const Mutation*>(kAllMutations);
}

}  // namespace layout_scene
