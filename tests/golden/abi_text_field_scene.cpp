#include "abi_text_field_scene.h"

#include <cstring>

#include "drawgui/abi/drawgui.h"

std::vector<std::uint8_t> produce_abi_text_field_scene(const char* font_dir) {
  dg_app_t* app = dg_app_create(nullptr);
  if (app == nullptr) {
    return {};
  }

  // "DgTest Latin" covers ASCII 0x20..0x7E (tests/fonts/gen_test_fonts.py's
  // own coverage table) - enough for the plain ASCII "Hi" typed below, and
  // deterministic across every machine this test runs on. Every codepoint
  // renders as the SAME narrow vertical bar (SPECS' own rect(200, 0, 400,
  // 700)), so the baseline image is a row of bars, not readable glyphs -
  // that is the point: it pins glyph COUNT and POSITION, not shape.
  if (dg_app_add_font(app, font_dir, "DgTest Latin") != DG_ERR_OK) {
    dg_app_destroy(app);
    return {};
  }

  dg_window_opts opts;
  std::memset(&opts, 0, sizeof(opts));
  opts.size = sizeof(opts);
  opts.title = "golden: abi_text_field";
  opts.width = 280;
  opts.height = 56;
  opts.background_argb = 0xFF0E1218U;  // examples/12_text_input's own window fill.

  dg_window_t* window = dg_window_create(app, &opts);
  if (window == nullptr) {
    dg_app_destroy(app);
    return {};
  }

  dg_node_t* field = dg_node_create(app, DG_NODE_TYPE_TEXT_FIELD);
  if (field == nullptr) {
    dg_app_destroy(app);
    return {};
  }

  if (dg_window_set_root(window, field) != DG_ERR_OK) {
    dg_app_destroy(app);
    return {};
  }

  // Forces layout + a real paint pass so the field (and its four composed,
  // caller-invisible children - abi_impl.cpp's attach_pending_node()) has
  // real on-screen bounds before a click is posted against it.
  dg_wait_events(app, 10);

  // The field is this window's sole 260x36 content root at (0,0) -
  // attach_pending_node()'s own DG_NODE_TYPE_TEXT_FIELD box size - so its
  // centre is (130, 18) regardless of this window's own 280x56 size. This
  // focuses it (process_pointer_event()'s dg::Focus::set()) and places the
  // caret via WidgetSet::text_field_click().
  dg_debug_warp_pointer(window, 130, 18);
  dg_debug_post_pointer_button(window, 1, 130, 18);
  dg_debug_post_pointer_button(window, 0, 130, 18);
  dg_wait_events(app, 10);

  // "Hi" typed one committed character at a time - the same shape a real
  // keystroke stream delivers (one SDL_EVENT_TEXT_INPUT per character),
  // and examples/19_c_client's own DG_EVENT_TEXT_CHANGED count assertion
  // relies on the identical one-call-per-character shape.
  dg_debug_post_text_input(window, "H");
  dg_wait_events(app, 10);
  dg_debug_post_text_input(window, "i");
  dg_wait_events(app, 10);

  const char* png = dg_debug_capture_png(window);
  const std::uint32_t len = dg_debug_capture_png_len();
  std::vector<std::uint8_t> out;
  if (png != nullptr && len > 0) {
    out.assign(reinterpret_cast<const std::uint8_t*>(png),
              reinterpret_cast<const std::uint8_t*>(png) + len);
  }
  dg_app_destroy(app);
  return out;
}
