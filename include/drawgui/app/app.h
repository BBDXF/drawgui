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

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "drawgui/anim/clock.h"
#include "drawgui/app/events.h"
#include "drawgui/app/status.h"
#include "drawgui/base/expected.h"
#include "drawgui/graphics/types.h"
#include "drawgui/render/image_catalog.h"
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

// ActionEvent::action_id values from here up are the app's own
// (App::add_shortcut()/Window::add_shortcut()); below it are
// input/shortcuts.toml's built-in DG_ACTION_* ids.
inline constexpr std::uint16_t kUserActionBase = 0x8000;

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
  // Window::set_close_confirmation() from the start.
  bool close_confirmation = false;
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
  // Closes the OS window (if the user has not already) and frees the
  // Window: `window`, every ref into it and every escape-hatch reference
  // dangle afterwards. Events already queued keep naming its WindowId, which
  // find() no longer resolves. Safe on a window the user closed; a pointer
  // this App does not own is ignored. Never call it from inside a callback
  // the runtime is running on that window's behalf.
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
  // pump(-1) in a loop, handing every event to `on_event`, until every
  // window is closed (or destroyed) or quit() is called. Returns at once
  // when there is no open window. `on_event` may create, destroy and edit
  // windows; a window the user closes stays in windows() (closed()) until
  // destroy_window().
  void run(const std::function<void(const Event&)>& on_event);
  // Makes run() return after the event it is delivering.
  void quit();

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

  // -- Animation ---------------------------------------------------------
  // Reduced motion (the user's accessibility preference, read by the host):
  // every animation and transition of every window, current and future,
  // takes zero time - it lands on its target at the next pump().
  void set_reduced_motion(bool enabled);
  [[nodiscard]] bool reduced_motion() const;
  // The time animations advance by; null (the default) = steady_anim_time().
  // A test seam: a fake clock makes animation frames deterministic. It must
  // not run behind a time already used (start it at steady_anim_time()).
  void set_clock(std::function<AnimTime()> clock);

  // TimerId{0} when the platform refused.
  [[nodiscard]] TimerId add_timer(int delay_ms, bool repeat);
  bool cancel_timer(TimerId id);
  [[nodiscard]] Waker waker() const;

  // -- Shortcuts ---------------------------------------------------------
  // Binds `chord` (e.g. "Ctrl+Shift+K", the input/shortcuts.toml syntax) to
  // `action_id` (>= kUserActionBase) in every window (router level 4); it
  // reports ActionEvent{node = none}. Rebinding a chord replaces it.
  // kInvalidArgument for a chord that does not parse or an id below
  // kUserActionBase.
  Status add_shortcut(std::string_view chord, std::uint16_t action_id);
  // Drops an add_shortcut() binding (and every Window::add_shortcut() one)
  // of `chord`; kUnknownId when none.
  Status remove_shortcut(std::string_view chord);
  // The platform label of `chord` (e.g. "Ctrl+K", or "⌘K" on macOS) for
  // MenuEntry::shortcut_label. kInvalidArgument when it does not parse.
  [[nodiscard]] Expected<std::string, Status> chord_label(std::string_view chord) const;

  // -- Images ------------------------------------------------------------
  // Decodes a PNG/JPEG/WebP/SVG (sniffed; `svg_tint` recolours an SVG) once
  // into images() and returns its id, for Window::set_image(),
  // RowItem::icon, TreeEntry::icon and TableCell::icon.
  [[nodiscard]] Expected<ImageId, AppError> load_image(const std::uint8_t* bytes,
                                                       std::size_t size,
                                                       std::optional<Color> svg_tint = {});
  [[nodiscard]] Expected<ImageId, AppError> load_image_file(const std::string& path,
                                                            std::optional<Color> svg_tint = {});

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
