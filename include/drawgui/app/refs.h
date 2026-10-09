// Typed handles to one component node of a dg::Window: Window::list(id),
// dropdown(id), tabs(id), tree(id), table(id), progress(id), slider(id) and
// radio(id) check the
// node's kind once and hand back the matching ref.
//
// A ref is a cheap value ({window, node}); copy it freely. It never keeps the
// node alive: every call re-checks it and answers kInvalidNode once the node
// was removed. A ref must not outlive its Window.

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "drawgui/app/items.h"
#include "drawgui/app/status.h"
#include "drawgui/base/expected.h"
#include "drawgui/render/node_id.h"

namespace dg {

class Window;

namespace app_detail {
struct WinState;
}  // namespace app_detail

class ComponentRef {
 public:
  [[nodiscard]] NodeId node() const { return node_; }

 protected:
  ComponentRef(app_detail::WinState* window, NodeId node) : window_(window), node_(node) {}
  [[nodiscard]] app_detail::WinState& window() const { return *window_; }

 private:
  app_detail::WinState* window_;
  NodeId node_;
};

// NodeKind::kList: a virtualised list of text rows.
class ListRef : public ComponentRef {
 public:
  // Replaces every row and clears the selection. `item_height` > 0 changes
  // the row height; 0 keeps it. At most limits::kListMaxItems rows.
  Status set_items(std::vector<std::string> items, int item_height = 0);
  // -1 = none.
  [[nodiscard]] Expected<int, Status> selected() const;
  // -1 deselects. Reports no event.
  Status select(int index);

 private:
  friend class Window;
  using ComponentRef::ComponentRef;
};

// NodeKind::kDropdown. Its placeholder is Window::set_text(DG_PROP_TEXT).
class DropdownRef : public ComponentRef {
 public:
  // Replaces the options (closing the popup) and clears the selection.
  Status set_items(std::vector<std::string> items);
  // -1 = none.
  [[nodiscard]] Expected<int, Status> selected() const;
  // -1 clears. Reports no event.
  Status select(int index);

 private:
  friend class Window;
  using ComponentRef::ComponentRef;
};

// NodeKind::kTabs: a strip of tabs, each with one caller-visible panel box.
class TabsRef : public ComponentRef {
 public:
  // Adds a tab and returns its (created, initially empty) panel node.
  // kValueOutOfRange past limits::kMaxTabs.
  [[nodiscard]] Expected<NodeId, Status> add(std::string label, bool disabled = false);
  [[nodiscard]] Expected<int, Status> selected() const;
  // kValueOutOfRange for a disabled or missing tab. Reports no event.
  Status select(int index);

 private:
  friend class Window;
  using ComponentRef::ComponentRef;
};

// NodeKind::kTree.
class TreeRef : public ComponentRef {
 public:
  // kInvalidArgument when an item is more than one level deeper than the
  // one before it, or past limits::kMaxDataItems.
  Status set_items(std::vector<TreeEntry> items);
  [[nodiscard]] Expected<int, Status> selected() const;
  // -1 deselects. Reports no event.
  Status select(int index);
  Status set_expanded(int index, bool expanded);

 private:
  friend class Window;
  using ComponentRef::ComponentRef;
};

// NodeKind::kTable. A header click on a sortable column reports a
// TableEvent::Kind::kSort request; sorting the rows is the caller's job.
class TableRef : public ComponentRef {
 public:
  // Replaces the columns and drops the rows and the sort indicator.
  // kInvalidArgument for none or more than limits::kMaxTableColumns.
  Status set_columns(std::vector<TableColumnSpec> columns);
  [[nodiscard]] Expected<int, Status> column_count() const;
  // One string per column per row (a missing cell shows empty).
  Status set_rows(std::vector<std::vector<std::string>> rows);
  [[nodiscard]] Expected<int, Status> selected() const;
  // -1 deselects. Reports no event.
  Status select(int row);
  // nullopt = no indicator.
  Status set_sort_indicator(std::optional<int> column, bool ascending);

 private:
  friend class Window;
  using ComponentRef::ComponentRef;
};

// NodeKind::kProgress. The same values as DG_PROP_VALUE/DG_PROP_INDETERMINATE.
class ProgressRef : public ComponentRef {
 public:
  // 0..1; kValueOutOfRange otherwise.
  Status set_value(float value);
  [[nodiscard]] Expected<float, Status> value() const;
  Status set_indeterminate(bool indeterminate);
  [[nodiscard]] Expected<bool, Status> indeterminate() const;

 private:
  friend class Window;
  using ComponentRef::ComponentRef;
};

// NodeKind::kSlider: a horizontal track whose thumb a drag, a press on the
// track or the keys (Left/Right/Up/Down a step, PageUp/PageDown a tenth of
// the range, Home/End the ends) move. User changes report a SliderEvent;
// these calls report none. The same values as DG_PROP_VALUE/MIN_VALUE/
// MAX_VALUE/STEP. Defaults: 0..100, step 0 (continuous), value 0.
class SliderRef : public ComponentRef {
 public:
  // Clamped to the range and snapped to the step. kValueOutOfRange for a
  // non-finite value.
  Status set_value(float value);
  [[nodiscard]] Expected<float, Status> value() const;
  // kValueOutOfRange unless min < max (both finite) and step >= 0. The value
  // is re-clamped and re-snapped.
  Status set_range(float min_value, float max_value, float step = 0.0F);
  [[nodiscard]] Expected<float, Status> min_value() const;
  [[nodiscard]] Expected<float, Status> max_value() const;
  [[nodiscard]] Expected<float, Status> step() const;

 private:
  friend class Window;
  using ComponentRef::ComponentRef;
};

// NodeKind::kRadio: a round checkbox in a group (DG_PROP_RADIO_GROUP,
// default 0) of its window; selecting one clears the others in the group.
// A click selects it (a ClickEvent follows); Left/Up and Right/Down on a
// focused radio move focus and the selection to the previous/next enabled
// radio of its group, in creation order, wrapping (a ClickEvent with
// clicks = 0 follows). Window::checked() reads it.
class RadioRef : public ComponentRef {
 public:
  // Selects it (clearing the rest of its group). Reports no event.
  Status select();
  [[nodiscard]] Expected<bool, Status> selected() const;
  // Moves it to another group; kValueOutOfRange outside +-2^24. A selected
  // radio joining a group with a selection is cleared.
  Status set_group(int group);
  [[nodiscard]] Expected<int, Status> group() const;

 private:
  friend class Window;
  using ComponentRef::ComponentRef;
};

}  // namespace dg
