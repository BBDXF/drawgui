// P6 sub-step a2 (second half)'s golden scene, built through the C ABI - the
// same `produce` channel abi_label_scene.h/abi_checkbox_scene.h already use.
// Only compiled/linked when DRAWGUI_HAVE_SDL3, exactly like both of those.

#pragma once

#include <cstdint>
#include <vector>

// Opens an app, builds one DG_NODE_TYPE_TEXT_FIELD window root, forces one
// paint via dg_wait_events(), clicks the field's centre (focusing it and
// starting real text input - abi_impl.cpp's process_pointer_event()), types
// "Hi" through dg_debug_post_text_input() one character at a time (each
// commits as its own SDL_EVENT_TEXT_INPUT, matching a real keystroke
// stream), and forces a second paint. Returns the encoded PNG bytes
// dg_debug_capture_png() hands back. Empty on any failure.
std::vector<std::uint8_t> produce_abi_text_field_scene(const char* font_dir);
