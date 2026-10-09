// NodeKind::kDropdown: a dg::Dropdown anchored on the node itself, with a
// composed label showing the selection (or the placeholder text).

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "app/components/components.h"
#include "app/internal.h"

namespace dg::app_detail {
namespace {

constexpr int kDropdownWidth = 200;
constexpr int kDropdownHeight = 32;
constexpr int kDropdownInset = 10;

}  // namespace

namespace components {

void dropdown_defaults(const AppState& app, dg::BoxStyle& box, dg::NodeStyle& style) {
  const dg::ControlStyle colors = control_style(app);
  box.kind = dg::LayoutKind::kLeaf;
  box.width = kDropdownWidth;
  box.height = kDropdownHeight;
  style.fill = colors.surface;
  style.border_color = colors.border;
  style.border_width = dg::BorderWidths::all(1.0F);
  style.radii = dg::Radii::all(static_cast<float>(colors.radius));
}

void compose_dropdown(WinState& window, NodeRecord& record) {
  DropdownState& state = record.state.emplace<DropdownState>();
  dg::BoxStyle label_box;
  label_box.percent.width = 100.0F;
  label_box.percent.height = 100.0F;
  dg::NodeStyle label_style;
  label_style.text.font = window.app.default_font;
  label_style.text.size = default_text_size();
  label_style.text.align = dg::TextAlign::kLeft;
  label_style.text.inset = kDropdownInset;
  dg::Widget widget;
  widget.kind = dg::WidgetKind::kDropdown;
  widget.label = window.tree.add_child(record.id, label_box, label_style);
  window.widgets.attach(record.id, widget);
  state.dropdown.emplace(record.id);
  paint_dropdown(window, record);
}

void paint_dropdown(WinState& window, NodeRecord& record) {
  const dg::ControlStyle style = control_style(window.app);
  dg::RenderTree& tree = window.tree.render();
  dg::NodeStyle node_style = tree.style(record.id);
  node_style.border_color = style.border;
  tree.set_style(record.id, node_style);
  dg::Widget widget = window.widgets.at(record.id);
  widget.fill_normal = style.surface;
  widget.fill_hover = style.border;
  widget.fill_pressed = style.border;
  window.widgets.attach(record.id, widget);
  dg::TextStyle text = tree.style(widget.label).text;
  text.color = style.on_surface;
  if (!widget.selected_index.has_value()) {
    text.text = std::get<DropdownState>(record.state).placeholder;
  }
  tree.set_text(widget.label, text);
  window.widgets.refresh(tree, record.id, window.interaction.state_of(record.id));
}

void restyle_state(WinState& window, NodeRecord& record, DropdownState& /*state*/) {
  paint_dropdown(window, record);
}

std::optional<dg::Status> set_text_state(WinState& window, NodeRecord& record,
                                         DropdownState& state, std::string_view text) {
  state.placeholder = std::string(text);
  paint_dropdown(window, record);
  return dg::Status::kOk;
}

}  // namespace components
}  // namespace dg::app_detail

namespace dg {

using app_detail::DropdownState;
using app_detail::components::component_of;

Status DropdownRef::set_items(std::vector<std::string> items) {
  app_detail::WinState& win = window();
  const auto found = component_of<DropdownState>(win, node());
  if (found.status != Status::kOk) {
    return found.status;
  }
  if (items.size() > limits::kMaxMenuItems) {
    return Status::kInvalidArgument;
  }
  app_detail::NodeRecord& record = *found.record;
  dg::UiContext ctx = app_detail::components::ui(win);
  if (found.state->dropdown.has_value()) {
    found.state->dropdown->close(ctx);
  }
  dg::Widget widget = win.widgets.at(record.id);
  widget.options = items;
  widget.selected_index.reset();
  win.widgets.attach(record.id, widget);
  if (const dg::FontCatalog* fonts = app_detail::components::fonts_of(win.app)) {
    win.widgets.dropdown_set_options(win.tree.render(), *fonts, record.id, std::move(items));
  }
  app_detail::components::paint_dropdown(win, record);
  app_detail::report_focus_drift(win);
  return Status::kOk;
}

Expected<int, Status> DropdownRef::selected() const {
  const app_detail::WinState& win = window();
  const auto found = component_of<DropdownState>(win, node());
  if (found.status != Status::kOk) {
    return Unexpected(found.status);
  }
  return win.widgets.dropdown_selected_index(found.record->id).value_or(-1);
}

Status DropdownRef::select(int index) {
  app_detail::WinState& win = window();
  const auto found = component_of<DropdownState>(win, node());
  if (found.status != Status::kOk) {
    return found.status;
  }
  const dg::NodeId id = found.record->id;
  const auto count = static_cast<int>(win.widgets.dropdown_options(id).size());
  if (index < -1 || index >= count) {
    return Status::kValueOutOfRange;
  }
  const dg::FontCatalog* fonts = app_detail::components::fonts_of(win.app);
  if (index >= 0 && fonts != nullptr) {
    (void)win.widgets.dropdown_select(win.tree.render(), *fonts, id, index);
    return Status::kOk;
  }
  dg::Widget widget = win.widgets.at(id);
  widget.selected_index = index >= 0 ? std::optional<int>(index) : std::nullopt;
  win.widgets.attach(id, widget);
  app_detail::components::paint_dropdown(win, *found.record);
  return Status::kOk;
}

}  // namespace dg
