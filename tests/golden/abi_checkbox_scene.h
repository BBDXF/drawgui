// P6 sub-step a2's golden scene, built through the C ABI - the same
// `produce` channel abi_label_scene.h introduced for a1. Only compiled/
// linked when DRAWGUI_HAVE_SDL3, exactly like abi_label_scene.h/.cpp.

#pragma once

#include <cstdint>
#include <vector>

// Opens an app, builds one DG_NODE_TYPE_CHECKBOX window root, forces one
// paint via dg_wait_events(), and - when `click` is true - drives a real
// press+release through dg_debug_warp_pointer()/dg_debug_post_pointer_button()
// at the checkbox's centre (toggling it checked, per abi_impl.cpp's
// process_pointer_event()) before forcing a second paint. Returns the
// encoded PNG bytes dg_debug_capture_png() hands back for whichever state
// was captured last. Empty on any failure.
//
// Two Scene table entries call this with `click` false and true
// respectively (golden_test.cpp's scene_abi_checkbox_off()/_on()) rather
// than one scene somehow encoding two states in one image: the indicator
// child's on/off fill is the ENTIRE pixel difference between the two
// baselines, and two separate, named PNGs make that difference visible by
// just looking at the two files side by side - the same reasoning
// abi_label_scene.h's own single-state shape already follows, extended to
// the two states a checkbox (unlike a label) actually has.
std::vector<std::uint8_t> produce_abi_checkbox_scene(bool click);
