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
    // cursor), not merely to a plain child of it - doc/text-input.md
    // section 3.
    case WidgetKind::kTextField:
    // A dropdown's anchor is a plain click target exactly like kButton -
    // opening the popup IS the activation, doc/menus.md.
    case WidgetKind::kDropdown:
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

// Replaces 4-9's filter_ascii(): accepts arbitrary well-formed Unicode text
// rather than dropping every non-ASCII byte whole (doc/text-input.md's
// cross-reference to doc/text-layout.md section 2 - libgrapheme's
// segmentation, proven by 7-1, is what makes lifting the restriction safe).
// Two things are still rejected, not merely "everything except ASCII":
//
//   - malformed UTF-8 (a lone continuation byte, a truncated multi-byte
//     lead, an overlong encoding, a surrogate codepoint) is repaired to
//     U+FFFD one invalid byte at a time via dg::sanitize_utf8() - 7-2's own
//     paragraph_build.cpp substitution (doc/text-layout.md section 3),
//     reused here rather than a second hand-rolled policy;
//   - ASCII control characters (0x00-0x1F) and DEL (0x7F) are dropped: a
//     literal newline or tab a paste/IME might commit has no meaning in a
//     single-line field (multi-line TextArea editing stays out of scope -
//     design.md's MVP-8 list names TextField, not TextArea, and 4-9 already
//     verified that).
//
// Stripping control bytes AFTER sanitize_utf8() (rather than folding both
// into one pass) is safe because well-formed UTF-8 guarantees every byte
// below 0x80 is a complete one-byte codepoint on its own, never a
// continuation byte of something else - so a plain byte-range filter over
// the sanitized result cannot split anything.
std::string sanitize_insertable_text(std::string_view input) {
  const std::string well_formed = sanitize_utf8(input);
  std::string out;
  out.reserve(well_formed.size());
  for (const char byte : well_formed) {
    const auto value = static_cast<unsigned char>(byte);
    if (value < 0x20 || value == 0x7F) {
      continue;
    }
    out.push_back(byte);
  }
  return out;
}

TextSelection normalize_selection(int cursor, int anchor) {
  return TextSelection{std::min(cursor, anchor), std::max(cursor, anchor)};
}

// Builds a throwaway, single-line, unwrapped Paragraph over `text` purely
// for pixel measurement - the shaping-aware replacement for 4-9's
// measure_ascii_width()/ascii_offset_at_x() (doc/text-input.md's 7-2b
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

// One grapheme-cluster step from `from`, which MUST already be a boundary
// in `boundaries` (every TextField mutator's own invariant - doc/widgets.h's
// Widget::cursor comment). Left/right unified into one function via
// `left`, matching design.md's own "the minimum unit is the grapheme
// cluster, not the byte" mandate for both directions at once rather than as
// two independently-derived pieces of arithmetic.
int grapheme_step(const std::vector<int>& boundaries, int from, bool left) {
  const auto it = std::lower_bound(boundaries.begin(), boundaries.end(), from);
  if (it == boundaries.end() || *it != from) {
    return from;  // defensive: `from` was not on a boundary - should not happen
  }
  if (left) {
    return it == boundaries.begin() ? from : *(it - 1);
  }
  const auto next = it + 1;
  return next == boundaries.end() ? from : *next;
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
// the unfocused overflow treatment doc/text-input.md section 5 chose over
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

// A thin bar under the whole composing span (7-3, doc/ime.md) - the
// conventional underline treatment, sized independently of the caret's own
// width constant above because the two are visually distinct shapes (a
// vertical bar vs. a horizontal one).
constexpr int kCompositionUnderlineHeightPx = 2;

// The byte range within a composition's own preedit string that SDL's
// start/length units resolve to (Widget::composition_focus_start/_length).
struct CompositionFocusBytes {
  int start = 0;
  int end = 0;
};

// Converts SDL_TextEditingEvent's own start/length ("UTF-8 characters",
// TextEditingEvent's own header comment) into a byte range within `text`
// (assumed well-formed - the caller already ran it through
// sanitize_insertable_text()) by walking codepoints via dg::utf8_decode().
// `int64_t` accumulators rather than `int` ones: a hostile or merely buggy
// IME's start/length are exactly the "absurd cursor offset"
// -DDG_SANITIZE=ON is meant to catch, and `start_units + length_units`
// computed directly in `int` could overflow before either value is ever
// compared against `text`'s own length. Both ends are clamped to
// `text.size()` by the walk itself stopping there, never by trusting the
// input.
CompositionFocusBytes composition_focus_bytes(std::string_view text, int start_units,
                                              int length_units) {
  const std::int64_t start = std::max<std::int64_t>(0, start_units);
  const std::int64_t end = start + std::max<std::int64_t>(0, length_units);

  CompositionFocusBytes result;
  std::size_t byte = 0;
  std::int64_t index = 0;
  bool start_found = false;
  while (true) {
    if (!start_found && index >= start) {
      result.start = static_cast<int>(byte);
      start_found = true;
    }
    if (start_found && index >= end) {
      result.end = static_cast<int>(byte);
      return result;
    }
    if (byte >= text.size()) {
      break;
    }
    const Utf8Step step = utf8_decode(text, byte);
    byte += step.length;
    ++index;
  }
  if (!start_found) {
    result.start = static_cast<int>(text.size());
  }
  result.end = static_cast<int>(text.size());
  return result;
}

// Clears every composition field back to "not composing" - shared by
// text_field_cancel_composition(), a blur (text_field_set_focus(false)) and
// a click/commit arriving mid-composition (text_field_click()/
// text_field_insert()), none of which touch `text`/`cursor`/
// `selection_anchor` themselves: composition never wrote to any of them, so
// there is nothing to undo.
void reset_composition_fields(Widget& widget) {
  widget.composing = false;
  widget.composition_text.clear();
  widget.composition_focus_start = 0;
  widget.composition_focus_length = 0;
  widget.composition_replace_start = 0;
  widget.composition_replace_end = 0;
}

// What the FOCUSED display branch of text_field_refresh_display() paints,
// computed once rather than branched on `widget.composing` repeatedly
// inline - split out for the same reason `dispatch_keyboard()` was split
// out of the SDL3 backend's own `dispatch()`: clang-tidy's cognitive-
// complexity budget. `highlight` is the user's own selection OR (while
// composing) SDL's own "focused clause" range, mutually exclusive by
// construction; `underline` is only set while composing, spanning the
// WHOLE preedit.
struct FocusedProjection {
  std::string text;
  int cursor = 0;
  std::optional<TextSelection> highlight;
  std::optional<TextSelection> underline;
};

FocusedProjection focused_display_projection(const Widget& widget) {
  if (!widget.composing) {
    FocusedProjection projection{widget.text, widget.cursor, std::nullopt, std::nullopt};
    if (widget.selection_anchor.has_value() && *widget.selection_anchor != widget.cursor) {
      projection.highlight = normalize_selection(widget.cursor, *widget.selection_anchor);
    }
    return projection;
  }

  // While composing (7-3, doc/ime.md), the DISPLAY splices the IME's
  // not-yet-committed preedit into `widget.text` at the point composition
  // began - `widget.text`/`cursor`/`selection_anchor` are never written by
  // composition itself, only read here, exactly the same "paint-time
  // projection, not a second copy" shape ellipsize() already has for
  // `widget.text` itself.
  FocusedProjection projection;
  projection.text =
      widget.text.substr(0, static_cast<std::size_t>(widget.composition_replace_start)) +
      widget.composition_text +
      widget.text.substr(static_cast<std::size_t>(widget.composition_replace_end));
  projection.cursor = widget.composition_replace_start + widget.composition_focus_start;
  if (widget.composition_focus_length > 0) {
    const int start = widget.composition_replace_start + widget.composition_focus_start;
    projection.highlight = TextSelection{start, start + widget.composition_focus_length};
  }
  projection.underline = TextSelection{
      widget.composition_replace_start,
      widget.composition_replace_start + static_cast<int>(widget.composition_text.size())};
  return projection;
}

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
    tree.set_local_bounds(widget.composition_underline, PixelRect{0, 0, 0, inner_height});
    return;
  }

  const FocusedProjection projection = focused_display_projection(widget);

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

  // selection_highlight is dual-purpose while composing (7-3, doc/ime.md):
  // the user's OWN selection when not composing, or SDL_TextEditingEvent's
  // own "focused clause" range within the preedit when composing -
  // focused_display_projection() above is where the two are told apart.
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

  // The composition underline (7-3, doc/ime.md): a thin bar under the
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
  // A REAL commit (7-3, doc/ime.md) - whether IME-sourced or an ordinary
  // keystroke/paste, this is the one call that actually writes to the
  // model. `widget->cursor`/`selection_anchor` were never touched while
  // composing, so discarding the stale preview here and falling straight
  // through to the unchanged logic below inserts/replaces at exactly the
  // range that was already there when composition began - the commit path
  // this slice was asked to verify, not add a parallel one to.
  if (widget->composing) {
    reset_composition_fields(*widget);
  }
  const std::string sanitized = sanitize_insertable_text(input);
  if (widget->selection_anchor.has_value()) {
    const TextSelection selection =
        normalize_selection(widget->cursor, *widget->selection_anchor);
    return text_field_replace_range(tree, fonts, id, *widget, selection.start, selection.end,
                                    sanitized);
  }
  if (sanitized.empty()) {
    return false;
  }
  return text_field_replace_range(tree, fonts, id, *widget, widget->cursor, widget->cursor,
                                  sanitized);
}

bool WidgetSet::text_field_backspace(RenderTree& tree, const FontCatalog& fonts, NodeId id) {
  Widget* widget = find(id);
  // Suppressed while composing (7-3, doc/ime.md section 6): a real IME
  // consumes Backspace itself for candidate/clause editing while it is
  // active, so it never reaches this far in practice; simulating that here
  // (rather than editing the committed model underneath an active preview)
  // is what keeps the two from disagreeing about what is on screen.
  if (widget == nullptr || widget->kind != WidgetKind::kTextField || widget->composing) {
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
  const int start = grapheme_step(grapheme_boundaries(widget->text), widget->cursor, true);
  return text_field_replace_range(tree, fonts, id, *widget, start, widget->cursor, "");
}

bool WidgetSet::text_field_delete_forward(RenderTree& tree, const FontCatalog& fonts,
                                          NodeId id) {
  Widget* widget = find(id);
  // Suppressed while composing - the same rule text_field_backspace() above
  // applies, for the identical reason.
  if (widget == nullptr || widget->kind != WidgetKind::kTextField || widget->composing) {
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
  const int end = grapheme_step(grapheme_boundaries(widget->text), widget->cursor, false);
  return text_field_replace_range(tree, fonts, id, *widget, widget->cursor, end, "");
}

bool WidgetSet::text_field_move(RenderTree& tree, const FontCatalog& fonts, NodeId id,
                                TextFieldMove move, bool extend_selection) {
  Widget* widget = find(id);
  // Suppressed while composing, matching text_field_backspace()'s own
  // reasoning: arrow-key navigation belongs to the IME's own clause/
  // candidate selection while it is active.
  if (widget == nullptr || widget->kind != WidgetKind::kTextField || widget->composing) {
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

  // Grapheme-cluster boundaries of the CURRENT text - the font-independent
  // source kCharLeft/kCharRight step through (7-2b), computed once and
  // reused by whichever case needs it rather than per-branch.
  const std::vector<int> boundaries = grapheme_boundaries(widget->text);

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
        return collapsing ? current.start : grapheme_step(boundaries, old_cursor, true);
      case TextFieldMove::kCharRight:
        return collapsing ? current.end : grapheme_step(boundaries, old_cursor, false);
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

bool WidgetSet::text_field_select_all(RenderTree& tree, const FontCatalog& fonts, NodeId id) {
  Widget* widget = find(id);
  if (widget == nullptr || widget->kind != WidgetKind::kTextField || widget->composing) {
    return false;
  }
  const int size = static_cast<int>(widget->text.size());
  if (size == 0) {
    return false;
  }
  if (widget->selection_anchor == 0 && widget->cursor == size) {
    return false;
  }
  widget->selection_anchor = 0;
  widget->cursor = size;
  text_field_refresh_display(tree, fonts, id, *widget, true);
  return true;
}

bool WidgetSet::text_field_click(RenderTree& tree, const FontCatalog& fonts, NodeId id,
                                 int pointer_x, bool extend_selection) {
  Widget* widget = find(id);
  if (widget == nullptr || widget->kind != WidgetKind::kTextField) {
    return false;
  }
  // A click always ends composition without committing it (7-3, doc/ime.md
  // section 6, "clicking elsewhere mid-composition") - the position it is
  // about to move the cursor to may not even be inside the composition's
  // own replace range any more, so there is no sensible way to keep a
  // preview alive across it. Falls through and still positions the cursor
  // at the click, exactly as it would have with no composition in
  // progress.
  if (widget->composing) {
    reset_composition_fields(*widget);
  }
  const PixelRect field_bounds = tree.absolute_bounds(id);
  const TextStyle& content_style = tree.style(widget->content).text;
  const float local_x = static_cast<float>(pointer_x - field_bounds.x + widget->scroll_x);
  const std::optional<Paragraph> para =
      build_edit_paragraph(fonts, content_style.font, content_style.size, widget->text);
  const int offset =
      para ? grapheme_offset_at_x(*para, grapheme_boundaries(widget->text), local_x) : 0;

  const int old_cursor = widget->cursor;
  const std::optional<int> old_anchor = widget->selection_anchor;

  if (extend_selection) {
    if (!widget->selection_anchor.has_value()) {
      widget->selection_anchor = old_cursor;
    }
  } else {
    widget->selection_anchor.reset();
  }
  widget->cursor = offset;

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
    // Losing focus ends composition without committing it (7-3, doc/
    // ime.md section 6) - the identical rule Escape and a click already
    // apply, restated here because a blur can arrive with no key or click
    // of its own (a DIFFERENT field being focused instead).
    reset_composition_fields(*widget);
  }
  text_field_refresh_display(tree, fonts, id, *widget, focused);
}

void WidgetSet::text_field_composition_update(RenderTree& tree, const FontCatalog& fonts,
                                              NodeId id, std::string_view text, int start_units,
                                              int length_units) {
  Widget* widget = find(id);
  if (widget == nullptr || widget->kind != WidgetKind::kTextField) {
    return;
  }
  const std::string sanitized = sanitize_insertable_text(text);
  if (sanitized.empty()) {
    // SDL's own convention: an empty preedit means composition ended
    // WITHOUT a commit - a real commit is always a separate, later
    // text_field_insert() call. Identical to an explicit cancellation, so
    // this reuses it rather than duplicating the reset.
    text_field_cancel_composition(tree, fonts, id);
    return;
  }
  if (!widget->composing) {
    // Composition START: capture the anchor ONCE from whatever
    // selection/cursor the model already has - never re-derived from it
    // again while composing (doc/ime.md's own argument: composition must
    // never touch `cursor`/`selection_anchor` itself, so a later real
    // commit through text_field_insert() replaces exactly this range).
    const std::optional<int> anchor = widget->selection_anchor;
    if (anchor.has_value() && *anchor != widget->cursor) {
      const TextSelection selection = normalize_selection(widget->cursor, *anchor);
      widget->composition_replace_start = selection.start;
      widget->composition_replace_end = selection.end;
    } else {
      widget->composition_replace_start = widget->cursor;
      widget->composition_replace_end = widget->cursor;
    }
    widget->composing = true;
  }
  widget->composition_text = sanitized;
  const CompositionFocusBytes bytes =
      composition_focus_bytes(sanitized, start_units, length_units);
  widget->composition_focus_start = bytes.start;
  widget->composition_focus_length = bytes.end - bytes.start;
  text_field_refresh_display(tree, fonts, id, *widget, true);
}

void WidgetSet::text_field_cancel_composition(RenderTree& tree, const FontCatalog& fonts,
                                              NodeId id) {
  Widget* widget = find(id);
  if (widget == nullptr || widget->kind != WidgetKind::kTextField || !widget->composing) {
    return;
  }
  reset_composition_fields(*widget);
  text_field_refresh_display(tree, fonts, id, *widget, true);
}

bool WidgetSet::text_field_is_composing(NodeId id) const {
  const Widget* widget = find(id);
  return widget != nullptr && widget->kind == WidgetKind::kTextField && widget->composing;
}

const std::string& WidgetSet::text_field_composition_text(NodeId id) const {
  static const std::string kEmpty;
  const Widget* widget = find(id);
  return (widget != nullptr && widget->kind == WidgetKind::kTextField)
             ? widget->composition_text
             : kEmpty;
}

}  // namespace dg
