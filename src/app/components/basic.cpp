// NodeKind::kBox, kButton, kLabel, kCheckbox and kImage: their default boxes
// and the widgets composed inside them.

#include <algorithm>
#include <cstdint>
#include <optional>

#include "app/components/components.h"
#include "app/internal.h"
#include "ui/control_visuals.h"

namespace dg::app_detail::components {
namespace {

// NodeKind::kLabel's engine-chosen default text size (props/drawgui.props
// .toml's own comment on `text`: font_size is not a property of the C ABI).
// Not measured against anything - only large enough to be legible at the
// default window sizes, and small enough to fit a one-line label inside
// the 32px-tall default box basic_defaults() gives it.
constexpr float kDefaultLabelTextSize = 16.0F;

// A checkbox's indicator corner radius (a radio's dot is fully round).
constexpr float kCheckboxIndicatorRadius = 4.0F;
constexpr float kControlRadius = 6.0F;

// `base` with `over` laid on top at `amount` (0..1); alpha from `base`.
dg::Color mix(dg::Color base, dg::Color over, float amount) {
  const auto channel = [amount](std::uint8_t a, std::uint8_t b) {
    return static_cast<std::uint8_t>(static_cast<float>(a) +
                                     ((static_cast<float>(b) - static_cast<float>(a)) * amount) +
                                     0.5F);
  };
  return dg::Color::rgba(channel(base.red(), over.red()), channel(base.green(), over.green()),
                         channel(base.blue(), over.blue()), base.alpha());
}

bool is_dark(dg::Color c) {
  return (299 * c.red()) + (587 * c.green()) + (114 * c.blue()) < 128000;
}

// Hover/press shades of a caller's own fill: towards white on a dark fill,
// towards black on a light one.
constexpr float kHoverShade = 0.08F;
constexpr float kPressShade = 0.16F;
dg::Color shade(dg::Color c, float amount) {
  return mix(c, is_dark(c) ? dg::Color::rgba(0xFF, 0xFF, 0xFF) : dg::Color::rgba(0, 0, 0),
             amount);
}

bool user_set(const NodeRecord& record, std::uint16_t prop_id) {
  return std::find(record.user_props.begin(), record.user_props.end(), prop_id) !=
         record.user_props.end();
}

constexpr int kImageSize = 64;

// A caller fill (pressed shade when a toggle is on), or a toggle button's
// own tone: quiet surface when off, the lists' soft selection tint when on.
void set_toggle_or_user_fills(dg::Widget& widget, const dg::ControlStyle& cs,
                              std::optional<dg::Color> user_fill, bool toggle) {
  if (user_fill.has_value()) {
    const dg::Color base = toggle && widget.checked ? shade(*user_fill, kPressShade) : *user_fill;
    widget.fill_normal = base;
    widget.fill_hover = shade(base, kHoverShade);
    widget.fill_pressed = shade(base, kPressShade);
    return;
  }
  const float hover = is_dark(cs.surface) ? 0.10F : 0.06F;
  const dg::Color base = widget.checked ? dg::control_visuals::selection_fill(cs)
                                        : mix(cs.surface, cs.on_surface, hover / 2.0F);
  widget.fill_normal = base;
  widget.fill_hover = mix(base, cs.on_surface, hover / 2.0F);
  widget.fill_pressed = mix(base, cs.on_surface, hover);
}

}  // namespace

float default_text_size() {
  return kDefaultLabelTextSize;
}

void basic_defaults(const AppState& app, NodeKind kind, dg::BoxStyle& box,
                    dg::NodeStyle& style) {
  if (kind == NodeKind::kLabel) {
    // A label's own box carries NO fill - only the text paints - matching
    // examples/05_widgets/widget_scene.cpp's own transparent_text() shape,
    // the precedent this mirrors rather than inventing a second one.
    box.width = 240;
    box.height = 32;
    // The engine-chosen default shipped instead of font_size/font_family
    // properties (props/drawgui.props.toml's own comment on `text`):
    // add_font()'s FIRST successfully added family, at a fixed size, in an
    // opaque black that is visible against the default window background
    // (dg_window_opts::background_argb) - TextStyle::color's OWN default is
    // fully transparent (graphics/types.h's Color{}), so a label with no
    // default here would draw nothing until a caller also set
    // DG_PROP_TEXT_COLOR.
    style.text.font = app.default_font;
    style.text.size = kDefaultLabelTextSize;
    style.text.color = dg::Color::rgba(0x00, 0x00, 0x00);
  } else if (kind == NodeKind::kCheckbox) {
    // The outer 32x32 box (hover/press feedback on its OWN fill) - the exact
    // dimensions examples/11_form_controls/form_scene.cpp's add_toggle()
    // uses for an ungrouped (round == false) checkbox. The indicator CHILD
    // that paints checked/unchecked is composed by compose_basic(), once
    // the node exists to parent it under.
    box.width = 32;
    box.height = 32;
    // Padded like the radio, so the 20x20 indicator sits in the centre.
    box.padding = dg::EdgeInsets::all(6);
    // Colours: style_control(), once the node exists.
    style.border_width = dg::BorderWidths::all(1.0F);
    style.radii = dg::Radii::all(kControlRadius);
  } else if (kind == NodeKind::kImage) {
    box.width = kImageSize;
    box.height = kImageSize;
    style.image.fit = dg::ImageFit::kContain;
  } else if (kind == NodeKind::kButton) {
    box.width = 200;
    box.height = 64;
    style.radii = dg::Radii::all(kControlRadius);
    // Ready for set_text(): the label's default font and size. The colours
    // come from style_control(), once the node exists.
    style.text.font = app.default_font;
    style.text.size = kDefaultLabelTextSize;
  } else {
    // A plain box: 240x120 (DG_LENGTH_UNIT_AUTO / dg::prop::width_auto()
    // drops it, back to content-sized).
    style.fill = dg::Color::rgba(0x2B, 0x2F, 0x3A);
    box.width = 240;
    box.height = 120;
  }
}

void compose_basic(WinState& window, NodeRecord& record) {
  const dg::NodeId new_id = record.id;
  if (record.kind == NodeKind::kButton) {
    dg::Widget widget;
    widget.kind = dg::WidgetKind::kButton;
    window.widgets.attach(new_id, widget);
    style_control(window, record);
  } else if (record.kind == NodeKind::kLabel) {
    dg::Widget widget;
    widget.kind = dg::WidgetKind::kLabel;
    window.widgets.attach(new_id, widget);
  } else if (record.kind == NodeKind::kCheckbox) {
    // P6 sub-step a2: the indicator CHILD - composed here, unaddressable
    // through Window (create() never returns it), exactly the recipe
    // examples/11_form_controls/form_scene.cpp's add_toggle() uses. See
    // the DG_NODE_TYPE_CHECKBOX constant comment (abi/drawgui.def.toml) for
    // why a caller cannot be asked to do this itself.
    dg::BoxStyle indicator_box;
    indicator_box.width = 20;
    indicator_box.height = 20;
    dg::NodeStyle indicator_style;
    indicator_style.radii = dg::Radii::all(kCheckboxIndicatorRadius);
    const dg::NodeId indicator = window.tree.add_child(new_id, indicator_box, indicator_style);

    dg::Widget widget;
    widget.kind = dg::WidgetKind::kCheckbox;
    widget.indicator = indicator;
    widget.checked = false;
    // widget.group left unset (std::nullopt): a plain checkbox. A grouped
    // one is NodeKind::kRadio (slider_radio.cpp).
    window.widgets.attach(new_id, widget);
    window.widgets.attach(indicator, dg::Widget{});
    style_control(window, record);
  }
}

// Buttons: color.primary / -hover / -pressed with color.on-primary text.
// Checkboxes and radios: a color.surface box (hover/press a shade of
// color.on-surface over it) with a color.border outline; the checked
// indicator is color.primary, the unchecked one transparent. A colour
// property the caller wrote or bound on the node wins (NodeRecord::
// user_props); a caller BACKGROUND_COLOR becomes the normal fill, with the
// hover/press shades derived from it.
void style_control(WinState& window, NodeRecord& record) {
  const bool button = record.kind == NodeKind::kButton;
  if ((!button && record.kind != NodeKind::kCheckbox && record.kind != NodeKind::kRadio) ||
      !window.widgets.has(record.id)) {
    return;
  }
  const auto [theme, variant] = theme_for(window.app);
  const dg::ControlStyle cs = control_style(window.app);
  const auto token = [&, theme = theme, variant = variant](dg_token_id id, dg::Color fallback) {
    return theme != nullptr ? theme->color_value(id, variant).value_or(fallback) : fallback;
  };
  if (record.user_fill_token.has_value() && theme != nullptr) {
    if (const std::optional<dg::Color> c =
            theme->color_value(static_cast<dg_token_id>(*record.user_fill_token), variant)) {
      record.user_fill = *c;
    }
  }
  dg::RenderTree& tree = window.tree.render();
  dg::Widget widget = window.widgets.at(record.id);
  const bool toggle = button && is_toggle_button(record);
  if (record.user_fill.has_value() || toggle) {
    set_toggle_or_user_fills(widget, cs, record.user_fill, toggle);
  } else if (button) {
    widget.fill_normal = cs.accent;
    widget.fill_hover = token(DG_TOKEN_COLOR_PRIMARY_HOVER, shade(cs.accent, kHoverShade));
    widget.fill_pressed = token(DG_TOKEN_COLOR_PRIMARY_PRESSED, shade(cs.accent, kPressShade));
  } else {
    const float hover = is_dark(cs.surface) ? 0.10F : 0.06F;
    widget.fill_normal = cs.surface;
    widget.fill_hover = mix(cs.surface, cs.on_surface, hover);
    widget.fill_pressed = mix(cs.surface, cs.on_surface, 2.0F * hover);
  }
  dg::NodeStyle style = tree.style(record.id);
  if (button) {
    if (!user_set(record, DG_PROP_TEXT_COLOR)) {
      style.text.color = toggle ? cs.on_surface : cs.on_accent;
    }
  } else {
    widget.indicator_on = cs.accent;
    widget.indicator_off = dg::Color{};
    if (!user_set(record, DG_PROP_BORDER_COLOR)) {
      style.border_color = cs.border;
    }
  }
  tree.set_style(record.id, style);
  window.widgets.attach(record.id, widget);
  window.widgets.refresh(tree, record.id, window.interaction.state_of(record.id));
}

bool is_toggle_button(const NodeRecord& record) {
  return record.kind == NodeKind::kButton && user_set(record, DG_PROP_CHECKED);
}

dg::Status set_checked_prop(WinState& window, NodeRecord& record, const dg::PropValue& value) {
  if (value.type() != dg::PropType::k_bool) {
    return dg::Status::kTypeMismatch;
  }
  if ((record.kind != NodeKind::kButton && record.kind != NodeKind::kCheckbox) ||
      !window.widgets.has(record.id)) {
    return dg::Status::kUnsupported;
  }
  if (record.kind == NodeKind::kButton && !user_set(record, DG_PROP_CHECKED)) {
    record.user_props.push_back(DG_PROP_CHECKED);
  }
  dg::Widget widget = window.widgets.at(record.id);
  widget.checked = value.ordinal() != 0U;
  window.widgets.attach(record.id, widget);
  style_control(window, record);
  return dg::Status::kOk;
}

void flip_toggle_button(WinState& window, NodeRecord& record) {
  dg::Widget widget = window.widgets.at(record.id);
  widget.checked = !widget.checked;
  window.widgets.attach(record.id, widget);
  style_control(window, record);
}

}  // namespace dg::app_detail::components
