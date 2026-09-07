// The SDL3 backing for dg::WindowManager: several real OS windows at once,
// and the event routing that keeps them independent of one another.
//
// This file was written before the public header was, and the header says
// only what this file turned out to need. That order is the point - the
// previous attempt at a platform layer wrote twelve abstract headers against
// design.md and never ran any of them, so nothing ever disproved a field.
//
// There is no abstract base here and no vtable. A second backend is not
// anticipated in code; when one is written it will be measured first, and
// whatever the two genuinely share will be extracted then.
//
// Deliberately no renderer and no GL context. Painting goes through the
// window's own surface (SDL_GetWindowSurface / SDL_FillSurfaceRect /
// SDL_UpdateWindowSurface), which is the smallest thing that can put a colour
// on screen. How Skia attaches, and whether it attaches on CPU or GPU, is the
// next step's decision; creating a renderer here would quietly pre-empt it.

#include "drawgui/window/window_manager.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dg {
namespace {

// One window and everything that has to die with it.
//
// The SDL_WindowID is stored rather than derived from the pointer on demand,
// because it is the only handle that stays meaningful once SDL_DestroyWindow
// has run: an event already sitting in the queue still names the id, and this
// entry is gone by then. Looking the id up and finding nothing is how a stale
// event is recognised.
struct OwnedWindow {
  SDL_WindowID sdl_id = 0;
  SDL_Window* window = nullptr;
  Color fill;
};

// SDL reports failure as false or nullptr and leaves the reason in
// SDL_GetError(). Losing that string is what turns "window creation failed"
// into an afternoon, so it is carried out to the caller verbatim.
std::string sdl_failure(std::string_view call) {
  std::string message{call};
  message += " failed";
  const char* detail = SDL_GetError();
  if (detail != nullptr && *detail != '\0') {
    message += ": ";
    message += detail;
  }
  return message;
}

// Best effort, and no error to report: the only way this fails in practice is
// a window that is being destroyed or has no surface yet, and in both cases
// the next exposure repaints it.
void paint(const OwnedWindow& owned) {
  SDL_Surface* surface = SDL_GetWindowSurface(owned.window);
  if (surface == nullptr) {
    return;
  }
  const Uint32 colour =
      SDL_MapSurfaceRGB(surface, owned.fill.red(), owned.fill.green(), owned.fill.blue());
  if (SDL_FillSurfaceRect(surface, nullptr, colour)) {
    SDL_UpdateWindowSurface(owned.window);
  }
}

}  // namespace

// The whole of the manager's state. Its destructor is the only place windows
// and the video subsystem are released, so a moved-from WindowManager - whose
// unique_ptr is null - releases nothing, and the moved-to one releases
// everything exactly once.
struct WindowManager::Impl {
  Impl() = default;
  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;
  Impl(Impl&&) = delete;
  Impl& operator=(Impl&&) = delete;

  ~Impl() {
    for (const OwnedWindow& owned : windows) {
      SDL_DestroyWindow(owned.window);
    }
    SDL_Quit();
  }

  [[nodiscard]] std::vector<OwnedWindow>::iterator find(SDL_WindowID sdl_id) {
    return std::find_if(windows.begin(), windows.end(),
                        [sdl_id](const OwnedWindow& owned) { return owned.sdl_id == sdl_id; });
  }

  // Destroys one window and forgets it. Every other window is untouched -
  // this is the operation the whole file exists to get right.
  void close(std::vector<OwnedWindow>::iterator entry) {
    SDL_DestroyWindow(entry->window);
    windows.erase(entry);
  }

  // Handles one event and appends to `closed` if it ended a window's life.
  void dispatch(const SDL_Event& event, std::vector<WindowId>& closed) {
    switch (event.type) {
      case SDL_EVENT_WINDOW_CLOSE_REQUESTED: {
        const SDL_WindowID sdl_id = event.window.windowID;
        const auto entry = find(sdl_id);
        // Not an error. SDL keeps delivering events that were queued before
        // the window was destroyed, and request_close() is allowed to be
        // called twice; both arrive here as an id nobody owns.
        if (entry != windows.end()) {
          close(entry);
          closed.push_back(WindowId{sdl_id});
        }
        break;
      }
      case SDL_EVENT_WINDOW_EXPOSED: {
        const auto entry = find(event.window.windowID);
        if (entry != windows.end()) {
          paint(*entry);
        }
        break;
      }
      default:
        // SDL_EVENT_QUIT lands here on purpose. SDL raises it when the last
        // window goes away, which the caller already knows from
        // open_window_count(); acting on it would be a second, redundant way
        // to end the same loop.
        break;
    }
  }

  std::vector<OwnedWindow> windows;
};

WindowManager::WindowManager(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

WindowManager::WindowManager(WindowManager&&) noexcept = default;
WindowManager& WindowManager::operator=(WindowManager&&) noexcept = default;

// Out of line because Impl is incomplete in the header.
WindowManager::~WindowManager() = default;

Expected<WindowManager, WindowError> WindowManager::create() {
  // SDL3 returns bool, true on success - not SDL2's 0-on-success int. Reading
  // this as SDL2 gives a manager that reports failure on every good start.
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    return Unexpected{WindowError{sdl_failure("SDL_Init(SDL_INIT_VIDEO)")}};
  }
  return WindowManager{std::make_unique<Impl>()};
}

Expected<WindowId, WindowError> WindowManager::open(const WindowSpec& spec) {
  // SDL3's SDL_CreateWindow takes no x/y - placement belongs to the window
  // manager. Distinguishing one window from another is therefore the caller's
  // job, through the title and the fill colour it chooses.
  SDL_Window* window = SDL_CreateWindow(spec.title.c_str(), spec.width, spec.height, 0);
  if (window == nullptr) {
    return Unexpected{WindowError{sdl_failure("SDL_CreateWindow")}};
  }

  const SDL_WindowID sdl_id = SDL_GetWindowID(window);
  if (sdl_id == 0) {
    SDL_DestroyWindow(window);
    return Unexpected{WindowError{sdl_failure("SDL_GetWindowID")}};
  }

  impl_->windows.push_back(OwnedWindow{sdl_id, window, spec.fill});
  paint(impl_->windows.back());
  return WindowId{sdl_id};
}

std::size_t WindowManager::open_window_count() const {
  return impl_->windows.size();
}

void WindowManager::request_close(WindowId id) {
  if (impl_->find(id.value) == impl_->windows.end()) {
    return;
  }

  // A real SDL_EVENT_WINDOW_CLOSE_REQUESTED on the real queue, which is the
  // same thing the window manager's close button produces. A scripted close
  // that called close() directly would exercise a path no user can reach, and
  // would prove nothing about the one they can.
  SDL_Event event{};
  event.window.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
  event.window.windowID = id.value;
  SDL_PushEvent(&event);
}

std::vector<WindowId> WindowManager::pump(int timeout_ms) {
  std::vector<WindowId> closed;

  // One wait for the first event, then drain whatever else is queued without
  // waiting again. Waiting per event instead would spend a full timeout on
  // each one and turn a burst into a visible stall.
  SDL_Event event;
  bool have_event = SDL_WaitEventTimeout(&event, timeout_ms);
  while (have_event) {
    impl_->dispatch(event, closed);
    have_event = SDL_PollEvent(&event);
  }

  return closed;
}

}  // namespace dg
