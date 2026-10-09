// NodeKind::kSlider and kRadio: the WidgetSet kSlider and grouped kCheckbox
// widgets examples/11_form_controls built by hand, composed by the runtime.
// A slider is a track (the node) and a thumb (its one composed child) that
// a press, a drag or the keys move; a radio button is a round checkbox with
// Widget::group set, whose click path is the checkbox's own (pointer.cpp)
// and whose arrow keys move the selection through its group.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include "app/components/components.h"
#include "app/internal.h"

namespace dg::app_detail::components {
namespace {

constexpr int kSliderWidth = 200;
constexpr int kSliderHeight = 20;
constexpr float kSliderDefaultMax = 100.0F;
// Keyboard steps of a continuous slider (step 0): 1/100 of the range per
// arrow, and 1/10 per PageUp/PageDown for every slider.
constexpr float kArrowFraction = 0.01F;
constexpr float kPageFraction = 0.1F;

// The radio button: the checkbox's theme colours (style_control()), round.
constexpr int kRadioSize = 32;
constexpr int kRadioDot = 20;

// The track and thumb fills from the active theme (color.border and
// color.primary through dg::ControlStyle), both fully rounded.
void paint_slider(WinState& window, const NodeRecord& record) {
  if (!window.widgets.has(record.id)) {
    return;
  }
  const dg::ControlStyle style = control_style(window.app);
  dg::RenderTree& tree = window.tree.render();
  const dg::NodeId thumb = window.widgets.at(record.id).thumb;
  const float radius = static_cast<float>(wrapper_height(window, record, kSliderHeight)) / 2.0F;
  dg::NodeStyle track_style = tree.style(record.id);
  track_style.fill = style.border;
  track_style.radii = dg::Radii::all(radius);
  tree.set_style(record.id, track_style);
  dg::NodeStyle thumb_style = tree.style(thumb);
  thumb_style.fill = style.accent;
  thumb_style.radii = dg::Radii::all(radius);
  tree.set_style(thumb, thumb_style);
}

// A user change: the value, then a SliderEvent when it moved.
void move_slider(WinState& window, const NodeRecord& record, float value) {
  const float before = window.widgets.slider_value(record.id);
  if (window.widgets.set_slider_value(window.tree.render(), record.id, value)) {
    emit(window.app, dg::events::SliderEvent{window.sdl_window, record.id,
                                             window.widgets.slider_value(record.id), before});
  }
}

bool usable(const WinState& window, const NodeRecord& record) {
  return window.focus.current() == record.id && window.widgets.has(record.id) &&
         !dg::is_disabled(window.tree.render(), record.id);
}

// The enabled radios of `group`, in creation order.
std::vector<dg::NodeId> radio_group_members(WinState& window, int group) {
  std::vector<dg::NodeId> out;
  for (const NodeRecord* record : records_in_order(window)) {
    if (record->kind == NodeKind::kRadio && window.widgets.has(record->id) &&
        window.widgets.at(record->id).group == group &&
        !dg::is_disabled(window.tree.render(), record->id)) {
      out.push_back(record->id);
    }
  }
  return out;
}

}  // namespace

// Selects radio `id` and repaints it and every group member it cleared.
// Returns whether it was not selected before.
bool select_radio(WinState& window, dg::NodeId id) {
  const bool was = window.widgets.is_checked(id);
  (void)window.widgets.toggle(id);
  dg::RenderTree& tree = window.tree.render();
  window.widgets.refresh(tree, id, window.interaction.state_of(id));
  for (const dg::NodeId other : window.widgets.group_members(id)) {
    window.widgets.refresh(tree, other, window.interaction.state_of(other));
  }
  return !was;
}

void slider_radio_defaults(NodeKind kind, dg::BoxStyle& box, dg::NodeStyle& style) {
  if (kind == NodeKind::kSlider) {
    box.width = kSliderWidth;
    box.height = kSliderHeight;
    return;
  }
  // The checkbox's 32x32 box, padded so the 20x20 dot sits in its centre.
  box.width = kRadioSize;
  box.height = kRadioSize;
  box.padding = dg::EdgeInsets::all((kRadioSize - kRadioDot) / 2);
  style.border_width = dg::BorderWidths::all(1.0F);
  style.radii = dg::Radii::all(static_cast<float>(kRadioSize) / 2.0F);
}

void compose_slider(WinState& window, NodeRecord& record) {
  record.state.emplace<SliderState>();
  dg::BoxStyle thumb_box;
  thumb_box.width = kSliderHeight;
  thumb_box.height = kSliderHeight;
  const dg::NodeId thumb = window.tree.add_child(record.id, thumb_box, dg::NodeStyle{});
  dg::Widget widget;
  widget.kind = dg::WidgetKind::kSlider;
  widget.thumb = thumb;
  widget.min_value = 0.0F;
  widget.max_value = kSliderDefaultMax;
  widget.value = 0.0F;
  window.widgets.attach(record.id, widget);
  window.widgets.attach(thumb, dg::Widget{});
  paint_slider(window, record);
  window.relaid = true;
}

void compose_radio(WinState& window, NodeRecord& record) {
  record.state.emplace<RadioState>();
  dg::BoxStyle dot_box;
  dot_box.width = kRadioDot;
  dot_box.height = kRadioDot;
  dg::NodeStyle dot_style;
  dot_style.radii = dg::Radii::all(static_cast<float>(kRadioDot) / 2.0F);
  const dg::NodeId dot = window.tree.add_child(record.id, dot_box, dot_style);
  dg::Widget widget;
  widget.kind = dg::WidgetKind::kCheckbox;
  widget.indicator = dot;
  widget.group = 0;
  window.widgets.attach(record.id, widget);
  window.widgets.attach(dot, dg::Widget{});
  style_control(window, record);
}

bool control_pointer(WinState& window, NodeRecord& record, SliderState& state,
                     const dg::PointerEvent& event) {
  dg::RenderTree& tree = window.tree.render();
  const dg::PixelPoint at{event.x, event.y};
  if (event.action == dg::PointerAction::kDown) {
    if (event.button != dg::PointerButton::kPrimary) {
      return false;
    }
    const std::optional<dg::NodeId> hit = tree.hit_test(at);
    if (!hit.has_value() || window.widgets.slidable_owner_of(tree, *hit) != record.id) {
      return false;
    }
    // A press on the track jumps the thumb there and starts a drag; the
    // slider takes focus like any other pressed focusable widget.
    state.dragging = true;
    apply_focus_change(window, window.focus.set(record.id));
    move_slider(window, record, window.widgets.slider_value_at(tree, record.id, at.x));
    return true;
  }
  if (!state.dragging) {
    return false;
  }
  switch (event.action) {
    case dg::PointerAction::kMove:
      if (dg::is_disabled(tree, record.id)) {
        state.dragging = false;
        return false;
      }
      move_slider(window, record, window.widgets.slider_value_at(tree, record.id, at.x));
      return true;
    case dg::PointerAction::kUp:
      if (event.button != dg::PointerButton::kPrimary) {
        return false;
      }
      state.dragging = false;
      return true;
    case dg::PointerAction::kLeave:
      state.dragging = false;
      return false;
    case dg::PointerAction::kDown:
    case dg::PointerAction::kWheel:
      break;
  }
  return false;
}

bool control_key(WinState& window, NodeRecord& record, SliderState& /*state*/,
                 const dg::KeyEvent& event) {
  if (!usable(window, record) || dg::has(event.mods, dg::Modifier::kMod) ||
      dg::has(event.mods, dg::Modifier::kAlt)) {
    return false;
  }
  const dg::Widget& widget = window.widgets.at(record.id);
  const float range = widget.max_value - widget.min_value;
  const float arrow = widget.step > 0.0F ? widget.step : range * kArrowFraction;
  const float page = std::max(arrow, range * kPageFraction);
  const float value = widget.value;
  switch (event.key) {
    case dg::Key::kLeft:
    case dg::Key::kDown:
      move_slider(window, record, value - arrow);
      return true;
    case dg::Key::kRight:
    case dg::Key::kUp:
      move_slider(window, record, value + arrow);
      return true;
    case dg::Key::kPageDown:
      move_slider(window, record, value - page);
      return true;
    case dg::Key::kPageUp:
      move_slider(window, record, value + page);
      return true;
    case dg::Key::kHome:
      move_slider(window, record, widget.min_value);
      return true;
    case dg::Key::kEnd:
      move_slider(window, record, widget.max_value);
      return true;
    case dg::Key::kEscape:
    case dg::Key::kTab:
    case dg::Key::kEnter:
    case dg::Key::kBackspace:
    case dg::Key::kDelete:
    case dg::Key::kOther:
      break;
  }
  return false;
}

bool control_key(WinState& window, NodeRecord& record, RadioState& /*state*/,
                 const dg::KeyEvent& event) {
  if (!usable(window, record) || dg::has(event.mods, dg::Modifier::kMod) ||
      dg::has(event.mods, dg::Modifier::kAlt)) {
    return false;
  }
  int direction = 0;
  if (event.key == dg::Key::kLeft || event.key == dg::Key::kUp) {
    direction = -1;
  } else if (event.key == dg::Key::kRight || event.key == dg::Key::kDown) {
    direction = 1;
  } else {
    return false;
  }
  const std::optional<int> group = window.widgets.at(record.id).group;
  const std::vector<dg::NodeId> members = radio_group_members(window, group.value_or(0));
  const auto self = std::find(members.begin(), members.end(), record.id);
  if (self == members.end()) {
    return false;
  }
  const auto count = static_cast<int>(members.size());
  const int index = static_cast<int>(self - members.begin());
  const dg::NodeId next = members[static_cast<std::size_t>((index + direction + count) % count)];
  if (next != record.id) {
    apply_focus_change(window, window.focus.set(next));
  }
  if (select_radio(window, next)) {
    emit(window.app, dg::events::ClickEvent{window.sdl_window, next, 0, 0, 0});
  }
  return true;
}

void restyle_state(WinState& window, NodeRecord& record, RadioState& /*state*/) {
  style_control(window, record);
}

void restyle_state(WinState& window, NodeRecord& record, SliderState& /*state*/) {
  paint_slider(window, record);
}

std::optional<dg::Status> set_prop_state(WinState& window, NodeRecord& record,
                                         SliderState& /*state*/, std::uint16_t prop_id,
                                         const dg::PropValue& value) {
  if (prop_id != DG_PROP_VALUE && prop_id != DG_PROP_MIN_VALUE &&
      prop_id != DG_PROP_MAX_VALUE && prop_id != DG_PROP_STEP) {
    return std::nullopt;
  }
  if (value.type() != dg::PropType::k_float) {
    return dg::Status::kTypeMismatch;
  }
  const float number = value.scalar();
  if (!std::isfinite(number)) {
    return dg::Status::kValueOutOfRange;
  }
  dg::RenderTree& tree = window.tree.render();
  if (prop_id == DG_PROP_VALUE) {
    (void)window.widgets.set_slider_value(tree, record.id, number);
    return dg::Status::kOk;
  }
  dg::Widget updated = window.widgets.at(record.id);
  if (prop_id == DG_PROP_MIN_VALUE) {
    if (number >= updated.max_value) {
      return dg::Status::kValueOutOfRange;
    }
    updated.min_value = number;
  } else if (prop_id == DG_PROP_MAX_VALUE) {
    if (number <= updated.min_value) {
      return dg::Status::kValueOutOfRange;
    }
    updated.max_value = number;
  } else {
    if (number < 0.0F) {
      return dg::Status::kValueOutOfRange;
    }
    updated.step = number;
  }
  // attach() replaces the Widget by value; set_slider_value() then
  // re-clamps and re-snaps the old value against the new range, and the
  // next pump re-places the thumb (resync_sliders()).
  const float previous = updated.value;
  window.widgets.attach(record.id, updated);
  (void)window.widgets.set_slider_value(tree, record.id, previous);
  window.relaid = true;
  return dg::Status::kOk;
}

std::optional<dg::Status> set_prop_state(WinState& window, NodeRecord& record,
                                         RadioState& /*state*/, std::uint16_t prop_id,
                                         const dg::PropValue& value) {
  if (prop_id != DG_PROP_RADIO_GROUP) {
    return std::nullopt;
  }
  if (value.type() != dg::PropType::k_float) {
    return dg::Status::kTypeMismatch;
  }
  const float number = value.scalar();
  if (!std::isfinite(number) || number != std::trunc(number) ||
      number < static_cast<float>(-kRadioGroupLimit) || number > static_cast<float>(kRadioGroupLimit)) {
    return dg::Status::kValueOutOfRange;
  }
  dg::Widget updated = window.widgets.at(record.id);
  updated.group = static_cast<int>(number);
  window.widgets.attach(record.id, updated);
  if (updated.checked) {
    // At most one selection per group: a selected radio joining a group
    // that already has one gives up its own.
    for (const dg::NodeId other : window.widgets.group_members(record.id)) {
      if (window.widgets.is_checked(other)) {
        updated.checked = false;
        window.widgets.attach(record.id, updated);
        break;
      }
    }
  }
  window.widgets.refresh(window.tree.render(), record.id,
                         window.interaction.state_of(record.id));
  return dg::Status::kOk;
}

void after_prop_state(WinState& window, NodeRecord& record, SliderState& /*state*/,
                      std::uint16_t prop_id) {
  if (prop_id != DG_PROP_HEIGHT || !window.widgets.has(record.id)) {
    return;
  }
  // The thumb stays a circle as tall as the track.
  const int height = wrapper_height(window, record, kSliderHeight);
  const dg::NodeId thumb = window.widgets.at(record.id).thumb;
  dg::BoxStyle thumb_box = window.tree.box(thumb);
  thumb_box.width = height;
  thumb_box.height = height;
  window.tree.set_box(thumb, thumb_box);
  paint_slider(window, record);
  window.relaid = true;
}

}  // namespace dg::app_detail::components

namespace dg {

using app_detail::RadioState;
using app_detail::SliderState;
using app_detail::components::component_of;

Status SliderRef::set_value(float value) {
  app_detail::WinState& win = window();
  const auto found = component_of<SliderState>(win, node());
  if (found.status != Status::kOk) {
    return found.status;
  }
  return app_detail::components::set_prop_state(win, *found.record, *found.state,
                                                 DG_PROP_VALUE, PropValue::number(value))
      .value_or(Status::kUnsupported);
}

Expected<float, Status> SliderRef::value() const {
  const auto found = component_of<SliderState>(std::as_const(window()), node());
  if (found.status != Status::kOk) {
    return Unexpected(found.status);
  }
  return std::as_const(window()).widgets.slider_value(node());
}

Status SliderRef::set_range(float min_value, float max_value, float step) {
  app_detail::WinState& win = window();
  const auto found = component_of<SliderState>(win, node());
  if (found.status != Status::kOk) {
    return found.status;
  }
  if (!std::isfinite(min_value) || !std::isfinite(max_value) || !std::isfinite(step) ||
      min_value >= max_value || step < 0.0F) {
    return Status::kValueOutOfRange;
  }
  // Widen first so neither bound write is refused against the old range.
  Widget updated = win.widgets.at(node());
  const float previous = updated.value;
  updated.min_value = min_value;
  updated.max_value = max_value;
  updated.step = step;
  win.widgets.attach(node(), updated);
  (void)win.widgets.set_slider_value(win.tree.render(), node(), previous);
  win.relaid = true;
  return Status::kOk;
}

namespace {

template <class F>
Expected<float, Status> slider_field(const app_detail::WinState& win, NodeId node, F field) {
  const auto found = component_of<SliderState>(win, node);
  if (found.status != Status::kOk) {
    return Unexpected(found.status);
  }
  return field(win.widgets.at(node));
}

}  // namespace

Expected<float, Status> SliderRef::min_value() const {
  return slider_field(std::as_const(window()), node(),
                      [](const Widget& widget) { return widget.min_value; });
}

Expected<float, Status> SliderRef::max_value() const {
  return slider_field(std::as_const(window()), node(),
                      [](const Widget& widget) { return widget.max_value; });
}

Expected<float, Status> SliderRef::step() const {
  return slider_field(std::as_const(window()), node(),
                      [](const Widget& widget) { return widget.step; });
}

Status RadioRef::select() {
  app_detail::WinState& win = window();
  const auto found = component_of<RadioState>(win, node());
  if (found.status != Status::kOk) {
    return found.status;
  }
  (void)app_detail::components::select_radio(win, node());
  return Status::kOk;
}

Expected<bool, Status> RadioRef::selected() const {
  const auto found = component_of<RadioState>(std::as_const(window()), node());
  if (found.status != Status::kOk) {
    return Unexpected(found.status);
  }
  return std::as_const(window()).widgets.is_checked(node());
}

Status RadioRef::set_group(int group) {
  app_detail::WinState& win = window();
  const auto found = component_of<RadioState>(win, node());
  if (found.status != Status::kOk) {
    return found.status;
  }
  if (group < -app_detail::components::kRadioGroupLimit ||
      group > app_detail::components::kRadioGroupLimit) {
    return Status::kValueOutOfRange;
  }
  return app_detail::components::set_prop_state(
             win, *found.record, *found.state, DG_PROP_RADIO_GROUP,
             PropValue::number(static_cast<float>(group)))
      .value_or(Status::kUnsupported);
}

Expected<int, Status> RadioRef::group() const {
  const auto found = component_of<RadioState>(std::as_const(window()), node());
  if (found.status != Status::kOk) {
    return Unexpected(found.status);
  }
  return std::as_const(window()).widgets.at(node()).group.value_or(0);
}

}  // namespace dg
