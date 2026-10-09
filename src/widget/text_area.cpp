// WidgetKind::kTextArea: the multi-line editor's WidgetSet functions. The
// model operations are the shared editing core (text_edit.h); everything here
// is projecting that model onto the area's children through dg::Paragraph's
// line geometry, and the vertical moves that need that geometry.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "drawgui/render/paragraph.h"
#include "drawgui/widget/widget_set.h"

namespace dg {
namespace {

constexpr int kCaretWidthPx = 2;
constexpr int kUnderlineHeightPx = 2;
// A selected '\n' at the end of an EMPTY line has no glyph to highlight; it
// gets this small fixed-width block so the selection visibly covers it.
constexpr int kEmptyLineSelectionPx = 6;

int round_px(float value) {
  return static_cast<int>(std::lround(static_cast<double>(value)));
}

// The wrapped paragraph of `text` at `width`, cached on the widget. An empty
// text is laid out as one space so the caret still gets a line's metrics;
// every caller checks `text.empty()` before asking for anything but the
// caret at offset 0.
const Paragraph* ensure_layout(const FontCatalog& fonts, const TextStyle& base, Widget& widget,
                               const std::string& text, int width) {
  if (widget.layout && widget.layout_text == text && widget.layout_width == width) {
    return widget.layout.get();
  }
  widget.layout.reset();
  TextStyle style = base;
  style.text = text.empty() ? std::string(" ") : text;
  style.wrap = true;
  style.max_lines = 0;
  style.inset = 0;
  style.align = TextAlign::kLeft;
  Expected<Paragraph, FontError> built =
      Paragraph::build(fonts, style, static_cast<float>(std::max(1, width)));
  if (!built.has_value()) {
    return nullptr;
  }
  widget.layout = std::make_shared<const Paragraph>(std::move(built).value());
  widget.layout_text = text;
  widget.layout_width = width;
  return widget.layout.get();
}

bool soft_wrapped(const std::vector<LineInfo>& lines, std::size_t index) {
  return index + 1 < lines.size() && lines[index].end_excl_newline == lines[index].end_incl;
}

int line_at_y(const std::vector<LineInfo>& lines, float y) {
  for (std::size_t i = 0; i < lines.size(); ++i) {
    if (y < lines[i].top + lines[i].height) {
      return static_cast<int>(i);
    }
  }
  return static_cast<int>(lines.size()) - 1;
}

// One rectangle per visible line covering [lo, hi) - paragraph coordinates.
// `underline` draws a thin bar at each line's bottom instead of a block and
// skips the empty-line newline marker.
std::vector<PixelRect> line_segments(const Paragraph& para, int lo, int hi, float visible_top,
                                     float visible_bottom, bool underline) {
  std::vector<PixelRect> out;
  const std::vector<LineInfo> lines = para.lines();
  for (const LineInfo& line : lines) {
    if (line.top + line.height < visible_top || line.top > visible_bottom) {
      continue;
    }
    const int a = std::max(lo, line.start);
    const int b = std::min(hi, line.end_excl_newline);
    int x0 = 0;
    int x1 = 0;
    if (a < b) {
      const std::vector<PixelRectF> rects = para.range_rects(a, b);
      if (rects.empty()) {
        continue;
      }
      float left = rects.front().x;
      float right = rects.front().x + rects.front().width;
      for (const PixelRectF& r : rects) {
        left = std::min(left, r.x);
        right = std::max(right, r.x + r.width);
      }
      x0 = round_px(left);
      x1 = round_px(right);
    } else if (!underline && line.start == line.end_excl_newline &&
               line.end_incl > line.start && lo <= line.start && line.start < hi) {
      x0 = round_px(para.caret_rect(line.start).x);
      x1 = x0 + kEmptyLineSelectionPx;
    } else {
      continue;
    }
    const int top = round_px(line.top);
    const int height = round_px(line.height);
    if (underline) {
      out.push_back(
          PixelRect{x0, top + height - kUnderlineHeightPx, x1 - x0, kUnderlineHeightPx});
    } else {
      out.push_back(PixelRect{x0, top, x1 - x0, height});
    }
  }
  return out;
}

void fill_pool(RenderTree& tree, const std::vector<NodeId>& pool,
               const std::vector<PixelRect>& rects, int dx, int dy) {
  for (std::size_t i = 0; i < pool.size(); ++i) {
    PixelRect target{};
    if (i < rects.size()) {
      const PixelRect& r = rects[i];
      target = PixelRect{r.x + dx, r.y + dy, r.width, r.height};
    }
    if (tree.local_bounds(pool[i]) != target) {
      tree.set_local_bounds(pool[i], target);
    }
  }
}

std::vector<PixelRect> pool_rects(const RenderTree& tree, const std::vector<NodeId>& pool) {
  std::vector<PixelRect> out;
  for (const NodeId node : pool) {
    const PixelRect r = tree.local_bounds(node);
    if (r.width > 0 && r.height > 0) {
      out.push_back(r);
    }
  }
  return out;
}

// Where a caret move lands: the offset, its soft-wrap affinity, and (for
// PageUp/PageDown) how far the view should scroll along with it.
struct CaretTarget {
  int offset = 0;
  bool upstream = false;
  int page_scroll = 0;
};

// Home/End on the caret's VISUAL line. End on a soft-wrapped line lands on
// the wrap boundary with upstream affinity, so it stays on that row.
CaretTarget line_edge_target(const Paragraph& para, int cursor, bool upstream, bool end) {
  const std::vector<LineInfo> lines = para.lines();
  const int line = para.caret_rect(cursor, upstream).line;
  const LineInfo& info = lines[static_cast<std::size_t>(line)];
  if (!end) {
    return CaretTarget{info.start, false, 0};
  }
  const bool soft = soft_wrapped(lines, static_cast<std::size_t>(line));
  const int target = soft ? info.end_incl : info.end_excl_newline;
  return CaretTarget{target, soft && target > info.start, 0};
}

// Up/Down/PageUp/PageDown at TextEditState::preferred_x (set from the caret
// on the first vertical move). Past the first/last line: the text's ends.
CaretTarget vertical_target(const Paragraph& para, TextEditState& edit, bool upstream,
                            TextAreaMove move, int page) {
  const bool up = move == TextAreaMove::kLineUp || move == TextAreaMove::kPageUp;
  const bool paging = move == TextAreaMove::kPageUp || move == TextAreaMove::kPageDown;
  const std::vector<LineInfo> lines = para.lines();
  const int last = static_cast<int>(lines.size()) - 1;
  const CaretRect caret = para.caret_rect(edit.cursor, upstream);
  const float x = edit.preferred_x.value_or(caret.x);
  edit.preferred_x = x;
  if (up && caret.line == 0) {
    return CaretTarget{0, false, 0};
  }
  if (!up && caret.line == last) {
    return CaretTarget{static_cast<int>(edit.text.size()), false, 0};
  }
  CaretTarget target;
  float y = 0.0F;
  if (paging) {
    target.page_scroll = up ? -page : page;
    y = caret.top + (caret.height * 0.5F) + static_cast<float>(target.page_scroll);
  } else {
    const int next_line = caret.line + (up ? -1 : 1);
    const LineInfo& next = lines[static_cast<std::size_t>(next_line)];
    y = next.top + (next.height * 0.5F);
  }
  const int line = line_at_y(lines, y);
  const LineInfo& info = lines[static_cast<std::size_t>(line)];
  target.offset = para.offset_at(x, info.top + (info.height * 0.5F));
  target.upstream =
      target.offset == info.end_incl && soft_wrapped(lines, static_cast<std::size_t>(line));
  return target;
}

}  // namespace

int WidgetSet::text_area_wrap_width(const RenderTree& tree, NodeId id,
                                    const Widget& widget) const {
  const PixelRect bounds = tree.local_bounds(id);
  const bool gutter = widget.scrollbar_track != RenderTree::root() &&
                      tree.style(id).scrollbar != ScrollbarMode::kNever;
  return std::max(1, bounds.width - (2 * widget.text_inset) -
                         (gutter ? std::max(0, scrollbar_style_.size) : 0));
}

void WidgetSet::text_area_refresh_display(RenderTree& tree, const FontCatalog& fonts, NodeId id,
                                          Widget& widget, bool reveal_caret) const {
  const int inset = widget.text_inset;
  const int width = text_area_wrap_width(tree, id, widget);
  const int viewport = std::max(0, tree.local_bounds(id).height);

  const text_edit::Projection projection =
      widget.focused ? text_edit::display_projection(widget.edit)
                     : text_edit::Projection{widget.edit.text, widget.edit.cursor, std::nullopt,
                                             std::nullopt};
  TextStyle style = tree.style(widget.content).text;
  const Paragraph* para = ensure_layout(fonts, style, widget, projection.text, width);

  int text_height = 0;
  if (para != nullptr) {
    const std::vector<LineInfo> lines = para->lines();
    text_height = round_px(lines.back().top + lines.back().height);
    text_height = std::max(text_height, para->metrics().height);
  }
  widget.content_height = text_height + (2 * inset);

  style.text = projection.text;
  style.wrap = true;
  style.align = TextAlign::kLeft;
  style.inset = 0;
  tree.set_text(widget.content, style);
  const PixelRect content_box{inset, inset, width, std::max(1, text_height)};
  if (tree.local_bounds(widget.content) != content_box) {
    tree.set_local_bounds(widget.content, content_box);
  }
  if (widget.selection_layer != RenderTree::root()) {
    const PixelRect layer{0, 0, width + (2 * inset), widget.content_height};
    if (tree.local_bounds(widget.selection_layer) != layer) {
      tree.set_local_bounds(widget.selection_layer, layer);
    }
  }

  // Caret, then scroll so it is visible, then the pools (which only cover
  // the lines the final scroll position shows).
  PixelRect caret{};
  if (widget.focused && para != nullptr) {
    const bool upstream = widget.caret_upstream && !widget.edit.composing;
    const CaretRect c =
        para->caret_rect(projection.text.empty() ? 0 : projection.cursor, upstream);
    caret = PixelRect{inset + std::clamp(round_px(c.x), 0, std::max(0, width - kCaretWidthPx)),
                      inset + round_px(c.top), kCaretWidthPx, std::max(1, round_px(c.height))};
  }
  if (tree.local_bounds(widget.caret) != caret) {
    tree.set_local_bounds(widget.caret, caret);
  }

  const int max_offset = std::max(0, widget.content_height - viewport);
  int offset = tree.scroll_offset(id).y;
  if (reveal_caret && widget.focused && caret.height > 0) {
    if (caret.y - inset < offset) {
      offset = caret.y - inset;
    } else if (caret.y + caret.height + inset > offset + viewport) {
      offset = caret.y + caret.height + inset - viewport;
    }
  }
  offset = std::clamp(offset, 0, max_offset);
  if (tree.scroll_offset(id) != PixelPoint{0, offset}) {
    tree.set_scroll_offset(id, PixelPoint{0, offset});
  }
  sync_scrollbar(tree, id, viewport);

  const float visible_top = static_cast<float>(offset - inset);
  const float visible_bottom = static_cast<float>(offset + viewport - inset);
  std::vector<PixelRect> highlights;
  std::vector<PixelRect> underlines;
  if (widget.focused && para != nullptr && !projection.text.empty()) {
    if (projection.highlight.has_value()) {
      highlights = line_segments(*para, projection.highlight->start, projection.highlight->end,
                                 visible_top, visible_bottom, false);
    }
    if (projection.underline.has_value()) {
      underlines = line_segments(*para, projection.underline->start, projection.underline->end,
                                 visible_top, visible_bottom, true);
    }
  }
  fill_pool(tree, widget.highlight_pool, highlights, inset, inset);
  fill_pool(tree, widget.underline_pool, underlines, inset, inset);
}

TextEditState* WidgetSet::text_area_state(NodeId id) {
  Widget* widget = find(id);
  return (widget != nullptr && widget->kind == WidgetKind::kTextArea) ? &widget->edit : nullptr;
}

bool WidgetSet::text_area_commit(RenderTree& tree, const FontCatalog& fonts, NodeId id,
                                 bool changed, bool reveal_caret) {
  if (changed) {
    text_area_refresh_display(tree, fonts, id, *find(id), reveal_caret);
  }
  return changed;
}

const std::string& WidgetSet::text_area_text(NodeId id) const {
  static const std::string kEmpty;
  const Widget* widget = find(id);
  return (widget != nullptr && widget->kind == WidgetKind::kTextArea) ? widget->edit.text
                                                                      : kEmpty;
}

int WidgetSet::text_area_cursor(NodeId id) const {
  const Widget* widget = find(id);
  return (widget != nullptr && widget->kind == WidgetKind::kTextArea) ? widget->edit.cursor : 0;
}

std::optional<TextSelection> WidgetSet::text_area_selection(NodeId id) const {
  const Widget* widget = find(id);
  if (widget == nullptr || widget->kind != WidgetKind::kTextArea) {
    return std::nullopt;
  }
  return text_edit::selection(widget->edit);
}

bool WidgetSet::text_area_insert(RenderTree& tree, const FontCatalog& fonts, NodeId id,
                                 std::string_view input) {
  Widget* widget = find(id);
  if (widget == nullptr || widget->kind != WidgetKind::kTextArea) {
    return false;
  }
  widget->edit.preferred_x.reset();
  widget->caret_upstream = false;
  return text_area_commit(tree, fonts, id, text_edit::insert(widget->edit, input, true));
}

bool WidgetSet::text_area_backspace(RenderTree& tree, const FontCatalog& fonts, NodeId id) {
  TextEditState* state = text_area_state(id);
  if (state == nullptr) {
    return false;
  }
  state->preferred_x.reset();
  find(id)->caret_upstream = false;
  return text_area_commit(tree, fonts, id, text_edit::backspace(*state));
}

bool WidgetSet::text_area_delete_forward(RenderTree& tree, const FontCatalog& fonts,
                                         NodeId id) {
  TextEditState* state = text_area_state(id);
  if (state == nullptr) {
    return false;
  }
  state->preferred_x.reset();
  find(id)->caret_upstream = false;
  return text_area_commit(tree, fonts, id, text_edit::delete_forward(*state));
}

bool WidgetSet::text_area_select_all(RenderTree& tree, const FontCatalog& fonts, NodeId id) {
  TextEditState* state = text_area_state(id);
  if (state == nullptr) {
    return false;
  }
  state->preferred_x.reset();
  find(id)->caret_upstream = false;
  return text_area_commit(tree, fonts, id, text_edit::select_all(*state));
}

bool WidgetSet::text_area_move(RenderTree& tree, const FontCatalog& fonts, NodeId id,
                               TextAreaMove move, bool extend_selection) {
  Widget* widget = find(id);
  if (widget == nullptr || widget->kind != WidgetKind::kTextArea || widget->edit.composing) {
    return false;
  }
  TextEditState& edit = widget->edit;
  const int size = static_cast<int>(edit.text.size());
  const bool old_upstream = widget->caret_upstream;
  const bool vertical = move == TextAreaMove::kLineUp || move == TextAreaMove::kLineDown ||
                        move == TextAreaMove::kPageUp || move == TextAreaMove::kPageDown;
  if (!vertical) {
    edit.preferred_x.reset();
  }

  const Paragraph* para =
      edit.text.empty() ? nullptr
                        : ensure_layout(fonts, tree.style(widget->content).text, *widget,
                                        edit.text, text_area_wrap_width(tree, id, *widget));

  bool changed = false;
  bool upstream = false;
  int page_scroll = 0;
  switch (move) {
    case TextAreaMove::kCharLeft:
    case TextAreaMove::kCharRight:
      changed =
          text_edit::move_horizontal(edit, move == TextAreaMove::kCharLeft, extend_selection);
      break;
    case TextAreaMove::kDocStart:
      changed = text_edit::set_cursor(edit, 0, extend_selection);
      break;
    case TextAreaMove::kDocEnd:
      changed = text_edit::set_cursor(edit, size, extend_selection);
      break;
    case TextAreaMove::kLineStart:
    case TextAreaMove::kLineEnd:
      if (para != nullptr) {
        const CaretTarget target =
            line_edge_target(*para, edit.cursor, old_upstream, move == TextAreaMove::kLineEnd);
        changed = text_edit::set_cursor(edit, target.offset, extend_selection);
        upstream = target.upstream;
      }
      break;
    case TextAreaMove::kLineUp:
    case TextAreaMove::kLineDown:
    case TextAreaMove::kPageUp:
    case TextAreaMove::kPageDown:
      if (para != nullptr) {
        const int page = std::max(1, tree.local_bounds(id).height - (2 * widget->text_inset));
        const CaretTarget target = vertical_target(*para, edit, old_upstream, move, page);
        changed = text_edit::set_cursor(edit, target.offset, extend_selection);
        upstream = target.upstream;
        page_scroll = target.page_scroll;
      }
      break;
  }
  widget->caret_upstream = upstream;
  changed = changed || upstream != old_upstream;
  if (page_scroll != 0) {
    const PixelPoint offset = tree.scroll_offset(id);
    const int max_offset = std::max(0, widget->content_height - tree.local_bounds(id).height);
    const PixelPoint paged{0, std::clamp(offset.y + page_scroll, 0, max_offset)};
    if (paged != offset) {
      tree.set_scroll_offset(id, paged);
      changed = true;
    }
  }
  return text_area_commit(tree, fonts, id, changed);
}

bool WidgetSet::text_area_click(RenderTree& tree, const FontCatalog& fonts, NodeId id,
                                PixelPoint pointer, bool extend_selection) {
  Widget* widget = find(id);
  if (widget == nullptr || widget->kind != WidgetKind::kTextArea) {
    return false;
  }
  const bool cancelled = text_edit::cancel_composition(widget->edit);
  widget->edit.preferred_x.reset();
  const bool old_upstream = widget->caret_upstream;
  widget->caret_upstream = false;
  int offset = 0;
  if (!widget->edit.text.empty()) {
    const Paragraph* para =
        ensure_layout(fonts, tree.style(widget->content).text, *widget, widget->edit.text,
                      text_area_wrap_width(tree, id, *widget));
    if (para != nullptr) {
      const PixelRect area = tree.absolute_bounds(id);
      const PixelPoint scroll = tree.scroll_offset(id);
      const float x = static_cast<float>(pointer.x - area.x - widget->text_inset);
      const float y = static_cast<float>(pointer.y - area.y + scroll.y - widget->text_inset);
      offset = para->offset_at(x, y);
      const std::vector<LineInfo> lines = para->lines();
      const int line = line_at_y(lines, y);
      widget->caret_upstream = offset == lines[static_cast<std::size_t>(line)].end_incl &&
                               soft_wrapped(lines, static_cast<std::size_t>(line));
    }
  }
  const bool moved = text_edit::set_cursor(widget->edit, offset, extend_selection);
  return text_area_commit(tree, fonts, id,
                          moved || cancelled || old_upstream != widget->caret_upstream);
}

void WidgetSet::text_area_set_focus(RenderTree& tree, const FontCatalog& fonts, NodeId id,
                                    bool focused) {
  Widget* widget = find(id);
  if (widget == nullptr || widget->kind != WidgetKind::kTextArea) {
    return;
  }
  if (!focused) {
    widget->edit.selection_anchor.reset();
    widget->edit.preferred_x.reset();
    text_edit::cancel_composition(widget->edit);
  }
  widget->focused = focused;
  text_area_refresh_display(tree, fonts, id, *widget, focused);
}

void WidgetSet::text_area_composition_update(RenderTree& tree, const FontCatalog& fonts,
                                             NodeId id, std::string_view text, int start_units,
                                             int length_units) {
  TextEditState* state = text_area_state(id);
  if (state != nullptr) {
    state->preferred_x.reset();
    text_area_commit(tree, fonts, id,
                     text_edit::composition_update(*state, text, start_units, length_units));
  }
}

void WidgetSet::text_area_cancel_composition(RenderTree& tree, const FontCatalog& fonts,
                                             NodeId id) {
  TextEditState* state = text_area_state(id);
  if (state != nullptr) {
    text_area_commit(tree, fonts, id, text_edit::cancel_composition(*state));
  }
}

void WidgetSet::text_area_refresh(RenderTree& tree, const FontCatalog& fonts, NodeId id) {
  Widget* widget = find(id);
  if (widget != nullptr && widget->kind == WidgetKind::kTextArea) {
    text_area_refresh_display(tree, fonts, id, *widget, false);
  }
}

std::optional<PixelRect> WidgetSet::text_area_caret_rect(const RenderTree& tree,
                                                         NodeId id) const {
  const Widget* widget = find(id);
  if (widget == nullptr || widget->kind != WidgetKind::kTextArea || !widget->focused) {
    return std::nullopt;
  }
  return tree.absolute_bounds(widget->caret);
}

std::vector<PixelRect> WidgetSet::text_area_highlight_rects(const RenderTree& tree,
                                                            NodeId id) const {
  const Widget* widget = find(id);
  if (widget == nullptr || widget->kind != WidgetKind::kTextArea) {
    return {};
  }
  return pool_rects(tree, widget->highlight_pool);
}

std::vector<PixelRect> WidgetSet::text_area_underline_rects(const RenderTree& tree,
                                                            NodeId id) const {
  const Widget* widget = find(id);
  if (widget == nullptr || widget->kind != WidgetKind::kTextArea) {
    return {};
  }
  return pool_rects(tree, widget->underline_pool);
}

}  // namespace dg
