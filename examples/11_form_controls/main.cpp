// examples/11_form_controls: a checkbox, two radio groups, two sliders and a
// disable switch, on dg::App / dg::Window.
//
// NodeKind::kRadio and NodeKind::kSlider compose the indicator dot and the
// thumb themselves, repaint the radio siblings a selection clears, follow a
// drag and the keys, and report SliderEvent / ClickEvent; this file only
// builds the scene and reacts to the enable switch.
//
//   (no arguments)              the demo window - click the checkbox/radios,
//                               drag either slider (or focus it and use the
//                               arrow keys, PageUp/PageDown, Home/End)
//   --size WxH                  open at a given size
//   --run-ms N                  close after N milliseconds
//   --font-dir DIR              accepted for symmetry; the scene has no text
//   --preset-radio-a N          pre-select radio group A's option N
//   --preset-volume V           pre-set the volume slider's value
//   --preset-checkbox           pre-check the checkbox (a real click)
//   --probe X,Y                 print the pixel AND the node at one point
//   --verify-form-controls      the headless check. No display needed.
//   --dump-png FILE             one frame to PNG

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "drawgui/app/app.h"
#include "drawgui/app/prop.h"
#include "drawgui/app/window.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/prop_ids.generated.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/widget/widget_set.h"
#include "example_app.h"

namespace {

using dg::NodeId;
using dg::NodeKind;

constexpr int kDefaultWidth = 760;
constexpr int kDefaultHeight = 500;

constexpr int kFixedTrackWidth = 240;
constexpr float kVolumeMin = 0.0F;
constexpr float kVolumeMax = 100.0F;
constexpr float kVolumeInitial = 30.0F;
constexpr float kBrightnessMin = 0.0F;
constexpr float kBrightnessMax = 10.0F;
constexpr float kBrightnessStep = 1.0F;
constexpr float kBrightnessInitial = 3.0F;

constexpr int kGroupA = 1;
constexpr int kGroupB = 2;

enum class Mode : std::uint8_t { kWindow, kVerify, kDumpPng, kProbe };

struct Options {
  Mode mode = Mode::kWindow;
  int width = kDefaultWidth;
  int height = kDefaultHeight;
  int run_ms = 0;
  std::string font_dir = "/usr/share/fonts";
  std::string png_path;
  dg::PixelPoint probe;
  int preset_radio_a = -1;
  std::optional<float> preset_volume;
  bool preset_checkbox = false;
};

struct Scene {
  dg::Window* window = nullptr;
  NodeId body;
  NodeId checkbox;
  std::vector<NodeId> radio_a;
  std::vector<NodeId> radio_b;
  NodeId volume;
  NodeId brightness;
  // The disabled-state demo: `enable_switch` writes DG_PROP_DISABLED on
  // `disabled_group` (a row holding `disabled_checkbox` and
  // `disabled_slider`) - unchecked at start, so the row starts disabled.
  NodeId enable_switch;
  NodeId disabled_group;
  NodeId disabled_checkbox;
  NodeId disabled_slider;
};

void pad(dg::Window& w, NodeId node, float px) {
  for (const std::uint16_t id :
       {DG_PROP_PADDING_L, DG_PROP_PADDING_T, DG_PROP_PADDING_R, DG_PROP_PADDING_B}) {
    (void)w.set_prop(node, id, dg::PropValue::number(px));
  }
}

NodeId section(dg::Window& w, NodeId body) {
  const NodeId row = w.create(NodeKind::kBox, body).value();
  (void)w.set(row, {dg::prop::row(), dg::prop::gap(24), dg::prop::height(60),
                    dg::prop::background(dg::Color::from_argb(0xFF14171C))});
  (void)w.set_prop(row, DG_PROP_ALIGN, dg::PropValue::option(DG_ALIGN_CENTER));
  pad(w, row, 14);
  return row;
}

// The checkbox's 20x20 indicator sits at its content origin; 6px of padding
// centres it in the 32x32 box, as the radio does by default.
NodeId checkbox(dg::Window& w, NodeId parent) {
  const NodeId node = w.create(NodeKind::kCheckbox, parent).value();
  pad(w, node, 6);
  return node;
}

NodeId radio(dg::Window& w, NodeId parent, int group) {
  const NodeId node = w.create(NodeKind::kRadio, parent).value();
  (void)w.radio(node).value().set_group(group);
  return node;
}

NodeId slider(dg::Window& w, NodeId parent, int width, float min_value, float max_value,
              float step, float value) {
  const NodeId node = w.create(NodeKind::kSlider, parent).value();
  if (width > 0) {
    (void)w.set(node, dg::prop::width(static_cast<float>(width)));
  } else {
    (void)w.set(node, dg::prop::grow(1));
  }
  dg::SliderRef ref = w.slider(node).value();
  (void)ref.set_range(min_value, max_value, step);
  (void)ref.set_value(value);
  return node;
}

Scene open(dg::App& app, int width, int height) {
  dg::WindowOptions options;
  options.title = "drawgui form controls";
  options.width = width;
  options.height = height;
  options.background = dg::Color::from_argb(0xFF0E1218);
  dg::Expected<dg::Window*, dg::AppError> made = app.create_window(options);
  if (!made) {
    std::cerr << "create_window() failed: " << made.error().message << "\n";
    return {};
  }
  dg::Window& w = *made.value();
  Scene s;
  s.window = &w;
  s.body = w.create(NodeKind::kBox).value();
  (void)w.set(s.body, {dg::prop::width_percent(100), dg::prop::height_percent(100),
                       dg::prop::column(), dg::prop::gap(20),
                       dg::prop::background(options.background)});
  (void)w.set_prop(s.body, DG_PROP_ALIGN, dg::PropValue::option(DG_ALIGN_STRETCH));
  pad(w, s.body, 20);

  s.checkbox = checkbox(w, section(w, s.body));

  const NodeId group_a = section(w, s.body);
  for (int i = 0; i < 3; ++i) {
    s.radio_a.push_back(radio(w, group_a, kGroupA));
  }
  const NodeId group_b = section(w, s.body);
  for (int i = 0; i < 2; ++i) {
    s.radio_b.push_back(radio(w, group_b, kGroupB));
  }

  const NodeId sliders = section(w, s.body);
  s.volume = slider(w, sliders, 0, kVolumeMin, kVolumeMax, 0.0F, kVolumeInitial);
  s.brightness = slider(w, sliders, kFixedTrackWidth, kBrightnessMin, kBrightnessMax,
                        kBrightnessStep, kBrightnessInitial);

  const NodeId disabled_row = section(w, s.body);
  s.enable_switch = checkbox(w, disabled_row);
  s.disabled_group = w.create(NodeKind::kBox, disabled_row).value();
  (void)w.set(s.disabled_group, {dg::prop::row(), dg::prop::gap(24), dg::prop::grow(1),
                                 dg::prop::background(dg::Color{})});
  (void)w.set_prop(s.disabled_group, DG_PROP_ALIGN, dg::PropValue::option(DG_ALIGN_CENTER));
  s.disabled_checkbox = checkbox(w, s.disabled_group);
  s.disabled_slider =
      slider(w, s.disabled_group, 0, kVolumeMin, kVolumeMax, 0.0F, kVolumeInitial);
  (void)w.set_disabled(s.disabled_group, true);
  return s;
}

// The app's only reaction: the enable switch's click (the checkbox has
// already flipped) enables or disables the row beside it.
void react(const Scene& s, const dg::Event& event) {
  const auto* click = dg::event_as<dg::events::ClickEvent>(event);
  if (click != nullptr && click->node == s.enable_switch) {
    (void)s.window->set_disabled(s.disabled_group, !s.window->checked(s.enable_switch).value());
  }
}

std::vector<dg::Event> react_all(const Scene& s, std::vector<dg::Event> events) {
  for (const dg::Event& event : events) {
    react(s, event);
  }
  return events;
}

std::string describe(const Scene& s, std::optional<NodeId> id) {
  if (!id.has_value()) {
    return "<nothing>";
  }
  for (std::size_t i = 0; i < s.radio_a.size(); ++i) {
    if (s.radio_a[i] == *id) {
      return "radio a " + std::to_string(i);
    }
  }
  for (std::size_t i = 0; i < s.radio_b.size(); ++i) {
    if (s.radio_b[i] == *id) {
      return "radio b " + std::to_string(i);
    }
  }
  const std::pair<NodeId, const char*> named[] = {
      {s.checkbox, "checkbox"},
      {s.enable_switch, "enable switch"},
      {s.disabled_checkbox, "disableable checkbox"},
      {s.disabled_slider, "disableable slider"},
      {s.volume, "slider volume"},
      {s.brightness, "slider brightness"},
  };
  for (const auto& [node, name] : named) {
    if (node == *id) {
      return name;
    }
  }
  return "node " + std::to_string(id->index);
}

// The formula WidgetSet::reposition_slider() implements, recomputed here so
// a defect in the widget code has something external to disagree with.
int expected_thumb_x(float value, float min_value, float max_value, int track_width,
                     int thumb_width) {
  const int travel = std::max(0, track_width - thumb_width);
  const float fraction = (value - min_value) / (max_value - min_value);
  return static_cast<int>(
      std::lround(static_cast<double>(fraction) * static_cast<double>(travel)));
}

NodeId thumb_of(const dg::Window& w, NodeId slider_node) {
  return w.layout().render().children(slider_node).front();
}

NodeId dot_of(const dg::Window& w, NodeId toggle) {
  return w.layout().render().children(toggle).front();
}

int verify(dg::App& app, std::ostream& out) {
  const Scene s = open(app, kDefaultWidth, kDefaultHeight);
  if (s.window == nullptr) {
    return 1;
  }
  dg::Window& w = *s.window;
  example::Driver drive(app, w);
  const auto click = [&](NodeId node) { return react_all(s, drive.click(node)); };
  drive.pump();
  bool ok = true;
  auto check = [&](bool pass, const std::string& what) { example::check(pass, what, out, ok); };
  const dg::RenderTree& tree = w.layout().render();
  const auto checked = [&](NodeId node) { return w.checked(node).value(); };

  out << "claim 1: the plain checkbox flips freely and has no group\n";
  check(!checked(s.checkbox), "the checkbox starts clear");
  click(s.checkbox);
  check(checked(s.checkbox), "one click checks it");
  click(s.checkbox);
  check(!checked(s.checkbox), "a second click clears it (a plain checkbox is not a radio)");
  check(w.widgets().group_members(s.checkbox).empty(), "it reports no group members");

  out << "\nclaim 2: radio group exclusivity and cross-group independence\n";
  check(std::none_of(s.radio_a.begin(), s.radio_a.end(), checked),
        "group A starts with nothing selected");
  click(s.radio_a[0]);
  check(checked(s.radio_a[0]), "clicking a/0 selects it");
  click(s.radio_a[2]);
  check(checked(s.radio_a[2]) && !checked(s.radio_a[0]) && !checked(s.radio_a[1]),
        "clicking a/2 selects it and clears every other option of group A");
  click(s.radio_a[2]);
  check(checked(s.radio_a[2]), "re-clicking the selected a/2 keeps it (no toggle-off)");
  check(std::none_of(s.radio_b.begin(), s.radio_b.end(), checked),
        "group B is untouched by group A's selections");
  click(s.radio_b[1]);
  check(checked(s.radio_b[1]) && checked(s.radio_a[2]),
        "selecting b/1 leaves group A's selection alone");

  out << "\nclaim 2b: a real click repaints the selected dot AND the sibling it clears\n";
  const dg::Color off = tree.style(dot_of(w, s.radio_a[1])).fill;
  click(s.radio_a[0]);
  check(tree.style(dot_of(w, s.radio_a[0])).fill != off, "a/0's dot paints as selected");
  check(tree.style(dot_of(w, s.radio_a[2])).fill == off,
        "a/2's dot, deselected by that click, paints as clear again");

  out << "\nclaim 3: slider clamping, snapping and hand-derived thumb geometry\n";
  dg::SliderRef volume = w.slider(s.volume).value();
  dg::SliderRef brightness = w.slider(s.brightness).value();
  const dg::PixelRect volume_track = drive.bounds(s.volume);
  dg::PixelRect volume_thumb = tree.local_bounds(thumb_of(w, s.volume));
  check(volume_thumb.x == expected_thumb_x(kVolumeInitial, kVolumeMin, kVolumeMax,
                                           volume_track.width, volume_thumb.width),
        "the volume thumb starts at 30% of its travel");
  check(volume_thumb.y == (volume_track.height - volume_thumb.height) / 2,
        "the thumb is centred on the track vertically");
  (void)volume.set_value(-1000.0F);
  drive.pump();
  check(volume.value().value() == kVolumeMin &&
            tree.local_bounds(thumb_of(w, s.volume)).x == 0,
        "far below the minimum clamps to 0, the thumb flush with the track's start");
  (void)volume.set_value(1000.0F);
  drive.pump();
  check(volume.value().value() == kVolumeMax &&
            tree.local_bounds(thumb_of(w, s.volume)).x ==
                volume_track.width - volume_thumb.width,
        "far above the maximum clamps to 100, the thumb flush with the track's end");
  const dg::PixelRect brightness_track = drive.bounds(s.brightness);
  const dg::PixelRect brightness_thumb = tree.local_bounds(thumb_of(w, s.brightness));
  check(brightness_thumb.x == expected_thumb_x(kBrightnessInitial, kBrightnessMin,
                                               kBrightnessMax, brightness_track.width,
                                               brightness_thumb.width),
        "the stepped brightness thumb starts at 3 of 10");
  (void)brightness.set_value(4.4F);
  const float snapped_down = brightness.value().value();
  (void)brightness.set_value(4.6F);
  check(snapped_down == 4.0F && brightness.value().value() == 5.0F,
        "under step = 1, 4.4 snaps to 4 and 4.6 to 5");

  out << "\nclaim 3b: the pointer and the keys move a slider and report SliderEvent\n";
  (void)volume.set_value(kVolumeInitial);
  drive.pump();
  const int y = volume_track.y + (volume_track.height / 2);
  std::vector<dg::events::SliderEvent> moved = example::events_of<dg::events::SliderEvent>(
      drive.click(volume_track.x + volume_thumb.width / 2, y));
  check(moved.size() == 1 && moved[0].value == kVolumeMin &&
            moved[0].previous == kVolumeInitial,
        "a press at the track's start moves it to 0 and reports the previous 30");
  drive.press(volume_track.x, y, true);
  drive.move(volume_track.x + volume_track.width + 50, y);
  drive.press(volume_track.x + volume_track.width + 50, y, false);
  check(volume.value().value() == kVolumeMax, "a drag past the track's end ends at 100");
  check(w.focused() == s.volume, "the pressed slider has focus");
  drive.key(dg::Key::kHome);
  drive.key(dg::Key::kPageUp);
  check(volume.value().value() == kVolumeMax / 10.0F,
        "Home then PageUp: 0, then a tenth of the range");

  out << "\nclaim 4: a resize widens the grow-weighted track; its thumb keeps its fraction\n";
  (void)volume.set_value(kVolumeInitial);
  drive.pump();
  const int brightness_x_before = tree.local_bounds(thumb_of(w, s.brightness)).x;
  (void)w.set_size(kDefaultWidth + 400, kDefaultHeight);
  drive.pump(6);
  const dg::PixelRect volume_after = drive.bounds(s.volume);
  check(volume_after.width > volume_track.width,
        "widening the window by 400px widens the flexible track (" +
            std::to_string(volume_track.width) + " -> " + std::to_string(volume_after.width) +
            ")");
  check(drive.bounds(s.brightness).width == brightness_track.width &&
            tree.local_bounds(thumb_of(w, s.brightness)).x == brightness_x_before,
        "the fixed-width slider's track and thumb do not move");
  volume_thumb = tree.local_bounds(thumb_of(w, s.volume));
  check(volume_thumb.x == expected_thumb_x(kVolumeInitial, kVolumeMin, kVolumeMax,
                                           volume_after.width, volume_thumb.width),
        "the volume thumb sits at 30% of the NEW travel");

  out << "\nclaim 5: moving a slider costs a repaint, never a relayout\n";
  (void)w.layout().layout();
  (void)volume.set_value(70.0F);
  const dg::LayoutStats stats = w.layout().layout();
  check(stats.nodes_visited == 0 && stats.nodes_relaid_out == 0,
        "layout() visits 0 nodes after a value-only change");

  out << "\nclaim 6: re-placing the thumbs never moves the root\n";
  const dg::Expected<dg::PixelSize, dg::WindowError> size = w.size();
  check(size.has_value() && tree.local_bounds(dg::RenderTree::root()) ==
                                dg::PixelRect{0, 0, size.value().width, size.value().height},
        "the root keeps the window's rectangle");

  out << "\nclaim 7: DG_PROP_DISABLED on the demo row, toggled by the enable switch\n";
  const std::optional<dg::Color> disabled_fill = w.widgets().disabled_palette().fill;
  click(s.disabled_checkbox);
  check(!checked(s.disabled_checkbox), "a click on the disabled checkbox does not toggle it");
  dg::SliderRef row_slider = w.slider(s.disabled_slider).value();
  const dg::PixelRect row_track = drive.bounds(s.disabled_slider);
  drive.click(row_track.x + row_track.width - 5, row_track.y + 5);
  check(row_slider.value().value() == kVolumeInitial, "the disabled slider cannot be moved");
  check(disabled_fill.has_value() && tree.style(s.disabled_checkbox).fill == *disabled_fill,
        "the disabled checkbox paints the theme's color.disabled");
  click(s.enable_switch);
  click(s.disabled_checkbox);
  drive.click(row_track.x + row_track.width - 5, row_track.y + 5);
  check(checked(s.disabled_checkbox) && row_slider.value().value() > kVolumeInitial &&
            tree.style(s.disabled_checkbox).fill != *disabled_fill,
        "after the enable switch the row is interactive again");
  click(s.enable_switch);
  click(s.disabled_checkbox);
  check(checked(s.disabled_checkbox), "switching it off again disables it again");

  out << "\nclaim 8: the incremental frame equals a full repaint, byte for byte\n";
  std::optional<dg::RasterSurface> whole = dg::RasterSurface::create(
      w.frame()->peek_pixels().width, w.frame()->peek_pixels().height);
  bool identical = whole.has_value();
  const float volumes[] = {30.0F, 0.0F, 55.0F, 100.0F, 12.0F};
  const float brightnesses[] = {3.0F, 7.4F, 0.0F, 10.0F, 5.0F};
  for (std::size_t step = 0; identical && step < std::size(volumes); ++step) {
    (void)volume.set_value(volumes[step]);
    (void)brightness.set_value(brightnesses[step]);
    if (step % 2 == 0) {
      click(s.radio_a[step % s.radio_a.size()]);
    } else {
      click(s.checkbox);
    }
    drive.pump();
    w.layout().render().repaint_full(*whole);
    const dg::PixelView a = w.frame()->peek_pixels();
    const dg::PixelView b = whole->peek_pixels();
    const auto row_len = static_cast<std::size_t>(a.width) * 4U;
    for (std::size_t row = 0; identical && row < static_cast<std::size_t>(a.height); ++row) {
      identical = std::equal(a.pixels + (row * a.row_bytes),
                             a.pixels + (row * a.row_bytes) + row_len,
                             b.pixels + (row * b.row_bytes));
    }
  }
  check(identical,
        "across radio clicks, checkbox clicks and both sliders, the runtime's damaged frame "
        "matches a full repaint");

  out << "\n" << (ok ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED") << "\n";
  return ok ? 0 : 1;
}

void apply_presets(dg::App& app, const Scene& s, const Options& options) {
  dg::Window& w = *s.window;
  example::Driver drive(app, w);
  drive.pump();
  if (options.preset_radio_a >= 0 &&
      static_cast<std::size_t>(options.preset_radio_a) < s.radio_a.size()) {
    (void)w.radio(s.radio_a[static_cast<std::size_t>(options.preset_radio_a)]).value().select();
  }
  if (options.preset_volume.has_value()) {
    (void)w.slider(s.volume).value().set_value(*options.preset_volume);
  }
  if (options.preset_checkbox) {
    react_all(s, drive.click(s.checkbox));
  }
  drive.pump();
}

int dump_png(dg::App& app, const Options& options, std::ostream& out) {
  const Scene s = open(app, options.width, options.height);
  if (s.window == nullptr) {
    return 1;
  }
  apply_presets(app, s, options);
  out << "volume " << s.window->slider(s.volume).value().value().value() << "  brightness "
      << s.window->slider(s.brightness).value().value().value() << "\n";
  return example::write_png(*s.window, options.png_path, out);
}

int probe(dg::App& app, const Options& options, std::ostream& out) {
  const Scene s = open(app, options.width, options.height);
  if (s.window == nullptr) {
    return 1;
  }
  apply_presets(app, s, options);
  const dg::RasterSurface* frame = s.window->frame();
  if (frame == nullptr) {
    out << "no frame was painted\n";
    return 1;
  }
  const dg::PixelView view = frame->peek_pixels();
  const dg::PixelPoint at = options.probe;
  if (at.x < 0 || at.y < 0 || at.x >= view.width || at.y >= view.height) {
    out << "  " << at.x << "," << at.y << " is outside the frame\n";
    return 1;
  }
  const std::uint8_t* px =
      view.pixels + (static_cast<std::size_t>(at.y) * view.row_bytes) + (at.x * 4);
  const std::optional<NodeId> hit = s.window->layout().render().hit_test(at);
  std::optional<NodeId> created;
  for (std::optional<NodeId> node = hit; node.has_value();) {
    if (s.window->kind_of(*node).has_value()) {
      created = node;
      break;
    }
    const NodeId parent = s.window->layout().render().parent(*node);
    node = parent == *node ? std::nullopt : std::optional<NodeId>(parent);
  }
  out << "  at " << at.x << "," << at.y << "  pixel #" << std::hex
      << ((static_cast<unsigned>(px[2]) << 16U) | (static_cast<unsigned>(px[1]) << 8U) |
          static_cast<unsigned>(px[0]))
      << std::dec << "  hit " << describe(s, created) << "\n";
  return 0;
}

int run_window(dg::App& app, const Options& options) {
  const Scene s = open(app, options.width, options.height);
  if (s.window == nullptr) {
    return 1;
  }
  apply_presets(app, s, options);
  std::cout << "form controls demo: click the checkbox/radios, drag or arrow-key a slider.\n"
            << "  the bottom-left switch enables/disables the row beside it.\n";
  example::close_after(app, options.run_ms);
  app.run([&](const dg::Event& event) {
    react(s, event);
    if (dg::event_as<dg::events::TimerEvent>(event) != nullptr) {
      app.quit();
    } else if (const auto* moved = dg::event_as<dg::events::SliderEvent>(event)) {
      std::cout << "  " << describe(s, moved->node) << " -> " << moved->value << "\n";
    } else if (const auto* click = dg::event_as<dg::events::ClickEvent>(event)) {
      if (click->node.has_value() && s.window->checked(*click->node).has_value()) {
        std::cout << "  " << describe(s, click->node) << " -> "
                  << (s.window->checked(*click->node).value() ? "on" : "off") << "\n";
      }
    }
  });
  return 0;
}

bool parse_pair(const std::string& text, char separator, int& first, int& second) {
  const std::size_t at = text.find(separator);
  if (at == std::string::npos) {
    return false;
  }
  const std::optional<int> left = example::parse_int(text.substr(0, at));
  const std::optional<int> right = example::parse_int(text.substr(at + 1));
  if (!left.has_value() || !right.has_value()) {
    return false;
  }
  first = *left;
  second = *right;
  return true;
}

std::optional<float> parse_float(const std::string& text) {
  float value = 0.0F;
  const char* end = text.data() + text.size();
  const std::from_chars_result parsed = std::from_chars(text.data(), end, value);
  if (parsed.ec != std::errc{} || parsed.ptr != end) {
    return std::nullopt;
  }
  return value;
}

std::optional<Options> parse(int argc, char** argv) {
  Options options;
  const std::vector<std::string> args{argv + 1, argv + argc};
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string& arg = args[i];
    const bool has_value = i + 1 < args.size();
    if (arg == "--verify-form-controls") {
      options.mode = Mode::kVerify;
    } else if (arg == "--preset-checkbox") {
      options.preset_checkbox = true;
    } else if (arg == "--dump-png" && has_value) {
      options.mode = Mode::kDumpPng;
      options.png_path = args[++i];
    } else if (arg == "--probe" && has_value) {
      options.mode = Mode::kProbe;
      if (!parse_pair(args[++i], ',', options.probe.x, options.probe.y)) {
        return std::nullopt;
      }
    } else if (arg == "--size" && has_value) {
      if (!parse_pair(args[++i], 'x', options.width, options.height) || options.width < 1 ||
          options.height < 1) {
        return std::nullopt;
      }
    } else if (arg == "--run-ms" && has_value) {
      options.run_ms = example::parse_int(args[++i]).value_or(0);
    } else if (arg == "--font-dir" && has_value) {
      options.font_dir = args[++i];
    } else if (arg == "--preset-radio-a" && has_value) {
      options.preset_radio_a = example::parse_int(args[++i]).value_or(-1);
    } else if (arg == "--preset-volume" && has_value) {
      options.preset_volume = parse_float(args[++i]);
      if (!options.preset_volume.has_value()) {
        return std::nullopt;
      }
    } else {
      return std::nullopt;
    }
  }
  return options;
}

}  // namespace

int main(int argc, char** argv) {
  const std::optional<Options> options = parse(argc, argv);
  if (!options.has_value()) {
    std::cerr << "usage: drawgui_form_controls [--size WxH] [--run-ms N] [--font-dir DIR]\n"
              << "                             [--preset-radio-a N] [--preset-volume V]\n"
              << "                             [--preset-checkbox] [--probe X,Y]\n"
              << "                             [--dump-png FILE] [--verify-form-controls]\n";
    return 2;
  }
  if (options->mode != Mode::kWindow) {
    example::use_headless_driver();
  }
  dg::Expected<dg::App, dg::AppError> app = dg::App::create();
  if (!app) {
    std::cerr << "no app: " << app.error().message << "\n";
    return 1;
  }
  switch (options->mode) {
    case Mode::kVerify:
      return verify(app.value(), std::cout);
    case Mode::kDumpPng:
      return dump_png(app.value(), *options, std::cout);
    case Mode::kProbe:
      return probe(app.value(), *options, std::cout);
    case Mode::kWindow:
      break;
  }
  return run_window(app.value(), *options);
}
