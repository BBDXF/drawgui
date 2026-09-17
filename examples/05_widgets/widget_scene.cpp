#include "widget_scene.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "drawgui/graphics/types.h"
#include "drawgui/layout/box.h"

namespace widget_scene {
namespace {

using dg::BoxStyle;
using dg::Color;
using dg::CrossAlign;
using dg::EdgeInsets;
using dg::LayoutKind;
using dg::MainAlign;
using dg::NodeId;
using dg::NodeStyle;
using dg::TextAlign;
using dg::TextStyle;
using dg::Widget;
using dg::WidgetKind;

constexpr float kContainerRadius = 6.0F;
constexpr float kControlRadius = 5.0F;

constexpr std::uint32_t kInk = 0xFFE8EDF4;
constexpr std::uint32_t kInkDim = 0xFF97A3B4;
constexpr std::uint32_t kInkOnAccent = 0xFF0B1016;

constexpr std::uint32_t kButtonNormal = 0xFF2C3644;
constexpr std::uint32_t kButtonHover = 0xFF3B4A5E;
constexpr std::uint32_t kButtonPressed = 0xFF1F2731;

constexpr std::uint32_t kAccentNormal = 0xFF2E86DE;
constexpr std::uint32_t kAccentHover = 0xFF4FA3F7;
constexpr std::uint32_t kAccentPressed = 0xFF1B5F9E;

constexpr int kLabelSize = 13;
constexpr int kTitleSize = 15;

NodeStyle flat(std::uint32_t argb) {
  NodeStyle style;
  style.fill = Color::from_argb(argb);
  return style;
}

NodeStyle panel(std::uint32_t fill, std::uint32_t border, bool rounded) {
  NodeStyle style;
  style.fill = Color::from_argb(fill);
  style.border_color = Color::from_argb(border);
  style.border_width = dg::BorderWidths::all(1.0F);
  if (rounded) {
    style.radii = dg::Radii::all(kContainerRadius);
  }
  return style;
}

TextStyle text_of(std::string content, dg::FontId font, int size, std::uint32_t colour,
                  TextAlign align, int inset) {
  TextStyle text;
  text.text = std::move(content);
  text.font = font;
  text.size = static_cast<float>(size);
  text.color = Color::from_argb(colour);
  text.align = align;
  text.inset = inset;
  return text;
}

BoxStyle stack(LayoutKind kind, int gap, CrossAlign cross) {
  BoxStyle box;
  box.kind = kind;
  box.gap = gap;
  box.cross_align = cross;
  return box;
}

BoxStyle sized(int width, int height) {
  BoxStyle box;
  if (width > 0) {
    box.width = width;
  }
  if (height > 0) {
    box.height = height;
  }
  return box;
}

// A label's own box carries NO fill, so the widget underneath shows through.
// That is what lets a button change colour on hover without its caption having
// to be repainted in the new colour too - and it is why a button's label can
// cover the whole button without hiding it.
NodeStyle transparent_text(const TextStyle& text) {
  NodeStyle style;
  style.text = text;
  return style;
}

Widget button_widget(std::uint32_t normal, std::uint32_t hover, std::uint32_t pressed) {
  Widget widget;
  widget.kind = WidgetKind::kButton;
  widget.fill_normal = Color::from_argb(normal);
  widget.fill_hover = Color::from_argb(hover);
  widget.fill_pressed = Color::from_argb(pressed);
  return widget;
}

Widget plain(WidgetKind kind) {
  Widget widget;
  widget.kind = kind;
  return widget;
}

// Every button is a box with a label INSIDE it, never a box carrying its own
// caption. That is the composition a real widget has, and it means the pointer
// lands on the label on every single button - so the climb from the hit node to
// the widget that owns it is exercised continuously rather than by one case
// written to exercise it.
struct ButtonParts {
  NodeId button;
  NodeId label;
};

ButtonParts add_button(Scene& scene, NodeId parent, const BoxStyle& box,
                       const std::string& caption, const Widget& widget, bool rounded,
                       std::uint32_t caption_colour) {
  NodeStyle style = flat(widget.fill_normal.argb());
  style.border_color = Color::from_argb(0xFF141A22);
  style.border_width = dg::BorderWidths::all(1.0F);
  if (rounded) {
    style.radii = dg::Radii::all(kControlRadius);
  }

  BoxStyle host = box;
  host.kind = LayoutKind::kRow;
  host.cross_align = CrossAlign::kStretch;
  host.padding = EdgeInsets::symmetric(6, 0);

  const NodeId node = scene.tree.add_child(parent, host, style);

  BoxStyle caption_box;
  caption_box.grow = 1;
  const NodeId label =
      scene.tree.add_child(node, caption_box,
                           transparent_text(text_of(caption, scene.handles.ui_font, kLabelSize,
                                                    caption_colour, TextAlign::kCenter, 0)));

  scene.widgets.attach(node, widget);
  scene.widgets.attach(label, plain(WidgetKind::kLabel));
  scene.handles.interactive.push_back(node);
  return ButtonParts{node, label};
}

NodeId add_label(Scene& scene, NodeId parent, const BoxStyle& box, const std::string& content,
                 int size, std::uint32_t colour, TextAlign align, int inset) {
  const NodeId node = scene.tree.add_child(
      parent, box,
      transparent_text(text_of(content, scene.handles.ui_font, size, colour, align, inset)));
  scene.widgets.attach(node, plain(WidgetKind::kLabel));
  return node;
}

void build_header(Scene& scene, NodeId root) {
  BoxStyle header = stack(LayoutKind::kRow, 10, CrossAlign::kCenter);
  header.height = 46;
  header.padding = EdgeInsets::symmetric(14, 0);
  const NodeId node = scene.tree.add_child(root, header, panel(0xFF1B2028, 0xFF2A323D, false));
  scene.widgets.attach(node, plain(WidgetKind::kPanel));

  // A HEIGHT, not just a grow weight. `grow` is a MAIN-axis share; in a row
  // whose cross_align is kCenter a child is loosely constrained vertically and
  // a leaf with no content shrinks to fit - which for a text node is zero,
  // because sub-step 2 excluded intrinsic sizing and a string therefore
  // contributes no height. The label then paints nothing at all, silently.
  // Two labels in this scene were built that way and were invisible until an
  // injection that nothing caught led back to them.
  BoxStyle title = sized(0, 20);
  title.grow = 1;
  add_label(scene, node, title, "drawgui  widgets", kTitleSize, kInk, TextAlign::kLeft, 0);

  scene.handles.hover_label = add_label(scene, node, sized(330, 20), "pointer: -", kLabelSize,
                                        kInkDim, TextAlign::kRight, 0);
}

void build_sidebar(Scene& scene, NodeId body, bool rounded, bool rounded_container) {
  BoxStyle sidebar = stack(LayoutKind::kColumn, 6, CrossAlign::kStretch);
  sidebar.width = 190;
  sidebar.padding = EdgeInsets::all(10);
  const NodeId node =
      scene.tree.add_child(body, sidebar, panel(0xFF191E26, 0xFF232A34, rounded_container));
  scene.widgets.attach(node, plain(WidgetKind::kPanel));

  static constexpr const char* kItems[] = {"Overview", "Damage", "Layout", "Widgets", "Fonts"};
  for (const char* item : kItems) {
    BoxStyle row = sized(0, 30);
    add_button(scene, node, row, item,
               button_widget(kButtonNormal, kButtonHover, kButtonPressed), rounded, kInk);
  }
}

void build_toolbar(Scene& scene, NodeId content, bool rounded, bool rounded_container) {
  BoxStyle toolbar = stack(LayoutKind::kRow, 8, CrossAlign::kCenter);
  toolbar.height = 46;
  toolbar.padding = EdgeInsets::symmetric(10, 0);
  const NodeId node =
      scene.tree.add_child(content, toolbar, panel(0xFF1A1F27, 0xFF2A323D, rounded_container));
  scene.widgets.attach(node, plain(WidgetKind::kPanel));

  const ButtonParts primary = add_button(
      scene, node, sized(104, 30), "Apply",
      button_widget(kAccentNormal, kAccentHover, kAccentPressed), rounded, kInkOnAccent);

  // Named so a nesting check can ask about this exact pair rather than
  // guessing at an index.
  scene.handles.nested_owner = primary.button;
  scene.handles.nested_label = primary.label;

  add_button(scene, node, sized(104, 30), "Reset",
             button_widget(kButtonNormal, kButtonHover, kButtonPressed), rounded, kInk);

  // The checkbox: a row holding an indicator box and a caption. Its box changes
  // colour with the pointer like any button, and its indicator carries the
  // state that survives the click.
  BoxStyle check = stack(LayoutKind::kRow, 8, CrossAlign::kCenter);
  check.height = 30;
  check.padding = EdgeInsets::symmetric(8, 0);
  NodeStyle check_style = flat(kButtonNormal);
  check_style.border_color = Color::from_argb(0xFF141A22);
  check_style.border_width = dg::BorderWidths::all(1.0F);
  if (rounded) {
    check_style.radii = dg::Radii::all(kControlRadius);
  }
  const NodeId checkbox = scene.tree.add_child(node, check, check_style);

  const NodeId indicator = scene.tree.add_child(checkbox, sized(16, 16), flat(0xFF10151C));
  scene.handles.checkbox_label = add_label(scene, checkbox, sized(96, 18), "rounded",
                                           kLabelSize, kInk, TextAlign::kLeft, 0);

  Widget check_widget = button_widget(kButtonNormal, kButtonHover, kButtonPressed);
  check_widget.kind = WidgetKind::kCheckbox;
  check_widget.indicator = indicator;
  check_widget.indicator_on = Color::from_argb(kAccentNormal);
  check_widget.indicator_off = Color::from_argb(0xFF10151C);
  check_widget.checked = rounded;
  scene.widgets.attach(checkbox, check_widget);
  scene.widgets.attach(indicator, plain(WidgetKind::kPanel));
  scene.handles.interactive.push_back(checkbox);
  scene.handles.checkbox = checkbox;

  BoxStyle spacer;
  spacer.grow = 1;
  const NodeId gap = scene.tree.add_child(node, spacer, NodeStyle{});
  scene.widgets.attach(gap, plain(WidgetKind::kPanel));
}

// The lab: the shapes a hit test gets wrong, arranged so a human can aim at
// them and a test can enumerate them.
void build_lab(Scene& scene, NodeId content, bool rounded, bool rounded_container) {
  BoxStyle lab;
  lab.kind = LayoutKind::kAbsolute;
  lab.grow = 1;
  lab.padding = EdgeInsets::all(12);
  scene.handles.lab =
      scene.tree.add_child(content, lab, panel(0xFF141A22, 0xFF232A34, rounded_container));
  scene.widgets.attach(scene.handles.lab, plain(WidgetKind::kPanel));

  BoxStyle caption = sized(360, 18);
  caption.left = 4;
  caption.top = 0;
  add_label(scene, scene.handles.lab, caption, "overlap, overflow and adjacency", kLabelSize,
            kInkDim, TextAlign::kLeft, 0);

  // Two buttons on top of one another. `over` is added second, so it paints
  // second and must win every pixel they share.
  BoxStyle under_box = sized(150, 56);
  under_box.left = 8;
  under_box.top = 26;
  scene.handles.lab_under =
      add_button(scene, scene.handles.lab, under_box, "under",
                 button_widget(0xFF7D5BA6, 0xFF9B76C7, 0xFF5C4179), rounded, kInk)
          .button;

  BoxStyle over_box = sized(150, 56);
  over_box.left = 92;
  over_box.top = 54;
  scene.handles.lab_over =
      add_button(scene, scene.handles.lab, over_box, "over",
                 button_widget(0xFF2E9E6B, 0xFF44C489, 0xFF1F6F4B), rounded, kInk)
          .button;

  // Adjacency: two buttons sharing an edge exactly, so the boundary column
  // belongs to precisely one of them.
  BoxStyle pair = stack(LayoutKind::kRow, 0, CrossAlign::kStretch);
  pair.left = 8;
  pair.top = 128;
  pair.width = 180;
  pair.height = 34;
  const NodeId pair_host = scene.tree.add_child(scene.handles.lab, pair, NodeStyle{});
  scene.widgets.attach(pair_host, plain(WidgetKind::kPanel));
  add_button(scene, pair_host, sized(90, 0), "left",
             button_widget(kButtonNormal, kButtonHover, kButtonPressed), rounded, kInk);
  add_button(scene, pair_host, sized(90, 0), "right",
             button_widget(kButtonNormal, kButtonHover, kButtonPressed), rounded, kInk);

  // Pinned to the far corner: its position is a function of the lab's size,
  // so a resize moves it and a stale hit rectangle would show up immediately.
  BoxStyle pinned = sized(120, 34);
  pinned.right = 12;
  pinned.bottom = 12;
  scene.handles.lab_pinned =
      add_button(scene, scene.handles.lab, pinned, "pinned",
                 button_widget(0xFF3E6E8E, 0xFF5A93B8, 0xFF2A4E66), rounded, kInk)
          .button;

  // Overflow: a row too narrow for what is in it. The layout tree reports the
  // overrun as a diagnostic rather than hiding it, painting draws the second
  // button outside its host, and hit testing therefore has to find it there.
  BoxStyle host = stack(LayoutKind::kRow, 8, CrossAlign::kStretch);
  host.left = 230;
  host.top = 26;
  host.width = 120;
  host.height = 44;
  scene.handles.lab_host = scene.tree.add_child(
      scene.handles.lab, host, panel(0xFF20262F, 0xFF3B4A5E, rounded_container));
  scene.widgets.attach(scene.handles.lab_host, plain(WidgetKind::kPanel));

  add_button(scene, scene.handles.lab_host, sized(84, 0), "in",
             button_widget(kButtonNormal, kButtonHover, kButtonPressed), rounded, kInk);
  scene.handles.lab_overflow =
      add_button(scene, scene.handles.lab_host, sized(84, 0), "spills",
                 button_widget(0xFFB2603C, 0xFFD97F55, 0xFF854427), rounded, kInk)
          .button;
}

void build_status(Scene& scene, NodeId content) {
  BoxStyle status = stack(LayoutKind::kRow, 10, CrossAlign::kCenter);
  status.height = 26;
  status.padding = EdgeInsets::symmetric(10, 0);
  const NodeId node = scene.tree.add_child(content, status, flat(0xFF1A1F27));
  scene.widgets.attach(node, plain(WidgetKind::kPanel));

  BoxStyle counter = sized(0, 18);
  counter.grow = 1;
  scene.handles.counter_label =
      add_label(scene, node, counter, "clicks: 0", kLabelSize, kInkDim, TextAlign::kLeft, 0);
}

}  // namespace

Scene build(const Options& options) {
  dg::TreeSpec spec = options.spec;

  dg::FontId ui;
  dg::FontId bold;
  dg::FontId cjk;
  dg::Expected<dg::FontCatalog, dg::FontError> scanned =
      dg::FontCatalog::scan(options.font_dir);
  if (scanned) {
    dg::FontCatalog catalog = std::move(scanned).value();

    // Named explicitly, every one of them. This font manager has no fallback
    // chain, so there is no family that quietly covers what another misses -
    // asking for the wrong name draws nothing at all.
    if (const auto id = catalog.add("DejaVu Sans", false)) {
      ui = id.value();
    }
    if (const auto id = catalog.add("DejaVu Sans", true)) {
      bold = id.value();
    }
    if (const auto id = catalog.add("WenQuanYi Zen Hei", false)) {
      cjk = id.value();
    }
    spec.fonts = std::move(catalog);
  }

  Scene scene{dg::LayoutTree{spec}, dg::WidgetSet{}, Handles{}, 0};
  scene.handles.ui_font = ui;
  scene.handles.bold_font = bold;
  scene.handles.cjk_font = cjk;

  const NodeId root = dg::LayoutTree::root();
  BoxStyle root_box = stack(LayoutKind::kColumn, 0, CrossAlign::kStretch);
  scene.tree.set_box(root, root_box);
  scene.widgets.attach(root, plain(WidgetKind::kPanel));

  build_header(scene, root);

  BoxStyle body = stack(LayoutKind::kRow, 0, CrossAlign::kStretch);
  body.grow = 1;
  const NodeId body_node = scene.tree.add_child(root, body, NodeStyle{});
  scene.widgets.attach(body_node, plain(WidgetKind::kPanel));

  build_sidebar(scene, body_node, options.rounded_controls, options.rounded_containers);

  BoxStyle content = stack(LayoutKind::kColumn, 8, CrossAlign::kStretch);
  content.grow = 1;
  content.padding = EdgeInsets::all(10);
  const NodeId content_node = scene.tree.add_child(body_node, content, flat(0xFF11161D));
  scene.widgets.attach(content_node, plain(WidgetKind::kPanel));

  build_toolbar(scene, content_node, options.rounded_controls, options.rounded_containers);
  build_lab(scene, content_node, options.rounded_controls, options.rounded_containers);
  build_status(scene, content_node);

  BoxStyle readout = sized(0, 150);
  scene.handles.readout =
      scene.tree.add_child(root, readout, panel(0xFF0E1218, 0xFF232A34, false));
  scene.widgets.attach(scene.handles.readout, plain(WidgetKind::kPanel));

  scene.tree.layout_full();
  return scene;
}

std::string describe(const Scene& scene, NodeId id) {
  if (!scene.widgets.has(id)) {
    return "node " + std::to_string(id.index);
  }
  const dg::Widget& widget = scene.widgets.at(id);
  const dg::TextStyle& text = scene.tree.render().style(id).text;
  std::string kind;
  switch (widget.kind) {
    case WidgetKind::kPanel:
      kind = "panel";
      break;
    case WidgetKind::kLabel:
      kind = "label";
      break;
    case WidgetKind::kButton:
      kind = "button";
      break;
    case WidgetKind::kCheckbox:
      kind = "checkbox";
      break;
    case WidgetKind::kScrollView:
      kind = "scroll view";
      break;
    case WidgetKind::kSlider:
      kind = "slider";
      break;
    case WidgetKind::kTextField:
      kind = "text field";
      break;
    case WidgetKind::kList:
      kind = "list";
      break;
    case WidgetKind::kDropdown:
      kind = "dropdown";
      break;
  }

  // A button's caption lives on its label child, so fall back to the first
  // child that has one. Without this every button would be reported as
  // "button #17", which is exactly as useful as the index alone.
  std::string caption = text.text;
  if (caption.empty()) {
    for (std::uint32_t index = 0; index < scene.tree.node_count(); ++index) {
      const NodeId child{index};
      if (scene.tree.render().parent(child) == id && child != id) {
        const std::string& child_text = scene.tree.render().style(child).text.text;
        if (!child_text.empty()) {
          caption = child_text;
          break;
        }
      }
    }
  }
  return caption.empty() ? kind + " #" + std::to_string(id.index) : kind + " '" + caption + "'";
}

void react(Scene& scene, const dg::Interaction& interaction,
           const dg::InteractionChange& change) {
  if (change.clicked.has_value()) {
    ++scene.clicks;
    scene.widgets.toggle(*change.clicked);
    scene.tree.render().set_text(scene.handles.counter_label, [&] {
      dg::TextStyle text = scene.tree.render().style(scene.handles.counter_label).text;
      text.text = "clicks: " + std::to_string(scene.clicks) +
                  "   last: " + describe(scene, *change.clicked);
      return text;
    }());
  }

  // Every widget the change named, refreshed from the state machine's current
  // answer rather than from what the change implies. Deriving the appearance
  // from the event would need one rule per event kind; asking the machine
  // needs none, and cannot disagree with what state_of() reports elsewhere.
  const std::optional<dg::NodeId> touched[] = {change.left, change.entered, change.pressed,
                                               change.released, change.clicked};
  for (const std::optional<dg::NodeId>& id : touched) {
    if (id.has_value()) {
      scene.widgets.refresh(scene.tree.render(), *id, interaction.state_of(*id));
    }
  }
}

dg::InteractionChange dispatch(Scene& scene, dg::Interaction& interaction,
                               const dg::PointerEvent& event) {
  const dg::PixelPoint at{event.x, event.y};
  dg::InteractionChange change;
  switch (event.action) {
    case dg::PointerAction::kMove:
      change = interaction.moved_over(scene.widgets.widget_at(scene.tree.render(), at));
      break;
    case dg::PointerAction::kDown:
      change = interaction.pressed_on(scene.widgets.widget_at(scene.tree.render(), at));
      break;
    case dg::PointerAction::kUp:
      change = interaction.released_on(scene.widgets.widget_at(scene.tree.render(), at));
      break;
    case dg::PointerAction::kLeave:
      change = interaction.left_window();
      break;
    case dg::PointerAction::kWheel:
      // This scene has no scrollable widget, so a wheel changes nothing here
      // - it is a no-op rather than an omission. examples/10_scrolling is
      // where it is consumed.
      break;
  }
  react(scene, interaction, change);
  return change;
}

dg::InteractionChange resync(Scene& scene, dg::Interaction& interaction, dg::PixelPoint at) {
  const dg::InteractionChange change =
      interaction.moved_over(scene.widgets.widget_at(scene.tree.render(), at));
  react(scene, interaction, change);
  return change;
}

}  // namespace widget_scene
