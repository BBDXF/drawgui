#include "drawgui/widget/widget_set.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "drawgui/render/text_metrics.h"

namespace dg {
namespace {

bool interactive(WidgetKind kind) {
  switch (kind) {
    case WidgetKind::kButton:
    case WidgetKind::kCheckbox:
    // kTextField accepts pointer input directly, unlike kSlider/kScrollView:
    // a click has to resolve TO the field (to focus it and place the
    // cursor), not merely to a plain child of it - doc/text-input.md
    // section 3.
    case WidgetKind::kTextField:
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

// Printable ASCII only (space through tilde). Everything else - control
// characters, DEL, and every byte of a multi-byte UTF-8 sequence an IME or a
// paste might commit - is dropped rather than mis-split, which is the
// concrete mechanism behind doc/text-input.md section 1's scoping decision.
bool is_editable_ascii(char byte) {
  const auto value = static_cast<unsigned char>(byte);
  return value >= 0x20 && value <= 0x7E;
}

std::string filter_ascii(std::string_view input) {
  std::string filtered;
  filtered.reserve(input.size());
  for (const char byte : input) {
    if (is_editable_ascii(byte)) {
      filtered.push_back(byte);
    }
  }
  return filtered;
}

TextSelection normalize_selection(int cursor, int anchor) {
  return TextSelection{std::min(cursor, anchor), std::max(cursor, anchor)};
}

// Truncates `text` to the longest prefix such that PREFIX + "..." still fits
// `visible_width` device pixels, appending the ellipsis - the unfocused
// overflow treatment doc/text-input.md section 5 chose over scrolling an
// unfocused field with no visible caret to justify it. Returns `text`
// unchanged (no ellipsis) when it already fits.
std::string ellipsize(const FontCatalog& fonts, FontId font, float size,
                      const std::string& text, int visible_width) {
  const auto width_of = [&](std::string_view candidate) {
    return measure_ascii_width(fonts, font, size, candidate);
  };
  if (visible_width <= 0 || width_of(text) <= static_cast<float>(visible_width)) {
    return text;
  }
  static constexpr std::string_view kEllipsis = "...";
  std::size_t prefix = 0;
  for (; prefix <= text.size(); ++prefix) {
    std::string candidate = text.substr(0, prefix);
    candidate += kEllipsis;
    if (width_of(candidate) > static_cast<float>(visible_width)) {
      break;
    }
  }
  const std::size_t kept = prefix > 0 ? prefix - 1 : 0;
  std::string result = text.substr(0, kept);
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
  if (widgets_.size() <= id.value) {
    widgets_.resize(static_cast<std::size_t>(id.value) + 1);
  }
  widgets_[id.value] = widget;
}

const Widget* WidgetSet::find(NodeId id) const {
  if (id.value >= widgets_.size()) {
    return nullptr;
  }
  const std::optional<Widget>& slot = widgets_[id.value];
  return slot.has_value() ? &slot.value() : nullptr;
}

Widget* WidgetSet::find(NodeId id) {
  if (id.value >= widgets_.size()) {
    return nullptr;
  }
  std::optional<Widget>& slot = widgets_[id.value];
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
    if (!slot.has_value() || i == id.value) {
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
    if (widget != nullptr && widget->kind == WidgetKind::kScrollView) {
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
  if (widget == nullptr || widget->kind != WidgetKind::kScrollView) {
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

  const int extent = widget->list_item_extent;
  const int top_index = extent > 0 ? next / extent : 0;
  return list_sync(tree, id, top_index);
}

std::optional<NodeId> WidgetSet::slidable_owner_of(const RenderTree& tree, NodeId id) const {
  NodeId current = id;
  while (true) {
    const Widget* widget = find(current);
    if (widget != nullptr && widget->kind == WidgetKind::kSlider) {
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

void WidgetSet::resync_sliders(RenderTree& tree) const {
  for (std::size_t i = 0; i < widgets_.size(); ++i) {
    const std::optional<Widget>& slot = widgets_[i];
    if (!slot.has_value() || slot->kind != WidgetKind::kSlider) {
      continue;
    }
    reposition_slider(tree, NodeId{static_cast<std::uint32_t>(i)}, *slot);
  }
}

void WidgetSet::refresh(RenderTree& tree, NodeId id, PointerState state) const {
  const Widget* found = find(id);
  if (found == nullptr) {
    return;
  }
  const Widget& widget = *found;

  // Pressed beats hovered: a widget the pointer is held down on is always
  // hovered too, and reporting it as merely hovered would drop the press
  // feedback exactly while the user is asking for it.
  Color fill = widget.fill_normal;
  if (state.pressed) {
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
  const Color mark = widget.checked ? widget.indicator_on : widget.indicator_off;
  if (tree.style(widget.indicator).fill != mark) {
    tree.set_fill(widget.indicator, mark);
  }
}

namespace {
constexpr int kCaretWidthPx = 2;
}  // namespace

const std::string& WidgetSet::text_field_text(NodeId id) const {
  static const std::string kEmpty;
  const Widget* widget = find(id);
  return (widget != nullptr && widget->kind == WidgetKind::kTextField) ? widget->text : kEmpty;
}

int WidgetSet::text_field_cursor(NodeId id) const {
  const Widget* widget = find(id);
  return (widget != nullptr && widget->kind == WidgetKind::kTextField) ? widget->cursor : 0;
}

std::optional<TextSelection> WidgetSet::text_field_selection(NodeId id) const {
  const Widget* widget = find(id);
  if (widget == nullptr || widget->kind != WidgetKind::kTextField ||
      !widget->selection_anchor.has_value() || *widget->selection_anchor == widget->cursor) {
    return std::nullopt;
  }
  return normalize_selection(widget->cursor, *widget->selection_anchor);
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
    content_style.text = ellipsize(fonts, font, size, widget.text, visible_width);
    tree.set_text(widget.content, content_style);
    // The content child's own box must be wide enough to hold what it
    // paints - paint_text() clips to ITS OWN NODE's bounds (doc/widgets.md
    // section 3.2), which is a different clip than the field's `overflow:
    // kClip` and runs regardless of it. A 1px placeholder box (this
    // widget's construction-time default, never since resized) would clip
    // away all but a sliver of the text before the field's own clip is ever
    // consulted - visible_width is always enough because ellipsize()
    // guarantees the truncated string fits inside it.
    tree.set_local_bounds(widget.content,
                          PixelRect{0, 0, std::max(1, visible_width), inner_height});
    tree.set_local_bounds(widget.caret, PixelRect{0, 0, 0, inner_height});
    tree.set_local_bounds(widget.selection_highlight, PixelRect{0, 0, 0, inner_height});
    return;
  }

  content_style.text = widget.text;
  const float cursor_x = measure_ascii_width(
      fonts, font, size,
      std::string_view{widget.text}.substr(0, static_cast<std::size_t>(widget.cursor)));
  const float total_width = measure_ascii_width(fonts, font, size, widget.text);

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

  const std::optional<int> anchor = widget.selection_anchor;
  if (anchor.has_value() && *anchor != widget.cursor) {
    const TextSelection selection = normalize_selection(widget.cursor, *anchor);
    const float start_x = measure_ascii_width(
        fonts, font, size,
        std::string_view{widget.text}.substr(0, static_cast<std::size_t>(selection.start)));
    const float end_x = measure_ascii_width(
        fonts, font, size,
        std::string_view{widget.text}.substr(0, static_cast<std::size_t>(selection.end)));
    const int hl_x = static_cast<int>(std::lround(static_cast<double>(start_x))) - scroll_x;
    const int hl_w =
        std::max(0, static_cast<int>(std::lround(static_cast<double>(end_x - start_x))));
    tree.set_local_bounds(widget.selection_highlight, PixelRect{hl_x, 0, hl_w, inner_height});
  } else {
    tree.set_local_bounds(widget.selection_highlight, PixelRect{0, 0, 0, inner_height});
  }
}

bool WidgetSet::text_field_replace_range(RenderTree& tree, const FontCatalog& fonts, NodeId id,
                                         Widget& widget, int lo, int hi,
                                         std::string_view replacement) {
  const int size = static_cast<int>(widget.text.size());
  const int start = std::clamp(std::min(lo, hi), 0, size);
  const int end = std::clamp(std::max(lo, hi), 0, size);
  if (start == end && replacement.empty()) {
    return false;
  }
  widget.text.replace(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start),
                      replacement);
  widget.cursor = start + static_cast<int>(replacement.size());
  widget.selection_anchor.reset();
  text_field_refresh_display(tree, fonts, id, widget, true);
  return true;
}

bool WidgetSet::text_field_insert(RenderTree& tree, const FontCatalog& fonts, NodeId id,
                                  std::string_view input) {
  Widget* widget = find(id);
  if (widget == nullptr || widget->kind != WidgetKind::kTextField) {
    return false;
  }
  const std::string filtered = filter_ascii(input);
  if (widget->selection_anchor.has_value()) {
    const TextSelection selection =
        normalize_selection(widget->cursor, *widget->selection_anchor);
    return text_field_replace_range(tree, fonts, id, *widget, selection.start, selection.end,
                                    filtered);
  }
  if (filtered.empty()) {
    return false;
  }
  return text_field_replace_range(tree, fonts, id, *widget, widget->cursor, widget->cursor,
                                  filtered);
}

bool WidgetSet::text_field_backspace(RenderTree& tree, const FontCatalog& fonts, NodeId id) {
  Widget* widget = find(id);
  if (widget == nullptr || widget->kind != WidgetKind::kTextField) {
    return false;
  }
  if (widget->selection_anchor.has_value()) {
    const TextSelection selection =
        normalize_selection(widget->cursor, *widget->selection_anchor);
    return text_field_replace_range(tree, fonts, id, *widget, selection.start, selection.end,
                                    "");
  }
  if (widget->cursor <= 0) {
    return false;
  }
  return text_field_replace_range(tree, fonts, id, *widget, widget->cursor - 1, widget->cursor,
                                  "");
}

bool WidgetSet::text_field_delete_forward(RenderTree& tree, const FontCatalog& fonts,
                                          NodeId id) {
  Widget* widget = find(id);
  if (widget == nullptr || widget->kind != WidgetKind::kTextField) {
    return false;
  }
  if (widget->selection_anchor.has_value()) {
    const TextSelection selection =
        normalize_selection(widget->cursor, *widget->selection_anchor);
    return text_field_replace_range(tree, fonts, id, *widget, selection.start, selection.end,
                                    "");
  }
  const int size = static_cast<int>(widget->text.size());
  if (widget->cursor >= size) {
    return false;
  }
  return text_field_replace_range(tree, fonts, id, *widget, widget->cursor, widget->cursor + 1,
                                  "");
}

bool WidgetSet::text_field_move(RenderTree& tree, const FontCatalog& fonts, NodeId id,
                                TextFieldMove move, bool extend_selection) {
  Widget* widget = find(id);
  if (widget == nullptr || widget->kind != WidgetKind::kTextField) {
    return false;
  }
  const int size = static_cast<int>(widget->text.size());
  const int old_cursor = widget->cursor;
  const std::optional<int> old_anchor = widget->selection_anchor;

  // Plain (non-extending) Left/Right with an active selection collapses to
  // the selection's near edge rather than moving from the cursor - matching
  // every desktop toolkit. Home/End always jump to the absolute ends
  // regardless of a selection, which is why only kCharLeft/kCharRight below
  // consult `collapsing`.
  const bool collapsing = !extend_selection && old_anchor.has_value();
  const TextSelection current = collapsing ? normalize_selection(old_cursor, *old_anchor)
                                           : TextSelection{old_cursor, old_cursor};

  // A returning switch inside an immediately-invoked lambda, rather than a
  // single mutable local the switch assigns and `break`s out of: the
  // earlier shape (one `int new_cursor;` assigned in every case) could not
  // satisfy GCC and clang-tidy at once - GCC's `-O3` flow analysis wants an
  // initializer (`cppcoreguidelines-init-variables` wants the same thing
  // clang-tidy-side), but any initializer clang-tidy can see is never read
  // is a dead store (`clang-analyzer-deadcode.DeadStores`) once every case
  // below overwrites it. A `return` per case has no separate "initial
  // value" for either check to disagree about.
  const int new_cursor = [&] {
    switch (move) {
      case TextFieldMove::kCharLeft:
        return collapsing ? current.start : std::max(0, old_cursor - 1);
      case TextFieldMove::kCharRight:
        return collapsing ? current.end : std::min(size, old_cursor + 1);
      case TextFieldMove::kLineStart:
        return 0;
      case TextFieldMove::kLineEnd:
        return size;
    }
    __builtin_unreachable();
  }();

  if (extend_selection) {
    if (!old_anchor.has_value()) {
      widget->selection_anchor = old_cursor;
    }
  } else {
    widget->selection_anchor.reset();
  }
  widget->cursor = new_cursor;

  if (new_cursor == old_cursor && widget->selection_anchor == old_anchor) {
    return false;
  }
  text_field_refresh_display(tree, fonts, id, *widget, true);
  return true;
}

bool WidgetSet::text_field_click(RenderTree& tree, const FontCatalog& fonts, NodeId id,
                                 int pointer_x, bool extend_selection) {
  Widget* widget = find(id);
  if (widget == nullptr || widget->kind != WidgetKind::kTextField) {
    return false;
  }
  const PixelRect field_bounds = tree.absolute_bounds(id);
  const TextStyle& content_style = tree.style(widget->content).text;
  const float local_x = static_cast<float>(pointer_x - field_bounds.x + widget->scroll_x);
  const std::size_t offset =
      ascii_offset_at_x(fonts, content_style.font, content_style.size, widget->text, local_x);

  const int old_cursor = widget->cursor;
  const std::optional<int> old_anchor = widget->selection_anchor;

  if (extend_selection) {
    if (!widget->selection_anchor.has_value()) {
      widget->selection_anchor = old_cursor;
    }
  } else {
    widget->selection_anchor.reset();
  }
  widget->cursor = static_cast<int>(offset);

  if (widget->cursor == old_cursor && widget->selection_anchor == old_anchor) {
    return false;
  }
  text_field_refresh_display(tree, fonts, id, *widget, true);
  return true;
}

void WidgetSet::text_field_set_focus(RenderTree& tree, const FontCatalog& fonts, NodeId id,
                                     bool focused) {
  Widget* widget = find(id);
  if (widget == nullptr || widget->kind != WidgetKind::kTextField) {
    return;
  }
  if (!focused) {
    widget->selection_anchor.reset();
  }
  text_field_refresh_display(tree, fonts, id, *widget, focused);
}

}  // namespace dg
