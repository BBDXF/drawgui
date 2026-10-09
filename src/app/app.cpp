// dg::App: the window manager, the shared fonts/images, and window creation.

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "app/components/components.h"
#include "app/internal.h"

namespace dg {

using app_detail::AppState;
using app_detail::WinState;

bool Waker::wake() const {
  return manager_ != nullptr && manager_->wake();
}

bool Waker::post_user_event(std::uint64_t payload) const {
  return manager_ != nullptr && manager_->post_user_event(payload);
}

App::App(std::unique_ptr<AppState> state) : state_(std::move(state)) {}
App::App(App&&) noexcept = default;
App& App::operator=(App&&) noexcept = default;
App::~App() = default;

Expected<App, AppError> App::create(const AppOptions& /*options*/) {
  Expected<WindowManager, WindowError> created = WindowManager::create();
  if (!created) {
    return Unexpected(AppError{Status::kUnsupported, created.error().message});
  }
  return App(std::make_unique<AppState>(std::move(created).value()));
}

Expected<Window*, AppError> App::create_window(const WindowOptions& options) {
  AppState& app = *state_;
  WindowSpec spec;
  spec.title = options.title;
  spec.width = options.width;
  spec.height = options.height;
  spec.fill = options.background;
  spec.resizable = options.resizable;
  spec.min_width = options.min_width;
  spec.min_height = options.min_height;
  spec.max_width = options.max_width;
  spec.max_height = options.max_height;

  Expected<WindowId, WindowError> opened = app.wm.open(spec);
  if (!opened) {
    return Unexpected(AppError{Status::kUnsupported, opened.error().message});
  }

  TreeSpec tree_spec;
  tree_spec.viewport = PixelSize{options.width, options.height};
  tree_spec.background.fill = options.background;
  // add_font()'s catalog, copied (cheap - a shared, reference-counted
  // table, per FontCatalog's own header comment) into THIS window's own
  // TreeSpec. A window opened before any add_font() call gets
  // std::nullopt here, matching render_tree.h's "absent means no node in
  // this tree draws text".
  tree_spec.fonts = app.fonts;
  tree_spec.images = app.images;

  auto state = std::make_unique<WinState>(app, tree_spec);
  state->sdl_window = opened.value();
  state->widgets.set_disabled_palette(app_detail::disabled_palette_for(app));
  state->widgets.set_scrollbar_style(app_detail::scrollbar_style_for(app));
  // P6 sub-step a2 (second half): enabled for the window's whole lifetime,
  // never toggled per focus change - the identical choice
  // examples/12_text_input/text_field_window.cpp's own Runner::
  // resize_if_needed() makes and explains: SDL generates no
  // SDL_EVENT_TEXT_INPUT at all until this has been called on the window
  // (window_manager.h's own start_text_input() doc comment), but it is
  // the runtime's OWN dg::Focus - not SDL's IME capture state - that decides
  // which node (if any) a committed character actually reaches
  // (process_pump()'s pumped.text_input loop only ever delivers to
  // window.focus.current(), and only when it names an editor).
  app.wm.start_text_input(state->sdl_window, PixelRect{0, 0, 1, 1});
  app_detail::components::init_window(*state);
  app.windows.push_back(std::make_unique<Window>(std::move(state)));
  return app.windows.back().get();
}

void App::destroy_window(Window* window) {
  AppState& app = *state_;
  const auto found = std::find_if(
      app.windows.begin(), app.windows.end(),
      [window](const std::unique_ptr<Window>& owned) { return owned.get() == window; });
  if (found == app.windows.end()) {
    return;
  }
  WinState& state = (*found)->impl();
  if (!state.closed) {
    app.wm.close_now(state.sdl_window);
  }
  app.windows.erase(found);
}

std::vector<Window*> App::windows() const {
  std::vector<Window*> out;
  out.reserve(state_->windows.size());
  for (const std::unique_ptr<Window>& window : state_->windows) {
    out.push_back(window.get());
  }
  return out;
}

Window* App::find(WindowId id) const {
  for (const std::unique_ptr<Window>& window : state_->windows) {
    if (!window->closed() && window->id() == id) {
      return window.get();
    }
  }
  return nullptr;
}

Expected<void, AppError> App::add_font(const std::string& directory,
                                       const std::string& family) {
  AppState& app = *state_;
  // The scan happens once per app: a second call reuses the SAME catalog
  // (FontCatalog is a handle to one reference-counted table, per its own
  // header comment) rather than re-scanning `directory` a second time -
  // this `directory` argument is therefore ignored on every call after the
  // first.
  if (!app.fonts.has_value()) {
    Expected<FontCatalog, FontError> scanned = FontCatalog::scan(directory);
    if (!scanned) {
      return Unexpected(AppError{Status::kInvalidArgument, scanned.error().message});
    }
    app.fonts = std::move(scanned).value();
  }

  const Expected<FontId, FontError> added = app.fonts->add(family, false);
  if (!added) {
    return Unexpected(AppError{Status::kInvalidArgument, added.error().message});
  }
  if (!app.default_font.is_valid()) {
    app.default_font = added.value();
  }
  return {};
}

TimerId App::add_timer(int delay_ms, bool repeat) {
  return state_->wm.add_timer(delay_ms, repeat);
}

bool App::cancel_timer(TimerId id) {
  return state_->wm.cancel_timer(id);
}

Waker App::waker() const {
  return Waker(&state_->wm);
}

WindowManager& App::window_manager() {
  return state_->wm;
}

FontCatalog* App::fonts() {
  return state_->fonts.has_value() ? &*state_->fonts : nullptr;
}

ImageCatalog& App::images() {
  return state_->images;
}

}  // namespace dg
