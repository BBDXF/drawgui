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
std::int32_t app_add_font(dg_app_t* app, const char* directory, const char* family);

dg_window_t* window_create(dg_app_t* app, const dg_window_opts* opts);
std::int32_t window_set_root(dg_window_t* window, dg_node_t* node);
std::int32_t window_set_title(dg_window_t* window, const char* utf8);
std::int32_t window_get_title(dg_window_t* window, char* buf, std::uint32_t cap,
                              std::uint32_t* out_len);
std::int32_t window_set_size(dg_window_t* window, std::int32_t width, std::int32_t height);
std::int32_t window_get_size(dg_window_t* window, std::int32_t* width, std::int32_t* height);
std::int32_t window_set_position(dg_window_t* window, std::int32_t x, std::int32_t y);
std::int32_t window_get_position(dg_window_t* window, std::int32_t* x, std::int32_t* y);
std::int32_t window_minimize(dg_window_t* window);
std::int32_t window_maximize(dg_window_t* window);
std::int32_t window_restore(dg_window_t* window);
std::int32_t window_set_fullscreen(dg_window_t* window, std::int32_t fullscreen);
std::int32_t window_set_resizable(dg_window_t* window, std::int32_t resizable);
std::int32_t window_set_min_size(dg_window_t* window, std::int32_t width, std::int32_t height);
std::int32_t window_set_max_size(dg_window_t* window, std::int32_t width, std::int32_t height);
std::int32_t window_set_icon(dg_window_t* window, const void* pixels, std::int32_t width,
                             std::int32_t height, std::uint32_t row_bytes,
                             std::uint32_t format);
std::int32_t window_get_state(dg_window_t* window);

dg_node_t* node_create(dg_app_t* app, std::uint16_t type_id);
std::int32_t node_set_prop(dg_node_t* node, std::uint16_t prop_id, const dg_value* value);
std::int32_t node_set_text(dg_node_t* node, std::uint16_t prop_id, const char* utf8,
                           std::uint32_t len);
std::int32_t node_insert_before(dg_node_t* parent, dg_node_t* child, dg_node_t* ref);
std::int32_t node_remove(dg_node_t* node);
std::int32_t node_get_checked(dg_node_t* node);
std::int32_t node_get_text(dg_node_t* node, char* buf, std::uint32_t cap,
                           std::uint32_t* out_len);
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
std::int32_t debug_get_cursor(dg_window_t* window);
std::int32_t debug_post_pointer_button(dg_window_t* window, std::int32_t down, std::int32_t x,
                                       std::int32_t y);
std::int32_t debug_post_key(dg_window_t* window, std::int32_t down,
                            const char* logical_key_name);
std::int32_t debug_trigger_exception(std::int32_t kind);

const char* debug_capture_png(dg_window_t* window);
std::uint32_t debug_capture_png_len();
std::int32_t debug_post_text_input(dg_window_t* window, const char* utf8);
std::int32_t debug_post_text_field_key(dg_window_t* window, std::int32_t down,
                                       const char* key_name, std::int32_t shift);

std::int32_t node_focus(dg_node_t* node);
dg_node_t* window_get_focus(dg_window_t* window);
std::int32_t window_clear_focus(dg_window_t* window);
std::int32_t node_get_scroll_offset(dg_node_t* node, std::int32_t* x, std::int32_t* y);
std::int32_t list_set_items(dg_node_t* node, const char* const* items, std::uint32_t count,
                            std::int32_t item_height);
std::int32_t list_get_selected(dg_node_t* node);
std::int32_t list_set_selected(dg_node_t* node, std::int32_t index);
std::int32_t node_set_image_data(dg_node_t* node, const void* bytes, std::uint32_t len,
                                 std::uint32_t svg_tint_argb);
std::int32_t node_set_image_file(dg_node_t* node, const char* path,
                                 std::uint32_t svg_tint_argb);
std::uint32_t timer_add(dg_app_t* app, std::int32_t delay_ms, std::int32_t repeat);
std::int32_t timer_cancel(dg_app_t* app, std::uint32_t timer_id);
std::int32_t wake(dg_app_t* app);
std::int32_t post_user_event(dg_app_t* app, std::uint64_t payload);
std::int32_t debug_post_wheel(dg_window_t* window, float dx, float dy);
std::int32_t debug_post_pointer_button_ex(dg_window_t* window, std::int32_t down,
                                          std::int32_t x, std::int32_t y, std::int32_t button,
                                          std::int32_t clicks);

// ABI 0.2 phase B (src/abi/abi_components.cpp).
std::int32_t menu_open(dg_window_t* window, dg_node_t* anchor, std::int32_t x, std::int32_t y,
                       const char* const* labels, const std::uint32_t* flags,
                       std::uint32_t count);
std::int32_t menu_close(dg_window_t* window);
std::int32_t dropdown_set_items(dg_node_t* node, const char* const* items, std::uint32_t count);
std::int32_t dropdown_get_selected(dg_node_t* node);
std::int32_t dropdown_set_selected(dg_node_t* node, std::int32_t index);
std::int32_t slider_get_value(dg_node_t* node, float* out_value);
std::int32_t radio_select(dg_node_t* node);
std::int32_t node_set_tooltip(dg_node_t* node, const char* utf8);
std::int32_t app_set_tooltip_delay(dg_app_t* app, std::int32_t delay_ms);
std::int32_t dialog_open(dg_window_t* window, const char* title, const char* body,
                         const char* const* buttons, std::uint32_t count,
                         std::int32_t default_button, std::int32_t cancel_button);
std::int32_t dialog_close(dg_window_t* window);
dg_node_t* tabs_add(dg_node_t* node, const char* label, std::int32_t disabled);
std::int32_t tabs_get_selected(dg_node_t* node);
std::int32_t tabs_set_selected(dg_node_t* node, std::int32_t index);
std::int32_t tree_set_items(dg_node_t* node, const char* const* labels,
                            const std::int32_t* depths, const std::uint32_t* flags,
                            std::uint32_t count);
std::int32_t tree_get_selected(dg_node_t* node);
std::int32_t tree_set_selected(dg_node_t* node, std::int32_t index);
std::int32_t tree_set_expanded(dg_node_t* node, std::int32_t index, std::int32_t expanded);
std::int32_t table_set_columns(dg_node_t* node, const char* const* titles, const float* widths,
                               const std::uint32_t* units, const std::uint32_t* flags,
                               std::uint32_t count);
std::int32_t table_set_cells(dg_node_t* node, const char* const* cells, std::uint32_t rows);
std::int32_t table_get_selected(dg_node_t* node);
std::int32_t table_set_selected(dg_node_t* node, std::int32_t row);
std::int32_t table_set_sort_indicator(dg_node_t* node, std::int32_t column,
                                      std::int32_t ascending);
std::int32_t debug_node_count(dg_window_t* window);

// ABI 0.2 enterprise features (src/abi/abi_enterprise.cpp).
std::int32_t app_add_shortcut(dg_app_t* app, const char* chord, std::uint16_t action_id);
std::int32_t window_add_shortcut(dg_node_t* scope, const char* chord, std::uint16_t action_id);
std::int32_t app_remove_shortcut(dg_app_t* app, const char* chord);
const char* chord_label(dg_app_t* app, const char* chord);
std::int32_t app_load_image(dg_app_t* app, const void* bytes, std::uint32_t len,
                            std::uint32_t svg_tint_argb);
std::int32_t app_load_image_file(dg_app_t* app, const char* path, std::uint32_t svg_tint_argb);
std::int32_t list_set_rows(dg_node_t* node, const char* const* texts,
                           const char* const* secondaries, const std::uint32_t* icons,
                           const std::uint32_t* flags, const std::uint32_t* trailing_actions,
                           std::uint32_t count, std::int32_t item_height);
std::int32_t list_set_row_actions(dg_node_t* node, const std::uint32_t* icons,
                                  const char* const* tooltips, std::uint32_t count);
std::int32_t list_row_checked(dg_node_t* node, std::int32_t row);
std::int32_t tree_set_items_ex(dg_node_t* node, const char* const* labels,
                               const std::int32_t* depths, const std::uint32_t* flags,
                               const std::uint32_t* icons, const char* const* secondaries,
                               std::uint32_t count);
std::int32_t tree_row_checked(dg_node_t* node, std::int32_t index);
std::int32_t table_set_cells_ex(dg_node_t* node, const char* const* cells,
                                const std::uint32_t* icons, const std::uint32_t* flags,
                                std::uint32_t rows);
std::int32_t table_cell_checked(dg_node_t* node, std::int32_t row, std::int32_t column);
std::int32_t menubar_set_menus(dg_node_t* node, const char* const* titles,
                               const std::uint32_t* entry_counts, std::uint32_t menu_count,
                               const char* const* labels, const char* const* shortcut_labels,
                               const std::uint32_t* flags, const std::int32_t* commands,
                               std::uint32_t entry_total);
std::int32_t menubar_set_entry(dg_node_t* node, std::int32_t menu, std::int32_t item,
                               const char* label, const char* shortcut_label,
                               std::uint32_t flags, std::int32_t command);
std::int32_t menu_open_ex(dg_window_t* window, dg_node_t* anchor, std::int32_t x,
                          std::int32_t y, const char* const* labels, const std::uint32_t* flags,
                          const char* const* shortcut_labels, const std::int32_t* commands,
                          std::uint32_t count);
std::int32_t dialog_open_ex(dg_window_t* window, const char* title, const char* body,
                            const char* const* buttons, std::uint32_t count,
                            std::int32_t default_button, std::int32_t cancel_button,
                            dg_node_t* content);
std::int32_t dialog_set_button_disabled(dg_window_t* window, std::int32_t dialog_id,
                                        std::int32_t button, std::int32_t disabled);

}  // namespace dg::abi
