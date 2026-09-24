#include "abi_label_scene.h"

#include <cstdint>
#include <cstring>

#include "drawgui/abi/drawgui.h"

// DG_PROP_TEXT is deliberately EXCLUDED from drawgui.h in a C++ translation
// unit (its own comment: prop_ids.generated.h declares the identical name as
// an `inline constexpr`, and a macro of the same spelling would silently
// rewrite that declaration wherever both headers reach one TU). This file is
// C++ - not a real external C consumer, exactly like src/abi/abi_impl.cpp
// itself - so it reaches the constant the same way abi_impl.cpp does: the
// generated C++ header directly, never the macro form.
#include "drawgui/render/prop_ids.generated.h"

std::vector<std::uint8_t> produce_abi_label_scene(const char* font_dir) {
  dg_app_t* app = dg_app_create(nullptr);
  if (app == nullptr) {
    return {};
  }

  // "DgTest Latin" covers ASCII 0x20..0x7E (tests/fonts/gen_test_fonts.py's
  // own coverage table) - enough for the plain ASCII label below, and
  // deterministic across every machine this test runs on.
  if (dg_app_add_font(app, font_dir, "DgTest Latin") != DG_ERR_OK) {
    dg_app_destroy(app);
    return {};
  }

  dg_window_opts opts;
  std::memset(&opts, 0, sizeof(opts));
  opts.size = sizeof(opts);
  opts.title = "golden: abi_label";
  opts.width = 320;
  opts.height = 120;
  opts.background_argb = 0xFFFFFFFFU;  // opaque white - the label's default
                                       // text colour is opaque black.

  dg_window_t* window = dg_window_create(app, &opts);
  if (window == nullptr) {
    dg_app_destroy(app);
    return {};
  }

  dg_node_t* label = dg_node_create(app, DG_NODE_TYPE_LABEL);
  if (label == nullptr) {
    dg_app_destroy(app);
    return {};
  }

  if (dg_window_set_root(window, label) != DG_ERR_OK) {
    dg_app_destroy(app);
    return {};
  }

  const char text[] = "Hello, drawgui!";
  if (dg_node_set_text(label, DG_PROP_TEXT, text,
                       static_cast<std::uint32_t>(sizeof(text) - 1)) != DG_ERR_OK) {
    dg_app_destroy(app);
    return {};
  }

  // Forces layout + a real paint pass (abi_impl.cpp's process_pump(), the
  // same internal frame loop dg_wait_events()/dg_poll_events() both drive).
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
