#include "sizing_scene.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/types.h"
#include "drawgui/layout/box.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/render_tree.h"

namespace sizing_scene {
namespace {

using dg::BoxStyle;
using dg::Color;
using dg::CrossAlign;
using dg::EdgeInsets;
using dg::LayoutKind;
using dg::LayoutTree;
using dg::MainAlign;
using dg::MainSize;
using dg::NodeId;
using dg::NodeStyle;

constexpr std::uint32_t kPanel = 0xFF1B1F27;
constexpr std::uint32_t kButtonFill[] = {0xFF2E86DE, 0xFF27AE60, 0xFFE67E22};
constexpr std::uint32_t kChipFill = 0xFF9B59B6;
constexpr std::uint32_t kWideFill = 0xFFE74C3C;
constexpr std::uint32_t kSquareFill = 0xFFF6C445;
constexpr std::uint32_t kFooterFill = 0xFF4A5568;
constexpr std::uint32_t kFillerFill = 0xFF232833;

// Square corners on every container, and a small radius only on the pieces
// that move. doc/damage-repaint.md measured a rounded container at 30x the
// damage of a square one, because an anti-aliased rounded node has to be
// repainted whole - and every one of these rows is a container whose children
// move on every drag.
NodeStyle flat(std::uint32_t argb) {
  NodeStyle style;
  style.fill = Color::from_argb(argb);
  return style;
}

NodeStyle rounded(std::uint32_t argb) {
  NodeStyle style = flat(argb);
  style.radii = dg::Radii{4, 4, 4, 4};
  return style;
}

BoxStyle row_style(int height, int gap) {
  BoxStyle box;
  box.kind = LayoutKind::kRow;
  box.height = height;
  box.gap = gap;
  box.padding = EdgeInsets::all(8);
  box.cross_align = CrossAlign::kStretch;
  return box;
}

}  // namespace

Scene build(const dg::TreeSpec& spec) {
  LayoutTree tree{spec};
  Handles handles;

  BoxStyle page;
  page.kind = LayoutKind::kColumn;
  page.cross_align = CrossAlign::kStretch;
  page.gap = 10;
  page.padding = EdgeInsets::all(10);
  tree.set_box(LayoutTree::root(), page);
  handles.body = LayoutTree::root();

  // A row that FILLS the main axis it was offered. Without that its width
  // would follow its buttons, and the buttons would then never be handed less
  // room than they asked for - so nothing would ever shrink.
  BoxStyle toolbar = row_style(64, 12);
  toolbar.main_size = MainSize::kMax;
  handles.toolbar = tree.add_child(handles.body, toolbar, flat(kPanel));

  for (std::size_t slot = 0; slot < std::size(kButtonWeights); ++slot) {
    BoxStyle button;
    button.width = kButtonBase;
    button.shrink = kButtonWeights[slot];
    button.min_width = 40;
    handles.buttons.push_back(
        tree.add_child(handles.toolbar, button, rounded(kButtonFill[slot])));
  }

  // Takes whatever vertical space the other three rows leave, so the window's
  // HEIGHT is what drives the thumbnails' cross axis.
  // A quarter of the leftover vertical space, with the content panel below
  // taking the other three quarters.
  //
  // The weights are what keeps this row a sensible height, and a `max_height`
  // would NOT have done the job: a grow child is handed a TIGHT main
  // constraint, and box.h's rule is that the parent's constraint wins over the
  // child's own style, so the maximum would be ignored. Measured while
  // building this demo, not assumed - the row came out 370 tall and its 16:9
  // thumbnail overran the window.
  BoxStyle ratio = row_style(0, 12);
  ratio.height.reset();
  ratio.grow = 1;
  ratio.main_size = MainSize::kMax;
  handles.ratio_row = tree.add_child(handles.body, ratio, flat(kPanel));

  BoxStyle wide;
  wide.aspect_ratio = kWideRatio;
  handles.wide_thumb = tree.add_child(handles.ratio_row, wide, rounded(kWideFill));

  BoxStyle square;
  square.aspect_ratio = kSquareRatio;
  handles.square_thumb = tree.add_child(handles.ratio_row, square, rounded(kSquareFill));

  BoxStyle filler;
  filler.grow = 1;
  handles.ratio_filler = tree.add_child(handles.ratio_row, filler, flat(kFillerFill));

  BoxStyle content;
  content.kind = LayoutKind::kRow;
  content.grow = 3;
  handles.content_panel = tree.add_child(handles.body, content, flat(kFillerFill));

  BoxStyle chips = row_style(56, 8);
  chips.main_size = MainSize::kMax;
  handles.chip_row = tree.add_child(handles.body, chips, flat(kPanel));

  for (const int base : kChipBases) {
    BoxStyle chip;
    chip.basis = base;
    chip.shrink = 1;
    handles.chips.push_back(tree.add_child(handles.chip_row, chip, rounded(kChipFill)));
  }

  BoxStyle footer = row_style(44, 8);
  footer.main_size = MainSize::kMax;
  footer.main_align = MainAlign::kEnd;
  handles.footer = tree.add_child(handles.body, footer, flat(kPanel));

  for (int slot = 0; slot < 2; ++slot) {
    BoxStyle item;
    item.width = 110;
    handles.footer_items.push_back(tree.add_child(handles.footer, item, rounded(kFooterFill)));
  }

  tree.layout();
  return Scene{std::move(tree), std::move(handles)};
}

std::string describe(const Scene& scene, NodeId id) {
  for (std::size_t slot = 0; slot < scene.handles.buttons.size(); ++slot) {
    if (scene.handles.buttons[slot] == id) {
      return "button " + std::to_string(slot) + " (shrink " +
             std::to_string(kButtonWeights[slot]) + ")";
    }
  }
  for (std::size_t slot = 0; slot < scene.handles.chips.size(); ++slot) {
    if (scene.handles.chips[slot] == id) {
      return "chip " + std::to_string(slot) + " (basis " + std::to_string(kChipBases[slot]) +
             ")";
    }
  }
  for (std::size_t slot = 0; slot < scene.handles.footer_items.size(); ++slot) {
    if (scene.handles.footer_items[slot] == id) {
      return "footer item " + std::to_string(slot);
    }
  }
  if (id == scene.handles.wide_thumb) {
    return "16:9 thumbnail";
  }
  if (id == scene.handles.square_thumb) {
    return "1:1 thumbnail";
  }
  if (id == scene.handles.ratio_filler) {
    return "filler";
  }
  if (id == scene.handles.toolbar) {
    return "toolbar";
  }
  if (id == scene.handles.ratio_row) {
    return "ratio row";
  }
  if (id == scene.handles.chip_row) {
    return "chip row";
  }
  if (id == scene.handles.footer) {
    return "footer";
  }
  return "node " + std::to_string(id.index);
}

}  // namespace sizing_scene
