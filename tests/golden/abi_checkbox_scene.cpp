#include "abi_checkbox_scene.h"

#include <cstring>

#include "drawgui/abi/drawgui.h"

std::vector<std::uint8_t> produce_abi_checkbox_scene(bool click) {
  dg_app_t* app = dg_app_create(nullptr);
  if (app == nullptr) {
    return {};
  }

  dg_window_opts opts;
  std::memset(&opts, 0, sizeof(opts));
  opts.size = sizeof(opts);
  opts.title = "golden: abi_checkbox";
  opts.width = 64;
  opts.height = 64;
  opts.background_argb = 0xFFFFFFFFU;  // opaque white, matching abi_label's own background.

  dg_window_t* window = dg_window_create(app, &opts);
  if (window == nullptr) {
    dg_app_destroy(app);
    return {};
  }

  dg_node_t* checkbox = dg_node_create(app, DG_NODE_TYPE_CHECKBOX);
  if (checkbox == nullptr) {
    dg_app_destroy(app);
    return {};
  }

  if (dg_window_set_root(window, checkbox) != DG_ERR_OK) {
    dg_app_destroy(app);
    return {};
  }

  // Forces layout + a real paint pass so the checkbox (and its composed
  // indicator child - abi_impl.cpp's attach_pending_node()) has real
  // on-screen bounds before a click is posted against it.
  dg_wait_events(app, 10);

  if (click) {
    // The checkbox is this window's sole 32x32 content root at (0,0) -
    // attach_pending_node()'s own DG_NODE_TYPE_CHECKBOX box size - so its
    // centre is (16, 16) regardless of this window's own 64x64 size.
    dg_debug_warp_pointer(window, 16, 16);
    dg_debug_post_pointer_button(window, 1, 16, 16);
    dg_debug_post_pointer_button(window, 0, 16, 16);
    dg_wait_events(app, 10);
  }

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
