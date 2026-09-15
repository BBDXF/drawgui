// The scene examples/15_list puts on screen, and the handles a check needs.
//
// ONE VIRTUALIZED LIST: `item_count` (1000 by default - design.md's own
// "1000 项列表" acceptance bar, section 5's P3 row) logical rows behind a
// PERMANENT pool of only `kPoolSize` real nodes - enough to cover the
// viewport plus a couple of spare rows, never one node per logical item.
// `doc/list.md` is the decision record this scene exists to demonstrate.
//
// EVERY RECYCLED FIELD DIFFERS BY DESIGN, so a residue bug (a stale fill,
// label or image surviving a recycle) is VISIBLE rather than invisible:
//   fill    one of 12 fixed colours, `index % 12`
//   text    "item #NNNN", the zero-padded index itself - a stale label
//           shows the WRONG NUMBER, not a plausible-looking one
//   image   `index % 3`: a cyan square, a magenta square, or no source at
//           all (the placeholder colour) - three visually distinct states
//           cycling every three rows, so two adjacent ring-buffer neighbours
//           (which sit `kPoolSize` items apart, not next to each other on
//           screen) are never in the same image state by accident
//
// `style_for()` builds a BRAND NEW NodeStyle from nothing on every call and
// `paint_item()` replaces a node's WHOLE style with it (RenderTree::
// set_style(), not the field-at-a-time set_fill()/set_text()/set_image()) -
// which is what makes residue structurally impossible rather than merely
// remembered-not-to-happen: there is no old field a forgetful call could
// leave behind, because nothing here ever reads the node's PREVIOUS style.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/types.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/font_catalog.h"
#include "drawgui/render/image_catalog.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/widget/widget_set.h"

namespace list_scene {

inline constexpr int kItemCount = 1000;
inline constexpr int kItemHeight = 48;
inline constexpr int kViewportWidth = 340;
inline constexpr int kViewportHeight = 480;
inline constexpr int kFontSize = 15;
inline constexpr int kThumbSize = 28;

// Visible rows plus two spare - the FIXED, PERMANENT pool size. It does not
// grow with `item_count`: 12 nodes cover 1000 items exactly as they cover 10.
inline constexpr int kVisibleRows = (kViewportHeight + kItemHeight - 1) / kItemHeight;
inline constexpr int kPoolSize = kVisibleRows + 2;

inline constexpr dg::Color kImageAColor = dg::Color::rgba(0x27, 0xCE, 0xC2);
inline constexpr dg::Color kImageBColor = dg::Color::rgba(0xE0, 0x3F, 0xA6);
inline constexpr dg::Color kPlaceholderColor = dg::Color::rgba(0x3A, 0x40, 0x4A);

enum class ItemImage : std::uint8_t { kA, kB, kNone };

// Deterministic content for logical index `i` - reproducible from the index
// alone, which is what lets a check recompute the expected answer instead of
// reading it back off the tree it is checking.
[[nodiscard]] dg::Color item_fill(int index);
[[nodiscard]] std::string item_text(int index);
[[nodiscard]] ItemImage item_image(int index);

// The whole visible style for logical index `i`, built from nothing (see
// the file header for why that is what keeps recycling residue-free).
[[nodiscard]] dg::NodeStyle style_for(int index, dg::FontId font, dg::ImageId image_a,
                                      dg::ImageId image_b, bool has_font);

struct Handles {
  dg::NodeId body;
  dg::NodeId list;
};

struct Options {
  dg::TreeSpec spec;
  std::string font_dir = "/usr/share/fonts";
  int item_count = kItemCount;
};

struct Scene {
  dg::LayoutTree tree;
  dg::WidgetSet widgets;
  std::optional<dg::FontCatalog> fonts;
  dg::ImageCatalog images;
  dg::ImageId image_a;
  dg::ImageId image_b;
  dg::FontId font;
  Handles handles;
};

// Builds the virtualized scene: the pool, attached as a kList, synced and
// painted once for the initial (top_index == 0) window.
Scene build(const Options& options);

// The pre-virtualization baseline this slice's own doc measures against:
// `item_count` REAL, permanently-allocated kLeaf nodes stacked in a kColumn
// inside an ordinary kScrollView - exactly examples/10_scrolling's shape,
// generalized to `item_count` items instead of 24, and painted with the
// same style_for() content so the comparison is content-for-content rather
// than "flat rectangles vs decorated rows".
Scene build_baseline(const Options& options);

// Applies `slots` (whatever list_sync()/list_scroll_by() just returned) by
// calling style_for()/RenderTree::set_style() for each - the whole of this
// slice's data-source seam (doc/list.md section 4). A slot that did not
// change is never in `slots`, so it is never repainted.
void refresh(Scene& scene, const std::vector<dg::ListSlot>& slots);

// The logical index currently shown at pool node `node`, read back from
// where list_sync() actually placed it (`local_bounds().y / item_height`) -
// independent of WidgetSet's own bookkeeping, which is what lets a check
// verify the OBSERVABLE effect rather than re-reading the same state
// list_sync() just wrote.
[[nodiscard]] int logical_index_of(const dg::RenderTree& tree, dg::NodeId node);

}  // namespace list_scene
