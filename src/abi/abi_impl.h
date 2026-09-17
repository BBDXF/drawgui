// The hand-written half of the C ABI (design.md section 5.17.1): every
// function below is free to throw, has the IDENTICAL signature as its
// generated dg_*() trampoline (src/abi/drawgui_abi.generated.cpp), and is
// never itself exported with C linkage - only tools/gen_abi.py's trampolines
// are, and they are what wraps the try/catch around a call to one of these.
//
// "If you find yourself hand-writing an export function, that is the slice
// telling you the generator is incomplete" (the task's own words) is honoured
// by construction here: nothing in this file has `extern "C"` and nothing in
// it is declared in drawgui.h, so there is no way to call one of these except
// through the generated trampoline that wraps it.

#pragma once

#include <cstdint>
#include <string>

#include "drawgui/abi/drawgui.h"

namespace dg::abi {

// Shared by every trampoline's catch clauses (src/abi/drawgui_abi.generated.cpp)
// and by last_error() below. Thread-local, matching design.md section 5.8's
// `const char* dg_last_error(void)` - no app parameter, so the answer has to
// live somewhere that is not per-app.
void set_last_error(std::string message);

std::uint32_t abi_version();

dg_app_t* app_create(const dg_app_opts* opts);
void app_destroy(dg_app_t* app);

dg_window_t* window_create(dg_app_t* app, const dg_window_opts* opts);
std::int32_t window_set_root(dg_window_t* window, dg_node_t* node);

dg_node_t* node_create(dg_app_t* app, std::uint16_t type_id);
std::int32_t node_set_prop(dg_node_t* node, std::uint16_t prop_id, const dg_value* value);
std::int32_t node_insert_before(dg_node_t* parent, dg_node_t* child, dg_node_t* ref);
std::int32_t node_remove(dg_node_t* node);
std::int32_t node_scope_action(dg_node_t* node, std::uint16_t action_id);

std::int32_t wait_events(dg_app_t* app, std::int32_t timeout_ms);
std::int32_t poll_events(dg_app_t* app, dg_event* out, std::int32_t max);

const char* last_error();
const char* dump_layout_tree(dg_node_t* node);
const char* shortcut_label(std::uint16_t action_id);

dg_theme_t* theme_load_dir(dg_app_t* app, const char* dir, dg_theme_err* err);
dg_theme_t* theme_load_memory(dg_app_t* app, const char* json, std::uint32_t len,
                              const char* base_dir, dg_theme_err* err);
std::int32_t theme_set_variant(dg_theme_t* theme, const char* variant);
std::int32_t theme_override(dg_theme_t* theme, std::uint16_t token_id, const dg_value* value);
std::int32_t app_set_theme(dg_app_t* app, dg_theme_t* theme);

std::int32_t debug_warp_pointer(dg_window_t* window, std::int32_t x, std::int32_t y);
std::int32_t debug_post_pointer_button(dg_window_t* window, std::int32_t down, std::int32_t x,
                                       std::int32_t y);
std::int32_t debug_post_key(dg_window_t* window, std::int32_t down,
                            const char* logical_key_name);
std::int32_t debug_trigger_exception(std::int32_t kind);

}  // namespace dg::abi
