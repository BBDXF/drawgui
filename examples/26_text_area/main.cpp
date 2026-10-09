// examples/26_text_area: DG_NODE_TYPE_TEXT_AREA through the C ABI.
//
// A heading row (a title and a live byte count, laid out by a BOX made a
// row with DG_PROP_DIRECTION) above one multi-line text area pre-filled
// with a document long enough to scroll. Type, Enter, arrows, Home/End,
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
constexpr int kHeadingHeight = 40;

dg_value length(float number, std::uint32_t unit = DG_LENGTH_UNIT_PX) {
  dg_value value{};
  value.size = sizeof(dg_value);
  value.type = DG_VALUE_LENGTH;
  value.number = number;
  value.bits = unit;
  return value;
}

dg_value number(float value) {
  dg_value out{};
  out.size = sizeof(dg_value);
  out.type = DG_VALUE_FLOAT;
  out.number = value;
  return out;
}

dg_value option(std::uint32_t ordinal) {
  dg_value out{};
  out.size = sizeof(dg_value);
  out.type = DG_VALUE_ENUM;
  out.bits = ordinal;
  return out;
}

dg_value color(std::uint32_t argb) {
  dg_value out{};
  out.size = sizeof(dg_value);
  out.type = DG_VALUE_COLOR;
  out.bits = argb;
  return out;
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
  dg_node_t* count = nullptr;
};

dg_node_t* add(const Demo& demo, dg_node_t* parent, std::uint16_t type) {
  dg_node_t* node = dg_node_create(demo.app, type);
  if (node != nullptr && dg_node_insert_before(parent, node, nullptr) != DG_ERR_OK) {
    return nullptr;
  }
  return node;
}

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

std::string text_of(dg_node_t* node) {
  std::uint32_t len = 0;
  (void)dg_node_get_text(node, nullptr, 0, &len);
  std::string out(len, '\0');
  (void)dg_node_get_text(node, out.data(), len, nullptr);
  return out;
}

void update_count(const Demo& demo) {
  set_text(demo.count, std::to_string(text_of(demo.area).size()) + " bytes");
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

  // A column: the heading row, then the area filling the rest.
  dg_node_t* root = dg_node_create(demo.app, DG_NODE_TYPE_BOX);
  if (root == nullptr || dg_window_set_root(demo.window, root) != DG_ERR_OK) {
    return false;
  }
  set(root, DG_PROP_WIDTH, length(static_cast<float>(kWindowWidth)));
  set(root, DG_PROP_HEIGHT, length(static_cast<float>(kWindowHeight)));
  set(root, DG_PROP_BACKGROUND_COLOR, color(0xFF0E1218U));
  set(root, DG_PROP_DIRECTION, option(DG_DIRECTION_COLUMN));

  dg_node_t* heading = add(demo, root, DG_NODE_TYPE_BOX);
  dg_node_t* title = heading != nullptr ? add(demo, heading, DG_NODE_TYPE_LABEL) : nullptr;
  demo.count = heading != nullptr ? add(demo, heading, DG_NODE_TYPE_LABEL) : nullptr;
  demo.area = add(demo, root, DG_NODE_TYPE_TEXT_AREA);
  if (title == nullptr || demo.count == nullptr || demo.area == nullptr) {
    return false;
  }
  set(heading, DG_PROP_DIRECTION, option(DG_DIRECTION_ROW));
  set(heading, DG_PROP_JUSTIFY, option(DG_JUSTIFY_SPACE_BETWEEN));
  set(heading, DG_PROP_ALIGN, option(DG_ALIGN_CENTER));
  set(heading, DG_PROP_WIDTH, length(100.0F, DG_LENGTH_UNIT_PERCENT));
  set(heading, DG_PROP_HEIGHT, length(static_cast<float>(kHeadingHeight)));
  set(heading, DG_PROP_PADDING_L, number(12));
  set(heading, DG_PROP_PADDING_R, number(12));
  set(heading, DG_PROP_BACKGROUND_COLOR, color(0xFF1A2130U));
  for (dg_node_t* label : {title, demo.count}) {
    set(label, DG_PROP_TEXT_COLOR, color(0xFFE8EDF4U));
    set(label, DG_PROP_WIDTH, length(200));
  }
  set(title, DG_PROP_FONT_SIZE, number(20));
  set(title, DG_PROP_TEXT_ALIGN, option(DG_TEXT_ALIGN_LEFT));
  set_text(title, "Notes");
  set(demo.count, DG_PROP_FONT_SIZE, number(13));
  set(demo.count, DG_PROP_TEXT_ALIGN, option(DG_TEXT_ALIGN_RIGHT));

  set(demo.area, DG_PROP_WIDTH, length(100.0F, DG_LENGTH_UNIT_PERCENT));
  set(demo.area, DG_PROP_HEIGHT, length(static_cast<float>(kWindowHeight - kHeadingHeight)));
  set_text(demo.area, sample_document());
  update_count(demo);
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
  if (changed > 0) {
    update_count(demo);
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
    check(
        dg_dump_layout_tree(demo.count) != nullptr &&
            std::string(dg_dump_layout_tree(demo.count)).find("\"x\":308") != std::string::npos,
        "the heading row puts the byte count at its right edge");
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
      if (events[k].kind == DG_EVENT_TEXT_CHANGED) {
        update_count(demo);
      }
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
