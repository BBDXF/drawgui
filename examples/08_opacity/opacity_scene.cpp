#include "opacity_scene.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "drawgui/graphics/types.h"
#include "drawgui/layout/box.h"

namespace opacity_scene {
namespace {

using dg::BoxStyle;
using dg::Color;
using dg::CrossAlign;
using dg::EdgeInsets;
using dg::LayoutKind;
using dg::NodeId;
using dg::NodeStyle;

// Opaque and half-transparent versions of the same three colours. The RGB is
// identical in both, so a chip in the per-object panel and the chip facing it
// in the grouped panel differ in exactly one byte.
constexpr std::uint32_t kChipRgb[3] = {0x3C78D8, 0xE8B45A, 0x6AA84F};

constexpr int kChipCount = 3;
constexpr int kChipWidth = 96;
constexpr int kChipHeight = 118;

// Chosen so that consecutive chips overlap and the third does NOT reach the
// first: at a stride of 60 against a width of 96 each chip keeps a solo band
// 24 pixels wide with a 36-pixel overlap on either side of it. Two layers of
// overlap and never three, so the per-object panel reads as a staircase with
// one step rather than as a single dark blob.
constexpr int kChipStride = 60;

constexpr int kStageInset = 14;

// A stage of FIXED size, so that the chips inside it are laid out identically
// in every panel however wide the window is.
//
// This is not styling, it is what makes the two comparable panels comparable.
// The panels are grow=1 siblings and the free space is split by exact integer
// division with the remainder handed to the earliest children, so two panels
// can differ by one pixel - and a chip whose definite width is clamped by its
// panel's content box then differs by one pixel too. Comparing the two panels
// pixel for pixel would be comparing two different geometries and calling the
// difference a defect. A fixed stage removes the possibility.
//
// The FADING panel deliberately keeps a stage that fills its panel, because
// nothing is compared against it and it is the one that gives the width
// ladder a layer whose extent changes SIZE rather than only position.
constexpr int kStageWidth = (2 * kStageInset) + (2 * kChipStride) + kChipWidth;
constexpr int kStageHeight = (2 * kStageInset) + kChipHeight;

NodeStyle flat(std::uint32_t argb) {
  NodeStyle style;
  style.fill = Color::from_argb(argb);
  return style;
}

NodeStyle card() {
  NodeStyle style;
  style.fill = Color::from_argb(0xFF1E252F);
  style.border_color = Color::from_argb(0xFF5A6472);
  style.border_width = dg::BorderWidths::all(1.0F);
  return style;
}

// A stage paints nothing of its own, so the only thing its `opacity` can fade
// is the chips. A stage with a visible background would put its own fill into
// the layer and the two panels would stop being comparable.
NodeStyle stage(float opacity) {
  NodeStyle style;
  style.opacity = opacity;
  return style;
}

BoxStyle panel_box() {
  BoxStyle box;
  box.kind = LayoutKind::kAbsolute;
  box.grow = 1;
  box.padding = EdgeInsets::all(10);
  return box;
}

BoxStyle stage_box(bool fixed) {
  BoxStyle box;
  box.kind = LayoutKind::kAbsolute;
  box.left = 0;
  box.top = 0;
  if (fixed) {
    box.width = kStageWidth;
    box.height = kStageHeight;
    return box;
  }
  box.right = 0;
  box.bottom = 0;
  return box;
}

BoxStyle chip_box(int index) {
  BoxStyle box;
  box.width = kChipWidth;
  box.height = kChipHeight;
  box.left = kStageInset + (index * kChipStride);
  box.top = kStageInset;
  return box;
}

struct PanelSpec {
  float stage_opacity = 1.0F;
  std::uint32_t chip_alpha = 0xFF;
  bool fixed_stage = true;
};

struct Panel {
  NodeId stage;
  std::vector<NodeId> chips;
};

// One panel: a card, a stage inside it, and three overlapping chips inside
// that. The stage's opacity and the chips' alpha are the only two knobs, and
// the whole demo is what happens when the same amount of translucency is
// asked for through one rather than the other.
Panel add_panel(dg::LayoutTree& tree, NodeId strip, const PanelSpec& spec) {
  Panel panel;
  const NodeId card_node = tree.add_child(strip, panel_box(), card());
  panel.stage =
      tree.add_child(card_node, stage_box(spec.fixed_stage), stage(spec.stage_opacity));
  for (int index = 0; index < kChipCount; ++index) {
    const std::uint32_t argb =
        (spec.chip_alpha << 24U) | kChipRgb[static_cast<std::size_t>(index)];
    panel.chips.push_back(tree.add_child(panel.stage, chip_box(index), flat(argb)));
  }
  return panel;
}

}  // namespace

Scene build(const dg::TreeSpec& spec) {
  dg::LayoutTree tree{spec};
  Handles handles;

  BoxStyle strip_box;
  strip_box.kind = LayoutKind::kRow;
  strip_box.gap = 18;
  strip_box.padding = EdgeInsets{16, 16, 16, 16};
  strip_box.cross_align = CrossAlign::kStretch;
  const NodeId strip = tree.add_child(dg::LayoutTree::root(), strip_box, flat(0xFF14171C));

  Panel per_object = add_panel(tree, strip, PanelSpec{1.0F, 0x80, true});
  handles.per_object_stage = per_object.stage;
  handles.per_object_chips = std::move(per_object.chips);

  Panel grouped = add_panel(tree, strip, PanelSpec{kHalf, 0xFF, true});
  handles.grouped_stage = grouped.stage;
  handles.grouped_chips = std::move(grouped.chips);

  // The nested panel carries its fade on a stage inside a stage, so the two
  // multiply. Its chips are opaque like the grouped panel's, which is what
  // makes "one fade against two" the only difference between them.
  const NodeId nested_panel = tree.add_child(strip, panel_box(), card());
  handles.nested_outer = tree.add_child(nested_panel, stage_box(true), stage(kHalf));
  handles.nested_inner = tree.add_child(handles.nested_outer, stage_box(true), stage(kHalf));
  for (int index = 0; index < kChipCount; ++index) {
    const std::uint32_t argb = 0xFF000000U | kChipRgb[static_cast<std::size_t>(index)];
    handles.nested_chips.push_back(
        tree.add_child(handles.nested_inner, chip_box(index), flat(argb)));
  }

  Panel fading = add_panel(tree, strip, PanelSpec{1.0F, 0xFF, false});
  handles.fading_stage = fading.stage;
  handles.fading_chips = std::move(fading.chips);

  tree.layout();
  return Scene{std::move(tree), handles};
}

dg::PixelRect overlap_of(const Scene& scene, const std::vector<NodeId>& chips,
                         std::size_t first) {
  return dg::intersect(scene.tree.bounds(chips[first]), scene.tree.bounds(chips[first + 1]));
}

dg::PixelRect solo_band(const Scene& scene, const std::vector<NodeId>& chips,
                        std::size_t index) {
  const dg::PixelRect box = scene.tree.bounds(chips[index]);
  const int left = index == 0 ? box.left() : scene.tree.bounds(chips[index - 1]).right();
  const int right =
      index + 1 == chips.size() ? box.right() : scene.tree.bounds(chips[index + 1]).left();
  return dg::PixelRect::from_edges(left, box.top(), right, box.bottom());
}

void set_opacity(Scene& scene, NodeId node, float opacity) {
  NodeStyle style = scene.tree.render().style(node);
  style.opacity = opacity;
  scene.tree.render().set_style(node, style);
}

std::string describe(const Scene& scene, NodeId id) {
  const Handles& handles = scene.handles;
  const auto named = [id](const std::vector<NodeId>& chips, const char* label) {
    for (std::size_t index = 0; index < chips.size(); ++index) {
      if (chips[index] == id) {
        return std::string{label} + " chip " + std::to_string(index);
      }
    }
    return std::string{};
  };

  for (const auto& [chips, label] : {std::pair{&handles.per_object_chips, "per-object"},
                                     std::pair{&handles.grouped_chips, "grouped"},
                                     std::pair{&handles.nested_chips, "nested"},
                                     std::pair{&handles.fading_chips, "fading"}}) {
    const std::string found = named(*chips, label);
    if (!found.empty()) {
      return found;
    }
  }
  if (id == handles.per_object_stage) {
    return "per-object stage";
  }
  if (id == handles.grouped_stage) {
    return "grouped stage";
  }
  if (id == handles.nested_outer) {
    return "nested outer stage";
  }
  if (id == handles.nested_inner) {
    return "nested inner stage";
  }
  if (id == handles.fading_stage) {
    return "fading stage";
  }
  return "panel or background #" + std::to_string(id.index);
}

}  // namespace opacity_scene
