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
// window's own surface (SDL_GetWindowSurface / SDL_UpdateWindowSurfaceRects),
// which is the smallest thing that can put pixels on screen.
// SDL_CreateRenderer would pick an OpenGL backend on Linux and thereby decide,
// silently, how Skia attaches.
//
// present() is the whole of the Skia integration on this side, and it knows
// nothing about Skia: it takes an address, a shape, a stride and a channel
// order. The graphics layer owns the rasterizer and hands over finished
// bytes. That is the same direction the ownership ran in the deleted
// FrameTarget design, without the abstract base class it did not need.

#include "drawgui/window/window_manager.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dg {
namespace {

constexpr std::size_t kBytesPerPixel = 4;

// SDL names a pixel format by the layout of the packed 32-bit word, so
// XRGB8888 means blue in the lowest-addressed byte only on a little-endian
// machine. Every platform in design.md section 3.2 is little-endian, and
// present() copies bytes rather than words, so the assumption is asserted at
// compile time instead of being handled: a big-endian port would otherwise
// swap red and blue in every frame and pass every test that does not look at
// a pixel.
static_assert(std::endian::native == std::endian::little,
              "present() copies BGRA bytes straight into an SDL XRGB8888 surface, which is "
              "only the same layout on a little-endian machine");

bool is_bgra8888(SDL_PixelFormat format) {
  return format == SDL_PIXELFORMAT_XRGB8888 || format == SDL_PIXELFORMAT_ARGB8888;
}

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

  // Cleared by the first present(). Until then the window shows its flat
  // WindowSpec::fill and repaints itself on exposure; afterwards the owner's
  // pixels are the truth and repainting over them with a colour would erase
  // the frame it just drew.
  bool shows_fill = true;
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
  if (!owned.shows_fill) {
    return;
  }
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

// Intersection of a caller's dirty region with the window it is being copied
// into. Done here rather than trusted, because the caller's idea of the
// window size is one pump() out of date whenever a resize is in flight, and
// an unclipped copy of a stale rectangle is a heap overflow.
PixelRect clip_to(const PixelRect& dirty, int width, int height) {
  return intersect(dirty, PixelRect{0, 0, width, height});
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

  // Handles one event and records what the caller now has to do about it.
  void dispatch(const SDL_Event& event, PumpResult& result) {
    switch (event.type) {
      case SDL_EVENT_WINDOW_CLOSE_REQUESTED: {
        const SDL_WindowID sdl_id = event.window.windowID;
        const auto entry = find(sdl_id);
        // Not an error. SDL keeps delivering events that were queued before
        // the window was destroyed, and request_close() is allowed to be
        // called twice; both arrive here as an id nobody owns.
        if (entry != windows.end()) {
          close(entry);
          result.closed.push_back(WindowId{sdl_id});
        }
        break;
      }
      case SDL_EVENT_WINDOW_EXPOSED:
      case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
        const SDL_WindowID sdl_id = event.window.windowID;
        const auto entry = find(sdl_id);
        if (entry != windows.end()) {
          paint(*entry);
          result.needs_repaint.push_back(WindowId{sdl_id});
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

PumpResult WindowManager::pump(int timeout_ms) {
  PumpResult result;

  // One wait for the first event, then drain whatever else is queued without
  // waiting again. Waiting per event instead would spend a full timeout on
  // each one and turn a burst into a visible stall.
  SDL_Event event;
  bool have_event = SDL_WaitEventTimeout(&event, timeout_ms);
  while (have_event) {
    impl_->dispatch(event, result);
    have_event = SDL_PollEvent(&event);
  }

  return result;
}

Expected<PixelSize, WindowError> WindowManager::drawable_size(WindowId id) const {
  const auto entry = impl_->find(id.value);
  if (entry == impl_->windows.end()) {
    return Unexpected{WindowError{"no such window"}};
  }

  // The surface's own dimensions rather than SDL_GetWindowSizeInPixels: the
  // two disagree for the one frame between a resize being reported and the
  // surface being rebuilt, and the surface is the thing about to be written
  // into.
  SDL_Surface* surface = SDL_GetWindowSurface(entry->window);
  if (surface == nullptr) {
    return Unexpected{WindowError{sdl_failure("SDL_GetWindowSurface")}};
  }
  return PixelSize{surface->w, surface->h};
}

Expected<PixelFormat, WindowError> WindowManager::surface_format(WindowId id) const {
  const auto entry = impl_->find(id.value);
  if (entry == impl_->windows.end()) {
    return Unexpected{WindowError{"no such window"}};
  }

  SDL_Surface* surface = SDL_GetWindowSurface(entry->window);
  if (surface == nullptr) {
    return Unexpected{WindowError{sdl_failure("SDL_GetWindowSurface")}};
  }
  if (!is_bgra8888(surface->format)) {
    std::string message{"window surface is "};
    message += SDL_GetPixelFormatName(surface->format);
    message += ", which drawgui cannot describe";
    return Unexpected{WindowError{std::move(message)}};
  }
  return PixelFormat::kBgra8888;
}

Expected<void, WindowError> WindowManager::present(WindowId id, const ImageView& image,
                                                   const PixelRect& dirty) {
  const auto entry = impl_->find(id.value);
  if (entry == impl_->windows.end()) {
    return Unexpected{WindowError{"no such window"}};
  }
  if (image.pixels == nullptr) {
    return Unexpected{WindowError{"present() was given no pixels"}};
  }

  SDL_Surface* surface = SDL_GetWindowSurface(entry->window);
  if (surface == nullptr) {
    return Unexpected{WindowError{sdl_failure("SDL_GetWindowSurface")}};
  }
  if (!is_bgra8888(surface->format)) {
    return Unexpected{WindowError{"window surface is not a format present() can copy into"}};
  }

  // Clipped against the image as well as the window. A frame rasterized
  // before the last resize is smaller than the window it is being shown in,
  // and copying window-sized rows out of it would read past the end.
  const PixelRect region =
      clip_to(clip_to(dirty, image.width, image.height), surface->w, surface->h);
  if (region.width <= 0 || region.height <= 0) {
    return {};
  }

  auto* destination = static_cast<std::uint8_t*>(surface->pixels);
  if (destination == nullptr) {
    return Unexpected{WindowError{"window surface has no pixels"}};
  }

  const auto row_span = static_cast<std::size_t>(region.width) * kBytesPerPixel;
  const auto x_offset = static_cast<std::size_t>(region.x) * kBytesPerPixel;
  const auto destination_pitch = static_cast<std::size_t>(surface->pitch);

  for (int row = 0; row < region.height; ++row) {
    const std::size_t source_row = static_cast<std::size_t>(region.y + row) * image.row_bytes;
    const std::size_t destination_row =
        static_cast<std::size_t>(region.y + row) * destination_pitch;
    std::memcpy(destination + destination_row + x_offset, image.pixels + source_row + x_offset,
                row_span);
  }

  entry->shows_fill = false;

  const SDL_Rect updated{region.x, region.y, region.width, region.height};
  if (!SDL_UpdateWindowSurfaceRects(entry->window, &updated, 1)) {
    return Unexpected{WindowError{sdl_failure("SDL_UpdateWindowSurfaceRects")}};
  }
  return {};
}

}  // namespace dg
