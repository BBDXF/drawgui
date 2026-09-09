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
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
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

// SDL reports pointer positions in LOGICAL window coordinates and this library
// speaks physical framebuffer pixels everywhere else. The two differ by the
// window's pixel density on a scaled display, and a pointer that is off by
// that factor lands near widgets rather than on them - a defect that is
// invisible at density 1, which is every developer machine that has not been
// tested on a second monitor.
//
// Floored rather than rounded: a coordinate names the pixel it is inside, and
// rounding would make the upper half of the last row belong to the row after
// it, which does not exist.
PixelPoint to_physical(SDL_Window* window, float x, float y) {
  const float density = SDL_GetWindowPixelDensity(window);
  const float scale = density > 0.0F ? density : 1.0F;
  return PixelPoint{static_cast<int>(std::floor(x * scale)),
                    static_cast<int>(std::floor(y * scale))};
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
      case SDL_EVENT_MOUSE_MOTION: {
        const auto entry = find(event.motion.windowID);
        if (entry != windows.end()) {
          const PixelPoint at = to_physical(entry->window, event.motion.x, event.motion.y);
          result.pointer.push_back(
              PointerEvent{WindowId{entry->sdl_id}, PointerAction::kMove, at.x, at.y});
        }
        break;
      }
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
      case SDL_EVENT_MOUSE_BUTTON_UP: {
        // Every other button is dropped here rather than carried and ignored
        // upstream. Nothing routes a secondary button, and reporting one would
        // let a right-click reach a state machine that has no case for it.
        if (event.button.button != SDL_BUTTON_LEFT) {
          break;
        }
        const auto entry = find(event.button.windowID);
        if (entry != windows.end()) {
          const PixelPoint at = to_physical(entry->window, event.button.x, event.button.y);
          const PointerAction action =
              event.button.down ? PointerAction::kDown : PointerAction::kUp;
          result.pointer.push_back(PointerEvent{WindowId{entry->sdl_id}, action, at.x, at.y});
        }
        break;
      }
      case SDL_EVENT_WINDOW_MOUSE_LEAVE: {
        const auto entry = find(event.window.windowID);
        if (entry != windows.end()) {
          // No position, deliberately: SDL does not report where the pointer
          // went, and inventing one would clear hover against a hit test of
          // the origin.
          result.pointer.push_back(
              PointerEvent{WindowId{entry->sdl_id}, PointerAction::kLeave});
        }
        break;
      }
      case SDL_EVENT_MOUSE_WHEEL: {
        const auto entry = find(event.wheel.windowID);
        if (entry != windows.end()) {
          // SDL_MOUSEWHEEL_FLIPPED means "natural scrolling" is off at the
          // OS level and the reported sign is already reversed from what a
          // normal wheel would produce - flip it back once here rather than
          // teach every consumer the platform's own convention.
          const float sign = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0F : 1.0F;
          const PixelPoint at =
              to_physical(entry->window, event.wheel.mouse_x, event.wheel.mouse_y);
          PointerEvent wheel{WindowId{entry->sdl_id}, PointerAction::kWheel, at.x, at.y};
          wheel.wheel_x = sign * event.wheel.x;
          wheel.wheel_y = sign * event.wheel.y;
          result.pointer.push_back(wheel);
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

void WindowManager::warp_pointer(WindowId id, int x, int y) {
  const auto entry = impl_->find(id.value);
  if (entry == impl_->windows.end()) {
    return;
  }
  // Back into logical coordinates, because that is what SDL takes - the
  // inverse of the conversion every reported position goes through.
  const float density = SDL_GetWindowPixelDensity(entry->window);
  const float scale = density > 0.0F ? density : 1.0F;
  SDL_WarpMouseInWindow(entry->window, static_cast<float>(x) / scale,
                        static_cast<float>(y) / scale);
}

void WindowManager::post_pointer_button(WindowId id, bool down, int x, int y) {
  const auto entry = impl_->find(id.value);
  if (entry == impl_->windows.end()) {
    return;
  }
  const float density = SDL_GetWindowPixelDensity(entry->window);
  const float scale = density > 0.0F ? density : 1.0F;

  SDL_Event event{};
  event.button.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
  event.button.windowID = id.value;
  event.button.button = SDL_BUTTON_LEFT;
  event.button.down = down;
  event.button.clicks = 1;
  event.button.x = static_cast<float>(x) / scale;
  event.button.y = static_cast<float>(y) / scale;
  SDL_PushEvent(&event);
}

void WindowManager::post_wheel(WindowId id, float dx, float dy) {
  const auto entry = impl_->find(id.value);
  if (entry == impl_->windows.end()) {
    return;
  }
  // A real wheel event has no position of its own - it reports wherever the
  // cursor already is - so this asks SDL for the same thing rather than
  // inventing a position a caller would have to keep in step with
  // warp_pointer().
  float logical_x = 0.0F;
  float logical_y = 0.0F;
  SDL_GetMouseState(&logical_x, &logical_y);

  SDL_Event event{};
  event.wheel.type = SDL_EVENT_MOUSE_WHEEL;
  event.wheel.windowID = id.value;
  event.wheel.x = dx;
  event.wheel.y = dy;
  event.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
  event.wheel.mouse_x = logical_x;
  event.wheel.mouse_y = logical_y;
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
  return present(id, image, std::span<const PixelRect>{&dirty, 1});
}

Expected<void, WindowError> WindowManager::present(WindowId id, const ImageView& image,
                                                   std::span<const PixelRect> dirty) {
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

  auto* destination = static_cast<std::uint8_t*>(surface->pixels);
  if (destination == nullptr) {
    return Unexpected{WindowError{"window surface has no pixels"}};
  }

  std::vector<SDL_Rect> updated;
  updated.reserve(dirty.size());

  for (const PixelRect& requested : dirty) {
    // Clipped against the image as well as the window. A frame rasterized
    // before the last resize is smaller than the window it is being shown in,
    // and copying window-sized rows out of it would read past the end.
    const PixelRect region =
        clip_to(clip_to(requested, image.width, image.height), surface->w, surface->h);
    if (region.width <= 0 || region.height <= 0) {
      continue;
    }

    const auto row_span = static_cast<std::size_t>(region.width) * kBytesPerPixel;
    const auto x_offset = static_cast<std::size_t>(region.x) * kBytesPerPixel;
    const auto destination_pitch = static_cast<std::size_t>(surface->pitch);

    for (int row = 0; row < region.height; ++row) {
      const std::size_t source_row = static_cast<std::size_t>(region.y + row) * image.row_bytes;
      const std::size_t destination_row =
          static_cast<std::size_t>(region.y + row) * destination_pitch;
      std::memcpy(destination + destination_row + x_offset,
                  image.pixels + source_row + x_offset, row_span);
    }
    updated.push_back(SDL_Rect{region.x, region.y, region.width, region.height});
  }

  if (updated.empty()) {
    return {};
  }
  entry->shows_fill = false;

  // One call, however many rectangles. Each call is a round trip to the
  // display server, and at 1080p that round trip dominates everything else
  // present() does - see doc/cpu-raster-findings.md.
  if (!SDL_UpdateWindowSurfaceRects(entry->window, updated.data(),
                                    static_cast<int>(updated.size()))) {
    return Unexpected{WindowError{sdl_failure("SDL_UpdateWindowSurfaceRects")}};
  }
  return {};
}

}  // namespace dg
