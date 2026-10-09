// See abi_impl.h's own header comment: everything here is hand-written,
// throws freely, and is called only from the generated trampolines in
// src/abi/drawgui_abi.generated.cpp.

#include "abi_impl.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "drawgui/anim/clock.h"
#include "drawgui/base/expected.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/graphics/types.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/props/node_props.h"
#include "drawgui/render/node_lifecycle.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/shortcuts/chord.h"
#include "drawgui/shortcuts/router.h"
#include "drawgui/theme/theme_loader.h"
#include "drawgui/widget/click_counter.h"
#include "drawgui/widget/cursor.h"
#include "drawgui/widget/focus.h"
#include "drawgui/widget/interaction.h"
#include "drawgui/widget/widget_set.h"
#include "drawgui/window/window_manager.h"

#include "abi_types.h"

namespace dg::abi {
namespace {

// DG_NODE_TYPE_LABEL's engine-chosen default text size (props/drawgui.props
// .toml's own comment on `text`: font_size is not a property this slice).
// Not measured against anything - only large enough to be legible at this
// ABI's own default window sizes, and small enough to fit a one-line label
// inside the 32px-tall default box attach_pending_node() gives it.
constexpr float kDefaultLabelTextSize = 16.0F;

// P6 sub-step a2: DG_NODE_TYPE_CHECKBOX's colours - lifted unchanged from
// examples/11_form_controls/form_scene.cpp's own add_toggle() palette
// (kToggleNormal/kToggleHover/kTogglePressed/kBorder/kOff/kAccent), the
// recipe this ABI branch copies rather than invents.
constexpr std::uint32_t kCheckboxNormalArgb = 0xFF2C3644;
constexpr std::uint32_t kCheckboxHoverArgb = 0xFF3B4A5E;
constexpr std::uint32_t kCheckboxPressedArgb = 0xFF1F2731;
constexpr std::uint32_t kCheckboxBorderArgb = 0xFF141A22;
constexpr std::uint32_t kCheckboxOffArgb = 0xFF10151C;
constexpr std::uint32_t kCheckboxOnArgb = 0xFF2E86DE;

constexpr dg::Color kCheckboxNormal = dg::Color::from_argb(kCheckboxNormalArgb);
constexpr dg::Color kCheckboxBorder = dg::Color::from_argb(kCheckboxBorderArgb);

// DG_NODE_TYPE_TEXT_FIELD / DG_NODE_TYPE_TEXT_AREA colours come from the
// theme (editor_palette_for() below), so both editors follow light/dark and
// dg_theme_override(). These are only the fallbacks for a theme lacking a
// token - the editors' original dark palette.
constexpr std::uint32_t kTextFieldFillArgb = 0xFF161C24;
constexpr std::uint32_t kTextFieldBorderUnfocusedArgb = 0xFF2C3644;
constexpr std::uint32_t kTextFieldBorderFocusedArgb = 0xFF2E86DE;
constexpr std::uint32_t kTextFieldTextColorArgb = 0xFFE8EDF4;
// Selection = the accent at this opacity, so text stays readable on top.
constexpr std::uint32_t kTextFieldSelectionAlpha = 0x8A;
constexpr int kTextFieldWidth = 260;
constexpr int kTextFieldHeight = 36;
constexpr float kTextFieldFontSize = 16.0F;

// DG_NODE_TYPE_TEXT_AREA: the text field's palette and font size above,
// reused verbatim so the two editors look like one family; only the box is
// larger. `kTextAreaHighlightPool` bounds how many selected lines can show
// at once (WidgetSet's pool contract) - comfortably more than the default
// box holds, with room for a host that enlarges it.
constexpr int kTextAreaWidth = 320;
constexpr int kTextAreaHeight = 160;
constexpr int kTextAreaInset = 10;
constexpr int kTextAreaHighlightPool = 64;
constexpr int kTextAreaUnderlinePool = 4;

// Both editors' colours, resolved from the active (else builtin dark) theme:
// box = color.surface, border = color.border, focused border / caret =
// color.primary, text and IME underline = color.on-surface, selection =
// color.primary made translucent.
struct EditorPalette {
  dg::Color fill = dg::Color::from_argb(kTextFieldFillArgb);
  dg::Color border = dg::Color::from_argb(kTextFieldBorderUnfocusedArgb);
  dg::Color border_focused = dg::Color::from_argb(kTextFieldBorderFocusedArgb);
  dg::Color text = dg::Color::from_argb(kTextFieldTextColorArgb);
  dg::Color caret = dg::Color::from_argb(kTextFieldBorderFocusedArgb);
  dg::Color selection = dg::Color::from_argb((kTextFieldSelectionAlpha << 24U) |
                                             (kTextFieldBorderFocusedArgb & 0xFFFFFFU));
  dg::Color underline = dg::Color::from_argb(kTextFieldTextColorArgb);
};
EditorPalette editor_palette_for(const AppImpl& impl);

dg::NodeStyle text_field_box_style(const EditorPalette& palette, bool focused) {
  dg::NodeStyle style;
  style.fill = palette.fill;
  style.border_color = focused ? palette.border_focused : palette.border;
  style.border_width = dg::BorderWidths::all(1.0F);
  style.radii = dg::Radii::all(6.0F);
  style.overflow = dg::Overflow::kClip;
  return style;
}

bool is_text_editor(const WindowImpl& window, dg::NodeId id) {
  return window.widgets.has(id) && dg::is_editable_text(window.widgets.at(id).kind);
}

bool is_text_area(const WindowImpl& window, dg::NodeId id) {
  return window.widgets.has(id) && window.widgets.at(id).kind == dg::WidgetKind::kTextArea;
}

// Re-applies text_field_box_style() without clobbering DG_PROP_DISABLED,
// which lives in the same NodeStyle this replaces wholesale.
void set_text_field_box_style(const EditorPalette& palette, dg::RenderTree& tree, dg::NodeId id,
                              bool focused) {
  dg::NodeStyle style = text_field_box_style(palette, focused);
  style.disabled = tree.style(id).disabled;
  tree.set_style(id, style);
}

// Points the platform IME's candidate window at a focused text area's caret
// (WindowManager::start_text_input() is SDL_SetTextInputArea + start).
void follow_caret(AppImpl& impl, WindowImpl& window, dg::NodeId area);

// Focus/blur side effects for either text editor: the display mode, the
// border colour, and (text area) the IME's caret rectangle.
void set_editor_focus(AppImpl& impl, WindowImpl& window, dg::NodeId id, bool focused) {
  if (!impl.fonts.has_value() || !is_text_editor(window, id)) {
    return;
  }
  dg::RenderTree& tree = window.tree.render();
  if (is_text_area(window, id)) {
    window.widgets.text_area_set_focus(tree, *impl.fonts, id, focused);
    if (focused) {
      follow_caret(impl, window, id);
    }
  } else {
    window.widgets.text_field_set_focus(tree, *impl.fonts, id, focused);
  }
  set_text_field_box_style(editor_palette_for(impl), tree, id, focused);
}

// -- Global arenas ------------------------------------------------------
//
// dg_app_create() takes no app parameter, so the top-level arena has to
// live at file scope. Function-local statics rather than a plain global,
// which sidesteps static-initialization-order questions entirely - nothing
// else in this translation unit constructs before first use needs these.
std::vector<std::unique_ptr<AppImpl>>& all_apps() {
  static std::vector<std::unique_ptr<AppImpl>> apps;
  return apps;
}

std::vector<bool>& all_apps_alive() {
  static std::vector<bool> alive;
  return alive;
}

// Handle-wrapper storage - see abi_types.h's own header comment for the
// "why" in full. THE KEY PROPERTY THIS BUYS: a std::deque never invalidates
// a previously-returned element's address as more are appended (unlike
// std::vector, which may reallocate), so a pointer this file hands out as
// dg_app_t*/dg_window_t*/dg_node_t* stays valid for exactly as long as the
// process runs, independent of how many more handles are created later.
//
// GLOBAL, NOT per-AppImpl, and that is the fix for a real defect the first
// version of this file had: putting a window's/node's handle deque INSIDE
// AppImpl would free it the moment dg_app_destroy() resets that AppImpl,
// turning "the handle is now stale" into an actual dangling pointer the
// instant a host dereferenced it - a real use-after-free, not the ordinary
// DG_ERR_INVALID_HANDLE this design exists to guarantee instead. Kept here,
// a handle wrapper outlives its owning app's teardown exactly as it outlives
// dg_node_remove(): its own two integers stay readable forever; only
// resolve_app()'s liveness check (all_apps_alive()) stops answering yes.
//
// Measured, not assumed: `new`-ing each wrapper and never freeing it (this
// design's first draft) passed every functional check but was reported by
// LeakSanitizer as exactly the "leaked" allocations it looked like from the
// allocator's point of view - an UNREACHABLE block is what LSan defines a
// leak to be, and a bare `new` with no owner anywhere is unreachable the
// moment the pointer returned from it is only ever read via a C caller LSan
// cannot see into. A deque owned by a reachable static IS a root LSan's own
// scan already walks, so the identical bytes stop being a "leak" without the
// pointer itself moving, being freed, or losing the stability property this
// design depends on.
std::deque<dg_app_s>& all_app_handles() {
  static std::deque<dg_app_s> handles;
  return handles;
}

std::deque<dg_window_s>& all_window_handles() {
  static std::deque<dg_window_s> handles;
  return handles;
}

std::deque<dg_node_s>& all_node_handles() {
  static std::deque<dg_node_s> handles;
  return handles;
}

// 7-6: identical shape/reasoning to the three handle decks above - see
// abi_types.h's own header comment for the "never freed, stable address"
// argument this deck relies on exactly as they do.
std::deque<dg_theme_s>& all_theme_handles() {
  static std::deque<dg_theme_s> handles;
  return handles;
}

std::string& theme_err_storage() {
  static thread_local std::string buffer;
  return buffer;
}

// Per-thread scratch storage, wrapped in accessors for the identical reason
// all_apps()/all_apps_alive() above are: a function-local static is not
// "globally accessible" the way a namespace-scope variable is
// (cppcoreguidelines-avoid-non-const-global-variables draws exactly that
// line), and design.md section 5.8's `dg_last_error(void)` signature - no
// app parameter - is what forces this to live somewhere other than an
// AppImpl member in the first place.
std::string& last_error_storage() {
  static thread_local std::string message;
  return message;
}

std::string& dump_buffer_storage() {
  static thread_local std::string buffer;
  return buffer;
}

// dg_debug_capture_png()'s own buffer - the identical lifetime rule
// dump_buffer_storage() already has, except this one holds bytes rather
// than text, which is exactly why dg_debug_capture_png_len() exists beside
// it: a caller cannot strlen() a PNG.
std::string& capture_png_storage() {
  static thread_local std::string buffer;
  return buffer;
}

// 8-3d: dg_shortcut_label()'s returned `const char*` needs an owned, stable
// buffer for the SAME reason dump_buffer_storage() above and
// last_error_storage() do (dg::shortcut_label() returns a std::string BY
// VALUE - there is no C++-side object outliving the call to point at)
// rather than a second lifetime rule invented for one more function.
std::string& shortcut_label_storage() {
  static thread_local std::string buffer;
  return buffer;
}

// -- Handle resolution ----------------------------------------------------
//
// Every one of these returns nullptr for a handle naming nothing live,
// INCLUDING a null pointer itself - no caller below has to null-check a
// handle before resolving it, only after.

AppImpl* resolve_app(dg_app_t* handle) {
  if (handle == nullptr) {
    return nullptr;
  }
  std::vector<std::unique_ptr<AppImpl>>& apps = all_apps();
  std::vector<bool>& alive = all_apps_alive();
  if (handle->index >= apps.size() || !alive[handle->index]) {
    return nullptr;
  }
  return apps[handle->index].get();
}

WindowImpl* resolve_window(dg_window_t* handle) {
  if (handle == nullptr) {
    return nullptr;
  }
  AppImpl* impl = resolve_app(handle->app);
  if (impl == nullptr || handle->index >= impl->windows.size()) {
    return nullptr;
  }
  WindowImpl* window = impl->windows[handle->index].get();
  if (window == nullptr || window->closed) {
    return nullptr;
  }
  return window;
}

NodeSlot* resolve_node_slot(dg_node_t* handle) {
  if (handle == nullptr) {
    return nullptr;
  }
  AppImpl* impl = resolve_app(handle->app);
  if (impl == nullptr || handle->index >= impl->nodes.size()) {
    return nullptr;
  }
  NodeSlot& slot = impl->nodes[handle->index];
  if (slot.state == NodeSlot::State::kRemoved) {
    return nullptr;
  }
  return &slot;
}

ThemeImpl* resolve_theme(dg_theme_t* handle) {
  if (handle == nullptr) {
    return nullptr;
  }
  AppImpl* impl = resolve_app(handle->app);
  if (impl == nullptr || handle->index >= impl->themes.size()) {
    return nullptr;
  }
  return impl->themes[handle->index].get();
}

WindowImpl* find_window_by_sdl_id(AppImpl& impl, dg::WindowId id) {
  for (const std::unique_ptr<WindowImpl>& window : impl.windows) {
    if (!window->closed && window->sdl_window == id) {
      return window.get();
    }
  }
  return nullptr;
}

dg_node_t* find_node_handle(AppImpl& impl, std::size_t window_index, dg::NodeId node_id) {
  for (NodeSlot& slot : impl.nodes) {
    if (slot.state == NodeSlot::State::kLive && slot.window_index == window_index &&
        slot.node_id == node_id) {
      return slot.self_handle;
    }
  }
  return nullptr;
}

// -- dg_value / dg::PropStatus conversions ---------------------------------

std::optional<dg::PropValue> to_prop_value(const dg_value& value) {
  switch (value.type) {
    case DG_VALUE_FLOAT:
      return dg::PropValue::number(value.number);
    case DG_VALUE_LENGTH:
      return dg::PropValue::length(value.number);
    case DG_VALUE_COLOR:
      return dg::PropValue::color(dg::Color::from_argb(value.bits));
    case DG_VALUE_ENUM:
      return dg::PropValue::option(value.bits);
    case DG_VALUE_BOOL:
      return dg::PropValue::boolean_bits(value.bits);
    default:
      return std::nullopt;
  }
}

std::int32_t to_error_code(dg::PropStatus status) {
  switch (status) {
    case dg::PropStatus::kApplied:
      return DG_ERR_OK;
    case dg::PropStatus::kUnknownId:
      return DG_ERR_UNKNOWN_ID;
    case dg::PropStatus::kTypeMismatch:
      return DG_ERR_TYPE_MISMATCH;
    case dg::PropStatus::kValueOutOfRange:
      return DG_ERR_VALUE_OUT_OF_RANGE;
    case dg::PropStatus::kNotApplicable:
      return DG_ERR_NOT_APPLICABLE;
    case dg::PropStatus::kUnsupported:
      return DG_ERR_UNSUPPORTED;
  }
  return DG_ERR_INTERNAL;
}

// -- 7-6: theme handling ----------------------------------------------------

std::uint32_t to_theme_err_status(dg::ThemeLoadStatus status) {
  switch (status) {
    case dg::ThemeLoadStatus::kParseError:
      return DG_THEME_ERR_PARSE_ERROR;
    case dg::ThemeLoadStatus::kMissingField:
      return DG_THEME_ERR_MISSING_FIELD;
    case dg::ThemeLoadStatus::kUnknownToken:
      return DG_THEME_ERR_UNKNOWN_TOKEN;
    case dg::ThemeLoadStatus::kTypeMismatch:
      return DG_THEME_ERR_TYPE_MISMATCH;
    case dg::ThemeLoadStatus::kUnsupportedSchemaVersion:
      return DG_THEME_ERR_UNSUPPORTED_SCHEMA_VERSION;
    case dg::ThemeLoadStatus::kIoError:
      return DG_THEME_ERR_IO_ERROR;
    case dg::ThemeLoadStatus::kPathTraversal:
      return DG_THEME_ERR_PATH_TRAVERSAL;
    case dg::ThemeLoadStatus::kResourceTooLarge:
      return DG_THEME_ERR_RESOURCE_TOO_LARGE;
    case dg::ThemeLoadStatus::kTooManyResources:
      return DG_THEME_ERR_TOO_MANY_RESOURCES;
  }
  return DG_THEME_ERR_IO_ERROR;
}

void fill_theme_err(dg_theme_err* err, const dg::ThemeLoadError& error) {
  theme_err_storage() = error.message;
  set_last_error(std::string("dg_theme_load: ") + error.message);
  if (err != nullptr && err->size >= sizeof(dg_theme_err)) {
    err->status = to_theme_err_status(error.status);
  }
}

std::optional<dg::ThemeVariant> parse_variant(const char* variant) {
  if (variant == nullptr) {
    return std::nullopt;
  }
  const std::string name(variant);
  if (name == "light") {
    return dg::ThemeVariant::kLight;
  }
  if (name == "dark") {
    return dg::ThemeVariant::kDark;
  }
  return std::nullopt;
}

// DG_PROP_DISABLED's appearance, from the theme tokens color.disabled/
// color.on-disabled: the app's active theme when it has one, else the
// builtin theme (dark variant, matching this file's own dark default
// palette). Held once per window (WidgetSet::set_disabled_palette()), so a
// theme change re-resolves it below instead of leaving stale colours on
// widgets attached earlier.
// The app's active theme and variant, else the builtin theme (dark).
std::pair<const dg::Theme*, dg::ThemeVariant> theme_for(const AppImpl& impl) {
  if (impl.active_theme != nullptr) {
    return {&impl.active_theme->theme, impl.active_theme->variant};
  }
  static const dg::Expected<dg::Theme, dg::ThemeLoadError> builtin = dg::load_builtin_theme();
  return {builtin.has_value() ? &builtin.value() : nullptr, dg::ThemeVariant::kDark};
}

dg::DisabledPalette disabled_palette_for(const AppImpl& impl) {
  const dg::Theme* theme = nullptr;
  dg::ThemeVariant variant = dg::ThemeVariant::kDark;
  if (impl.active_theme != nullptr) {
    theme = &impl.active_theme->theme;
    variant = impl.active_theme->variant;
  } else {
    static const dg::Expected<dg::Theme, dg::ThemeLoadError> builtin = dg::load_builtin_theme();
    if (builtin.has_value()) {
      theme = &builtin.value();
    }
  }
  if (theme == nullptr) {
    return {};
  }
  return dg::DisabledPalette{theme->color_value(DG_TOKEN_COLOR_DISABLED, variant),
                             theme->color_value(DG_TOKEN_COLOR_ON_DISABLED, variant)};
}

// The scrollbar look (DG_NODE_TYPE_TEXT_AREA's bar) from the theme tokens
// color.scrollbar-* and size.scrollbar, resolved like the disabled palette.
dg::ScrollbarStyle scrollbar_style_for(const AppImpl& impl) {
  const auto [theme, variant] = theme_for(impl);
  dg::ScrollbarStyle style;
  if (theme == nullptr) {
    return style;
  }
  style.track = theme->color_value(DG_TOKEN_COLOR_SCROLLBAR_TRACK, variant);
  style.thumb = theme->color_value(DG_TOKEN_COLOR_SCROLLBAR_THUMB, variant);
  style.thumb_hover = theme->color_value(DG_TOKEN_COLOR_SCROLLBAR_THUMB_HOVER, variant);
  style.thumb_pressed = theme->color_value(DG_TOKEN_COLOR_SCROLLBAR_THUMB_PRESSED, variant);
  style.size = theme->int_value(DG_TOKEN_SIZE_SCROLLBAR).value_or(style.size);
  return style;
}

EditorPalette editor_palette_for(const AppImpl& impl) {
  const auto [theme, variant] = theme_for(impl);
  EditorPalette palette;
  if (theme == nullptr) {
    return palette;
  }
  const auto pick = [&](dg_token_id id, dg::Color& out) {
    if (const std::optional<dg::Color> c = theme->color_value(id, variant)) {
      out = *c;
    }
  };
  pick(DG_TOKEN_COLOR_SURFACE, palette.fill);
  pick(DG_TOKEN_COLOR_BORDER, palette.border);
  pick(DG_TOKEN_COLOR_PRIMARY, palette.border_focused);
  pick(DG_TOKEN_COLOR_PRIMARY, palette.caret);
  pick(DG_TOKEN_COLOR_ON_SURFACE, palette.text);
  pick(DG_TOKEN_COLOR_ON_SURFACE, palette.underline);
  palette.selection = dg::Color::from_argb((kTextFieldSelectionAlpha << 24U) |
                                           (palette.caret.argb() & 0xFFFFFFU));
  return palette;
}

// Re-colours every live text field / text area in `window` with `palette`.
// Writes enabled colours only; refresh_subtree() afterwards re-dims the
// disabled ones (it notices the overwrite and remembers the new colour).
void restyle_editors(AppImpl& impl, std::size_t window_index, WindowImpl& window,
                     const EditorPalette& palette) {
  dg::RenderTree& tree = window.tree.render();
  for (const NodeSlot& slot : impl.nodes) {
    if (slot.state != NodeSlot::State::kLive || slot.window_index != window_index ||
        (slot.type_id != DG_NODE_TYPE_TEXT_FIELD && slot.type_id != DG_NODE_TYPE_TEXT_AREA) ||
        !window.widgets.has(slot.node_id)) {
      continue;
    }
    const dg::NodeId id = slot.node_id;
    set_text_field_box_style(palette, tree, id, window.focus.current() == id);
    // attach() replaces the stored Widget by value; a copy with only the
    // three box fills changed keeps the editing state intact.
    dg::Widget updated = window.widgets.at(id);
    updated.fill_normal = palette.fill;
    updated.fill_hover = palette.fill;
    updated.fill_pressed = palette.fill;
    window.widgets.attach(id, updated);
    const dg::Widget* widget = &window.widgets.at(id);
    dg::TextStyle text = tree.style(widget->content).text;
    text.color = palette.text;
    tree.set_text(widget->content, text);
    tree.set_fill(widget->caret, palette.caret);
    if (widget->kind == dg::WidgetKind::kTextArea) {
      for (const dg::NodeId n : widget->highlight_pool) {
        tree.set_fill(n, palette.selection);
      }
      for (const dg::NodeId n : widget->underline_pool) {
        tree.set_fill(n, palette.underline);
      }
    } else {
      tree.set_fill(widget->selection_highlight, palette.selection);
      tree.set_fill(widget->composition_underline, palette.underline);
    }
  }
}

// dg_app_set_theme()/dg_theme_set_variant()/dg_theme_override() all end
// here: whichever one changed the theme's DATA, the same re-apply against
// every open window's own dg::ThemeBindings is what makes the change live -
// design.md section 5.7.6's hot reload, at the ABI boundary, reusing 6-2's
// ThemeBindings::apply() unchanged rather than inventing per-caller
// invalidation logic three times over. The disabled palette is re-resolved
// AFTER the bindings, so a disabled node whose colour is token-bound keeps
// its disabled look and remembers the new enabled colour for later.
void reapply_theme_if_active(AppImpl& impl, const ThemeImpl& theme) {
  if (impl.active_theme != &theme) {
    return;
  }
  const dg::DisabledPalette palette = disabled_palette_for(impl);
  for (const std::unique_ptr<WindowImpl>& window : impl.windows) {
    if (!window->closed) {
      window->theme_bindings.apply(window->tree, theme.theme, theme.variant);
      window->widgets.set_disabled_palette(palette);
      window->widgets.set_scrollbar_style(scrollbar_style_for(impl));
      restyle_editors(impl, window->self_index, *window, editor_palette_for(impl));
      window->widgets.refresh_subtree(window->tree.render(), dg::LayoutTree::root(),
                                      window->interaction);
    }
  }
}

// -- Node attachment --------------------------------------------------------
//
// After a DG_PROP_DISABLED write on `node`: cancel a press/hover the write
// just disabled (no click fires), blur a focus it just disabled, and repaint
// every widget in the subtree, since the bit inherits.
void sync_disabled(AppImpl& impl, WindowImpl& window, dg::NodeId node) {
  dg::RenderTree& tree = window.tree.render();
  const dg::InteractionChange change = window.interaction.cancel_disabled(tree);
  const dg::FocusChange focus_change = window.focus.blur_if_disabled(tree);
  if (focus_change.blurred.has_value()) {
    set_editor_focus(impl, window, *focus_change.blurred, false);
  }
  const std::optional<dg::NodeId> touched[] = {change.left, change.released};
  for (const std::optional<dg::NodeId>& id : touched) {
    if (id.has_value()) {
      window.widgets.refresh(tree, *id, window.interaction.state_of(*id));
    }
  }
  window.widgets.refresh_subtree(tree, node, window.interaction);
}

// The one place a PENDING node becomes LIVE - shared by dg_window_set_root()
// (parent_id == the tree's own structural root) and dg_node_insert_before()
// (parent_id == an already-live node). Defaults are chosen by type_id
// because dg_node_set_prop() only works on a LIVE node (see node_props.h's
// PropWrite/set_prop(): a write needs a real LayoutTree&/NodeId, and a
// pending slot has neither) - so a node needs a sane box the instant it is
// attached, not an invisible 0x0 one waiting for a property write that
// cannot happen yet.
std::int32_t attach_pending_node(AppImpl& impl, std::size_t window_index, dg::NodeId parent_id,
                                 std::size_t child_index) {
  NodeSlot& slot = impl.nodes[child_index];
  if (slot.state != NodeSlot::State::kPending) {
    return DG_ERR_ALREADY_ATTACHED;
  }

  WindowImpl& window = *impl.windows[window_index];

  dg::BoxStyle box;
  dg::NodeStyle style;

  if (slot.type_id == DG_NODE_TYPE_LABEL) {
    // A label's own box carries NO fill - only the text paints - matching
    // examples/05_widgets/widget_scene.cpp's own transparent_text() shape,
    // the precedent this mirrors rather than inventing a second one.
    box.width = 240;
    box.height = 32;
    // The engine-chosen default this slice ships instead of font_size/
    // font_family properties (props/drawgui.props.toml's own comment on
    // `text`): dg_app_add_font()'s FIRST successfully added family, at a
    // fixed size, in an opaque black that is visible against this ABI's
    // own default window background (dg_window_opts::background_argb) -
    // TextStyle::color's OWN default is fully transparent
    // (graphics/types.h's Color{}), so a label with no default here would
    // draw nothing until a caller also set DG_PROP_TEXT_COLOR.
    style.text.font = impl.default_font;
    style.text.size = kDefaultLabelTextSize;
    style.text.color = dg::Color::rgba(0x00, 0x00, 0x00);
  } else if (slot.type_id == DG_NODE_TYPE_CHECKBOX) {
    // The outer 32x32 box (hover/press feedback on its OWN fill) - the exact
    // dimensions examples/11_form_controls/form_scene.cpp's add_toggle()
    // uses for an ungrouped (round == false) checkbox. The indicator CHILD
    // that paints checked/unchecked is composed further below, once new_id
    // exists to parent it under.
    box.width = 32;
    box.height = 32;
    style.fill = kCheckboxNormal;
    style.border_color = kCheckboxBorder;
    style.border_width = dg::BorderWidths::all(1.0F);
    style.radii = dg::Radii::all(6.0F);
  } else if (slot.type_id == DG_NODE_TYPE_TEXT_FIELD) {
    // examples/12_text_input/text_field_scene.cpp's add_field() box, copied
    // unchanged - starts unfocused (field_style(false)).
    box.width = kTextFieldWidth;
    box.height = kTextFieldHeight;
    style = text_field_box_style(editor_palette_for(impl), false);
  } else if (slot.type_id == DG_NODE_TYPE_TEXT_AREA) {
    box.width = kTextAreaWidth;
    box.height = kTextAreaHeight;
    style = text_field_box_style(editor_palette_for(impl), false);
  } else {
    style.fill = dg::Color::rgba(0x2B, 0x2F, 0x3A);
    if (slot.type_id == DG_NODE_TYPE_BUTTON) {
      box.width = 200;
      box.height = 64;
    } else {
      box.width = 240;
      box.height = 120;
    }
  }

  const dg::NodeId new_id = window.tree.add_child(parent_id, box, style);

  slot.state = NodeSlot::State::kLive;
  slot.window_index = window_index;
  slot.node_id = new_id;

  if (slot.type_id == DG_NODE_TYPE_BUTTON) {
    dg::Widget widget;
    widget.kind = dg::WidgetKind::kButton;
    widget.fill_normal = dg::Color::rgba(0x35, 0x6B, 0xC7);
    widget.fill_hover = dg::Color::rgba(0x4A, 0x86, 0xE3);
    widget.fill_pressed = dg::Color::rgba(0x25, 0x50, 0x9C);
    window.widgets.attach(new_id, widget);
    window.widgets.refresh(window.tree.render(), new_id, dg::PointerState{});
  } else if (slot.type_id == DG_NODE_TYPE_LABEL) {
    dg::Widget widget;
    widget.kind = dg::WidgetKind::kLabel;
    window.widgets.attach(new_id, widget);
  } else if (slot.type_id == DG_NODE_TYPE_CHECKBOX) {
    // P6 sub-step a2: the indicator CHILD - composed here, unaddressable
    // through this ABI (no dg_node_t handle names it), exactly the recipe
    // examples/11_form_controls/form_scene.cpp's add_toggle() uses. See
    // this file's own DG_NODE_TYPE_CHECKBOX constant comment (abi/
    // drawgui.def.toml) for why a caller cannot be asked to do this itself.
    dg::BoxStyle indicator_box;
    indicator_box.width = 20;
    indicator_box.height = 20;
    dg::NodeStyle indicator_style;
    indicator_style.fill = dg::Color::from_argb(kCheckboxOffArgb);
    const dg::NodeId indicator = window.tree.add_child(new_id, indicator_box, indicator_style);

    dg::Widget widget;
    widget.kind = dg::WidgetKind::kCheckbox;
    widget.fill_normal = dg::Color::from_argb(kCheckboxNormalArgb);
    widget.fill_hover = dg::Color::from_argb(kCheckboxHoverArgb);
    widget.fill_pressed = dg::Color::from_argb(kCheckboxPressedArgb);
    widget.indicator = indicator;
    widget.indicator_on = dg::Color::from_argb(kCheckboxOnArgb);
    widget.indicator_off = dg::Color::from_argb(kCheckboxOffArgb);
    widget.checked = false;
    // widget.group left unset (std::nullopt): this ABI never exports radio
    // grouping - see abi/drawgui.def.toml's own "WHAT IS DELIBERATELY NOT
    // HERE" entry on Widget::group for why.
    window.widgets.attach(new_id, widget);
    window.widgets.attach(indicator, dg::Widget{});
    window.widgets.refresh(window.tree.render(), new_id, dg::PointerState{});
  } else if (slot.type_id == DG_NODE_TYPE_TEXT_FIELD) {
    // examples/12_text_input/text_field_scene.cpp's add_field() recipe,
    // copied unchanged: four caller-invisible children, in the SAME paint
    // order (selection highlight, then composition underline, then text
    // content, then caret - "background before foreground"), none reachable
    // through this ABI (no dg_node_t names any of them), exactly the
    // DG_NODE_TYPE_CHECKBOX indicator's own trade (see this file's own
    // constant comment on DG_NODE_TYPE_TEXT_FIELD, abi/drawgui.def.toml).
    const EditorPalette editor = editor_palette_for(impl);
    dg::BoxStyle placeholder;
    placeholder.kind = dg::LayoutKind::kLeaf;
    placeholder.width = 1;
    placeholder.height = 1;

    dg::NodeStyle highlight_style;
    highlight_style.fill = editor.selection;
    const dg::NodeId highlight = window.tree.add_child(new_id, placeholder, highlight_style);

    // composition_underline: built so Widget::composition_underline names a
    // real NodeId (WidgetSet's own text_field_refresh_display() positions it
    // unconditionally), but never driven - this ABI exports no IME/
    // composition surface (abi/drawgui.def.toml's own "明确不做" entry), so
    // `composing` never becomes true and this child stays zero-size forever.
    dg::NodeStyle underline_style;
    underline_style.fill = editor.underline;
    const dg::NodeId underline = window.tree.add_child(new_id, placeholder, underline_style);

    dg::NodeStyle content_style;
    content_style.text.font = impl.default_font;
    content_style.text.size = kTextFieldFontSize;
    content_style.text.color = editor.text;
    content_style.text.align = dg::TextAlign::kLeft;
    const dg::NodeId content = window.tree.add_child(new_id, placeholder, content_style);

    dg::NodeStyle caret_style;
    caret_style.fill = editor.caret;
    const dg::NodeId caret = window.tree.add_child(new_id, placeholder, caret_style);

    dg::Widget widget;
    widget.kind = dg::WidgetKind::kTextField;
    // The box keeps its fill on hover/press, and gets it back after being
    // disabled (refresh() paints fill_normal for an enabled widget).
    widget.fill_normal = editor.fill;
    widget.fill_hover = widget.fill_normal;
    widget.fill_pressed = widget.fill_normal;
    widget.content = content;
    widget.caret = caret;
    widget.selection_highlight = highlight;
    widget.composition_underline = underline;
    window.widgets.attach(new_id, widget);
    if (impl.fonts.has_value()) {
      window.widgets.text_field_set_focus(window.tree.render(), *impl.fonts, new_id, false);
    }
  } else if (slot.type_id == DG_NODE_TYPE_TEXT_AREA) {
    // The text field's recipe, multi-line: a selection LAYER holding a pool
    // of highlight nodes (one per visible selected line), an underline pool
    // (one per visible line of an IME composition), the wrapping content,
    // the caret, and a scrollbar track + thumb added last so it paints and
    // hit-tests on top. All caller-invisible, like every composed child.
    const EditorPalette editor = editor_palette_for(impl);
    dg::BoxStyle placeholder;
    placeholder.kind = dg::LayoutKind::kLeaf;
    placeholder.width = 1;
    placeholder.height = 1;

    dg::Widget widget;
    widget.kind = dg::WidgetKind::kTextArea;
    widget.fill_normal = editor.fill;
    widget.fill_hover = widget.fill_normal;
    widget.fill_pressed = widget.fill_normal;
    widget.text_inset = kTextAreaInset;

    widget.selection_layer = window.tree.add_child(new_id, placeholder, dg::NodeStyle{});
    dg::NodeStyle highlight_style;
    highlight_style.fill = editor.selection;
    for (int i = 0; i < kTextAreaHighlightPool; ++i) {
      widget.highlight_pool.push_back(
          window.tree.add_child(widget.selection_layer, placeholder, highlight_style));
    }
    dg::NodeStyle underline_style;
    underline_style.fill = editor.underline;
    for (int i = 0; i < kTextAreaUnderlinePool; ++i) {
      widget.underline_pool.push_back(
          window.tree.add_child(new_id, placeholder, underline_style));
    }
    dg::NodeStyle content_style;
    content_style.text.font = impl.default_font;
    content_style.text.size = kTextFieldFontSize;
    content_style.text.color = editor.text;
    content_style.text.align = dg::TextAlign::kLeft;
    content_style.text.wrap = true;
    widget.content = window.tree.add_child(new_id, placeholder, content_style);
    dg::NodeStyle caret_style;
    caret_style.fill = editor.caret;
    widget.caret = window.tree.add_child(new_id, placeholder, caret_style);
    const dg::NodeId track = window.tree.add_child(new_id, placeholder, dg::NodeStyle{});
    const dg::NodeId thumb = window.tree.add_child(track, placeholder, dg::NodeStyle{});

    window.widgets.attach(new_id, widget);
    window.widgets.attach_scrollbar(window.tree.render(), new_id, track, thumb);
    // Before the first layout the area's bounds are still zero; a refresh
    // after every layout (repaint_and_present()) puts the children right.
    if (impl.fonts.has_value()) {
      window.widgets.text_area_set_focus(window.tree.render(), *impl.fonts, new_id, false);
    }
  }
  return DG_ERR_OK;
}

// -- The internal frame loop ------------------------------------------------
//
// Mirrors examples/11_form_controls/form_window.cpp's Runner::resize_if_needed()
// + Runner::draw() exactly, generalised from one window to any window this
// app owns - design.md section 5.16.1's "宿主只声明" applies to painting too:
// a C host never gets a canvas, it gets dg_poll_events()/dg_wait_events(),
// and drawgui paints and presents every frame internally.

bool ensure_surface_current(dg::WindowManager& wm, WindowImpl& window) {
  const dg::Expected<dg::PixelSize, dg::WindowError> size = wm.drawable_size(window.sdl_window);
  if (!size) {
    return false;
  }
  if (window.surface.has_value() && window.tree.viewport() == size.value()) {
    return true;
  }
  window.tree.resize(size.value());
  window.tree.layout();
  window.surface = dg::RasterSurface::create(size.value().width, size.value().height);
  return window.surface.has_value();
}

void repaint_and_present(dg::WindowManager& wm, WindowImpl& window) {
  if (!ensure_surface_current(wm, window)) {
    return;
  }
  // ensure_surface_current() returning true is what guarantees window.surface
  // is engaged, but that guarantee crosses a function boundary clang-tidy's
  // bugprone-unchecked-optional-access cannot see through - this repeats the
  // check locally, which is a real defensive redundancy as well as what
  // satisfies it: a future edit to ensure_surface_current() that stops
  // engaging the optional on some path fails here loudly instead of
  // dereferencing an empty optional.
  if (!window.surface.has_value()) {
    return;
  }
  window.tree.layout();
  if (window.tree.render().damage().is_empty()) {
    return;
  }
  window.tree.render().repaint(*window.surface);
  const dg::PixelView view = window.surface->peek_pixels();
  if (view.pixels == nullptr || !view.is_bgra8888) {
    return;
  }
  const dg::ImageView image{view.pixels, view.width, view.height, view.row_bytes,
                            dg::PixelFormat::kBgra8888};
  (void)wm.present(window.sdl_window, image, window.tree.render().painted().rects());
}

// Every transition dg::Interaction reports, refreshed the same way every
// prior C++ example's own dispatch()/react() pair already does (examples/
// 05_widgets/widget_scene.cpp) - asking dg::Interaction::state_of() for each
// touched widget rather than deriving an appearance from the event kind.
void refresh_touched(WindowImpl& window, const dg::InteractionChange& change) {
  const std::optional<dg::NodeId> touched[] = {change.left, change.entered, change.pressed,
                                               change.released, change.clicked};
  for (const std::optional<dg::NodeId>& id : touched) {
    if (id.has_value()) {
      window.widgets.refresh(window.tree.render(), *id, window.interaction.state_of(*id));
    }
  }
}

// A drag that started on a focused text area extends its selection; one that
// started on a text area's scrollbar thumb scrolls it.
void drag_text_area(AppImpl& impl, WindowImpl& window, dg::PixelPoint at) {
  const std::optional<dg::NodeId> held = window.interaction.holding();
  if (!held.has_value() || !impl.fonts.has_value()) {
    return;
  }
  const std::optional<dg::NodeId> bar_owner = window.widgets.scrollbar_owner_of(*held);
  if (bar_owner.has_value() && is_text_area(window, *bar_owner)) {
    window.widgets.scrollbar_drag(window.tree.render(), *held,
                                  window.tree.render().local_bounds(*bar_owner).height, at);
  } else if (is_text_area(window, *held) && window.focus.current() == held) {
    window.widgets.text_area_click(window.tree.render(), *impl.fonts, *held, at,
                                   /*extend_selection=*/true);
    follow_caret(impl, window, *held);
  }
}

// A press on a text area's own scrollbar scrolls it without moving focus or
// the caret. True when `hit` was such a part.
bool press_text_area_scrollbar(WindowImpl& window, std::optional<dg::NodeId> hit,
                               dg::PixelPoint at) {
  const std::optional<dg::NodeId> bar_owner =
      hit.has_value() ? window.widgets.scrollbar_owner_of(*hit) : std::nullopt;
  if (!bar_owner.has_value() || !is_text_area(window, *bar_owner)) {
    return false;
  }
  window.widgets.scrollbar_press(window.tree.render(), *hit,
                                 window.tree.render().local_bounds(*bar_owner).height, at);
  return true;
}

// Every press on a focused editor places its caret, not only the press that
// focused it.
void place_editor_caret(AppImpl& impl, WindowImpl& window, std::optional<dg::NodeId> hit,
                        dg::PixelPoint at) {
  if (!impl.fonts.has_value() || !hit.has_value() || !is_text_editor(window, *hit) ||
      window.focus.current() != hit) {
    return;
  }
  const int clicks = dg::register_click(window.clicks, at, dg::steady_anim_time());
  if (is_text_area(window, *hit)) {
    window.widgets.text_area_click(window.tree.render(), *impl.fonts, *hit, at,
                                   /*extend_selection=*/false, clicks);
    follow_caret(impl, window, *hit);
  } else {
    window.widgets.text_field_click(window.tree.render(), *impl.fonts, *hit, at.x,
                                    /*extend_selection=*/false, clicks);
  }
}

// The wheel scrolls a text area under the pointer (the only wheel-scrolled
// node type this ABI has); the caret stays where it is.
void wheel_text_area(WindowImpl& window, const dg::PointerEvent& event) {
  const dg::PixelPoint at{event.x, event.y};
  const std::optional<dg::NodeId> hit = window.tree.render().hit_test(at);
  const std::optional<dg::NodeId> owner =
      hit.has_value() ? window.widgets.scrollable_owner_of(window.tree.render(), *hit)
                      : std::nullopt;
  if (!owner.has_value() || !is_text_area(window, *owner) ||
      dg::is_disabled(window.tree.render(), *owner)) {
    return;
  }
  constexpr float kWheelStepPx = 40.0F;
  const dg::PixelRect bounds = window.tree.render().local_bounds(*owner);
  window.widgets.scroll_by(window.tree.render(), *owner,
                           dg::PixelRect{0, 0, bounds.width, bounds.height}, 0,
                           static_cast<int>(-event.wheel_y * kWheelStepPx));
}

void process_pointer_event(AppImpl& impl, WindowImpl& window, const dg::PointerEvent& event) {
  const dg::PixelPoint at{event.x, event.y};
  dg::InteractionChange change;
  switch (event.action) {
    case dg::PointerAction::kMove: {
      change =
          window.interaction.moved_over(window.widgets.widget_at(window.tree.render(), at));
      drag_text_area(impl, window, at);
      break;
    }
    case dg::PointerAction::kDown: {
      const std::optional<dg::NodeId> hit = window.widgets.widget_at(window.tree.render(), at);
      change = window.interaction.pressed_on(hit);
      // 8-3d: a press on a focusable widget focuses it (blurring whatever
      // held focus before), a press anywhere else blurs - the identical
      // rule examples/21_focus/focus_scene.cpp's own dispatch_pointer()
      // already applies to its own Scene, generalised from one example's
      // struct to this ABI's WindowImpl. This is what gives
      // dg_node_scope_action() a real `focused` node to bubble FROM
      // (design.md section 5.5.2 level 2): with no focus-follows-click
      // anywhere in this ABI, a scoped action could only ever be reached at
      // level 4 (the app-wide table, no target node), never level 2.
      const bool hit_is_focusable = hit.has_value() && window.widgets.has(*hit) &&
                                    dg::is_focusable(window.widgets.at(*hit).kind);
      if (press_text_area_scrollbar(window, hit, at)) {
        break;
      }
      const std::optional<dg::NodeId> new_focus = hit_is_focusable ? hit : std::nullopt;
      const dg::FocusChange focus_change = window.focus.set(new_focus);
      // P6 sub-step a2 (second half): the SAME border-recolour +
      // text_field_set_focus() pair examples/12_text_input/text_field_
      // scene.cpp's own set_focus() runs, generalised from one Scene's
      // handful of fields to any DG_NODE_TYPE_TEXT_FIELD this ABI creates -
      // WidgetSet::text_field_set_focus() re-derives the display projection
      // (truncation/scroll/caret visibility), the style write is cosmetic
      // feedback beside it, matching the checkbox indicator's own "state in
      // WidgetSet, pixels beside it" split.
      if (focus_change.blurred.has_value()) {
        set_editor_focus(impl, window, *focus_change.blurred, false);
      }
      if (focus_change.focused.has_value()) {
        set_editor_focus(impl, window, *focus_change.focused, true);
      }
      place_editor_caret(impl, window, hit, at);
      break;
    }
    case dg::PointerAction::kUp:
      change =
          window.interaction.released_on(window.widgets.widget_at(window.tree.render(), at));
      window.widgets.scrollbar_release();
      break;
    case dg::PointerAction::kLeave:
      change = window.interaction.left_window();
      break;
    case dg::PointerAction::kWheel:
      wheel_text_area(window, event);
      return;
  }
  refresh_touched(window, change);
  // ABI 0.2: the cursor follows the pointer. WindowManager only calls the
  // platform when the shape changes, so this is cheap on every motion.
  if (event.action != dg::PointerAction::kLeave) {
    impl.wm.set_cursor(window.sdl_window, dg::cursor_at(window.tree.render(), window.widgets,
                                                        window.interaction, at));
  }
  if (change.clicked.has_value() && window.widgets.has(*change.clicked) &&
      window.widgets.at(*change.clicked).kind == dg::WidgetKind::kCheckbox) {
    // P6 sub-step a2: a completed click on a checkbox flips its checked bit
    // immediately, before DG_EVENT_CLICK is queued - "receive CLICK, then
    // dg_node_get_checked() to ask" is the whole shape (abi/drawgui.def.toml's
    // own comment on dg_node_get_checked() explains why no DG_EVENT_TOGGLE
    // exists instead). toggle() only flips the stored bit - it has no
    // RenderTree and cannot repaint anything itself (widget_set.h's own
    // comment on toggle()/refresh()) - so refresh() runs again right after,
    // on the SAME widget, to repaint the indicator this toggle just changed.
    // No group_members() re-refresh: this ABI never exports Widget::group.
    window.widgets.toggle(*change.clicked);
    window.widgets.refresh(window.tree.render(), *change.clicked,
                           window.interaction.state_of(*change.clicked));
  }
  if (change.clicked.has_value()) {
    dg_event out{};
    out.size = sizeof(dg_event);
    out.kind = DG_EVENT_CLICK;
    out.window = window.self_handle;
    out.node = find_node_handle(impl, window.self_index, *change.clicked);
    out.x = event.x;
    out.y = event.y;
    impl.pending_events.push_back(out);
  }
}

// P6 sub-step a2 (second half): dispatches Left/Right/Home/End/Backspace/
// Delete on a focused DG_NODE_TYPE_TEXT_FIELD DIRECTLY to WidgetSet's own
// text_field_move()/backspace()/delete_forward() - the SAME switch
// examples/12_text_input/text_field_window.cpp's own Runner::handle_key()
// already runs, copied rather than routed through design.md section
// 5.5.2's four-level router: ActionScope::kTextField only ever carries the
// four clipboard actions (dg::clipboard_actions.h's own header comment -
// copy/cut/paste/select_all), never a caret move or a delete, so there is
// no ActionId for route_key_event() to resolve one of these keys to.
// Returns whether `field`'s COMMITTED TEXT changed (Backspace/Delete only -
// a plain move never touches Widget::edit.text), which is what tells the caller
// whether to queue a DG_EVENT_TEXT_CHANGED.
// kTextArea's counterpart of handle_text_field_key(): its larger key set
// (router.cpp's is_text_editing_intent() names the same one). Returns
// whether the committed text changed; `consumed` reports whether the key
// was one of the area's own (everything else continues to the router).
bool handle_text_area_key(WindowImpl& window, const dg::FontCatalog& fonts, dg::NodeId area,
                          const dg::KeyEvent& event, bool& consumed) {
  dg::RenderTree& tree = window.tree.render();
  const bool shift = dg::has(event.mods, dg::Modifier::kShift);
  const bool mod = dg::has(event.mods, dg::Modifier::kMod);
  const auto move = [&](dg::TextAreaMove where) {
    window.widgets.text_area_move(tree, fonts, area, where, shift);
    return false;
  };
  consumed = true;
  switch (event.key) {
    case dg::Key::kLeft:
      return move(mod ? dg::TextAreaMove::kWordLeft : dg::TextAreaMove::kCharLeft);
    case dg::Key::kRight:
      return move(mod ? dg::TextAreaMove::kWordRight : dg::TextAreaMove::kCharRight);
    case dg::Key::kUp:
      return move(dg::TextAreaMove::kLineUp);
    case dg::Key::kDown:
      return move(dg::TextAreaMove::kLineDown);
    case dg::Key::kPageUp:
      return move(dg::TextAreaMove::kPageUp);
    case dg::Key::kPageDown:
      return move(dg::TextAreaMove::kPageDown);
    case dg::Key::kHome:
      return move(mod ? dg::TextAreaMove::kDocStart : dg::TextAreaMove::kLineStart);
    case dg::Key::kEnd:
      return move(mod ? dg::TextAreaMove::kDocEnd : dg::TextAreaMove::kLineEnd);
    case dg::Key::kBackspace:
      return mod ? window.widgets.editor_delete_word(tree, fonts, area, true)
                 : window.widgets.text_area_backspace(tree, fonts, area);
    case dg::Key::kDelete:
      return mod ? window.widgets.editor_delete_word(tree, fonts, area, false)
                 : window.widgets.text_area_delete_forward(tree, fonts, area);
    case dg::Key::kEnter:
      if (!mod && !dg::has(event.mods, dg::Modifier::kAlt)) {
        return window.widgets.text_area_insert(tree, fonts, area, "\n");
      }
      break;
    case dg::Key::kEscape:
    case dg::Key::kTab:
    case dg::Key::kOther:
      break;
  }
  consumed = false;
  return false;
}

// Undo/redo (input/shortcuts.toml's undo/redo/redo_alt chords) on a focused,
// enabled editor, applied here before the router like the editing keys
// above - the host does not have to scope or handle them. Returns whether
// the key was one of those chords (consumed even when nothing was undone).
bool apply_editor_history_key(AppImpl& impl, WindowImpl& window, dg::NodeId editor,
                              const dg::KeyEvent& event) {
  if (!impl.fonts.has_value() || !is_text_editor(window, editor) ||
      dg::is_disabled(window.tree.render(), editor)) {
    return false;
  }
  const dg::Chord chord{event.mods, event.logical_key};
  for (const dg::ShortcutBinding& binding : dg::all_shortcut_bindings()) {
    if (binding.chord != chord) {
      continue;
    }
    bool changed = false;
    if (binding.action_id == DG_ACTION_UNDO) {
      changed = window.widgets.editor_undo(window.tree.render(), *impl.fonts, editor);
    } else if (binding.action_id == DG_ACTION_REDO || binding.action_id == DG_ACTION_REDO_ALT) {
      changed = window.widgets.editor_redo(window.tree.render(), *impl.fonts, editor);
    } else {
      continue;
    }
    if (changed) {
      dg_event out{};
      out.size = sizeof(dg_event);
      out.kind = DG_EVENT_TEXT_CHANGED;
      out.window = window.self_handle;
      out.node = find_node_handle(impl, window.self_index, editor);
      impl.pending_events.push_back(out);
    }
    if (is_text_area(window, editor)) {
      follow_caret(impl, window, editor);
    }
    return true;
  }
  return false;
}

bool handle_text_field_key(WindowImpl& window, const dg::FontCatalog& fonts, dg::NodeId field,
                           const dg::KeyEvent& event) {
  dg::RenderTree& tree = window.tree.render();
  const bool shift = dg::has(event.mods, dg::Modifier::kShift);
  const bool mod = dg::has(event.mods, dg::Modifier::kMod);
  switch (event.key) {
    case dg::Key::kLeft:
      window.widgets.text_field_move(
          tree, fonts, field, mod ? dg::TextFieldMove::kWordLeft : dg::TextFieldMove::kCharLeft,
          shift);
      return false;
    case dg::Key::kRight:
      window.widgets.text_field_move(
          tree, fonts, field,
          mod ? dg::TextFieldMove::kWordRight : dg::TextFieldMove::kCharRight, shift);
      return false;
    case dg::Key::kHome:
      window.widgets.text_field_move(tree, fonts, field, dg::TextFieldMove::kLineStart, shift);
      return false;
    case dg::Key::kEnd:
      window.widgets.text_field_move(tree, fonts, field, dg::TextFieldMove::kLineEnd, shift);
      return false;
    case dg::Key::kBackspace:
      return mod ? window.widgets.editor_delete_word(tree, fonts, field, true)
                 : window.widgets.text_field_backspace(tree, fonts, field);
    case dg::Key::kDelete:
      return mod ? window.widgets.editor_delete_word(tree, fonts, field, false)
                 : window.widgets.text_field_delete_forward(tree, fonts, field);
    case dg::Key::kEscape:
    case dg::Key::kTab:
    case dg::Key::kUp:
    case dg::Key::kDown:
    case dg::Key::kEnter:
    case dg::Key::kPageUp:
    case dg::Key::kPageDown:
    case dg::Key::kOther:
      return false;
  }
  return false;
}

// 8-3d: design.md section 5.5.2's four-level router, driven by a REAL
// dg::KeyEvent this ABI's own SDL pump produced (or, for
// dg_debug_post_key()'s own test injection, a real SDL key event posted
// through the real queue exactly like dg_debug_post_pointer_button() does
// for clicks) - the ABI's own second caller of dg::route_key_event(),
// after examples/10_scrolling's C++ one (8-3c). Only KeyAction::kDown
// reaches resolve_action(): route_key_event() itself is blind to
// event.action (a chord fires identically whichever way this file calls
// it), so gating here, once, is what keeps a single key press from firing
// its resolved action twice - on press AND on release.
void process_key_event(AppImpl& impl, WindowImpl& window, const dg::KeyEvent& event) {
  if (event.action != dg::KeyAction::kDown) {
    return;
  }
  // P6 sub-step a2 (second half): a focused text field's own editing keys
  // are consumed HERE, before the router runs at all - see
  // handle_text_field_key()'s own header comment for why these six keys
  // have no ActionId to route to in the first place. Consuming them first
  // also sidesteps a real ambiguity the router alone cannot resolve: Home/
  // End are ALSO the app-scope keyboard-scrolling chord (keyboard_scroll.h)
  // - a text field's own caret move must win while it is focused, matching
  // every desktop toolkit's "a focused text field owns Home/End, a
  // scrollable region only gets them when nothing more specific does".
  const std::optional<dg::NodeId> focused = window.focus.current();
  window.widgets.set_edit_time(dg::steady_anim_time());
  // A key press between two clicks ends a multi-click sequence: the second
  // click places a caret instead of selecting a word.
  window.clicks = dg::ClickCounter{};
  if (focused.has_value() && apply_editor_history_key(impl, window, *focused, event)) {
    return;
  }
  if (focused.has_value() && is_text_area(window, *focused) && impl.fonts.has_value() &&
      !dg::is_disabled(window.tree.render(), *focused)) {
    bool consumed = false;
    const bool text_changed =
        handle_text_area_key(window, *impl.fonts, *focused, event, consumed);
    if (text_changed) {
      dg_event out{};
      out.size = sizeof(dg_event);
      out.kind = DG_EVENT_TEXT_CHANGED;
      out.window = window.self_handle;
      out.node = find_node_handle(impl, window.self_index, *focused);
      impl.pending_events.push_back(out);
    }
    if (consumed) {
      follow_caret(impl, window, *focused);
      return;
    }
  }
  if (focused.has_value() && window.widgets.has(*focused) &&
      window.widgets.at(*focused).kind == dg::WidgetKind::kTextField &&
      impl.fonts.has_value()) {
    const bool text_changed = handle_text_field_key(window, *impl.fonts, *focused, event);
    if (text_changed) {
      dg_event out{};
      out.size = sizeof(dg_event);
      out.kind = DG_EVENT_TEXT_CHANGED;
      out.window = window.self_handle;
      out.node = find_node_handle(impl, window.self_index, *focused);
      impl.pending_events.push_back(out);
    }
    if (event.key != dg::Key::kOther) {
      // One of the six editing keys handle_text_field_key() switches on
      // (kOther is everything else - a plain character key SDL also
      // reports as a KeyEvent beside its own TextInputEvent, which must
      // still reach the router for e.g. Mod+C).
      return;
    }
  }
  const dg::RoutingContext ctx{window.tree.render(), window.widgets, window.action_scopes,
                               dg::all_shortcut_bindings()};
  const dg::KeyRouteResult routed =
      dg::route_key_event(event, window.sdl_window, window.focus.current(), ctx);
  if (routed.outcome != dg::KeyRouteOutcome::kRouted || !routed.action.has_value()) {
    return;
  }
  const dg::ResolvedAction& action = *routed.action;
  dg_event out{};
  out.size = sizeof(dg_event);
  out.kind = DG_EVENT_ACTION;
  out.window = window.self_handle;
  out.node = action.target.has_value()
                 ? find_node_handle(impl, window.self_index, *action.target)
                 : nullptr;
  out.action_id = action.action_id;
  impl.pending_events.push_back(out);
}

dg_event to_window_dg_event(const WindowImpl& window, const dg::WindowEvent& event) {
  dg_event out{};
  out.size = sizeof(dg_event);
  out.window = window.self_handle;
  switch (event.kind) {
    case dg::WindowEventKind::kResized:
      out.kind = DG_EVENT_WINDOW_RESIZED;
      out.x = event.width;
      out.y = event.height;
      break;
    case dg::WindowEventKind::kMoved:
      out.kind = DG_EVENT_WINDOW_MOVED;
      out.x = event.x;
      out.y = event.y;
      break;
    case dg::WindowEventKind::kMinimized:
      out.kind = DG_EVENT_WINDOW_MINIMIZED;
      break;
    case dg::WindowEventKind::kMaximized:
      out.kind = DG_EVENT_WINDOW_MAXIMIZED;
      break;
    case dg::WindowEventKind::kRestored:
      out.kind = DG_EVENT_WINDOW_RESTORED;
      break;
    case dg::WindowEventKind::kFocusGained:
      out.kind = DG_EVENT_WINDOW_FOCUS_GAINED;
      break;
    case dg::WindowEventKind::kFocusLost:
      out.kind = DG_EVENT_WINDOW_FOCUS_LOST;
      break;
    case dg::WindowEventKind::kEnteredFullscreen:
      out.kind = DG_EVENT_WINDOW_FULLSCREEN_ENTERED;
      break;
    case dg::WindowEventKind::kLeftFullscreen:
      out.kind = DG_EVENT_WINDOW_FULLSCREEN_LEFT;
      break;
  }
  return out;
}

// ABI 0.2. A resize needs nothing here beyond the event itself: process_pump()'s
// ensure_surface_current() pass has already rebuilt the surface at the new
// drawable size, the same path every resize always took.
void queue_window_events(AppImpl& impl, const std::vector<dg::WindowEvent>& events) {
  for (const dg::WindowEvent& event : events) {
    const WindowImpl* window = find_window_by_sdl_id(impl, event.window);
    if (window != nullptr) {
      impl.pending_events.push_back(to_window_dg_event(*window, event));
    }
  }
}

void follow_caret(AppImpl& impl, WindowImpl& window, dg::NodeId area) {
  const std::optional<dg::PixelRect> caret =
      window.widgets.text_area_caret_rect(window.tree.render(), area);
  if (caret.has_value()) {
    impl.wm.start_text_input(window.sdl_window, *caret);
  }
}

void resync_text_areas(AppImpl& impl, WindowImpl& window) {
  if (!impl.fonts.has_value()) {
    return;
  }
  for (const NodeSlot& slot : impl.nodes) {
    if (slot.type_id == DG_NODE_TYPE_TEXT_AREA && slot.state == NodeSlot::State::kLive &&
        impl.windows[slot.window_index].get() == &window) {
      window.widgets.text_area_refresh(window.tree.render(), *impl.fonts, slot.node_id);
    }
  }
}

bool insert_into_editor(AppImpl& impl, WindowImpl& window, dg::NodeId editor,
                        const std::string& text) {
  if (!impl.fonts.has_value()) {
    return false;
  }
  window.widgets.set_edit_time(dg::steady_anim_time());
  const bool changed =
      window.widgets.editor_insert(window.tree.render(), *impl.fonts, editor, text);
  if (is_text_area(window, editor)) {
    follow_caret(impl, window, editor);
  }
  return changed;
}

void process_pump(AppImpl& impl, std::int32_t timeout_ms) {
  const dg::PumpResult pumped = impl.wm.pump(timeout_ms);

  // A node attached since the last pump (dg_window_set_root()/
  // dg_node_insert_before()) has all-zero bounds() until LayoutTree::
  // layout() has run at least once - LayoutTree::add_child()'s own header
  // comment: "reading absolute_bounds() before then answers with zero".
  // Every still-open window's layout must therefore be brought current
  // BEFORE this pump's own pointer events are hit-tested against it, not
  // only at the end where repaint_and_present() would otherwise be the
  // first thing to call layout() - a click delivered in the very same
  // pump() call that first attached its target's parent must not be
  // hit-tested against a stale, never-laid-out tree.
  for (const std::unique_ptr<WindowImpl>& window : impl.windows) {
    if (!window->closed) {
      ensure_surface_current(impl.wm, *window);
    }
  }

  for (const dg::WindowId closed_id : pumped.closed) {
    WindowImpl* window = find_window_by_sdl_id(impl, closed_id);
    if (window == nullptr) {
      continue;
    }
    window->closed = true;
    dg_event out{};
    out.size = sizeof(dg_event);
    out.kind = DG_EVENT_WINDOW_CLOSED;
    out.window = window->self_handle;
    impl.pending_events.push_back(out);
  }

  queue_window_events(impl, pumped.window_events);

  for (const dg::PointerEvent& event : pumped.pointer) {
    WindowImpl* window = find_window_by_sdl_id(impl, event.window);
    if (window != nullptr) {
      process_pointer_event(impl, *window, event);
    }
  }

  for (const dg::KeyEvent& event : pumped.key) {
    WindowImpl* window = find_window_by_sdl_id(impl, event.window);
    if (window != nullptr) {
      process_key_event(impl, *window, event);
    }
  }

  // P6 sub-step a2 (second half): a committed SDL_EVENT_TEXT_INPUT reaches
  // whichever node THIS ABI's own dg::Focus currently names - never SDL's
  // own IME capture state, matching window_create()'s own comment on why
  // start_text_input() runs once, for a window's whole lifetime, rather
  // than being toggled per focus change. A no-op if nothing is focused, or
  // the focused node is not a DG_NODE_TYPE_TEXT_FIELD, or this app has no
  // FontCatalog yet (text_field_insert() needs one to re-measure the
  // display projection - the same precondition dg_node_set_text() already
  // has for DG_NODE_TYPE_LABEL).
  for (const dg::TextInputEvent& event : pumped.text_input) {
    WindowImpl* window = find_window_by_sdl_id(impl, event.window);
    if (window == nullptr || !impl.fonts.has_value()) {
      continue;
    }
    const std::optional<dg::NodeId> focused = window->focus.current();
    if (!focused.has_value() || !is_text_editor(*window, *focused)) {
      continue;
    }
    const bool changed = insert_into_editor(impl, *window, *focused, event.text);
    if (changed) {
      dg_event out{};
      out.size = sizeof(dg_event);
      out.kind = DG_EVENT_TEXT_CHANGED;
      out.window = window->self_handle;
      out.node = find_node_handle(impl, window->self_index, *focused);
      impl.pending_events.push_back(out);
    }
  }

  for (const std::unique_ptr<WindowImpl>& window : impl.windows) {
    if (!window->closed) {
      // A relayout re-places a text area's composed children at its content
      // origin (and may have resized it): re-derive them before painting.
      window->tree.layout();
      resync_text_areas(impl, *window);
      repaint_and_present(impl.wm, *window);
    }
  }
}

// -- dg_dump_layout_tree ------------------------------------------------

const char* layout_kind_name(dg::LayoutKind kind) {
  switch (kind) {
    case dg::LayoutKind::kLeaf:
      return "leaf";
    case dg::LayoutKind::kRow:
      return "row";
    case dg::LayoutKind::kColumn:
      return "column";
    case dg::LayoutKind::kWrapRow:
      return "wrap_row";
    case dg::LayoutKind::kWrapColumn:
      return "wrap_column";
    case dg::LayoutKind::kAbsolute:
      return "absolute";
  }
  return "unknown";
}

// design.md section 5.8's dump_layout_tree sketch asks for "类型/约束/尺寸/
// 偏移/parentData/边界标记" - type/constraints/size/offset/parentData/
// boundary flags. What is actually emitted, and what is not, honestly:
//
//   type       LayoutKind, via BoxStyle::kind.               EMITTED
//   size       the resolved border box, via LayoutTree::bounds().  EMITTED
//   offset     the box's own x/y is the offset from its parent's
//              content origin - the same rectangle carries both.  EMITTED
//   parentData grow/shrink/basis/left/top/right/bottom - the fields
//              box.h documents as parentData - when set to something
//              other than their default.                          EMITTED
//   constraints  NOT stored per-node anywhere LayoutTree exposes
//                publicly once layout() returns; only the resolved
//                size survives. Omitted rather than approximated.
//   boundary flags  "is this node a relayout boundary" is a fact
//                   layout_impl.h computes and discards per pass; no
//                   public accessor exists. Omitted for the same reason.
//
// Both omissions are named here rather than silently missing.
void dump_node(const dg::LayoutTree& tree, dg::NodeId id, std::string& out) {
  const dg::BoxStyle& box = tree.box(id);
  const dg::PixelRect bounds = tree.bounds(id);

  out += "{\"type\":\"";
  out += layout_kind_name(box.kind);
  out += "\",\"bounds\":{\"x\":";
  out += std::to_string(bounds.x);
  out += ",\"y\":";
  out += std::to_string(bounds.y);
  out += ",\"w\":";
  out += std::to_string(bounds.width);
  out += ",\"h\":";
  out += std::to_string(bounds.height);
  out += "},\"parent_data\":{";

  bool first = true;
  const auto emit = [&](const char* name, int field_value) {
    if (!first) {
      out += ",";
    }
    first = false;
    out += "\"";
    out += name;
    out += "\":";
    out += std::to_string(field_value);
  };
  if (box.grow != 0) {
    emit("grow", box.grow);
  }
  if (box.shrink != 0) {
    emit("shrink", box.shrink);
  }
  if (box.basis.has_value()) {
    emit("basis", *box.basis);
  }
  if (box.left.has_value()) {
    emit("left", *box.left);
  }
  if (box.top.has_value()) {
    emit("top", *box.top);
  }
  if (box.right.has_value()) {
    emit("right", *box.right);
  }
  if (box.bottom.has_value()) {
    emit("bottom", *box.bottom);
  }

  out += "},\"children\":[";
  const std::vector<dg::NodeId> kids = tree.render().children(id);
  for (std::size_t i = 0; i < kids.size(); ++i) {
    if (i != 0) {
      out += ",";
    }
    dump_node(tree, kids[i], out);
  }
  out += "]}";
}

}  // namespace

void set_last_error(std::string message) {
  last_error_storage() = std::move(message);
}

std::uint32_t abi_version() {
  return (static_cast<std::uint32_t>(DG_ABI_VERSION_MAJOR) << 16) |
         static_cast<std::uint32_t>(DG_ABI_VERSION_MINOR);
}

dg_app_t* app_create(const dg_app_opts* opts) {
  // dg_app_opts carries only `size` today (design.md section 5.8 decision 4:
  // a struct is future-proofed by its size field even before it has a second
  // one to append) - opts is accepted, matching the ABI signature, but there
  // is nothing in it yet for this function to read.
  (void)opts;
  dg::Expected<dg::WindowManager, dg::WindowError> created = dg::WindowManager::create();
  if (!created) {
    set_last_error(std::string("dg_app_create: ") + created.error().message);
    return nullptr;
  }

  std::vector<std::unique_ptr<AppImpl>>& apps = all_apps();
  std::vector<bool>& alive = all_apps_alive();
  apps.push_back(std::make_unique<AppImpl>(std::move(created).value()));
  alive.push_back(true);
  all_app_handles().emplace_back(dg_app_s{static_cast<std::uint32_t>(apps.size() - 1)});
  return &all_app_handles().back();
}

void app_destroy(dg_app_t* app) {
  AppImpl* impl = resolve_app(app);
  if (impl == nullptr) {
    set_last_error("dg_app_destroy: invalid handle");
    return;
  }
  all_apps()[app->index].reset();
  all_apps_alive()[app->index] = false;
}

std::int32_t app_add_font(dg_app_t* app, const char* directory, const char* family) {
  AppImpl* impl = resolve_app(app);
  if (impl == nullptr) {
    set_last_error("dg_app_add_font: invalid app handle");
    return DG_ERR_INVALID_HANDLE;
  }
  if (directory == nullptr || family == nullptr) {
    set_last_error("dg_app_add_font: directory and family must not be null");
    return DG_ERR_INVALID_ARGUMENT;
  }

  // The scan happens once per app: a second call reuses the SAME catalog
  // (FontCatalog is a handle to one reference-counted table, per its own
  // header comment) rather than re-scanning `directory` a second time -
  // this `directory` argument is therefore ignored on every call after the
  // first, exactly as this function's def.toml summary documents.
  if (!impl->fonts.has_value()) {
    dg::Expected<dg::FontCatalog, dg::FontError> scanned = dg::FontCatalog::scan(directory);
    if (!scanned) {
      set_last_error(std::string("dg_app_add_font: ") + scanned.error().message);
      return DG_ERR_INVALID_ARGUMENT;
    }
    impl->fonts = std::move(scanned).value();
  }

  const dg::Expected<dg::FontId, dg::FontError> added = impl->fonts->add(family, false);
  if (!added) {
    set_last_error(std::string("dg_app_add_font: ") + added.error().message);
    return DG_ERR_INVALID_ARGUMENT;
  }
  if (!impl->default_font.is_valid()) {
    impl->default_font = added.value();
  }
  return DG_ERR_OK;
}

dg_window_t* window_create(dg_app_t* app, const dg_window_opts* opts) {
  AppImpl* impl = resolve_app(app);
  if (impl == nullptr) {
    set_last_error("dg_window_create: invalid app handle");
    return nullptr;
  }
  if (opts == nullptr) {
    set_last_error("dg_window_create: opts must not be null");
    return nullptr;
  }
  if (opts->width <= 0 || opts->height <= 0) {
    set_last_error("dg_window_create: width and height must be positive");
    return nullptr;
  }

  dg::WindowSpec spec;
  spec.title = opts->title != nullptr ? std::string(opts->title) : std::string();
  spec.width = opts->width;
  spec.height = opts->height;
  spec.fill = dg::Color::from_argb(opts->background_argb);
  // ABI 0.2's appended fields, read only from a caller whose struct is long
  // enough to carry them - an ABI 0.1 build passes the shorter size and
  // gets 0.1's behaviour.
  if (opts->size >= offsetof(dg_window_opts, max_height) + sizeof(opts->max_height)) {
    spec.resizable = opts->resizable != 0;
    spec.min_width = opts->min_width;
    spec.min_height = opts->min_height;
    spec.max_width = opts->max_width;
    spec.max_height = opts->max_height;
  }

  dg::Expected<dg::WindowId, dg::WindowError> opened = impl->wm.open(spec);
  if (!opened) {
    set_last_error(std::string("dg_window_create: ") + opened.error().message);
    return nullptr;
  }

  dg::TreeSpec tree_spec;
  tree_spec.viewport = dg::PixelSize{opts->width, opts->height};
  tree_spec.background.fill = dg::Color::from_argb(opts->background_argb);
  // dg_app_add_font()'s catalog, copied (cheap - a shared, reference-counted
  // table, per FontCatalog's own header comment) into THIS window's own
  // TreeSpec. A window opened before any dg_app_add_font() call gets
  // std::nullopt here, matching render_tree.h's "absent means no node in
  // this tree draws text" - exactly the state every window had before this
  // slice.
  tree_spec.fonts = impl->fonts;

  auto window = std::make_unique<WindowImpl>(tree_spec);
  window->sdl_window = opened.value();
  window->self_index = impl->windows.size();
  window->widgets.set_disabled_palette(disabled_palette_for(*impl));
  window->widgets.set_scrollbar_style(scrollbar_style_for(*impl));
  // P6 sub-step a2 (second half): enabled for the window's whole lifetime,
  // never toggled per focus change - the identical choice
  // examples/12_text_input/text_field_window.cpp's own Runner::
  // resize_if_needed() makes and explains: SDL generates no
  // SDL_EVENT_TEXT_INPUT at all until this has been called on the window
  // (window_manager.h's own start_text_input() doc comment), but it is
  // this ABI's OWN dg::Focus - not SDL's IME capture state - that decides
  // which node (if any) a committed character actually reaches
  // (process_pump()'s pumped.text_input loop below only ever delivers to
  // window.focus.current(), and only when it names a DG_NODE_TYPE_TEXT_FIELD).
  impl->wm.start_text_input(window->sdl_window, dg::PixelRect{0, 0, 1, 1});
  impl->windows.push_back(std::move(window));

  all_window_handles().emplace_back(
      dg_window_s{app, static_cast<std::uint32_t>(impl->windows.size() - 1)});
  dg_window_t* handle = &all_window_handles().back();
  impl->windows.back()->self_handle = handle;
  return handle;
}

std::int32_t window_set_root(dg_window_t* window_h, dg_node_t* node_h) {
  if (window_h == nullptr || node_h == nullptr) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  if (window_h->app != node_h->app) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  WindowImpl* window = resolve_window(window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  AppImpl* impl = resolve_app(window_h->app);
  if (impl == nullptr || node_h->index >= impl->nodes.size()) {
    return DG_ERR_INVALID_HANDLE;
  }
  NodeSlot& slot = impl->nodes[node_h->index];
  if (slot.state == NodeSlot::State::kRemoved) {
    return DG_ERR_INVALID_HANDLE;
  }
  if (slot.state != NodeSlot::State::kPending) {
    return DG_ERR_ALREADY_ATTACHED;
  }
  if (window->root.has_value()) {
    return DG_ERR_ALREADY_ATTACHED;
  }

  const std::int32_t status =
      attach_pending_node(*impl, window_h->index, dg::LayoutTree::root(), node_h->index);
  if (status == DG_ERR_OK) {
    window->root = slot.node_id;
  }
  return status;
}

dg_node_t* node_create(dg_app_t* app, std::uint16_t type_id) {
  AppImpl* impl = resolve_app(app);
  if (impl == nullptr) {
    set_last_error("dg_node_create: invalid app handle");
    return nullptr;
  }
  if (type_id != DG_NODE_TYPE_BOX && type_id != DG_NODE_TYPE_BUTTON &&
      type_id != DG_NODE_TYPE_LABEL && type_id != DG_NODE_TYPE_CHECKBOX &&
      type_id != DG_NODE_TYPE_TEXT_FIELD && type_id != DG_NODE_TYPE_TEXT_AREA) {
    set_last_error("dg_node_create: unknown type_id");
    return nullptr;
  }

  NodeSlot slot;
  slot.type_id = type_id;
  impl->nodes.push_back(slot);
  all_node_handles().emplace_back(
      dg_node_s{app, static_cast<std::uint32_t>(impl->nodes.size() - 1)});
  dg_node_t* handle = &all_node_handles().back();
  impl->nodes.back().self_handle = handle;
  return handle;
}

std::int32_t node_set_prop(dg_node_t* node_h, std::uint16_t prop_id, const dg_value* value) {
  if (node_h == nullptr || value == nullptr) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  if (value->size < sizeof(dg_value)) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  NodeSlot* slot = resolve_node_slot(node_h);
  if (slot == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  if (slot->state != NodeSlot::State::kLive) {
    return DG_ERR_NO_WINDOW;
  }

  AppImpl* impl = resolve_app(node_h->app);
  WindowImpl& window = *impl->windows[slot->window_index];

  // 7-6: DG_VALUE_TOKEN is not a literal - it names a $token live reference
  // (design.md section 5.7.2) by token_id (carried in `bits`, the same
  // field an ordinary color value's ARGB already occupies). This reuses
  // dg_node_set_prop() unchanged rather than adding a second, bind-shaped
  // exported function - the ABI's own instance of the
  // "resolution reuses dg::set_prop() unchanged" rule, one layer up.
  if (value->type == DG_VALUE_TOKEN) {
    if (impl->active_theme == nullptr) {
      return DG_ERR_NO_ACTIVE_THEME;
    }
    const dg::PropWrite result = dg::bind_token(
        window.tree, window.theme_bindings, slot->node_id, prop_id, impl->active_theme->theme,
        impl->active_theme->variant, static_cast<dg_token_id>(value->bits));
    return to_error_code(result.status);
  }

  const std::optional<dg::PropValue> prop_value = to_prop_value(*value);
  if (!prop_value.has_value()) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  const dg::PropWrite result = dg::set_prop(window.tree, slot->node_id, prop_id, *prop_value);
  if (result.ok() && prop_id == DG_PROP_DISABLED) {
    sync_disabled(*impl, window, slot->node_id);
  }
  return to_error_code(result.status);
}

std::int32_t node_set_text(dg_node_t* node_h, std::uint16_t prop_id, const char* utf8,
                           std::uint32_t len) {
  if (node_h == nullptr || (utf8 == nullptr && len != 0)) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  NodeSlot* slot = resolve_node_slot(node_h);
  if (slot == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  if (slot->state != NodeSlot::State::kLive) {
    return DG_ERR_NO_WINDOW;
  }

  AppImpl* impl = resolve_app(node_h->app);
  WindowImpl& window = *impl->windows[slot->window_index];

  const std::string_view text(utf8 == nullptr ? "" : utf8, len);
  if (prop_id == DG_PROP_TEXT && is_text_editor(window, slot->node_id)) {
    // An editor's text is its MODEL, not its own node's TextStyle: replace
    // it (caret to the end). Not reported as DG_EVENT_TEXT_CHANGED - that
    // event is for user edits, and the caller already knows what it set.
    if (impl->fonts.has_value()) {
      window.widgets.editor_set_text(window.tree.render(), *impl->fonts, slot->node_id, text,
                                     window.focus.current() == slot->node_id);
    }
    return DG_ERR_OK;
  }
  const dg::PropWrite result = dg::set_text(window.tree, slot->node_id, prop_id, text);
  return to_error_code(result.status);
}

std::int32_t node_insert_before(dg_node_t* parent_h, dg_node_t* child_h, dg_node_t* ref_h) {
  if (parent_h == nullptr || child_h == nullptr) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  if (parent_h->app != child_h->app || (ref_h != nullptr && ref_h->app != parent_h->app)) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  NodeSlot* parent_slot = resolve_node_slot(parent_h);
  if (parent_slot == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  if (parent_slot->state != NodeSlot::State::kLive) {
    return DG_ERR_NO_WINDOW;
  }
  NodeSlot* child_slot = resolve_node_slot(child_h);
  if (child_slot == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  AppImpl* impl = resolve_app(parent_h->app);
  if (impl == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  WindowImpl& window = *impl->windows[parent_slot->window_index];

  // `ref` must be a live DIRECT child of `parent` in the same window, and
  // never the node being inserted - anything else names no position.
  std::optional<dg::NodeId> before;
  if (ref_h != nullptr) {
    NodeSlot* ref_slot = resolve_node_slot(ref_h);
    if (ref_slot == nullptr) {
      return DG_ERR_INVALID_HANDLE;
    }
    if (ref_h == child_h || ref_slot->state != NodeSlot::State::kLive ||
        ref_slot->window_index != parent_slot->window_index ||
        window.tree.render().parent(ref_slot->node_id) != parent_slot->node_id) {
      return DG_ERR_INVALID_ARGUMENT;
    }
    before = ref_slot->node_id;
  }

  if (child_slot->state == NodeSlot::State::kPending) {
    const std::int32_t status = attach_pending_node(*impl, parent_slot->window_index,
                                                    parent_slot->node_id, child_h->index);
    if (status != DG_ERR_OK || !before.has_value()) {
      return status;
    }
    // attach_pending_node() always appends; a fresh last child moved before
    // a sibling of the same parent cannot fail.
    window.tree.move_child(child_slot->node_id, parent_slot->node_id, before);
    return DG_ERR_OK;
  }

  // A live child is a MOVE, and only within its own window: its NodeId,
  // widget state and handle all survive, so no side table is touched.
  if (child_slot->window_index != parent_slot->window_index) {
    return DG_ERR_ALREADY_ATTACHED;
  }
  // Refused by LayoutTree::move_child() when `parent` lies inside `child`'s
  // own subtree (including parent == child) - a cycle.
  if (!window.tree.move_child(child_slot->node_id, parent_slot->node_id, before)) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  if (window.root == child_slot->node_id) {
    // Unreachable in practice - a window root moved under any live node is
    // always a cycle - but kept honest rather than leaving a stale root.
    window.root.reset();
  }
  return DG_ERR_OK;
}

std::int32_t node_remove(dg_node_t* node_h) {
  NodeSlot* slot = resolve_node_slot(node_h);
  if (slot == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  // A PENDING node was never attached to any window's tree at all
  // (attach_pending_node() is the only thing that ever calls
  // LayoutTree::add_child()), so there is nothing for remove_subtree() to
  // detach or any side table to have an entry for - tombstoning the ABI
  // handle below is the whole of what removing one requires.
  if (slot->state == NodeSlot::State::kLive) {
    AppImpl* impl = resolve_app(node_h->app);
    const std::size_t window_index = slot->window_index;
    WindowImpl& window = *impl->windows[window_index];
    // Collected BEFORE removal: afterwards the subtree's NodeIds are
    // tombstoned and may be reused by the next add_child().
    const std::vector<dg::NodeId> subtree = dg::subtree_of(window.tree.render(), slot->node_id);
    if (!dg::remove_subtree(window.tree, slot->node_id,
                            {window.widgets, window.theme_bindings, window.focus,
                             window.animation, window.action_scopes, window.interaction})) {
      // Unreachable: a LIVE slot never names the structural root
      // LayoutTree::root() (window.root is always a CHILD of it). Reported
      // rather than asserted.
      return DG_ERR_UNSUPPORTED;
    }
    // Removing the window's root node clears the root: the window shows its
    // bare background and dg_window_set_root() may attach a new one.
    if (window.root.has_value() &&
        std::find(subtree.begin(), subtree.end(), *window.root) != subtree.end()) {
      window.root.reset();
    }
    // Every DESCENDANT that has a handle is gone too - its handle must not
    // keep resolving to a NodeId a later add_child() may reuse.
    for (NodeSlot& other : impl->nodes) {
      if (other.state == NodeSlot::State::kLive && other.window_index == window_index &&
          std::find(subtree.begin(), subtree.end(), other.node_id) != subtree.end()) {
        other.state = NodeSlot::State::kRemoved;
      }
    }
  }
  slot->state = NodeSlot::State::kRemoved;
  return DG_ERR_OK;
}

std::int32_t node_get_checked(dg_node_t* node_h) {
  NodeSlot* slot = resolve_node_slot(node_h);
  if (slot == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  if (slot->state != NodeSlot::State::kLive) {
    return DG_ERR_NO_WINDOW;
  }
  if (slot->type_id != DG_NODE_TYPE_CHECKBOX) {
    return DG_ERR_UNSUPPORTED;
  }
  AppImpl* impl = resolve_app(node_h->app);
  WindowImpl& window = *impl->windows[slot->window_index];
  return window.widgets.is_checked(slot->node_id) ? 1 : 0;
}

std::int32_t node_get_text(dg_node_t* node_h, char* buf, std::uint32_t cap,
                           std::uint32_t* out_len) {
  NodeSlot* slot = resolve_node_slot(node_h);
  if (slot == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  if (slot->state != NodeSlot::State::kLive) {
    return DG_ERR_NO_WINDOW;
  }
  if (slot->type_id != DG_NODE_TYPE_TEXT_FIELD && slot->type_id != DG_NODE_TYPE_TEXT_AREA) {
    return DG_ERR_UNSUPPORTED;
  }
  if (cap > 0 && buf == nullptr) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  AppImpl* impl = resolve_app(node_h->app);
  WindowImpl& window = *impl->windows[slot->window_index];
  const dg::TextEditState* state = window.widgets.editor_state(slot->node_id);
  static const std::string kEmpty;
  const std::string& text = state != nullptr ? state->text : kEmpty;
  if (out_len != nullptr) {
    *out_len = static_cast<std::uint32_t>(text.size());
  }
  if (cap > 0) {
    const std::size_t to_copy = std::min<std::size_t>(cap, text.size());
    std::copy_n(text.data(), to_copy, buf);
  }
  return DG_ERR_OK;
}

std::int32_t node_scope_action(dg_node_t* node_h, std::uint16_t action_id) {
  NodeSlot* slot = resolve_node_slot(node_h);
  if (slot == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  if (slot->state != NodeSlot::State::kLive) {
    return DG_ERR_NO_WINDOW;
  }
  AppImpl* impl = resolve_app(node_h->app);
  WindowImpl& window = *impl->windows[slot->window_index];
  window.action_scopes.scope(slot->node_id, static_cast<dg_action_id>(action_id));
  return DG_ERR_OK;
}

std::int32_t wait_events(dg_app_t* app, std::int32_t timeout_ms) {
  AppImpl* impl = resolve_app(app);
  if (impl == nullptr) {
    set_last_error("dg_wait_events: invalid app handle");
    return -1;
  }
  process_pump(*impl, timeout_ms);
  return static_cast<std::int32_t>(impl->pending_events.size());
}

std::int32_t poll_events(dg_app_t* app, dg_event* out, std::int32_t max) {
  AppImpl* impl = resolve_app(app);
  if (impl == nullptr) {
    set_last_error("dg_poll_events: invalid app handle");
    return -1;
  }
  if (out == nullptr || max < 0) {
    set_last_error("dg_poll_events: invalid argument");
    return -1;
  }
  const auto count =
      std::min<std::size_t>(static_cast<std::size_t>(max), impl->pending_events.size());
  for (std::size_t i = 0; i < count; ++i) {
    out[i] = impl->pending_events[i];
  }
  impl->pending_events.erase(impl->pending_events.begin(),
                             impl->pending_events.begin() + static_cast<std::ptrdiff_t>(count));
  return static_cast<std::int32_t>(count);
}

const char* last_error() {
  return last_error_storage().c_str();
}

const char* dump_layout_tree(dg_node_t* node_h) {
  NodeSlot* slot = resolve_node_slot(node_h);
  if (slot == nullptr) {
    set_last_error("dg_dump_layout_tree: invalid handle");
    return nullptr;
  }
  if (slot->state != NodeSlot::State::kLive) {
    set_last_error("dg_dump_layout_tree: node is not attached to any window");
    return nullptr;
  }
  AppImpl* impl = resolve_app(node_h->app);
  WindowImpl& window = *impl->windows[slot->window_index];

  std::string& buffer = dump_buffer_storage();
  buffer.clear();
  dump_node(window.tree, slot->node_id, buffer);
  return buffer.c_str();
}

const char* shortcut_label(std::uint16_t action_id) {
  // Fully-qualified `dg::shortcut_label` (chord.h): unqualified would resolve
  // to THIS function (dg::abi::shortcut_label), recursing into itself - the
  // wrap this function exists to perform, not a call it can make unqualified
  // from inside its own namespace.
  const std::optional<std::string> label =
      dg::shortcut_label(static_cast<dg_action_id>(action_id), dg::Platform::kLinux);
  if (!label.has_value()) {
    set_last_error("dg_shortcut_label: action_id names no binding");
    return nullptr;
  }
  std::string& buffer = shortcut_label_storage();
  buffer = *label;
  return buffer.c_str();
}

dg_theme_t* theme_load_dir(dg_app_t* app, const char* dir, dg_theme_err* err) {
  AppImpl* impl = resolve_app(app);
  if (impl == nullptr) {
    set_last_error("dg_theme_load_dir: invalid app handle");
    return nullptr;
  }
  if (dir == nullptr) {
    set_last_error("dg_theme_load_dir: dir must not be null");
    return nullptr;
  }

  dg::Expected<dg::ThemePackage, dg::ThemeLoadError> package = dg::ThemePackage::open(dir);
  if (!package) {
    fill_theme_err(err, package.error());
    return nullptr;
  }
  dg::Expected<dg::Theme, dg::ThemeLoadError> theme = package.value().load_theme_json();
  if (!theme) {
    fill_theme_err(err, theme.error());
    return nullptr;
  }

  auto owned = std::make_unique<ThemeImpl>();
  owned->theme = std::move(theme).value();
  owned->package = std::move(package).value();
  impl->themes.push_back(std::move(owned));
  impl->themes.back()->self_handle = nullptr;  // set once the handle exists, below

  all_theme_handles().emplace_back(
      dg_theme_s{app, static_cast<std::uint32_t>(impl->themes.size() - 1)});
  dg_theme_t* handle = &all_theme_handles().back();
  impl->themes.back()->self_handle = handle;
  return handle;
}

dg_theme_t* theme_load_memory(dg_app_t* app, const char* json, std::uint32_t len,
                              const char* base_dir, dg_theme_err* err) {
  AppImpl* impl = resolve_app(app);
  if (impl == nullptr) {
    set_last_error("dg_theme_load_memory: invalid app handle");
    return nullptr;
  }
  if (json == nullptr) {
    set_last_error("dg_theme_load_memory: json must not be null");
    return nullptr;
  }

  std::optional<dg::ThemePackage> package;
  if (base_dir != nullptr) {
    dg::Expected<dg::ThemePackage, dg::ThemeLoadError> opened =
        dg::ThemePackage::open(base_dir);
    if (!opened) {
      fill_theme_err(err, opened.error());
      return nullptr;
    }
    package = std::move(opened).value();
  }

  const std::string_view text(json, len);
  dg::Expected<dg::Theme, dg::ThemeLoadError> theme = dg::load_theme(text);
  if (!theme) {
    fill_theme_err(err, theme.error());
    return nullptr;
  }

  auto owned = std::make_unique<ThemeImpl>();
  owned->theme = std::move(theme).value();
  owned->package = std::move(package);
  impl->themes.push_back(std::move(owned));

  all_theme_handles().emplace_back(
      dg_theme_s{app, static_cast<std::uint32_t>(impl->themes.size() - 1)});
  dg_theme_t* handle = &all_theme_handles().back();
  impl->themes.back()->self_handle = handle;
  return handle;
}

std::int32_t theme_set_variant(dg_theme_t* theme_h, const char* variant) {
  ThemeImpl* theme = resolve_theme(theme_h);
  if (theme == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  const std::optional<dg::ThemeVariant> parsed = parse_variant(variant);
  if (!parsed.has_value()) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  theme->variant = *parsed;
  AppImpl* impl = resolve_app(theme_h->app);
  reapply_theme_if_active(*impl, *theme);
  return DG_ERR_OK;
}

std::int32_t theme_override(dg_theme_t* theme_h, std::uint16_t token_id,
                            const dg_value* value) {
  if (value == nullptr || value->size < sizeof(dg_value)) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  ThemeImpl* theme = resolve_theme(theme_h);
  if (theme == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  const std::optional<dg::TokenType> kind = dg::token_type(token_id);
  if (!kind.has_value()) {
    return DG_ERR_UNKNOWN_ID;
  }
  if (*kind == dg::TokenType::k_color) {
    if (value->type != DG_VALUE_COLOR) {
      return DG_ERR_TYPE_MISMATCH;
    }
    theme->theme.set_color(token_id, theme->variant, dg::Color::from_argb(value->bits));
  } else {
    if (value->type != DG_VALUE_FLOAT && value->type != DG_VALUE_LENGTH) {
      return DG_ERR_TYPE_MISMATCH;
    }
    theme->theme.set_int(token_id, static_cast<int>(value->number));
  }
  AppImpl* impl = resolve_app(theme_h->app);
  reapply_theme_if_active(*impl, *theme);
  return DG_ERR_OK;
}

std::int32_t app_set_theme(dg_app_t* app, dg_theme_t* theme_h) {
  AppImpl* impl = resolve_app(app);
  if (impl == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  ThemeImpl* theme = resolve_theme(theme_h);
  if (theme == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  if (theme_h->app != app) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  impl->active_theme = theme;
  reapply_theme_if_active(*impl, *theme);
  return DG_ERR_OK;
}

// -- Window operations (ABI 0.2) ------------------------------------------
//
// A WindowError from dg::WindowManager means the platform refused the
// request - argument and handle problems are caught before it is called -
// so every one maps to DG_ERR_UNSUPPORTED, with the platform's reason kept
// in dg_last_error().

namespace {

std::int32_t to_status(const char* function,
                       const dg::Expected<void, dg::WindowError>& result) {
  if (!result) {
    set_last_error(std::string(function) + ": " + result.error().message);
    return DG_ERR_UNSUPPORTED;
  }
  return DG_ERR_OK;
}

// resolve_window() plus the app that owns it, or nullptr with the error
// recorded under `function`'s name.
dg::WindowManager* resolve_window_manager(const char* function, dg_window_t* window_h,
                                          dg::WindowId& id) {
  WindowImpl* window = resolve_window(window_h);
  if (window == nullptr) {
    set_last_error(std::string(function) + ": invalid window handle");
    return nullptr;
  }
  id = window->sdl_window;
  return &resolve_app(window_h->app)->wm;
}

}  // namespace

std::int32_t window_set_title(dg_window_t* window_h, const char* utf8) {
  dg::WindowId id;
  dg::WindowManager* wm = resolve_window_manager("dg_window_set_title", window_h, id);
  if (wm == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  if (utf8 == nullptr) {
    set_last_error("dg_window_set_title: utf8 must not be null");
    return DG_ERR_INVALID_ARGUMENT;
  }
  return to_status("dg_window_set_title", wm->set_title(id, utf8));
}

std::int32_t window_get_title(dg_window_t* window_h, char* buf, std::uint32_t cap,
                              std::uint32_t* out_len) {
  dg::WindowId id;
  dg::WindowManager* wm = resolve_window_manager("dg_window_get_title", window_h, id);
  if (wm == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  if (cap > 0 && buf == nullptr) {
    set_last_error("dg_window_get_title: buf must not be null when cap > 0");
    return DG_ERR_INVALID_ARGUMENT;
  }
  const dg::Expected<std::string, dg::WindowError> title = wm->title(id);
  if (!title) {
    set_last_error("dg_window_get_title: " + title.error().message);
    return DG_ERR_UNSUPPORTED;
  }
  const std::string& text = title.value();
  if (out_len != nullptr) {
    *out_len = static_cast<std::uint32_t>(text.size());
  }
  if (cap > 0) {
    std::copy_n(text.data(), std::min<std::size_t>(cap, text.size()), buf);
  }
  return DG_ERR_OK;
}

std::int32_t window_set_size(dg_window_t* window_h, std::int32_t width, std::int32_t height) {
  dg::WindowId id;
  dg::WindowManager* wm = resolve_window_manager("dg_window_set_size", window_h, id);
  if (wm == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  if (width <= 0 || height <= 0) {
    set_last_error("dg_window_set_size: width and height must be positive");
    return DG_ERR_INVALID_ARGUMENT;
  }
  return to_status("dg_window_set_size", wm->set_size(id, width, height));
}

std::int32_t window_get_size(dg_window_t* window_h, std::int32_t* width, std::int32_t* height) {
  dg::WindowId id;
  dg::WindowManager* wm = resolve_window_manager("dg_window_get_size", window_h, id);
  if (wm == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  const dg::Expected<dg::PixelSize, dg::WindowError> size = wm->size(id);
  if (!size) {
    set_last_error("dg_window_get_size: " + size.error().message);
    return DG_ERR_UNSUPPORTED;
  }
  if (width != nullptr) {
    *width = size.value().width;
  }
  if (height != nullptr) {
    *height = size.value().height;
  }
  return DG_ERR_OK;
}

std::int32_t window_set_position(dg_window_t* window_h, std::int32_t x, std::int32_t y) {
  dg::WindowId id;
  dg::WindowManager* wm = resolve_window_manager("dg_window_set_position", window_h, id);
  if (wm == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  return to_status("dg_window_set_position", wm->set_position(id, x, y));
}

std::int32_t window_get_position(dg_window_t* window_h, std::int32_t* x, std::int32_t* y) {
  dg::WindowId id;
  dg::WindowManager* wm = resolve_window_manager("dg_window_get_position", window_h, id);
  if (wm == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  const dg::Expected<dg::PixelPoint, dg::WindowError> position = wm->position(id);
  if (!position) {
    set_last_error("dg_window_get_position: " + position.error().message);
    return DG_ERR_UNSUPPORTED;
  }
  if (x != nullptr) {
    *x = position.value().x;
  }
  if (y != nullptr) {
    *y = position.value().y;
  }
  return DG_ERR_OK;
}

std::int32_t window_minimize(dg_window_t* window_h) {
  dg::WindowId id;
  dg::WindowManager* wm = resolve_window_manager("dg_window_minimize", window_h, id);
  if (wm == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  return to_status("dg_window_minimize", wm->minimize(id));
}

std::int32_t window_maximize(dg_window_t* window_h) {
  dg::WindowId id;
  dg::WindowManager* wm = resolve_window_manager("dg_window_maximize", window_h, id);
  if (wm == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  return to_status("dg_window_maximize", wm->maximize(id));
}

std::int32_t window_restore(dg_window_t* window_h) {
  dg::WindowId id;
  dg::WindowManager* wm = resolve_window_manager("dg_window_restore", window_h, id);
  if (wm == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  return to_status("dg_window_restore", wm->restore(id));
}

std::int32_t window_set_fullscreen(dg_window_t* window_h, std::int32_t fullscreen) {
  dg::WindowId id;
  dg::WindowManager* wm = resolve_window_manager("dg_window_set_fullscreen", window_h, id);
  if (wm == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  return to_status("dg_window_set_fullscreen", wm->set_fullscreen(id, fullscreen != 0));
}

std::int32_t window_set_resizable(dg_window_t* window_h, std::int32_t resizable) {
  dg::WindowId id;
  dg::WindowManager* wm = resolve_window_manager("dg_window_set_resizable", window_h, id);
  if (wm == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  return to_status("dg_window_set_resizable", wm->set_resizable(id, resizable != 0));
}

std::int32_t window_set_min_size(dg_window_t* window_h, std::int32_t width,
                                 std::int32_t height) {
  dg::WindowId id;
  dg::WindowManager* wm = resolve_window_manager("dg_window_set_min_size", window_h, id);
  if (wm == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  return to_status("dg_window_set_min_size", wm->set_min_size(id, width, height));
}

std::int32_t window_set_max_size(dg_window_t* window_h, std::int32_t width,
                                 std::int32_t height) {
  dg::WindowId id;
  dg::WindowManager* wm = resolve_window_manager("dg_window_set_max_size", window_h, id);
  if (wm == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  return to_status("dg_window_set_max_size", wm->set_max_size(id, width, height));
}

std::int32_t window_set_icon(dg_window_t* window_h, const void* pixels, std::int32_t width,
                             std::int32_t height, std::uint32_t row_bytes,
                             std::uint32_t format) {
  dg::WindowId id;
  dg::WindowManager* wm = resolve_window_manager("dg_window_set_icon", window_h, id);
  if (wm == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  if (pixels == nullptr || width <= 0 || height <= 0 ||
      row_bytes < static_cast<std::uint64_t>(width) * 4U) {
    set_last_error(
        "dg_window_set_icon: pixels must be non-null, the size positive and "
        "row_bytes >= width * 4");
    return DG_ERR_INVALID_ARGUMENT;
  }
  dg::PixelFormat pixel_format = dg::PixelFormat::kBgra8888;
  if (format == DG_PIXEL_FORMAT_RGBA8888) {
    pixel_format = dg::PixelFormat::kRgba8888;
  } else if (format != DG_PIXEL_FORMAT_BGRA8888) {
    set_last_error("dg_window_set_icon: format must be a DG_PIXEL_FORMAT_* value");
    return DG_ERR_INVALID_ARGUMENT;
  }
  const dg::ImageView icon{static_cast<const std::uint8_t*>(pixels), width, height, row_bytes,
                           pixel_format};
  return to_status("dg_window_set_icon", wm->set_icon(id, icon));
}

std::int32_t window_get_state(dg_window_t* window_h) {
  dg::WindowId id;
  dg::WindowManager* wm = resolve_window_manager("dg_window_get_state", window_h, id);
  if (wm == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  const dg::Expected<dg::WindowState, dg::WindowError> state = wm->state(id);
  if (!state) {
    set_last_error("dg_window_get_state: " + state.error().message);
    return DG_ERR_UNSUPPORTED;
  }
  std::int32_t bits = 0;
  bits |= state.value().minimized ? DG_WINDOW_STATE_MINIMIZED : 0;
  bits |= state.value().maximized ? DG_WINDOW_STATE_MAXIMIZED : 0;
  bits |= state.value().fullscreen ? DG_WINDOW_STATE_FULLSCREEN : 0;
  bits |= state.value().focused ? DG_WINDOW_STATE_FOCUSED : 0;
  bits |= state.value().resizable ? DG_WINDOW_STATE_RESIZABLE : 0;
  return bits;
}

std::int32_t debug_warp_pointer(dg_window_t* window_h, std::int32_t x, std::int32_t y) {
  WindowImpl* window = resolve_window(window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  AppImpl* impl = resolve_app(window_h->app);
  impl->wm.warp_pointer(window->sdl_window, x, y);
  return DG_ERR_OK;
}

std::int32_t debug_get_cursor(dg_window_t* window_h) {
  WindowImpl* window = resolve_window(window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  AppImpl* impl = resolve_app(window_h->app);
  const dg::Expected<dg::Cursor, dg::WindowError> cursor =
      impl->wm.current_cursor(window->sdl_window);
  if (!cursor) {
    return DG_ERR_INVALID_HANDLE;
  }
  return static_cast<std::int32_t>(cursor.value());
}

std::int32_t debug_post_pointer_button(dg_window_t* window_h, std::int32_t down, std::int32_t x,
                                       std::int32_t y) {
  WindowImpl* window = resolve_window(window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  AppImpl* impl = resolve_app(window_h->app);
  impl->wm.post_pointer_button(window->sdl_window, down != 0, x, y);
  return DG_ERR_OK;
}

std::int32_t debug_post_key(dg_window_t* window_h, std::int32_t down,
                            const char* logical_key_name) {
  WindowImpl* window = resolve_window(window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  if (logical_key_name == nullptr) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  // dg::parse_chord() is the ONE existing parser for this grammar
  // (tests/unit/test_shortcuts.cpp already proves it correct) - reused
  // rather than a second, hand-rolled name->LogicalKey lookup written
  // just for this debug hook. A bare key name ("PageUp") parses to
  // Chord{mods=kNone, key=...}; anything carrying a modifier is refused
  // below, matching WindowManager::post_logical_key()'s own inability to
  // inject one (see this function's def.toml summary).
  const std::optional<dg::Chord> chord = dg::parse_chord(logical_key_name);
  if (!chord.has_value()) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  AppImpl* impl = resolve_app(window_h->app);
  impl->wm.post_logical_key(window->sdl_window, down != 0, chord->key, chord->mods);
  return DG_ERR_OK;
}

std::int32_t debug_trigger_exception(std::int32_t kind) {
  // The exception-boundary test's own fault injector (design.md section
  // 5.17.1's risk-register mitigation). Three distinct throw shapes so the
  // test can verify all three catch clauses the generator emits: bad_alloc
  // -> DG_ERR_OOM, any other std::exception -> DG_ERR_INTERNAL with a real
  // message, and a value nothing but catch(...) can see.
  if (kind == 1) {
    throw std::bad_alloc();
  }
  if (kind == 2) {
    throw std::runtime_error(
        "dg_debug_trigger_exception: deliberate fault for the exception-boundary test");
  }
  throw 42;  // Not a std::exception at all - only catch(...) can see this one.
}

const char* debug_capture_png(dg_window_t* window_h) {
  WindowImpl* window = resolve_window(window_h);
  if (window == nullptr) {
    set_last_error("dg_debug_capture_png: invalid window handle");
    return nullptr;
  }
  if (!window->surface.has_value()) {
    set_last_error(
        "dg_debug_capture_png: this window has never painted a frame - call "
        "dg_wait_events()/dg_poll_events() at least once first");
    return nullptr;
  }

  const std::vector<std::uint8_t> png = window->surface->encode_png();
  if (png.empty()) {
    set_last_error("dg_debug_capture_png: PNG encoding failed");
    return nullptr;
  }

  std::string& buffer = capture_png_storage();
  buffer.assign(reinterpret_cast<const char*>(png.data()), png.size());
  return buffer.data();
}

std::uint32_t debug_capture_png_len() {
  return static_cast<std::uint32_t>(capture_png_storage().size());
}

std::int32_t debug_post_text_input(dg_window_t* window_h, const char* utf8) {
  WindowImpl* window = resolve_window(window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  if (utf8 == nullptr) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  AppImpl* impl = resolve_app(window_h->app);
  impl->wm.post_text_input(window->sdl_window, std::string(utf8));
  return DG_ERR_OK;
}

std::int32_t debug_post_text_field_key(dg_window_t* window_h, std::int32_t down,
                                       const char* key_name, std::int32_t shift) {
  WindowImpl* window = resolve_window(window_h);
  if (window == nullptr) {
    return DG_ERR_INVALID_HANDLE;
  }
  if (key_name == nullptr) {
    return DG_ERR_INVALID_ARGUMENT;
  }
  // Not dg::parse_chord(): none of these six is a shortcut binding name
  // (input/shortcuts.toml never binds one) - see this function's own
  // def.toml summary for why dg_debug_post_key() cannot be reused here.
  const std::string_view name(key_name);
  dg::Key key = dg::Key::kOther;
  if (name == "Left") {
    key = dg::Key::kLeft;
  } else if (name == "Right") {
    key = dg::Key::kRight;
  } else if (name == "Home") {
    key = dg::Key::kHome;
  } else if (name == "End") {
    key = dg::Key::kEnd;
  } else if (name == "Backspace") {
    key = dg::Key::kBackspace;
  } else if (name == "Delete") {
    key = dg::Key::kDelete;
  } else if (name == "Up") {
    key = dg::Key::kUp;
  } else if (name == "Down") {
    key = dg::Key::kDown;
  } else if (name == "Enter") {
    key = dg::Key::kEnter;
  } else if (name == "PageUp") {
    key = dg::Key::kPageUp;
  } else if (name == "PageDown") {
    key = dg::Key::kPageDown;
  } else {
    return DG_ERR_INVALID_ARGUMENT;
  }
  AppImpl* impl = resolve_app(window_h->app);
  impl->wm.post_key(window->sdl_window, down != 0, key, shift != 0);
  return DG_ERR_OK;
}

}  // namespace dg::abi
