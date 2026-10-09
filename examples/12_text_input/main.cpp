// examples/12_text_input: two single-line text fields on dg::App /
// dg::Window.
//
// The runtime does the editing: a click focuses a field and places its
// caret, typed and IME text goes to the focused field, Backspace/Delete/
// Home/End/arrows (with Shift to select) edit it, Mod+C/X/V/A use the
// system clipboard, and an unfocused field that overflows shows an
// ellipsis. TextChangedEvent reports each edit.
//
//   (no arguments)         the demo window - click a field, type, select
//   --size WxH             open at a given size
//   --run-ms N             close after N milliseconds
//   --preset-field-b TEXT  pre-fill field b (offscreen modes only)
//   --preset-focus-a       open with field a focused
//   --preset-select-a      ... and its first 19 bytes selected
//   --preset-compose-b     field b mid-IME-composition ("你好" preedit)
//   --font-dir DIR         where "DejaVu Sans" is looked for
//   --verify-text-input    the headless check. No display needed.
//   --dump-png FILE        one frame to PNG. No display needed.

#include <charconv>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "drawgui/app/app.h"
#include "drawgui/app/prop.h"
#include "drawgui/app/window.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/render/prop_ids.generated.h"
#include "drawgui/shortcuts/logical_key.generated.h"
#include "drawgui/widget/widget_set.h"
#include "example_app.h"

namespace {

using dg::NodeId;

constexpr int kFieldWidth = 260;
constexpr int kFieldHeight = 36;
constexpr const char* kFieldAInitial = "The quick brown fox jumps over the lazy dog";

enum class Mode : std::uint8_t { kWindow, kVerify, kDumpPng };

struct Options {
  Mode mode = Mode::kWindow;
  dg::PixelSize size{420, 220};
  int run_ms = 0;
  std::string preset_field_b;
  bool preset_focus_a = false;
  bool preset_select_a = false;
  bool preset_compose_b = false;
  std::string font_dir = "/usr/share/fonts";
  std::string png_path;
};

struct Scene {
  dg::Window* window = nullptr;
  NodeId field_a;
  NodeId field_b;
};

NodeId field(dg::Window& w, NodeId parent, const std::string& text) {
  const NodeId node = w.create(dg::NodeKind::kTextField, parent).value();
  (void)w.set(node, {dg::prop::width(kFieldWidth), dg::prop::height(kFieldHeight),
                     dg::prop::font_size(16)});
  (void)w.set_text(node, DG_PROP_TEXT, text);
  return node;
}

Scene open(dg::App& app, dg::PixelSize size) {
  dg::WindowOptions options;
  options.title = "drawgui text input";
  options.width = size.width;
  options.height = size.height;
  dg::Expected<dg::Window*, dg::AppError> made = app.create_window(options);
  if (!made) {
    std::cerr << "create_window() failed: " << made.error().message << "\n";
    return {};
  }
  dg::Window& w = *made.value();
  const NodeId body = w.create(dg::NodeKind::kBox).value();
  (void)w.set(body, {dg::prop::width_percent(100), dg::prop::height_percent(100),
                     dg::prop::column(), dg::prop::gap(20)});
  for (const std::uint16_t id :
       {DG_PROP_PADDING_L, DG_PROP_PADDING_T, DG_PROP_PADDING_R, DG_PROP_PADDING_B}) {
    (void)w.set_prop(body, id, dg::PropValue::number(24));
  }
  return Scene{&w, field(w, body, kFieldAInitial), field(w, body, "")};
}

std::vector<std::uint8_t> bytes_of(const dg::RasterSurface& surface) {
  const dg::PixelView view = surface.peek_pixels();
  const auto* data = static_cast<const std::uint8_t*>(view.pixels);
  return {data, data + (view.row_bytes * static_cast<std::size_t>(view.height))};
}

int verify(dg::App& app, std::ostream& out) {
  const Scene s = open(app, {420, 220});
  if (s.window == nullptr) {
    return 1;
  }
  dg::Window& w = *s.window;
  example::Driver drive(app, w);
  drive.pump();
  bool ok = true;
  auto check = [&](bool pass, const std::string& what) { example::check(pass, what, out, ok); };
  const dg::RenderTree& tree = w.layout().render();
  const dg::Widget& a = w.widgets().at(s.field_a);
  const dg::Widget& b = w.widgets().at(s.field_b);
  auto shown = [&](const dg::Widget& f) { return tree.style(f.content).text.text; };
  auto caret_visible = [&](const dg::Widget& f) { return tree.local_bounds(f.caret).width != 0; };
  auto text_b = [&] { return w.text(s.field_b).value(); };
  dg::WindowManager& wm = app.window_manager();

  out << "focus: a click focuses a field, and focus is exclusive\n";
  check(!w.focused().has_value(), "a freshly built window starts with nothing focused");
  const std::string first_display = shown(a);
  drive.click(s.field_a);
  check(w.focused() == s.field_a, "clicking field a focuses it");
  drive.click(s.field_b);
  check(w.focused() == s.field_b && !caret_visible(a),
        "clicking field b focuses it, and field a's caret is hidden");

  out << "\nediting: typing, Backspace, Delete, Home/End\n";
  wm.post_text_input(w.id(), "hello");
  drive.pump();
  check(text_b() == "hello", "typing \"hello\" into an empty field produces it");
  drive.key(dg::Key::kBackspace);
  check(text_b() == "hell", "Backspace removes the last character");
  drive.key(dg::Key::kHome);
  drive.key(dg::Key::kDelete);
  check(text_b() == "ell", "Delete at the start removes the first character");
  drive.key(dg::Key::kEnd);
  wm.post_text_input(w.id(), "Z");
  drive.pump();
  check(text_b() == "ellZ", "End moves the cursor to the end (typing lands there)");

  out << "\noverflow: ellipsis while unfocused, full text while focused\n";
  check(first_display != kFieldAInitial && first_display.ends_with("...") &&
            shown(a) == first_display,
        "unfocused, field a shows an ellipsis-truncated prefix from the first frame on (\"" +
            first_display + "\")");
  check(!caret_visible(a), "an unfocused field's caret is hidden");
  drive.click(s.field_a);
  check(shown(a) == kFieldAInitial && caret_visible(a),
        "focused, field a shows its full string and a visible caret");

  out << "\nidentity: the incremental frame equals a full repaint\n";
  drive.click(s.field_b);
  wm.post_text_input(w.id(), "drawgui");
  drive.pump();
  drive.key(dg::Key::kHome, true);
  drive.key(dg::Key::kBackspace);
  drive.click(s.field_a);
  drive.key(dg::Key::kLeft, true);
  drive.click(400, 200);
  const dg::RasterSurface* frame = w.frame();
  std::optional<dg::RasterSurface> whole =
      frame != nullptr ? dg::RasterSurface::create(frame->width(), frame->height())
                       : std::nullopt;
  if (whole.has_value()) {
    w.layout().render().repaint_full(*whole);
  }
  check(whole.has_value() && bytes_of(*frame) == bytes_of(*whole),
        "after focus changes, typing, selection and Backspace, the runtime's incremental "
        "frame matches a full repaint byte for byte");

  out << "\ngraphemes: CJK and a ZWJ emoji edit as whole clusters\n";
  drive.click(s.field_b);
  drive.key(dg::Key::kEnd);
  drive.key(dg::Key::kHome, true);
  drive.key(dg::Key::kBackspace);
  const std::string cjk = "\xE4\xB8\xAD\xE6\x96\x87";
  wm.post_text_input(w.id(), cjk);
  drive.pump();
  check(text_b() == cjk, "typing \"中文\" produces it");
  drive.key(dg::Key::kBackspace);
  check(text_b() == "\xE4\xB8\xAD", "one Backspace removes exactly one CJK character");
  drive.key(dg::Key::kBackspace);
  check(text_b().empty(), "a second Backspace clears the field");
  const std::string family = "\U0001F468\u200D\U0001F469\u200D\U0001F467\u200D\U0001F466";
  wm.post_text_input(w.id(), "X" + family);
  drive.pump();
  check(text_b() == "X" + family, "typing \"X\" + the ZWJ family emoji produces it");
  drive.key(dg::Key::kBackspace);
  check(text_b() == "X", "one Backspace removes all 25 bytes of the emoji");
  drive.key(dg::Key::kBackspace);

  out << "\nIME: the preview never touches the committed text\n";
  wm.post_text_editing(w.id(), "hi", 2, 0);
  drive.pump();
  check(w.widgets().text_field_is_composing(s.field_b), "a preedit enters the composing state");
  check(text_b().empty() && shown(b) == "hi",
        "the preview \"hi\" shows inline without touching the committed text");
  check(tree.local_bounds(b.composition_underline).width > 0,
        "a non-empty composition shows an underline");
  wm.post_text_editing(w.id(), "hell", 4, 0);
  wm.post_text_editing(w.id(), "h", 1, 0);
  wm.post_text_editing(w.id(), "", 0, 0);
  drive.pump();
  check(!w.widgets().text_field_is_composing(s.field_b) && text_b().empty() && shown(b).empty(),
        "growing, shrinking, then an empty preedit (a cancel) commits nothing");
  wm.post_text_editing(w.id(), "hi", 2, 0);
  wm.post_text_input(w.id(), "hi");
  drive.pump();
  check(!w.widgets().text_field_is_composing(s.field_b) && text_b() == "hi",
        "a commit ends composition and inserts \"hi\"");
  drive.key(dg::Key::kHome);
  drive.key(dg::Key::kRight, true);
  wm.post_text_editing(w.id(), "X", 1, 0);
  drive.pump();
  check(text_b() == "hi" && shown(b) == "Xi",
        "composing over a selected \"h\" previews \"Xi\" and leaves the text alone");
  wm.post_text_editing(w.id(), "", 0, 0);
  drive.pump();

  out << "\nclipboard: real Mod+C/V/A/X through the event queue\n";
  drive.key(dg::Key::kEnd);
  drive.key(dg::Key::kHome, true);
  drive.key(dg::Key::kBackspace);
  wm.post_text_input(w.id(), "hello world");
  drive.pump();
  drive.key(dg::Key::kHome);
  for (int i = 0; i < 5; ++i) {
    drive.key(dg::Key::kRight, true);
  }
  auto chord = [&](dg::LogicalKey key) {
    wm.post_logical_key(w.id(), true, key, dg::Modifier::kMod);
    drive.pump();
  };
  chord(dg::LogicalKey::kC);
  check(wm.get_clipboard_text() == "hello", "Mod+C copies the selection \"hello\"");
  drive.key(dg::Key::kEnd);
  chord(dg::LogicalKey::kV);
  check(text_b() == "hello worldhello", "Mod+V at the end appends it");
  chord(dg::LogicalKey::kA);
  chord(dg::LogicalKey::kX);
  check(text_b().empty() && wm.get_clipboard_text() == "hello worldhello",
        "Mod+A then Mod+X empties the field onto the clipboard");

  out << (ok ? "PASS\n" : "FAIL\n");
  return ok ? 0 : 1;
}

int dump_png(dg::App& app, const Options& options, std::ostream& out) {
  const Scene s = open(app, options.size);
  if (s.window == nullptr) {
    return 1;
  }
  dg::Window& w = *s.window;
  example::Driver drive(app, w);
  drive.pump();
  dg::WindowManager& wm = app.window_manager();
  if (!options.preset_field_b.empty()) {
    (void)w.set_text(s.field_b, DG_PROP_TEXT, options.preset_field_b);
  }
  if (options.preset_focus_a || options.preset_select_a) {
    (void)w.focus(s.field_a);
    drive.pump();
  }
  if (options.preset_select_a) {
    drive.key(dg::Key::kHome);
    for (int i = 0; i < 19; ++i) {
      drive.key(dg::Key::kRight, true);
    }
  }
  // Composing needs field b focused, so it comes last.
  if (options.preset_compose_b) {
    (void)w.focus(s.field_b);
    drive.pump();
    wm.post_text_editing(w.id(), "\xE4\xBD\xA0\xE5\xA5\xBD", 3, 3);
    drive.pump();
  }
  return example::write_png(w, options.png_path, out);
}

int run_window(dg::App& app, const Options& options) {
  const Scene s = open(app, options.size);
  if (s.window == nullptr) {
    return 1;
  }
  example::close_after(app, options.run_ms);
  app.run([&](const dg::Event& event) {
    if (dg::event_as<dg::events::TimerEvent>(event) != nullptr) {
      app.quit();
    } else if (const auto* changed = dg::event_as<dg::events::TextChangedEvent>(event)) {
      if (changed->node.has_value()) {
        std::cout << "text: \"" << s.window->text(*changed->node).value_or("") << "\"\n";
      }
    }
  });
  return 0;
}

std::optional<int> parse_int(const std::string& text) {
  int value = 0;
  const char* end = text.data() + text.size();
  const std::from_chars_result parsed = std::from_chars(text.data(), end, value);
  if (parsed.ec != std::errc{} || parsed.ptr != end || value < 0) {
    return std::nullopt;
  }
  return value;
}

std::optional<dg::PixelSize> parse_size(const std::string& value) {
  const std::size_t x = value.find('x');
  const std::optional<int> width = parse_int(value.substr(0, x));
  const std::optional<int> height =
      x == std::string::npos ? std::nullopt : parse_int(value.substr(x + 1));
  if (!width || !height || *width < 1 || *height < 1) {
    return std::nullopt;
  }
  return dg::PixelSize{*width, *height};
}

std::optional<Options> parse(int argc, char** argv) {
  Options options;
  const std::vector<std::string> args{argv + 1, argv + argc};
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string& arg = args[i];
    if (arg == "--verify-text-input") {
      options.mode = Mode::kVerify;
    } else if (arg == "--preset-focus-a") {
      options.preset_focus_a = true;
    } else if (arg == "--preset-select-a") {
      options.preset_select_a = true;
    } else if (arg == "--preset-compose-b") {
      options.preset_compose_b = true;
    } else if (i + 1 < args.size() && arg == "--dump-png") {
      options.mode = Mode::kDumpPng;
      options.png_path = args[++i];
    } else if (i + 1 < args.size() && arg == "--preset-field-b") {
      options.preset_field_b = args[++i];
    } else if (i + 1 < args.size() && arg == "--font-dir") {
      options.font_dir = args[++i];
    } else if (i + 1 < args.size() && arg == "--run-ms") {
      const std::optional<int> ms = parse_int(args[++i]);
      if (!ms) {
        return std::nullopt;
      }
      options.run_ms = *ms;
    } else if (i + 1 < args.size() && arg == "--size") {
      const std::optional<dg::PixelSize> size = parse_size(args[++i]);
      if (!size) {
        return std::nullopt;
      }
      options.size = *size;
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
    std::cerr << "usage: drawgui_text_input [--size WxH] [--run-ms N] [--preset-field-b TEXT]\n"
              << "         [--preset-focus-a] [--preset-select-a] [--preset-compose-b]\n"
              << "         [--font-dir DIR] [--dump-png FILE] [--verify-text-input]\n";
    return 2;
  }
  if (options->mode != Mode::kWindow) {
    example::use_headless_driver();
  }
  dg::Expected<dg::App, dg::AppError> app =
      example::make_app(options->font_dir, "DejaVu Sans", std::cout);
  if (!app) {
    return 1;
  }
  switch (options->mode) {
    case Mode::kVerify:
      return verify(app.value(), std::cout);
    case Mode::kDumpPng:
      return dump_png(app.value(), *options, std::cout);
    case Mode::kWindow:
      break;
  }
  return run_window(app.value(), *options);
}
