// Typed property settings for dg::Window::set():
//
//   window->set(node, {dg::prop::width(120), dg::prop::grow(1), dg::prop::column()});
//
// Each helper only pairs a DG_PROP_* id with a PropValue of the right type;
// Window::set_prop() with the raw id stays available for everything else.

#pragma once

#include <cstdint>

#include "drawgui/graphics/types.h"
#include "drawgui/props/node_props.h"
#include "drawgui/render/prop_ids.generated.h"

namespace dg::prop {

struct Setting {
  std::uint16_t id = DG_PROP_INVALID;
  PropValue value = PropValue::number(0.0F);
};

[[nodiscard]] inline Setting width(float px) { return {DG_PROP_WIDTH, PropValue::length(px)}; }
[[nodiscard]] inline Setting height(float px) { return {DG_PROP_HEIGHT, PropValue::length(px)}; }
[[nodiscard]] inline Setting width_percent(float percent) {
  return {DG_PROP_WIDTH, PropValue::percent(percent)};
}
[[nodiscard]] inline Setting height_percent(float percent) {
  return {DG_PROP_HEIGHT, PropValue::percent(percent)};
}
[[nodiscard]] inline Setting gap(float px) { return {DG_PROP_GAP, PropValue::number(px)}; }
[[nodiscard]] inline Setting grow(float factor) { return {DG_PROP_GROW, PropValue::number(factor)}; }
[[nodiscard]] inline Setting row() {
  return {DG_PROP_DIRECTION, PropValue::option(DG_DIRECTION_ROW)};
}
[[nodiscard]] inline Setting column() {
  return {DG_PROP_DIRECTION, PropValue::option(DG_DIRECTION_COLUMN)};
}
[[nodiscard]] inline Setting background(Color color) {
  return {DG_PROP_BACKGROUND_COLOR, PropValue::color(color)};
}
[[nodiscard]] inline Setting text_color(Color color) {
  return {DG_PROP_TEXT_COLOR, PropValue::color(color)};
}
[[nodiscard]] inline Setting font_size(float px) {
  return {DG_PROP_FONT_SIZE, PropValue::number(px)};
}

}  // namespace dg::prop
