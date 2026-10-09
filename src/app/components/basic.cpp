// NodeKind::kBox, kButton, kLabel, kCheckbox and kImage: their default boxes
// and the widgets composed inside them.

#include <cstdint>

#include "app/components/components.h"
#include "app/internal.h"

namespace dg::app_detail::components {
namespace {

// NodeKind::kLabel's engine-chosen default text size (props/drawgui.props
// .toml's own comment on `text`: font_size is not a property of the C ABI).
// Not measured against anything - only large enough to be legible at the
// default window sizes, and small enough to fit a one-line label inside
// the 32px-tall default box basic_defaults() gives it.
constexpr float kDefaultLabelTextSize = 16.0F;

// P6 sub-step a2: NodeKind::kCheckbox's colours - lifted unchanged from
// examples/11_form_controls/form_scene.cpp's own add_toggle() palette
// (kToggleNormal/kToggleHover/kTogglePressed/kBorder/kOff/kAccent), the
// recipe this runtime copies rather than invents.
constexpr std::uint32_t kCheckboxNormalArgb = 0xFF2C3644;
constexpr std::uint32_t kCheckboxHoverArgb = 0xFF3B4A5E;
constexpr std::uint32_t kCheckboxPressedArgb = 0xFF1F2731;
constexpr std::uint32_t kCheckboxBorderArgb = 0xFF141A22;
constexpr std::uint32_t kCheckboxOffArgb = 0xFF10151C;
constexpr std::uint32_t kCheckboxOnArgb = 0xFF2E86DE;

constexpr dg::Color kCheckboxNormal = dg::Color::from_argb(kCheckboxNormalArgb);
constexpr dg::Color kCheckboxBorder = dg::Color::from_argb(kCheckboxBorderArgb);

// NodeKind::kButton's default text: the label's size, white on the
// button's own blue fills.
constexpr std::uint32_t kButtonTextArgb = 0xFFFFFFFF;

constexpr int kImageSize = 64;

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
    style.fill = kCheckboxNormal;
    style.border_color = kCheckboxBorder;
    style.border_width = dg::BorderWidths::all(1.0F);
    style.radii = dg::Radii::all(6.0F);
  } else if (kind == NodeKind::kImage) {
    box.width = kImageSize;
    box.height = kImageSize;
    style.image.fit = dg::ImageFit::kContain;
  } else {
    style.fill = dg::Color::rgba(0x2B, 0x2F, 0x3A);
    if (kind == NodeKind::kButton) {
      box.width = 200;
      box.height = 64;
      // Ready for set_text(): the label's default font and size.
      style.text.font = app.default_font;
      style.text.size = kDefaultLabelTextSize;
      style.text.color = dg::Color::from_argb(kButtonTextArgb);
    } else {
      box.width = 240;
      box.height = 120;
    }
  }
}

void compose_basic(WinState& window, NodeRecord& record) {
  const dg::NodeId new_id = record.id;
  if (record.kind == NodeKind::kButton) {
    dg::Widget widget;
    widget.kind = dg::WidgetKind::kButton;
    widget.fill_normal = dg::Color::rgba(0x35, 0x6B, 0xC7);
    widget.fill_hover = dg::Color::rgba(0x4A, 0x86, 0xE3);
    widget.fill_pressed = dg::Color::rgba(0x25, 0x50, 0x9C);
    window.widgets.attach(new_id, widget);
    window.widgets.refresh(window.tree.render(), new_id, dg::PointerState{});
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
    indicator_style.fill = dg::Color::from_argb(kCheckboxOffArgb);
    const dg::NodeId indicator = window.tree.add_child(new_id, indicator_box, indicator_style);

    dg::Widget widget;
    widget.kind = dg::WidgetKind::kCheckbox;
    widget.fill_normal = dg::Color::from_argb(kCheckboxNormalArgb);
    widget.fill_hover = dg::Color::from_argb(kCheckboxHoverArgb);
    widget.fill_pressed = dg::Color::from_argb(kCheckboxPressedArgb);
    widget.indicator = indicator;
    widget.indicator_on = dg::Color::from_argb(kCheckboxOnArgb);
    widget.indicator_off = dg::Color::from_argb(kCheckboxOffArgb);
    widget.checked = false;
    // widget.group left unset (std::nullopt): a plain checkbox. A grouped
    // one is NodeKind::kRadio (slider_radio.cpp).
    window.widgets.attach(new_id, widget);
    window.widgets.attach(indicator, dg::Widget{});
    window.widgets.refresh(window.tree.render(), new_id, dg::PointerState{});
  }
}

}  // namespace dg::app_detail::components
