#include "drawgui/ui/tree_view.h"

#include <utility>

#include "drawgui/widget/focus.h"
#include "drawgui/widget/interaction.h"
#include "drawgui/widget/widget_set.h"
#include "ui/control_visuals.h"
#include "ui/row_list.h"

namespace dg {

void TreeView::build(ControlContext& ctx, NodeId parent, TreeViewSpec spec) {
  destroy(ctx);
  control_visuals::RowListNodes nodes = control_visuals::build_tree(ctx, parent, spec);
  root_ = nodes.root;
  list_ = nodes.list;
  track_ = nodes.track;
  thumb_ = nodes.thumb;
  pool_ = std::move(nodes.pool);
  items_ = std::move(spec.items);
  rich_ = !pool_.empty() && ctx.tree().children(pool_.front()).size() >= 5;
  built_ = true;
  rebuild_visible();
  reset_rows(ctx);
}

void TreeView::destroy(ControlContext& ctx) {
  if (built_ && ctx.tree().is_valid(root_)) {
    if (dragging_) {
      ctx.tables.widgets.scrollbar_release();
    }
    remove_subtree(ctx.layout, root_, ctx.tables);
  }
  built_ = false;
  rich_ = false;
  dragging_ = false;
  items_.clear();
  visible_.clear();
  pool_.clear();
  selected_.reset();
}

bool TreeView::has_children(int item) const {
  const auto next = static_cast<std::size_t>(item) + 1;
  return item >= 0 && next < items_.size() &&
         items_[next].depth > items_[static_cast<std::size_t>(item)].depth;
}

bool TreeView::is_expanded(int item) const {
  return has_children(item) && items_[static_cast<std::size_t>(item)].expanded;
}

int TreeView::parent_of(int item) const {
  if (item < 0 || static_cast<std::size_t>(item) >= items_.size()) {
    return -1;
  }
  const int depth = items_[static_cast<std::size_t>(item)].depth;
  for (int i = item - 1; i >= 0; --i) {
    if (items_[static_cast<std::size_t>(i)].depth < depth) {
      return i;
    }
  }
  return -1;
}

void TreeView::rebuild_visible() {
  visible_.clear();
  // Items deeper than `hidden_below` sit under a collapsed ancestor.
  int hidden_below = -1;
  for (std::size_t i = 0; i < items_.size(); ++i) {
    const int depth = items_[i].depth;
    if (hidden_below >= 0 && depth > hidden_below) {
      continue;
    }
    hidden_below = -1;
    visible_.push_back(static_cast<int>(i));
    if (has_children(static_cast<int>(i)) && !items_[i].expanded) {
      hidden_below = depth;
    }
  }
}

std::optional<int> TreeView::row_of(int item) const {
  for (std::size_t r = 0; r < visible_.size(); ++r) {
    if (visible_[r] == item) {
      return static_cast<int>(r);
    }
  }
  return std::nullopt;
}

int TreeView::viewport(const ControlContext& ctx) const {
  return row_list::viewport(ctx, list_);
}

void TreeView::reset_rows(ControlContext& ctx) {
  ctx.tables.widgets.list_reset(ctx.tree(), list_, static_cast<int>(visible_.size()),
                                viewport(ctx));
  repaint(ctx);
}

void TreeView::repaint(ControlContext& ctx) {
  const Widget& list = ctx.tables.widgets.at(list_);
  for (std::size_t slot = 0; slot < list.list_pool.size() && slot < list.list_assigned.size();
       ++slot) {
    const int row = list.list_assigned[slot];
    if (row < 0 || static_cast<std::size_t>(row) >= visible_.size()) {
      continue;
    }
    const int item = visible_[static_cast<std::size_t>(row)];
    control_visuals::paint_tree_row(ctx, list.list_pool[slot],
                                    items_[static_cast<std::size_t>(item)], has_children(item),
                                    is_expanded(item), selected_ == item);
  }
}

void TreeView::sync(ControlContext& ctx) {
  if (built_) {
    reset_rows(ctx);
  }
}

void TreeView::reveal(ControlContext& ctx, int row) {
  row_list::reveal(ctx, list_, row);
}

bool TreeView::select(ControlContext& ctx, int item) {
  if (!built_ || item < 0 || static_cast<std::size_t>(item) >= items_.size() ||
      selected_ == item) {
    return false;
  }
  bool expanded_any = false;
  for (int p = parent_of(item); p >= 0; p = parent_of(p)) {
    if (!items_[static_cast<std::size_t>(p)].expanded) {
      items_[static_cast<std::size_t>(p)].expanded = true;
      expanded_any = true;
    }
  }
  selected_ = item;
  if (expanded_any) {
    rebuild_visible();
    reset_rows(ctx);
  }
  reveal(ctx, row_of(item).value_or(0));
  repaint(ctx);
  return true;
}

bool TreeView::set_expanded(ControlContext& ctx, int item, bool expanded) {
  if (!built_ || !has_children(item) ||
      items_[static_cast<std::size_t>(item)].expanded == expanded) {
    return false;
  }
  items_[static_cast<std::size_t>(item)].expanded = expanded;
  if (!expanded && selected_.has_value()) {
    for (int p = parent_of(*selected_); p >= 0; p = parent_of(p)) {
      if (p == item) {
        selected_ = item;
        break;
      }
    }
  }
  rebuild_visible();
  reset_rows(ctx);
  return true;
}

bool TreeView::toggle_check(ControlContext& ctx, int item) {
  if (!built_ || item < 0 || static_cast<std::size_t>(item) >= items_.size()) {
    return false;
  }
  std::optional<bool>& check = items_[static_cast<std::size_t>(item)].check;
  if (!check.has_value()) {
    return false;
  }
  check = !*check;
  repaint(ctx);
  return true;
}

std::optional<TreeView::Hit> TreeView::hit(const ControlContext& ctx, PixelPoint point) const {
  if (!built_) {
    return std::nullopt;
  }
  const std::optional<int> row =
      row_list::row_at(ctx, list_, point, static_cast<int>(visible_.size()));
  if (!row.has_value()) {
    return std::nullopt;
  }
  Hit out;
  out.item = visible_[static_cast<std::size_t>(*row)];
  if (rich_ && items_[static_cast<std::size_t>(out.item)].check.has_value()) {
    const RenderTree& tree = ctx.tree();
    if (const std::optional<NodeId> node = tree.hit_test(point)) {
      const NodeId parent = tree.parent(*node);
      const std::vector<NodeId> kids = tree.children(parent);
      out.check = kids.size() >= 5 && kids[2] == *node;
    }
  }
  return out;
}

TreeResult TreeView::select_row(ControlContext& ctx, int row) {
  const int item = visible_[static_cast<std::size_t>(row)];
  if (selected_ == item) {
    reveal(ctx, row);
    repaint(ctx);
    return TreeResult{TreeEvent::kHandled, item};
  }
  selected_ = item;
  reveal(ctx, row);
  repaint(ctx);
  return TreeResult{TreeEvent::kSelected, item};
}

TreeResult TreeView::handle_pointer(ControlContext& ctx, const PointerEvent& event) {
  if (!built_) {
    return {};
  }
  bool scrolled = false;
  if (row_list::scroll_pointer(ctx, root_, list_, event, dragging_, scrolled)) {
    if (scrolled) {
      repaint(ctx);
    }
    return TreeResult{TreeEvent::kHandled, -1};
  }
  if (event.action != PointerAction::kDown || event.button != PointerButton::kPrimary) {
    return {};
  }
  const PixelPoint at{event.x, event.y};
  if (ctx.tables.widgets.widget_at(ctx.tree(), at) != root_) {
    return {};
  }
  ctx.tables.focus.set_guarded(ctx.tree(), root_);
  const std::optional<int> row =
      row_list::row_at(ctx, list_, at, static_cast<int>(visible_.size()));
  if (!row.has_value()) {
    return TreeResult{TreeEvent::kHandled, -1};
  }
  const int item = visible_[static_cast<std::size_t>(*row)];
  const int x = at.x - ctx.tree().absolute_bounds(list_).x;
  const int disclosure =
      control_visuals::tree_disclosure_x(ctx, items_[static_cast<std::size_t>(item)].depth);
  if (has_children(item) && x >= disclosure && x < disclosure + ctx.style.indent) {
    const bool expand = !is_expanded(item);
    set_expanded(ctx, item, expand);
    return TreeResult{expand ? TreeEvent::kExpanded : TreeEvent::kCollapsed, item};
  }
  return select_row(ctx, *row);
}

TreeResult TreeView::handle_key(ControlContext& ctx, const KeyEvent& event) {
  if (!built_ || !row_list::owns_key(ctx, root_, event)) {
    return {};
  }
  const std::optional<int> current = selected_.has_value() ? row_of(*selected_) : std::nullopt;
  if (event.key == Key::kLeft || event.key == Key::kRight) {
    if (!selected_.has_value()) {
      return TreeResult{TreeEvent::kHandled, -1};
    }
    const int item = *selected_;
    if (event.key == Key::kRight) {
      if (has_children(item) && !is_expanded(item)) {
        set_expanded(ctx, item, true);
        return TreeResult{TreeEvent::kExpanded, item};
      }
      if (is_expanded(item) && current.has_value()) {
        return select_row(ctx, *current + 1);
      }
      return TreeResult{TreeEvent::kHandled, item};
    }
    if (is_expanded(item)) {
      set_expanded(ctx, item, false);
      return TreeResult{TreeEvent::kCollapsed, item};
    }
    const int parent = parent_of(item);
    if (parent >= 0) {
      if (const std::optional<int> row = row_of(parent)) {
        return select_row(ctx, *row);
      }
    }
    return TreeResult{TreeEvent::kHandled, item};
  }
  const std::optional<int> target =
      row_list::nav_target(ctx, list_, event.key, current, static_cast<int>(visible_.size()));
  if (!target.has_value()) {
    return {};
  }
  return select_row(ctx, *target);
}

}  // namespace dg
