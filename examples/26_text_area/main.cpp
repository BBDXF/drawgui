// examples/26_text_area: DG_NODE_TYPE_TEXT_AREA through the C ABI.
//
// A window filled by one multi-line text area pre-filled with a document
// long enough to scroll. Type, Enter, arrows, Home/End,
// PageUp/PageDown, drag to select, wheel or drag the scrollbar.
//
//   drawgui_text_area                     interactive window
//   drawgui_text_area --run-ms N          ... closing itself after N ms
//   drawgui_text_area --dump-png FILE     scripted edits, then a PNG of the frame
//   drawgui_text_area --verify-text-area  headless self-check (CTest)

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "drawgui/abi/drawgui.h"
#include "drawgui/render/prop_ids.generated.h"

namespace {

constexpr int kWindowWidth = 520;
constexpr int kWindowHeight = 360;

dg_value length(float number) {
  dg_value value{};
  value.size = sizeof(dg_value);
  value.type = DG_VALUE_LENGTH;
  value.number = number;
  return value;
}

void set(dg_node_t* node, std::uint16_t prop, const dg_value& value) {
  (void)dg_node_set_prop(node, prop, &value);
}

void set_text(dg_node_t* node, std::string_view text) {
  (void)dg_node_set_text(node, DG_PROP_TEXT, text.data(),
                         static_cast<std::uint32_t>(text.size()));
}

std::string sample_document() {
  std::string doc =
      "drawgui text area: a multi-line editor over the shared editing core. Long lines like "
      "this one soft-wrap at the area's width, and Home/End follow the wrapped rows.\n\n";
  for (int i = 1; i <= 40; ++i) {
    doc += "Line " + std::to_string(i) + ": the quick brown fox jumps over the lazy dog.\n";
  }
  return doc;
}

struct Demo {
  dg_app_t* app = nullptr;
  dg_window_t* window = nullptr;
  dg_node_t* area = nullptr;
};

// Every system family tried in order, then the deterministic test font.
bool add_font(dg_app_t* app, bool deterministic) {
  if (!deterministic) {
    for (const char* family : {"Noto Sans", "DejaVu Sans", "Ubuntu Sans", "Liberation Sans"}) {
      if (dg_app_add_font(app, "/usr/share/fonts", family) == DG_ERR_OK) {
        return true;
      }
    }
  }
  return dg_app_add_font(app, DRAWGUI_TEXT_AREA_FONT_DIR, "DgTest Latin") == DG_ERR_OK;
}

bool build(Demo& demo, bool deterministic) {
  demo.app = dg_app_create(nullptr);
  if (demo.app == nullptr || !add_font(demo.app, deterministic)) {
    return false;
  }
  dg_window_opts opts{};
  opts.size = sizeof(opts);
  opts.title = "drawgui: text area";
  opts.width = kWindowWidth;
  opts.height = kWindowHeight;
  opts.background_argb = 0xFF0E1218U;
  opts.resizable = 0;
  demo.window = dg_window_create(demo.app, &opts);
  if (demo.window == nullptr) {
    return false;
  }

  // The area is the window's root, filling it: the C ABI's BOX is a leaf
  // (no flex direction), so there is no container to put a heading beside it.
  demo.area = dg_node_create(demo.app, DG_NODE_TYPE_TEXT_AREA);
  if (demo.area == nullptr || dg_window_set_root(demo.window, demo.area) != DG_ERR_OK) {
    return false;
  }
  set(demo.area, DG_PROP_WIDTH, length(static_cast<float>(kWindowWidth)));
  set(demo.area, DG_PROP_HEIGHT, length(static_cast<float>(kWindowHeight)));
  set_text(demo.area, sample_document());
  return true;
}

int pump(const Demo& demo, int rounds = 3) {
  int changed = 0;
  for (int i = 0; i < rounds; ++i) {
    (void)dg_wait_events(demo.app, 5);
    dg_event events[32];
    const std::int32_t n = dg_poll_events(demo.app, events, 32);
    for (std::int32_t k = 0; k < n; ++k) {
      changed += events[k].kind == DG_EVENT_TEXT_CHANGED ? 1 : 0;
    }
  }
  return changed;
}

void click(const Demo& demo, int x, int y) {
  (void)dg_debug_warp_pointer(demo.window, x, y);
  (void)dg_debug_post_pointer_button(demo.window, 1, x, y);
  (void)dg_debug_post_pointer_button(demo.window, 0, x, y);
  (void)pump(demo);
}

int key(const Demo& demo, const char* name, bool shift = false) {
  (void)dg_debug_post_text_field_key(demo.window, 1, name, shift ? 1 : 0);
  return pump(demo);
}

int type(const Demo& demo, const char* text) {
  (void)dg_debug_post_text_input(demo.window, text);
  return pump(demo);
}

std::string text_of(dg_node_t* node) {
  std::uint32_t len = 0;
  (void)dg_node_get_text(node, nullptr, 0, &len);
  std::string out(len, '\0');
  (void)dg_node_get_text(node, out.data(), len, nullptr);
  return out;
}

// The scripted session --dump-png and --verify share: focus near the top,
// type a line, select a few lines with Shift+Down, leave the caret mid-text.
int script(const Demo& demo) {
  (void)pump(demo);
  int changed = 0;
  click(demo, 40, 60);
  // dg_node_set_text left the caret (and the view) at the end; go back up.
  for (int i = 0; i < 10; ++i) {
    (void)key(demo, "PageUp");
  }
  click(demo, 40, 60);
  changed += type(demo, "Hello");
  changed += key(demo, "Enter");
  changed += type(demo, "world");
  (void)key(demo, "Down");
  (void)key(demo, "Down", true);
  (void)key(demo, "Down", true);
  (void)key(demo, "End", true);
  return changed;
}

int verify() {
  Demo demo;
  int failures = 0;
  const auto check = [&](bool ok, const char* what) {
    std::cout << (ok ? "ok   " : "FAIL ") << what << '\n';
    failures += ok ? 0 : 1;
  };
  check(build(demo, true), "the demo scene builds");
  if (failures == 0) {
    const std::string before = text_of(demo.area);
    check(before == sample_document(), "dg_node_set_text fills the area's model");
    check(script(demo) == 3, "Hello, Enter, world each report DG_EVENT_TEXT_CHANGED");
    const std::string after = text_of(demo.area);
    check(after.find("Hello\nworld") != std::string::npos, "Enter inserted a newline");
    check(after.size() == before.size() + 11, "exactly the typed bytes were added");
    for (int i = 0; i < 60; ++i) {
      (void)key(demo, "PageDown");
    }
    (void)key(demo, "Backspace");
    check(text_of(demo.area).size() == after.size() - 1,
          "PageDown reaches the end and Backspace deletes there");
    check(dg_debug_capture_png(demo.window) != nullptr && dg_debug_capture_png_len() > 0,
          "the frame can be captured");
  }
  dg_app_destroy(demo.app);
  std::cout << (failures == 0 ? "text area: all checks passed\n" : "text area: FAILED\n");
  return failures == 0 ? 0 : 1;
}

int dump_png(const std::string& path) {
  Demo demo;
  if (!build(demo, false)) {
    std::cerr << "could not build the demo scene\n";
    return 1;
  }
  (void)script(demo);
  (void)pump(demo);
  const char* png = dg_debug_capture_png(demo.window);
  const std::uint32_t len = dg_debug_capture_png_len();
  int status = 1;
  if (png != nullptr && len > 0) {
    std::ofstream out(path, std::ios::binary);
    out.write(png, static_cast<std::streamsize>(len));
    status = out ? 0 : 1;
  }
  dg_app_destroy(demo.app);
  if (status == 0) {
    std::cout << "wrote " << path << '\n';
  }
  return status;
}

int run_window(int run_ms) {
  Demo demo;
  if (!build(demo, false)) {
    std::cerr << "could not build the demo scene\n";
    return 1;
  }
  int elapsed = 0;
  bool open = true;
  while (open && (run_ms <= 0 || elapsed < run_ms)) {
    (void)dg_wait_events(demo.app, 16);
    elapsed += 16;
    dg_event events[32];
    const std::int32_t n = dg_poll_events(demo.app, events, 32);
    for (std::int32_t k = 0; k < n; ++k) {
      open = open && events[k].kind != DG_EVENT_WINDOW_CLOSED;
    }
  }
  dg_app_destroy(demo.app);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  const std::vector<std::string> args{argv + 1, argv + argc};
  if (args.size() == 1 && args[0] == "--verify-text-area") {
    return verify();
  }
  if (args.size() == 2 && args[0] == "--dump-png") {
    return dump_png(args[1]);
  }
  if (args.size() == 2 && args[0] == "--run-ms") {
    return run_window(std::stoi(args[1]));
  }
  if (args.empty()) {
    return run_window(0);
  }
  std::cerr << "usage: drawgui_text_area [--run-ms N] [--dump-png FILE] [--verify-text-area]\n";
  return 2;
}
