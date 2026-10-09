// dg::App - the C++ runtime the C ABI (src/abi) is a thin shell over.
//
// One App owns one WindowManager, the fonts/images every one of its windows
// shares, the active theme, and the queue of dg::Event its windows produce.
// It paints and presents every window itself: a host creates windows and
// nodes, then calls pump() in a loop and reacts to the events it returns.
//
// Single-threaded, like WindowManager, except for waker(), whose two calls
// may come from any thread.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "drawgui/app/events.h"
#include "drawgui/app/status.h"
#include "drawgui/base/expected.h"
#include "drawgui/graphics/types.h"
#include "drawgui/theme/theme.h"
#include "drawgui/window/window_id.h"

namespace dg {

class FontCatalog;
class ImageCatalog;
class Window;
class WindowManager;

namespace app_detail {
struct AppState;
}  // namespace app_detail

struct AppOptions {};

struct WindowOptions {
  std::string title;
  int width = 0;
  int height = 0;
  Color background;
  bool resizable = false;
  int min_width = 0;
  int min_height = 0;
  int max_width = 0;
  int max_height = 0;
};

// Wakes App::pump() from any thread. Valid only while the App that handed
// it out is alive - it points at that App's WindowManager.
class Waker {
 public:
  bool wake() const;
  bool post_user_event(std::uint64_t payload) const;

 private:
  friend class App;
  explicit Waker(WindowManager* manager) : manager_(manager) {}
  WindowManager* manager_ = nullptr;
};

class App {
 public:
  [[nodiscard]] static Expected<App, AppError> create(const AppOptions& options = {});

  App(App&&) noexcept;
  App& operator=(App&&) noexcept;
  App(const App&) = delete;
  App& operator=(const App&) = delete;
  ~App();

  // -- Windows -----------------------------------------------------------
  [[nodiscard]] Expected<Window*, AppError> create_window(const WindowOptions& options);
  // Closes the OS window and frees the Window; `window` dangles afterwards.
  void destroy_window(Window* window);
  // Every window ever created and not destroyed, in creation order -
  // including ones the user closed (Window::closed()).
  [[nodiscard]] std::vector<Window*> windows() const;
  // The open window `id` names, or null.
  [[nodiscard]] Window* find(WindowId id) const;

  // -- The loop ----------------------------------------------------------
  // Waits up to `timeout_ms` (-1 = forever, cut short by tooltips and
  // animations), dispatches everything the platform reported, repaints, and
  // returns every event queued since the last pump()/poll().
  [[nodiscard]] std::vector<Event> pump(int timeout_ms);
  // The events queued since the last pump()/poll() - by calls such as
  // Window::focus() - without pumping.
  [[nodiscard]] std::vector<Event> poll();

  // -- Shared resources --------------------------------------------------
  // The first call scans `directory`; every call adds `family`. The first
  // family added is the default font of every label/editor/list.
  [[nodiscard]] Expected<void, AppError> add_font(const std::string& directory,
                                                  const std::string& family);

  // Makes `theme` (in `variant`) the active theme and re-applies it to every
  // open window. Calling it again with the same pointer after mutating the
  // theme re-applies the change.
  void set_theme(std::shared_ptr<const Theme> theme, ThemeVariant variant);
  // kNoActiveTheme when no theme is active.
  Status set_theme_variant(ThemeVariant variant);
  // Copy-on-write override of one token of the active theme.
  Status override_color(std::uint16_t token_id, Color value);
  Status override_int(std::uint16_t token_id, int value);
  [[nodiscard]] bool has_theme() const;

  // 0..60000 ms; kValueOutOfRange otherwise.
  Status set_tooltip_delay(int delay_ms);

  // TimerId{0} when the platform refused.
  [[nodiscard]] TimerId add_timer(int delay_ms, bool repeat);
  bool cancel_timer(TimerId id);
  [[nodiscard]] Waker waker() const;

  // -- Escape hatches ----------------------------------------------------
  [[nodiscard]] WindowManager& window_manager();
  // Null until the first successful add_font().
  [[nodiscard]] FontCatalog* fonts();
  [[nodiscard]] ImageCatalog& images();

 private:
  explicit App(std::unique_ptr<app_detail::AppState> state);
  std::unique_ptr<app_detail::AppState> state_;
};

}  // namespace dg
