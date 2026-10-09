#include "drawgui/widget/widget_set.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "drawgui/base/utf8.h"
#include "drawgui/render/grapheme.h"
#include "drawgui/render/paragraph.h"

namespace dg {
namespace {

bool interactive(WidgetKind kind) {
  switch (kind) {
    case WidgetKind::kButton:
    case WidgetKind::kCheckbox:
    // kTextField accepts pointer input directly, unlike kSlider/kScrollView:
    // a click has to resolve TO the field (to focus it and place the
    // cursor), not merely to a plain child of it.
    case WidgetKind::kTextField:
    case WidgetKind::kTextArea:
    // A dropdown's anchor is a plain click target exactly like kButton -
    // opening the popup IS the activation.
    case WidgetKind::kDropdown:
    // A scrollbar's track and thumb own their hover/press like a button, so
    // dg::Interaction's press capture is what a thumb drag runs on.
    case WidgetKind::kScrollbar:
      return true;
    case WidgetKind::kPanel:
    case WidgetKind::kLabel:
    case WidgetKind::kScrollView:
    case WidgetKind::kSlider:
    case WidgetKind::kList:
      break;
  }
  return false;
}

// Builds a throwaway, single-line, unwrapped Paragraph over `text` purely
// for pixel measurement - the shaping-aware replacement for 4-9's
// measure_ascii_width()/ascii_offset_at_x() (7-2b's own
// cross-reference). The width is fixed at a value no realistic TextField
// content will ever reach, so the paragraph never wraps - a TextField stays
// single-line by construction, matching design.md's MVP-8 naming (4-9
// section 1.1), the scroll/ellipsis projection this file already builds
// being what handles overflow instead of word-wrap.
//
// nullopt only for the one input Paragraph::build() itself declines (empty
// text, invalid font, non-positive size) - the identical early-return shape
// measure_ascii_width() already had for the same inputs.
std::optional<Paragraph> build_edit_paragraph(const FontCatalog& fonts, FontId font, float size,
                                              const std::string& text) {
  if (text.empty() || size <= 0.0F || !fonts.holds(font)) {
    return std::nullopt;
  }
  static constexpr float kUnboundedWidth = 1.0e7F;
  TextStyle style;
  style.text = text;
  style.font = font;
  style.size = size;
  style.color = Color::from_argb(0xFFFFFFFFU);  // measurement-only; never painted
  // TextStyle::align defaults to kCenter, which would center this
  // measurement paragraph inside kUnboundedWidth and make every caret_x()
  // result meaningless (found by running this exact code: a first draft
  // without this line produced a caret_x() around -5,000,000). kLeft
  // anchors it at the paragraph's own x == 0, matching every pixel formula
  // in this file that assumes an unscrolled origin.
  style.align = TextAlign::kLeft;
  Expected<Paragraph, FontError> built = Paragraph::build(fonts, style, kUnboundedWidth);
  if (!built.has_value()) {
    return std::nullopt;
  }
  return std::move(built).value();
}

// The grapheme boundary in `boundaries` whose MIDPOINT to its neighbour
// `local_x` is on the near side of - the direct cluster-index analogue of
// 4-9's ascii_offset_at_x() per-byte midpoint loop, now walking grapheme
// boundaries instead of raw byte indices so a click inside a ZWJ/skin-tone/
// flag sequence can only resolve to its START or its END, never a byte in
// the middle of it.
int grapheme_offset_at_x(const Paragraph& para, const std::vector<int>& boundaries,
                         float local_x) {
  if (boundaries.size() < 2 || local_x <= 0.0F) {
    return boundaries.empty() ? 0 : boundaries.front();
  }
  float previous_x = para.caret_x(boundaries.front());
  for (std::size_t i = 1; i < boundaries.size(); ++i) {
    const float x = para.caret_x(boundaries[i]);
    if (local_x < (previous_x + x) * 0.5F) {
      return boundaries[i - 1];
    }
    previous_x = x;
  }
  return boundaries.back();
}

// Truncates `text` to the longest GRAPHEME-CLUSTER prefix such that PREFIX +
// "..." still fits `visible_width` device pixels, appending the ellipsis -
// the unfocused overflow treatment chosen over
// scrolling an unfocused field with no visible caret to justify it. Cuts at
// a cluster boundary rather than a byte offset (7-2b): a byte-oriented cut
// could bisect a multi-byte character or a ZWJ/skin-tone/flag sequence,
// corrupting the displayed string - the exact failure design.md's grapheme-
// cluster mandate exists to rule out. Returns `text` unchanged (no
// ellipsis) when it already fits.
std::string ellipsize(const FontCatalog& fonts, FontId font, float size,
                      const std::string& text, int visible_width) {
  if (text.empty()) {
    return text;
  }
  const std::optional<Paragraph> full = build_edit_paragraph(fonts, font, size, text);
  if (!full) {
    return text;
  }
  const float full_width = full->caret_x(static_cast<int>(text.size()));
  if (visible_width <= 0 || full_width <= static_cast<float>(visible_width)) {
    return text;
  }

  static constexpr std::string_view kEllipsis = "...";
  const std::optional<Paragraph> ellipsis_para =
      build_edit_paragraph(fonts, font, size, std::string(kEllipsis));
  const float ellipsis_width =
      ellipsis_para ? ellipsis_para->caret_x(static_cast<int>(kEllipsis.size())) : 0.0F;

  // Longest cluster prefix whose width plus the ellipsis's own still fits,
  // walking from the shortest prefix (boundary 0) upward and keeping the
  // last one that fit - the direct grapheme-cluster analogue of 4-9's
  // byte-index loop.
  int kept = 0;
  for (const int boundary : grapheme_boundaries(text)) {
    const float prefix_width = full->caret_x(boundary);
    if (prefix_width + ellipsis_width > static_cast<float>(visible_width)) {
      break;
    }
    kept = boundary;
  }
  std::string result = text.substr(0, static_cast<std::size_t>(kept));
  result += kEllipsis;
  return result;
}

// [min_value, max_value], then snapped to the nearest `step` above
// min_value when it is positive, then clamped again - snapping can push a
// value that was already at a bound slightly past it by rounding.
float clamp_slider_value(const Widget& widget, float value) {
  float clamped = std::clamp(value, widget.min_value, widget.max_value);
  if (widget.step > 0.0F) {
    const float steps = std::round((clamped - widget.min_value) / widget.step);
    clamped = std::clamp(widget.min_value + (steps * widget.step), widget.min_value,
                         widget.max_value);
  }
  return clamped;
}

}  // namespace

void WidgetSet::attach(NodeId id, const Widget& widget) {
  if (widgets_.size() <= id.index) {
    widgets_.resize(static_cast<std::size_t>(id.index) + 1);
  }
  widgets_[id.index] = widget;
}

void WidgetSet::forget(NodeId id) {
  if (id.index >= widgets_.size()) {
    return;
  }
  widgets_[id.index].reset();
  if (scrollbar_drag_.has_value() && scrollbar_drag_->thumb == id) {
    scrollbar_drag_.reset();
  }
  if (id.index < dimmed_.size()) {
    dimmed_[id.index].reset();
  }
}

void WidgetSet::set_disabled_palette(const DisabledPalette& palette) {
  disabled_palette_ = palette;
}

const DisabledPalette& WidgetSet::disabled_palette() const {
  return disabled_palette_;
}

const Widget* WidgetSet::find(NodeId id) const {
  if (id.index >= widgets_.size()) {
    return nullptr;
  }
  const std::optional<Widget>& slot = widgets_[id.index];
  return slot.has_value() ? &slot.value() : nullptr;
}

Widget* WidgetSet::find(NodeId id) {
  if (id.index >= widgets_.size()) {
    return nullptr;
  }
  std::optional<Widget>& slot = widgets_[id.index];
  return slot.has_value() ? &slot.value() : nullptr;
}

bool WidgetSet::has(NodeId id) const {
  return find(id) != nullptr;
}

std::size_t WidgetSet::count() const {
  std::size_t total = 0;
  for (const std::optional<Widget>& widget : widgets_) {
    total += widget.has_value() ? std::size_t{1} : std::size_t{0};
  }
  return total;
}

const Widget& WidgetSet::at(NodeId id) const {
  return *find(id);
}

bool WidgetSet::accepts_pointer(NodeId id) const {
  const Widget* widget = find(id);
  return widget != nullptr && interactive(widget->kind);
}

bool WidgetSet::is_checked(NodeId id) const {
  const Widget* widget = find(id);
  return widget != nullptr && widget->checked;
}

bool WidgetSet::toggle(NodeId id) {
  Widget* widget = find(id);
  if (widget == nullptr || widget->kind != WidgetKind::kCheckbox) {
    return false;
  }
  if (!widget->group.has_value()) {
    widget->checked = !widget->checked;
    return widget->checked;
  }

  // Radio behaviour: re-selecting the already-checked option in a group is
  // a no-op, matching every desktop toolkit, then every OTHER checkbox in
  // the same group is cleared.
  if (widget->checked) {
    return true;
  }
  widget->checked = true;
  const int group = *widget->group;
  for (std::optional<Widget>& slot : widgets_) {
    if (!slot.has_value() || &(*slot) == widget) {
      continue;
    }
    if (slot->kind == WidgetKind::kCheckbox && slot->group == group) {
      slot->checked = false;
    }
  }
  return true;
}

std::vector<NodeId> WidgetSet::group_members(NodeId id) const {
  std::vector<NodeId> members;
  const Widget* widget = find(id);
  if (widget == nullptr || widget->kind != WidgetKind::kCheckbox ||
      !widget->group.has_value()) {
    return members;
  }
  for (std::size_t i = 0; i < widgets_.size(); ++i) {
    const std::optional<Widget>& slot = widgets_[i];
    if (!slot.has_value() || i == id.index) {
      continue;
    }
    if (slot->kind == WidgetKind::kCheckbox && slot->group == widget->group) {
      members.push_back(NodeId{static_cast<std::uint32_t>(i)});
    }
  }
  return members;
}

std::optional<NodeId> WidgetSet::owner_of(const RenderTree& tree, NodeId id) const {
  NodeId current = id;
  while (true) {
    if (accepts_pointer(current)) {
      // A disabled owner SWALLOWS the pointer rather than passing it on to an
      // enabled ancestor: a click on a disabled button inside a clickable
      // card must not click the card.
      if (is_disabled(tree, current)) {
        return std::nullopt;
      }
      return current;
    }
    const NodeId parent = tree.parent(current);

    // The root is its own parent, so this is the top of the climb. Written as
    // equality rather than as `current.value == 0` so that it keeps working if
    // the root ever stops being index zero.
    if (parent == current) {
      return std::nullopt;
    }
    current = parent;
  }
}

std::optional<NodeId> WidgetSet::widget_at(const RenderTree& tree, PixelPoint point) const {
  const std::optional<NodeId> node = tree.hit_test(point);
  if (!node.has_value()) {
    return std::nullopt;
  }
  return owner_of(tree, *node);
}

std::optional<NodeId> WidgetSet::scrollable_owner_of(const RenderTree& tree, NodeId id) const {
  NodeId current = id;
  while (true) {
    const Widget* widget = find(current);
    if (widget != nullptr &&
        (widget->kind == WidgetKind::kScrollView || widget->kind == WidgetKind::kTextArea)) {
      return current;
    }
    const NodeId parent = tree.parent(current);
    if (parent == current) {
      return std::nullopt;
    }
    current = parent;
  }
}

bool WidgetSet::scroll_by(RenderTree& tree, NodeId id, const PixelRect& viewport_content,
                          int dx, int dy) const {
  const Widget* widget = find(id);
  if (widget == nullptr) {
    return false;
  }
  if (widget->kind == WidgetKind::kTextArea) {
    // A text area scrolls vertically only, over its wrapped text's height
    // (Widget::content_height, kept current by every display refresh).
    const int max_y = std::max(0, widget->content_height - viewport_content.height);
    PixelPoint offset = tree.scroll_offset(id);
    offset.y = std::clamp(offset.y + dy, 0, max_y);
    if (offset == tree.scroll_offset(id)) {
      return false;
    }
    tree.set_scroll_offset(id, offset);
    sync_scrollbar(tree, id, viewport_content.height);
    return true;
  }
  if (widget->kind != WidgetKind::kScrollView) {
    return false;
  }

  // The clamp: the content child's full natural size, less the room the
  // viewport itself offers, floored at zero so a child SMALLER than its
  // viewport (nothing to scroll) clamps to exactly one position rather than a
  // negative range.
  const PixelRect content = tree.local_bounds(widget->scroll_content);
  const int max_x = std::max(0, content.width - viewport_content.width);
  const int max_y = std::max(0, content.height - viewport_content.height);

  PixelPoint offset = tree.scroll_offset(id);
  switch (widget->scroll_axis) {
    case ScrollAxis::kNone:
      return false;
    case ScrollAxis::kVertical:
      offset.y = std::clamp(offset.y + dy, 0, max_y);
      break;
    case ScrollAxis::kHorizontal:
      offset.x = std::clamp(offset.x + dx, 0, max_x);
      break;
  }
  if (offset == tree.scroll_offset(id)) {
    return false;
  }
  tree.set_scroll_offset(id, offset);
  sync_scrollbar(tree, id,
                 widget->scroll_axis == ScrollAxis::kVertical ? viewport_content.height
                                                              : viewport_content.width);
  return true;
}

std::optional<NodeId> WidgetSet::list_owner_of(const RenderTree& tree, NodeId id) const {
  NodeId current = id;
  while (true) {
    const Widget* widget = find(current);
    if (widget != nullptr && widget->kind == WidgetKind::kList) {
      return current;
    }
    const NodeId parent = tree.parent(current);
    if (parent == current) {
      return std::nullopt;
    }
    current = parent;
  }
}

std::vector<ListSlot> WidgetSet::list_sync(RenderTree& tree, NodeId id, int top_index) {
  Widget* widget = find(id);
  if (widget == nullptr || widget->kind != WidgetKind::kList) {
    return {};
  }
  const auto pool_size = static_cast<int>(widget->list_pool.size());
  if (pool_size == 0) {
    return {};
  }
  // Defensive rather than assumed: a caller that forgot to size
  // `list_assigned` to match `list_pool` would otherwise read/write past the
  // end below. Idempotent once the sizes already agree.
  if (widget->list_assigned.size() != widget->list_pool.size()) {
    widget->list_assigned.assign(widget->list_pool.size(), -1);
  }

  std::vector<ListSlot> changed;
  for (int row = 0; row < pool_size; ++row) {
    const int logical = top_index + row;
    if (logical < 0 || logical >= widget->list_item_count) {
      continue;
    }
    const int slot = logical % pool_size;
    if (widget->list_assigned[static_cast<std::size_t>(slot)] == logical) {
      continue;
    }
    widget->list_assigned[static_cast<std::size_t>(slot)] = logical;

    const NodeId node = widget->list_pool[static_cast<std::size_t>(slot)];
    const PixelRect current = tree.local_bounds(node);
    const int main = logical * widget->list_item_extent;
    const PixelRect placed = widget->list_axis == ScrollAxis::kHorizontal
                                 ? PixelRect{main, 0, current.width, current.height}
                                 : PixelRect{0, main, current.width, current.height};
    tree.set_local_bounds(node, placed);
    changed.push_back(ListSlot{node, logical});
  }
  return changed;
}

std::vector<ListSlot> WidgetSet::list_scroll_by(RenderTree& tree, NodeId id,
                                                int viewport_extent, int dx, int dy) {
  Widget* widget = find(id);
  if (widget == nullptr || widget->kind != WidgetKind::kList ||
      widget->list_axis == ScrollAxis::kNone) {
    return {};
  }

  const int content_extent = widget->list_item_count * widget->list_item_extent;
  const int max_offset = std::max(0, content_extent - viewport_extent);

  PixelPoint offset = tree.scroll_offset(id);
  int& moved = widget->list_axis == ScrollAxis::kVertical ? offset.y : offset.x;
  const int delta = widget->list_axis == ScrollAxis::kVertical ? dy : dx;
  const int next = std::clamp(moved + delta, 0, max_offset);
  if (next == moved) {
    return {};
  }
  moved = next;
  tree.set_scroll_offset(id, offset);
  sync_scrollbar(tree, id, viewport_extent);

  const int extent = widget->list_item_extent;
  const int top_index = extent > 0 ? next / extent : 0;
  return list_sync(tree, id, top_index);
}

std::optional<NodeId> WidgetSet::slidable_owner_of(const RenderTree& tree, NodeId id) const {
  NodeId current = id;
  while (true) {
    const Widget* widget = find(current);
    if (widget != nullptr && widget->kind == WidgetKind::kSlider) {
      if (is_disabled(tree, current)) {
        return std::nullopt;
      }
      return current;
    }
    const NodeId parent = tree.parent(current);
    if (parent == current) {
      return std::nullopt;
    }
    current = parent;
  }
}

void WidgetSet::reposition_slider(RenderTree& tree, NodeId id, const Widget& widget) {
  const PixelRect track = tree.local_bounds(id);
  const PixelRect thumb = tree.local_bounds(widget.thumb);
  const int travel = std::max(0, track.width - thumb.width);
  const float span = widget.max_value - widget.min_value;
  const float fraction = span > 0.0F ? (widget.value - widget.min_value) / span : 0.0F;
  const auto thumb_x = static_cast<int>(
      std::lround(static_cast<double>(fraction) * static_cast<double>(travel)));
  const int thumb_y = (track.height - thumb.height) / 2;
  tree.set_local_origin(widget.thumb, thumb_x, thumb_y);
}

bool WidgetSet::set_slider_value(RenderTree& tree, NodeId id, float value) {
  Widget* widget = find(id);
  if (widget == nullptr || widget->kind != WidgetKind::kSlider) {
    return false;
  }
  const float clamped = clamp_slider_value(*widget, value);
  if (clamped == widget->value) {
    return false;
  }
  widget->value = clamped;
  reposition_slider(tree, id, *widget);
  return true;
}

float WidgetSet::slider_value(NodeId id) const {
  const Widget* widget = find(id);
  return widget != nullptr && widget->kind == WidgetKind::kSlider ? widget->value : 0.0F;
}

float WidgetSet::slider_value_at(const RenderTree& tree, NodeId id, int pointer_x) const {
  const Widget* widget = find(id);
  if (widget == nullptr || widget->kind != WidgetKind::kSlider) {
    return 0.0F;
  }
  const PixelRect track = tree.absolute_bounds(id);
  const PixelRect thumb = tree.local_bounds(widget->thumb);
  const int travel = std::max(0, track.width - thumb.width);
  const float half_thumb = static_cast<float>(thumb.width) / 2.0F;
  const float t = travel > 0
                      ? std::clamp((static_cast<float>(pointer_x - track.x) - half_thumb) /
                                       static_cast<float>(travel),
                                   0.0F, 1.0F)
                      : 0.0F;
  return widget->min_value + (t * (widget->max_value - widget->min_value));
}

void WidgetSet::attach_scrollbar(RenderTree& tree, NodeId id, NodeId track, NodeId thumb) {
  Widget* widget = find(id);
  if (widget == nullptr ||
      (widget->kind != WidgetKind::kScrollView && widget->kind != WidgetKind::kList &&
       widget->kind != WidgetKind::kTextArea)) {
    return;
  }
  widget->scrollbar_track = track;
  widget->scrollbar_thumb = thumb;

  Widget part;
  part.kind = WidgetKind::kScrollbar;
  part.scrollbar_of = id;
  attach(thumb, part);
  part.scrollbar_thumb = thumb;
  attach(track, part);
  refresh(tree, track, PointerState{});
  refresh(tree, thumb, PointerState{});
}

void WidgetSet::set_scrollbar_style(const ScrollbarStyle& style) {
  scrollbar_style_ = style;
}

const ScrollbarStyle& WidgetSet::scrollbar_style() const {
  return scrollbar_style_;
}

ScrollbarGeometry WidgetSet::scrollbar_geometry(const RenderTree& tree, NodeId id,
                                                int viewport_extent) const {
  ScrollbarGeometry geometry;
  const Widget* widget = find(id);
  if (widget == nullptr || widget->scrollbar_track == RenderTree::root()) {
    return geometry;
  }
  int content = 0;
  if (widget->kind == WidgetKind::kScrollView) {
    geometry.axis = widget->scroll_axis;
    const PixelRect bounds = tree.local_bounds(widget->scroll_content);
    content = geometry.axis == ScrollAxis::kHorizontal ? bounds.width : bounds.height;
  } else if (widget->kind == WidgetKind::kList) {
    geometry.axis = widget->list_axis;
    content = widget->list_item_count * widget->list_item_extent;
  } else if (widget->kind == WidgetKind::kTextArea) {
    geometry.axis = ScrollAxis::kVertical;
    content = widget->content_height;
  }
  if (geometry.axis == ScrollAxis::kNone) {
    return geometry;
  }
  const bool vertical = geometry.axis == ScrollAxis::kVertical;
  const PixelPoint offset = tree.scroll_offset(id);
  geometry.offset = vertical ? offset.y : offset.x;
  geometry.max_offset = std::max(0, content - viewport_extent);

  const ScrollbarMode mode = tree.style(id).scrollbar;
  if (mode == ScrollbarMode::kNever || viewport_extent <= 0 ||
      (mode == ScrollbarMode::kAuto && geometry.max_offset == 0)) {
    return geometry;
  }

  const PixelRect view = tree.local_bounds(id);
  const int length = vertical ? view.height : view.width;
  const int size =
      std::clamp(scrollbar_style_.size, 1, std::max(1, vertical ? view.width : view.height));
  if (length <= 0) {
    return geometry;
  }
  int thumb = length;
  if (geometry.max_offset > 0 && content > 0) {
    thumb = static_cast<int>(std::lround(static_cast<double>(length) * viewport_extent /
                                         static_cast<double>(content)));
    thumb = std::clamp(thumb, std::min(scrollbar_style_.min_thumb_length, length), length);
  }
  const int travel = length - thumb;
  const int position =
      geometry.max_offset > 0
          ? static_cast<int>(std::lround(static_cast<double>(travel) *
                                         std::clamp(geometry.offset, 0, geometry.max_offset) /
                                         static_cast<double>(geometry.max_offset)))
          : 0;

  geometry.visible = true;
  geometry.track = vertical ? PixelRect{view.width - size, 0, size, length}
                            : PixelRect{0, view.height - size, length, size};
  geometry.thumb =
      vertical ? PixelRect{0, position, size, thumb} : PixelRect{position, 0, thumb, size};
  return geometry;
}

void WidgetSet::sync_scrollbar(RenderTree& tree, NodeId id, int viewport_extent) const {
  const Widget* widget = find(id);
  if (widget == nullptr || widget->scrollbar_track == RenderTree::root()) {
    return;
  }
  const ScrollbarGeometry geometry = scrollbar_geometry(tree, id, viewport_extent);
  // The track is a child of the scrolled node, so RenderTree subtracts the
  // scroll offset from it like from the content; adding it back here is
  // what pins the bar to the viewport.
  const PixelPoint offset = tree.scroll_offset(id);
  const PixelRect track = geometry.visible ? geometry.track : PixelRect{};
  const PixelRect placed{track.x + offset.x, track.y + offset.y, track.width, track.height};
  if (tree.local_bounds(widget->scrollbar_track) != placed) {
    tree.set_local_bounds(widget->scrollbar_track, placed);
  }
  const PixelRect thumb = geometry.visible ? geometry.thumb : PixelRect{};
  if (tree.local_bounds(widget->scrollbar_thumb) != thumb) {
    tree.set_local_bounds(widget->scrollbar_thumb, thumb);
  }
}

std::optional<NodeId> WidgetSet::scrollbar_owner_of(NodeId id) const {
  const Widget* widget = find(id);
  if (widget == nullptr || widget->kind != WidgetKind::kScrollbar) {
    return std::nullopt;
  }
  return widget->scrollbar_of;
}

bool WidgetSet::is_scrollbar_thumb(NodeId id) const {
  const Widget* widget = find(id);
  return widget != nullptr && widget->kind == WidgetKind::kScrollbar &&
         widget->scrollbar_thumb == RenderTree::root();
}

ScrollResult WidgetSet::scroll_to(RenderTree& tree, NodeId id, int viewport_extent,
                                  int offset) {
  const Widget* widget = find(id);
  if (widget == nullptr) {
    return {};
  }
  const PixelPoint current = tree.scroll_offset(id);
  ScrollResult result;
  if (widget->kind == WidgetKind::kScrollView || widget->kind == WidgetKind::kTextArea) {
    const bool vertical =
        widget->kind == WidgetKind::kTextArea || widget->scroll_axis == ScrollAxis::kVertical;
    const int delta = offset - (vertical ? current.y : current.x);
    // scroll_by() reads only the scrolled axis' extent of this rectangle.
    const PixelRect viewport{0, 0, viewport_extent, viewport_extent};
    result.moved = scroll_by(tree, id, viewport, vertical ? 0 : delta, vertical ? delta : 0);
  } else if (widget->kind == WidgetKind::kList) {
    const bool vertical = widget->list_axis == ScrollAxis::kVertical;
    const int delta = offset - (vertical ? current.y : current.x);
    result.slots =
        list_scroll_by(tree, id, viewport_extent, vertical ? 0 : delta, vertical ? delta : 0);
    result.moved = tree.scroll_offset(id) != current;
  }
  return result;
}

ScrollResult WidgetSet::scrollbar_press(RenderTree& tree, NodeId part, int viewport_extent,
                                        PixelPoint pointer) {
  scrollbar_drag_.reset();
  const std::optional<NodeId> owner = scrollbar_owner_of(part);
  if (!owner.has_value() || is_disabled(tree, part)) {
    return {};
  }
  const ScrollbarGeometry geometry = scrollbar_geometry(tree, *owner, viewport_extent);
  if (!geometry.visible) {
    return {};
  }
  const bool vertical = geometry.axis == ScrollAxis::kVertical;
  const NodeId thumb_node = find(*owner)->scrollbar_thumb;
  const PixelRect thumb = tree.absolute_bounds(thumb_node);
  const int along = vertical ? pointer.y : pointer.x;
  const int thumb_start = vertical ? thumb.y : thumb.x;
  const int thumb_length = vertical ? thumb.height : thumb.width;

  if (is_scrollbar_thumb(part)) {
    scrollbar_drag_ = ScrollbarDrag{part, along - thumb_start};
    return {};
  }
  if (along >= thumb_start && along < thumb_start + thumb_length) {
    return {};
  }
  const int page = along < thumb_start ? -viewport_extent : viewport_extent;
  return scroll_to(tree, *owner, viewport_extent,
                   std::clamp(geometry.offset + page, 0, geometry.max_offset));
}

ScrollResult WidgetSet::scrollbar_drag(RenderTree& tree, NodeId part, int viewport_extent,
                                       PixelPoint pointer) {
  if (!scrollbar_drag_.has_value() || scrollbar_drag_->thumb != part) {
    return {};
  }
  const int grab = scrollbar_drag_->grab;
  const std::optional<NodeId> owner = scrollbar_owner_of(part);
  if (!owner.has_value() || is_disabled(tree, part)) {
    return {};
  }
  const ScrollbarGeometry geometry = scrollbar_geometry(tree, *owner, viewport_extent);
  if (!geometry.visible || geometry.max_offset == 0) {
    return {};
  }
  const bool vertical = geometry.axis == ScrollAxis::kVertical;
  const PixelRect track = tree.absolute_bounds(find(*owner)->scrollbar_track);
  const int travel = vertical ? geometry.track.height - geometry.thumb.height
                              : geometry.track.width - geometry.thumb.width;
  if (travel <= 0) {
    return {};
  }
  const int along = vertical ? pointer.y - track.y : pointer.x - track.x;
  const int position = std::clamp(along - grab, 0, travel);
  const auto offset = static_cast<int>(std::lround(
      static_cast<double>(position) * geometry.max_offset / static_cast<double>(travel)));
  return scroll_to(tree, *owner, viewport_extent, offset);
}

void WidgetSet::scrollbar_release() {
  scrollbar_drag_.reset();
}

void WidgetSet::resync_sliders(RenderTree& tree) const {
  for (std::size_t i = 0; i < widgets_.size(); ++i) {
    const std::optional<Widget>& slot = widgets_[i];
    if (!slot.has_value() || slot->kind != WidgetKind::kSlider) {
      continue;
    }
    reposition_slider(tree, NodeId{static_cast<std::uint32_t>(i)}, *slot);
  }
}

namespace {
// Re-derives a kDropdown's own label child from whatever `selected_index`
// currently is - a no-op while unset, which is what leaves the caller's own
// placeholder text (set once on the label node at construction, the same
// way an ordinary label starts with whatever text a scene author gave it)
// on screen until a real selection exists. Ellipsized to the label's own
// current width via the same grapheme-boundary-safe ellipsize() a
// kTextField's unfocused display already uses - a long option string is not
// this widget's problem to solve twice.
void dropdown_refresh_label(RenderTree& tree, const FontCatalog& fonts, const Widget& widget) {
  if (widget.label == RenderTree::root() || !widget.selected_index.has_value()) {
    return;
  }
  const int index = *widget.selected_index;
  if (index < 0 || static_cast<std::size_t>(index) >= widget.options.size()) {
    return;
  }
  TextStyle style = tree.style(widget.label).text;
  const int width = tree.local_bounds(widget.label).width;
  style.text = ellipsize(fonts, style.font, style.size,
                         widget.options[static_cast<std::size_t>(index)], width);
  tree.set_text(widget.label, style);
}
}  // namespace

const std::vector<std::string>& WidgetSet::dropdown_options(NodeId id) const {
  static const std::vector<std::string> kEmpty;
  const Widget* widget = find(id);
  return (widget != nullptr && widget->kind == WidgetKind::kDropdown) ? widget->options
                                                                      : kEmpty;
}

std::optional<int> WidgetSet::dropdown_selected_index(NodeId id) const {
  const Widget* widget = find(id);
  return (widget != nullptr && widget->kind == WidgetKind::kDropdown) ? widget->selected_index
                                                                      : std::nullopt;
}

void WidgetSet::dropdown_set_options(RenderTree& tree, const FontCatalog& fonts, NodeId id,
                                     std::vector<std::string> options) {
  Widget* widget = find(id);
  if (widget == nullptr || widget->kind != WidgetKind::kDropdown) {
    return;
  }
  widget->options = std::move(options);
  if (widget->selected_index.has_value() &&
      (*widget->selected_index < 0 ||
       static_cast<std::size_t>(*widget->selected_index) >= widget->options.size())) {
    widget->selected_index.reset();
  }
  dropdown_refresh_label(tree, fonts, *widget);
}

bool WidgetSet::dropdown_select(RenderTree& tree, const FontCatalog& fonts, NodeId id,
                                int index) {
  Widget* widget = find(id);
  if (widget == nullptr || widget->kind != WidgetKind::kDropdown) {
    return false;
  }
  if (index < 0 || static_cast<std::size_t>(index) >= widget->options.size()) {
    return false;
  }
  if (widget->selected_index.has_value() && *widget->selected_index == index) {
    return false;
  }
  widget->selected_index = index;
  dropdown_refresh_label(tree, fonts, *widget);
  return true;
}

void WidgetSet::refresh(RenderTree& tree, NodeId id, PointerState state) const {
  const Widget* found = find(id);
  if (found == nullptr) {
    return;
  }
  const Widget& widget = *found;

  if (widget.kind == WidgetKind::kScrollbar) {
    const bool is_thumb = widget.scrollbar_thumb == RenderTree::root();
    Color track_fill = scrollbar_style_.track.value_or(Color{});
    Color fill = is_thumb ? scrollbar_style_.thumb.value_or(Color{}) : track_fill;
    const bool disabled = is_disabled(tree, id);
    const bool active = is_thumb && !disabled && (state.pressed || state.hovered);
    if (disabled) {
      // The slider's disabled split, reused: the strip recedes into the
      // disabled surface, the thumb stays readable on it.
      track_fill = disabled_palette_.fill.value_or(track_fill);
      fill = is_thumb ? disabled_palette_.on.value_or(fill) : track_fill;
    } else if (is_thumb && state.pressed) {
      fill = scrollbar_style_.thumb_pressed.value_or(fill);
    } else if (is_thumb && state.hovered) {
      fill = scrollbar_style_.thumb_hover.value_or(fill);
    }
    // Pill look: both parts are fully rounded. The thumb's node keeps the
    // whole bar width (so the grab area is not reduced); a border painted in
    // the track colour makes the visible pill sit inset inside it. Hover
    // and press thin that border so the pill widens under the pointer.
    NodeStyle style = tree.style(id);
    const float size = static_cast<float>(std::max(1, scrollbar_style_.size));
    const float inset = !is_thumb ? 0.0F
                        : active  ? std::max(1.0F, size / 10.0F)
                                  : std::max(1.0F, size / 5.0F);
    style.fill = fill;
    style.radii = Radii::all(size / 2.0F);
    style.border_width = BorderWidths::all(inset);
    style.border_color = is_thumb ? track_fill : Color{};
    const NodeStyle& current = tree.style(id);
    if (current.fill != style.fill || !(current.radii == style.radii) ||
        !(current.border_width == style.border_width) ||
        current.border_color != style.border_color) {
      tree.set_style(id, style);
    }
    return;
  }

  // Pressed beats hovered: a widget the pointer is held down on is always
  // hovered too, and reporting it as merely hovered would drop the press
  // feedback exactly while the user is asking for it.
  Color fill = widget.fill_normal;
  if (is_disabled(tree, id)) {
    fill = widget.fill_disabled.value_or(disabled_palette_.fill.value_or(widget.fill_normal));
  } else if (state.pressed) {
    fill = widget.fill_pressed;
  } else if (state.hovered) {
    fill = widget.fill_hover;
  }
  if (tree.style(id).fill != fill) {
    tree.set_fill(id, fill);
  }

  if (widget.kind != WidgetKind::kCheckbox || widget.indicator == RenderTree::root()) {
    return;
  }
  Color mark = widget.checked ? widget.indicator_on : widget.indicator_off;
  if (widget.checked && is_disabled(tree, id)) {
    mark = widget.indicator_disabled.value_or(disabled_palette_.on.value_or(mark));
  }
  if (tree.style(widget.indicator).fill != mark) {
    tree.set_fill(widget.indicator, mark);
  }
}

void WidgetSet::refresh_subtree(RenderTree& tree, NodeId root,
                                const Interaction& interaction) const {
  if (accepts_pointer(root)) {
    refresh(tree, root, interaction.state_of(root));
  }

  // The fill refresh() does not own: a slider's track and thumb.
  std::optional<Color> fill_target;
  const Widget* widget = find(root);
  if (widget != nullptr && widget->kind == WidgetKind::kSlider) {
    fill_target = disabled_palette_.fill;
  } else if (root != RenderTree::root()) {
    const Widget* parent = find(tree.parent(root));
    if (parent != nullptr && parent->kind == WidgetKind::kSlider && parent->thumb == root) {
      fill_target = disabled_palette_.on;
    }
  }
  apply_disabled_look(tree, root, is_disabled(tree, root), fill_target);

  for (const NodeId child : tree.children(root)) {
    refresh_subtree(tree, child, interaction);
  }
}

std::optional<Color> WidgetSet::step_dimmed(std::optional<DimmedColor>& slot, Color current,
                                            std::optional<Color> target, bool disabled) {
  // Disabled: remember the enabled colour - re-captured when someone else
  // overwrote what was applied (a theme binding re-applied) - and paint
  // `target`. Enabled: put the remembered colour back unless someone else
  // has written the property since. Returns what to write, if anything.
  if (disabled && target.has_value()) {
    const Color applied = *target;
    if (!slot.has_value() || current != slot->applied) {
      slot = DimmedColor{current, applied};
    } else {
      slot->applied = applied;
    }
    return applied;
  }
  std::optional<Color> restore;
  if (slot.has_value() && current == slot->applied) {
    restore = slot->original;
  }
  slot.reset();
  return restore;
}

void WidgetSet::apply_disabled_look(RenderTree& tree, NodeId id, bool disabled,
                                    std::optional<Color> fill_target) const {
  const std::optional<DimmedNode>* found =
      id.index < dimmed_.size() ? &dimmed_[id.index] : nullptr;
  const bool remembered = found != nullptr && found->has_value() && (*found)->id == id;
  if (!disabled && !remembered) {
    return;  // the common case: an enabled node never dimmed
  }
  if (dimmed_.size() <= id.index) {
    dimmed_.resize(static_cast<std::size_t>(id.index) + 1);
  }
  std::optional<DimmedNode>& slot = dimmed_[id.index];
  DimmedNode& entry =
      (slot.has_value() && slot->id == id)
          ? *slot
          : slot.emplace(DimmedNode{id, std::nullopt, std::nullopt, std::nullopt});

  const NodeStyle& style = tree.style(id);
  const bool has_border = !style.border_width.is_zero() && style.border_color.alpha() != 0;
  const bool has_text = style.text.color.alpha() != 0;
  const std::optional<Color> fill = step_dimmed(entry.fill, style.fill, fill_target, disabled);
  const std::optional<Color> border =
      step_dimmed(entry.border, style.border_color,
                  has_border ? disabled_palette_.fill : std::nullopt, disabled);
  const std::optional<Color> text = step_dimmed(
      entry.text, style.text.color, has_text ? disabled_palette_.on : std::nullopt, disabled);

  if (fill.has_value() && fill != style.fill) {
    tree.set_fill(id, fill.value());
  }
  if (border.has_value() && border != tree.style(id).border_color) {
    NodeStyle next = tree.style(id);
    next.border_color = border.value();
    tree.set_style(id, next);
  }
  if (text.has_value() && text != tree.style(id).text.color) {
    TextStyle next = tree.style(id).text;
    next.color = text.value();
    tree.set_text(id, next);
  }
  if (!disabled) {
    slot.reset();
  }
}

namespace {
constexpr int kCaretWidthPx = 2;

// A thin bar under the whole composing span (7-3) - the
// conventional underline treatment, sized independently of the caret's own
// width constant above because the two are visually distinct shapes (a
// vertical bar vs. a horizontal one).
constexpr int kCompositionUnderlineHeightPx = 2;

}  // namespace

const std::string& WidgetSet::text_field_text(NodeId id) const {
  static const std::string kEmpty;
  const Widget* widget = find(id);
  return (widget != nullptr && widget->kind == WidgetKind::kTextField) ? widget->edit.text
                                                                       : kEmpty;
}

int WidgetSet::text_field_cursor(NodeId id) const {
  const Widget* widget = find(id);
  return (widget != nullptr && widget->kind == WidgetKind::kTextField) ? widget->edit.cursor
                                                                       : 0;
}

std::optional<TextSelection> WidgetSet::text_field_selection(NodeId id) const {
  const Widget* widget = find(id);
  if (widget == nullptr || widget->kind != WidgetKind::kTextField) {
    return std::nullopt;
  }
  return text_edit::selection(widget->edit);
}

void WidgetSet::text_field_refresh_display(RenderTree& tree, const FontCatalog& fonts,
                                           NodeId id, Widget& widget, bool focused) {
  const PixelRect field_bounds = tree.local_bounds(id);
  const int visible_width = std::max(0, field_bounds.width);
  const int inner_height = std::max(0, field_bounds.height);

  TextStyle content_style = tree.style(widget.content).text;
  const FontId font = content_style.font;
  const float size = content_style.size;

  if (!focused) {
    widget.scroll_x = 0;
    content_style.text = ellipsize(fonts, font, size, widget.edit.text, visible_width);
    tree.set_text(widget.content, content_style);
    // The content child's own box must be wide enough to hold what it
    // paints - paint_text() clips to ITS OWN NODE's bounds, which is a different clip than the
    // field's `overflow: kClip` and runs regardless of it. A 1px placeholder box (this widget's
    // construction-time default, never since resized) would clip away all but a sliver of the
    // text before the field's own clip is ever consulted - visible_width is always enough
    // because ellipsize() guarantees the truncated string fits inside it.
    tree.set_local_bounds(widget.content,
                          PixelRect{0, 0, std::max(1, visible_width), inner_height});
    tree.set_local_bounds(widget.caret, PixelRect{0, 0, 0, inner_height});
    tree.set_local_bounds(widget.selection_highlight, PixelRect{0, 0, 0, inner_height});
    tree.set_local_bounds(widget.composition_underline, PixelRect{0, 0, 0, inner_height});
    return;
  }

  const text_edit::Projection projection = text_edit::display_projection(widget.edit);

  content_style.text = projection.text;
  const std::optional<Paragraph> para =
      build_edit_paragraph(fonts, font, size, projection.text);
  const float cursor_x = para ? para->caret_x(projection.cursor) : 0.0F;
  const float total_width =
      para ? para->caret_x(static_cast<int>(projection.text.size())) : 0.0F;

  int scroll_x = widget.scroll_x;
  if (total_width <= static_cast<float>(visible_width)) {
    scroll_x = 0;
  } else {
    const float cursor_px = cursor_x - static_cast<float>(scroll_x);
    if (cursor_px < 0.0F) {
      scroll_x = static_cast<int>(std::lround(static_cast<double>(cursor_x)));
    } else if (cursor_px > static_cast<float>(visible_width)) {
      scroll_x = static_cast<int>(std::lround(static_cast<double>(cursor_x))) - visible_width;
    }
    const int max_scroll = std::max(0, static_cast<int>(total_width) - visible_width);
    scroll_x = std::clamp(scroll_x, 0, max_scroll);
  }
  widget.scroll_x = scroll_x;

  tree.set_text(widget.content, content_style);
  // The content child's box must hold the WHOLE string - unlike the
  // unfocused branch, this text is not pre-truncated, so the field's own
  // `overflow: kClip` is the ONLY thing confining an overflowing string;
  // the content node's own paint_text() clip must not additionally cut it
  // off before that happens.
  const int content_width = std::max(
      visible_width, static_cast<int>(std::lround(static_cast<double>(total_width))) + 1);
  tree.set_local_bounds(widget.content, PixelRect{-scroll_x, 0, content_width, inner_height});

  // Clamped so the caret's own width stays fully inside the field even when
  // the cursor sits at the very last byte of an overflowing string -
  // otherwise a caret exactly at the visible edge would be half-clipped by
  // the field's own `overflow: kClip`, which is a cosmetic defect the clamp
  // upper bound removes for free.
  const int caret_x =
      std::clamp(static_cast<int>(std::lround(static_cast<double>(cursor_x))) - scroll_x, 0,
                 std::max(0, visible_width - kCaretWidthPx));
  tree.set_local_bounds(widget.caret, PixelRect{caret_x, 0, kCaretWidthPx, inner_height});

  // selection_highlight is dual-purpose while composing (7-3):
  // the user's OWN selection when not composing, or SDL_TextEditingEvent's
  // own "focused clause" range within the preedit when composing -
  // text_edit::display_projection() is where the two are told apart.
  if (projection.highlight.has_value()) {
    const float start_x = para ? para->caret_x(projection.highlight->start) : 0.0F;
    const float end_x = para ? para->caret_x(projection.highlight->end) : 0.0F;
    const int hl_x = static_cast<int>(std::lround(static_cast<double>(start_x))) - scroll_x;
    const int hl_w =
        std::max(0, static_cast<int>(std::lround(static_cast<double>(end_x - start_x))));
    tree.set_local_bounds(widget.selection_highlight, PixelRect{hl_x, 0, hl_w, inner_height});
  } else {
    tree.set_local_bounds(widget.selection_highlight, PixelRect{0, 0, 0, inner_height});
  }

  // The composition underline (7-3): a thin bar under the
  // WHOLE preedit span, the conventional visual distinction between
  // composing and committed text - a fourth plain positioned child, the
  // same shape caret/selection_highlight already are, not a new paint
  // primitive.
  if (projection.underline.has_value()) {
    const float underline_start_x = para ? para->caret_x(projection.underline->start) : 0.0F;
    const float underline_end_x = para ? para->caret_x(projection.underline->end) : 0.0F;
    const int underline_x =
        static_cast<int>(std::lround(static_cast<double>(underline_start_x))) - scroll_x;
    const int underline_w =
        std::max(0, static_cast<int>(
                        std::lround(static_cast<double>(underline_end_x - underline_start_x))));
    const int underline_y = std::max(0, inner_height - kCompositionUnderlineHeightPx);
    const int underline_h = std::min(inner_height, kCompositionUnderlineHeightPx);
    tree.set_local_bounds(widget.composition_underline,
                          PixelRect{underline_x, underline_y, underline_w, underline_h});
  } else {
    tree.set_local_bounds(widget.composition_underline, PixelRect{0, 0, 0, inner_height});
  }
}

TextEditState* WidgetSet::text_field_state(NodeId id) {
  Widget* widget = find(id);
  if (widget == nullptr || widget->kind != WidgetKind::kTextField) {
    return nullptr;
  }
  widget->edit.history.now = edit_now_;
  return &widget->edit;
}

bool WidgetSet::text_field_commit(RenderTree& tree, const FontCatalog& fonts, NodeId id,
                                  bool changed) {
  if (changed) {
    text_field_refresh_display(tree, fonts, id, *find(id), true);
  }
  return changed;
}

bool WidgetSet::text_field_insert(RenderTree& tree, const FontCatalog& fonts, NodeId id,
                                  std::string_view input) {
  TextEditState* state = text_field_state(id);
  return state != nullptr &&
         text_field_commit(tree, fonts, id, text_edit::insert(*state, input));
}

bool WidgetSet::text_field_backspace(RenderTree& tree, const FontCatalog& fonts, NodeId id) {
  TextEditState* state = text_field_state(id);
  return state != nullptr && text_field_commit(tree, fonts, id, text_edit::backspace(*state));
}

bool WidgetSet::text_field_delete_forward(RenderTree& tree, const FontCatalog& fonts,
                                          NodeId id) {
  TextEditState* state = text_field_state(id);
  return state != nullptr &&
         text_field_commit(tree, fonts, id, text_edit::delete_forward(*state));
}

bool WidgetSet::text_field_move(RenderTree& tree, const FontCatalog& fonts, NodeId id,
                                TextFieldMove move, bool extend_selection) {
  TextEditState* state = text_field_state(id);
  // Suppressed while composing: arrow-key navigation belongs to the IME's
  // own clause/candidate selection while it is active.
  if (state == nullptr || state->composing) {
    return false;
  }
  // A returning switch inside an immediately-invoked lambda keeps GCC's
  // flow analysis and clang-tidy's dead-store check from disagreeing about
  // an initial value.
  const bool changed = [&] {
    switch (move) {
      case TextFieldMove::kCharLeft:
        return text_edit::move_horizontal(*state, true, extend_selection);
      case TextFieldMove::kCharRight:
        return text_edit::move_horizontal(*state, false, extend_selection);
      case TextFieldMove::kLineStart:
        return text_edit::set_cursor(*state, 0, extend_selection);
      case TextFieldMove::kLineEnd:
        return text_edit::set_cursor(*state, static_cast<int>(state->text.size()),
                                     extend_selection);
      case TextFieldMove::kWordLeft:
        return text_edit::move_word(*state, true, extend_selection);
      case TextFieldMove::kWordRight:
        return text_edit::move_word(*state, false, extend_selection);
    }
    __builtin_unreachable();
  }();
  return text_field_commit(tree, fonts, id, changed);
}

bool WidgetSet::text_field_select_all(RenderTree& tree, const FontCatalog& fonts, NodeId id) {
  TextEditState* state = text_field_state(id);
  return state != nullptr && text_field_commit(tree, fonts, id, text_edit::select_all(*state));
}

bool WidgetSet::text_field_click(RenderTree& tree, const FontCatalog& fonts, NodeId id,
                                 int pointer_x, bool extend_selection, int click_count) {
  Widget* widget = find(id);
  if (widget == nullptr || widget->kind != WidgetKind::kTextField) {
    return false;
  }
  // A click always ends composition without committing it (7-3,
  // "clicking elsewhere mid-composition") - the position it is
  // about to move the cursor to may not even be inside the composition's
  // own replace range any more, so there is no sensible way to keep a
  // preview alive across it. Falls through and still positions the cursor
  // at the click, exactly as it would have with no composition in
  // progress.
  text_edit::cancel_composition(widget->edit);
  const PixelRect field_bounds = tree.absolute_bounds(id);
  const TextStyle& content_style = tree.style(widget->content).text;
  const float local_x = static_cast<float>(pointer_x - field_bounds.x + widget->scroll_x);
  const std::optional<Paragraph> para =
      build_edit_paragraph(fonts, content_style.font, content_style.size, widget->edit.text);
  const int offset =
      para ? grapheme_offset_at_x(*para, grapheme_boundaries(widget->edit.text), local_x) : 0;
  if (click_count >= 3) {
    return text_field_commit(
        tree, fonts, id,
        text_edit::select_range(widget->edit, 0, static_cast<int>(widget->edit.text.size())));
  }
  if (click_count == 2) {
    const TextSelection word = text_edit::word_range(widget->edit.text, offset);
    return text_field_commit(tree, fonts, id,
                             text_edit::select_range(widget->edit, word.start, word.end));
  }
  return text_field_commit(tree, fonts, id,
                           text_edit::set_cursor(widget->edit, offset, extend_selection));
}

void WidgetSet::text_field_set_focus(RenderTree& tree, const FontCatalog& fonts, NodeId id,
                                     bool focused) {
  Widget* widget = find(id);
  if (widget == nullptr || widget->kind != WidgetKind::kTextField) {
    return;
  }
  if (!focused) {
    widget->edit.selection_anchor.reset();
    // Losing focus ends composition without committing it (7-3, doc/
    // ime.md section 6) - the identical rule Escape and a click already
    // apply, restated here because a blur can arrive with no key or click
    // of its own (a DIFFERENT field being focused instead).
    text_edit::cancel_composition(widget->edit);
  }
  text_field_refresh_display(tree, fonts, id, *widget, focused);
}

void WidgetSet::text_field_composition_update(RenderTree& tree, const FontCatalog& fonts,
                                              NodeId id, std::string_view text, int start_units,
                                              int length_units) {
  TextEditState* state = text_field_state(id);
  if (state != nullptr) {
    text_field_commit(tree, fonts, id,
                      text_edit::composition_update(*state, text, start_units, length_units));
  }
}

void WidgetSet::text_field_cancel_composition(RenderTree& tree, const FontCatalog& fonts,
                                              NodeId id) {
  TextEditState* state = text_field_state(id);
  if (state != nullptr) {
    text_field_commit(tree, fonts, id, text_edit::cancel_composition(*state));
  }
}

bool WidgetSet::text_field_is_composing(NodeId id) const {
  const Widget* widget = find(id);
  return widget != nullptr && widget->kind == WidgetKind::kTextField && widget->edit.composing;
}

const std::string& WidgetSet::text_field_composition_text(NodeId id) const {
  static const std::string kEmpty;
  const Widget* widget = find(id);
  return (widget != nullptr && widget->kind == WidgetKind::kTextField)
             ? widget->edit.composition_text
             : kEmpty;
}

const TextEditState* WidgetSet::editor_state(NodeId id) const {
  const Widget* widget = find(id);
  return (widget != nullptr && is_editable_text(widget->kind)) ? &widget->edit : nullptr;
}

bool WidgetSet::editor_is_composing(NodeId id) const {
  const TextEditState* state = editor_state(id);
  return state != nullptr && state->composing;
}

bool WidgetSet::editor_insert(RenderTree& tree, const FontCatalog& fonts, NodeId id,
                              std::string_view input) {
  const Widget* widget = find(id);
  if (widget == nullptr) {
    return false;
  }
  if (widget->kind == WidgetKind::kTextArea) {
    return text_area_insert(tree, fonts, id, input);
  }
  return text_field_insert(tree, fonts, id, input);
}

bool WidgetSet::editor_select_all(RenderTree& tree, const FontCatalog& fonts, NodeId id) {
  const Widget* widget = find(id);
  if (widget == nullptr) {
    return false;
  }
  if (widget->kind == WidgetKind::kTextArea) {
    return text_area_select_all(tree, fonts, id);
  }
  return text_field_select_all(tree, fonts, id);
}

bool WidgetSet::editor_delete_word(RenderTree& tree, const FontCatalog& fonts, NodeId id,
                                   bool backward) {
  if (TextEditState* state = text_field_state(id); state != nullptr) {
    return text_field_commit(tree, fonts, id, text_edit::delete_word(*state, backward));
  }
  if (TextEditState* state = text_area_state(id); state != nullptr) {
    state->preferred_x.reset();
    find(id)->caret_upstream = false;
    return text_area_commit(tree, fonts, id, text_edit::delete_word(*state, backward));
  }
  return false;
}

bool WidgetSet::editor_undo(RenderTree& tree, const FontCatalog& fonts, NodeId id) {
  if (TextEditState* state = text_field_state(id); state != nullptr) {
    return text_field_commit(tree, fonts, id, text_edit::undo(*state));
  }
  if (TextEditState* state = text_area_state(id); state != nullptr) {
    find(id)->caret_upstream = false;
    return text_area_commit(tree, fonts, id, text_edit::undo(*state));
  }
  return false;
}

bool WidgetSet::editor_redo(RenderTree& tree, const FontCatalog& fonts, NodeId id) {
  if (TextEditState* state = text_field_state(id); state != nullptr) {
    return text_field_commit(tree, fonts, id, text_edit::redo(*state));
  }
  if (TextEditState* state = text_area_state(id); state != nullptr) {
    find(id)->caret_upstream = false;
    return text_area_commit(tree, fonts, id, text_edit::redo(*state));
  }
  return false;
}

void WidgetSet::editor_set_max_length(NodeId id, int max_graphemes) {
  Widget* widget = find(id);
  if (widget != nullptr && is_editable_text(widget->kind)) {
    widget->edit.max_graphemes = std::max(0, max_graphemes);
  }
}

bool WidgetSet::editor_set_text(RenderTree& tree, const FontCatalog& fonts, NodeId id,
                                std::string_view text, bool focused) {
  Widget* widget = find(id);
  if (widget == nullptr || !is_editable_text(widget->kind)) {
    return false;
  }
  const bool area = widget->kind == WidgetKind::kTextArea;
  TextEditState& edit = widget->edit;
  text_edit::cancel_composition(edit);
  text_edit::clear_history(edit);
  std::string sanitized = text_edit::sanitize_insertable(text, area);
  if (edit.max_graphemes > 0) {
    const std::vector<int> bounds = grapheme_boundaries(sanitized);
    if (static_cast<int>(bounds.size()) - 1 > edit.max_graphemes) {
      sanitized.resize(
          static_cast<std::size_t>(bounds[static_cast<std::size_t>(edit.max_graphemes)]));
    }
  }
  const bool changed = sanitized != edit.text;
  edit.text = sanitized;
  edit.cursor = static_cast<int>(edit.text.size());
  edit.selection_anchor.reset();
  edit.preferred_x.reset();
  if (area) {
    widget->caret_upstream = false;
    widget->focused = focused;
    text_area_refresh_display(tree, fonts, id, *widget, true);
  } else {
    text_field_refresh_display(tree, fonts, id, *widget, focused);
  }
  return changed;
}

}  // namespace dg
