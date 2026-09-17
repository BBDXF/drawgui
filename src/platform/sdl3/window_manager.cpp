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
#include <deque>
#include <optional>
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

  // WindowSpec::cancellable_close, carried per-window - dispatch()'s
  // SDL_EVENT_WINDOW_CLOSE_REQUESTED case reads this to decide whether a
  // close request destroys the window immediately (false, every window's
  // behaviour before 7-5b) or surfaces through PumpResult::close_requested
  // instead, leaving the window open until close_now() is called.
  bool cancellable_close = false;
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

// Only the keys this engine's editing intents consume - see Key's own
// comment for why an unrecognised key becomes kOther rather than a bespoke
// enumerator.
Key to_key(SDL_Keycode keycode) {
  switch (keycode) {
    case SDLK_LEFT:
      return Key::kLeft;
    case SDLK_RIGHT:
      return Key::kRight;
    case SDLK_HOME:
      return Key::kHome;
    case SDLK_END:
      return Key::kEnd;
    case SDLK_BACKSPACE:
      return Key::kBackspace;
    case SDLK_DELETE:
      return Key::kDelete;
    case SDLK_ESCAPE:
      return Key::kEscape;
    case SDLK_TAB:
      return Key::kTab;
    case SDLK_UP:
      return Key::kUp;
    case SDLK_DOWN:
      return Key::kDown;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
      return Key::kEnter;
    default:
      return Key::kOther;
  }
}

SDL_Keycode from_key(Key key) {
  switch (key) {
    case Key::kLeft:
      return SDLK_LEFT;
    case Key::kRight:
      return SDLK_RIGHT;
    case Key::kHome:
      return SDLK_HOME;
    case Key::kEnd:
      return SDLK_END;
    case Key::kBackspace:
      return SDLK_BACKSPACE;
    case Key::kDelete:
      return SDLK_DELETE;
    case Key::kEscape:
      return SDLK_ESCAPE;
    case Key::kTab:
      return SDLK_TAB;
    case Key::kUp:
      return SDLK_UP;
    case Key::kDown:
      return SDLK_DOWN;
    case Key::kEnter:
      return SDLK_RETURN;
    case Key::kOther:
      break;
  }
  return SDLK_UNKNOWN;
}

// SDL_Keymod -> dg::Modifier (design.md section 5.5.1). `Mod` is Linux's own
// Ctrl per that section ("Mod 是 macOS 的 Cmd，其它平台的 Ctrl") - this
// backend is Linux-only (this file's own top comment), so the substitution
// this project's generator otherwise performs at label-text time
// (chord.h's mod_label()) is simply which physical key IS `Mod` here, done
// once at the one place a real SDL_Keymod ever reaches this engine. Only
// the three modifiers dg::Modifier itself names are read - Caps/Num/Scroll/
// Level5/AltGr/GUI carry no bit in dg::Modifier because nothing in
// input/shortcuts.toml binds against them.
Modifier to_modifier(SDL_Keymod sdl_mods) {
  Modifier mods = Modifier::kNone;
  if ((sdl_mods & SDL_KMOD_CTRL) != 0) {
    mods = mods | Modifier::kMod;
  }
  if ((sdl_mods & SDL_KMOD_SHIFT) != 0) {
    mods = mods | Modifier::kShift;
  }
  if ((sdl_mods & SDL_KMOD_ALT) != 0) {
    mods = mods | Modifier::kAlt;
  }
  return mods;
}

// SDL_Keycode -> LogicalKey, covering exactly the eight keys
// input/shortcuts.toml's bindings reference (LogicalKey's own generated
// header lists them). Same shape as to_key() above: SDL_Keycode is an open,
// platform-defined set, so `default:` is what reports "no shortcut key"
// here, the same way to_key()'s own `default:` reports "no editing intent" -
// LogicalKey itself is the closed side of this mapping, not this switch's
// input.
LogicalKey to_logical_key(SDL_Keycode keycode) {
  switch (keycode) {
    case SDLK_A:
      return LogicalKey::kA;
    case SDLK_C:
      return LogicalKey::kC;
    case SDLK_END:
      return LogicalKey::kEnd;
    case SDLK_HOME:
      return LogicalKey::kHome;
    case SDLK_PAGEDOWN:
      return LogicalKey::kPageDown;
    case SDLK_PAGEUP:
      return LogicalKey::kPageUp;
    case SDLK_V:
      return LogicalKey::kV;
    case SDLK_X:
      return LogicalKey::kX;
    default:
      return LogicalKey::kInvalid;
  }
}

// LogicalKey -> SDL_Keycode, from_key()'s own shape, over the identical
// set to_logical_key() maps in the other direction - post_logical_key()'s
// own real caller (8-3c's keyboard-scrolling check, examples/10_scrolling)
// is what makes this worth building now: PageUp/PageDown have no `Key`
// (this file's own editing-intent enum) enumerator at all, so post_key()'s
// existing `from_key()` cannot drive them onto the platform's own event
// queue. `kInvalid` falls to `SDLK_UNKNOWN`, the same "no real key behind
// this value" answer `from_key(Key::kOther)` already gives for itself.
SDL_Keycode from_logical_key(LogicalKey key) {
  switch (key) {
    case LogicalKey::kA:
      return SDLK_A;
    case LogicalKey::kC:
      return SDLK_C;
    case LogicalKey::kEnd:
      return SDLK_END;
    case LogicalKey::kHome:
      return SDLK_HOME;
    case LogicalKey::kPageDown:
      return SDLK_PAGEDOWN;
    case LogicalKey::kPageUp:
      return SDLK_PAGEUP;
    case LogicalKey::kV:
      return SDLK_V;
    case LogicalKey::kX:
      return SDLK_X;
    case LogicalKey::kInvalid:
      break;
  }
  return SDLK_UNKNOWN;
}

// PointerButton <-> SDL_BUTTON_*, 7-5b's own prerequisite (doc/menus.md
// section 6.1). std::nullopt is what X1/X2 ("back"/"forward") map to - still
// dropped, unchanged from before this slice, matching the file's own
// existing "only a primary-button event survives" comment at the one call
// site that reads this (dispatch()'s SDL_EVENT_MOUSE_BUTTON_DOWN/UP case).
std::optional<PointerButton> to_pointer_button(Uint8 sdl_button) {
  switch (sdl_button) {
    case SDL_BUTTON_LEFT:
      return PointerButton::kPrimary;
    case SDL_BUTTON_RIGHT:
      return PointerButton::kSecondary;
    case SDL_BUTTON_MIDDLE:
      return PointerButton::kMiddle;
    default:
      return std::nullopt;
  }
}

Uint8 from_pointer_button(PointerButton button) {
  switch (button) {
    case PointerButton::kPrimary:
      return SDL_BUTTON_LEFT;
    case PointerButton::kSecondary:
      return SDL_BUTTON_RIGHT;
    case PointerButton::kMiddle:
      return SDL_BUTTON_MIDDLE;
  }
  return SDL_BUTTON_LEFT;
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
          if (entry->cancellable_close) {
            // Left open on purpose (WindowSpec::cancellable_close,
            // design.md section 5.2's cancellable on_close_request) - the
            // caller decides via close_now() below, not this dispatch loop.
            result.close_requested.push_back(WindowId{sdl_id});
          } else {
            close(entry);
            result.closed.push_back(WindowId{sdl_id});
          }
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
        // X1/X2 ("back"/"forward") are still dropped here rather than
        // carried and ignored upstream - nothing routes them, matching
        // Key::kOther's identical policy. Left/Right/Middle all now survive
        // (7-5b, doc/menus.md section 6.1): PointerEvent::button is where a
        // caller tells them apart, not this dispatch loop.
        const std::optional<PointerButton> button = to_pointer_button(event.button.button);
        if (!button.has_value()) {
          break;
        }
        const auto entry = find(event.button.windowID);
        if (entry != windows.end()) {
          const PixelPoint at = to_physical(entry->window, event.button.x, event.button.y);
          const PointerAction action =
              event.button.down ? PointerAction::kDown : PointerAction::kUp;
          PointerEvent pointer_event{WindowId{entry->sdl_id}, action, at.x, at.y};
          pointer_event.button = *button;
          result.pointer.push_back(pointer_event);
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
      case SDL_EVENT_KEY_DOWN:
      case SDL_EVENT_KEY_UP:
      case SDL_EVENT_TEXT_INPUT:
      case SDL_EVENT_TEXT_EDITING:
        // Split out to keep dispatch()'s own branching within this
        // project's cognitive-complexity budget - clang-tidy's
        // readability-function-cognitive-complexity measured this switch at
        // 31 against a threshold of 25 once these three cases joined it.
        dispatch_keyboard(event, result);
        break;
      case SDL_EVENT_TEXT_EDITING_CANDIDATES:
        // Read and dropped, named rather than silently falling to
        // `default:` below - doc/ime.md section 4 records why: on this
        // project's own measured platform, a real IME never sends this
        // event at all (it draws its own candidate window), and nothing
        // else in this codebase has ever needed a second, redundant
        // candidate-list UI to build against - the same "an enumerator/
        // event nothing consumes is a promise this engine does not keep"
        // policy PointerAction's own comment already states for a
        // non-primary mouse button.
        break;
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

  // SDL_EVENT_KEY_DOWN/UP and SDL_EVENT_TEXT_INPUT, split out of dispatch()'s
  // own switch - see the case's own comment for why.
  void dispatch_keyboard(const SDL_Event& event, PumpResult& result) {
    if (event.type == SDL_EVENT_TEXT_INPUT) {
      const auto entry = find(event.text.windowID);
      if (entry != windows.end() && event.text.text != nullptr) {
        result.text_input.push_back(TextInputEvent{WindowId{entry->sdl_id}, event.text.text});
      }
      return;
    }
    if (event.type == SDL_EVENT_TEXT_EDITING) {
      const auto entry = find(event.edit.windowID);
      if (entry != windows.end()) {
        const std::string text = event.edit.text != nullptr ? event.edit.text : std::string{};
        result.text_editing.push_back(TextEditingEvent{WindowId{entry->sdl_id}, text,
                                                       event.edit.start, event.edit.length});
      }
      return;
    }
    const Key key = to_key(event.key.key);
    const LogicalKey logical_key = to_logical_key(event.key.key);
    // Dropped only when NEITHER routing level names this key: no editing
    // intent (Key::kOther) AND no shortcut binding (LogicalKey::kInvalid) -
    // matching PointerAction's identical policy for a non-primary mouse
    // button, extended to two independent "does anything consume this"
    // questions instead of one. A letter key like 'C' is exactly the case
    // that now survives this check where it did not before 8-2: kOther as
    // an editing intent, but LogicalKey::kC as a shortcut key 8-3's router
    // will read.
    if (key == Key::kOther && logical_key == LogicalKey::kInvalid) {
      return;
    }
    const auto entry = find(event.key.windowID);
    if (entry == windows.end()) {
      return;
    }
    const KeyAction action =
        event.key.type == SDL_EVENT_KEY_DOWN ? KeyAction::kDown : KeyAction::kUp;
    const Modifier mods = to_modifier(event.key.mod);
    result.key.push_back(KeyEvent{WindowId{entry->sdl_id}, action, key, mods, logical_key});
  }

  std::vector<OwnedWindow> windows;

  // Backing storage for text this manager pushes onto the event queue
  // itself (post_text_input()). SDL_TextInputEvent::text is a pointer, and
  // SDL owns the string for a REAL SDL_EVENT_TEXT_INPUT it generates, but
  // gives no such guarantee for one pushed via SDL_PushEvent - the memory a
  // pushed event points to has to outlive the call and be freed by whoever
  // allocated it. A deque, not a vector: push_back never invalidates a
  // previously returned c_str(), which a vector's reallocation would.
  std::deque<std::string> posted_text;
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

  impl_->windows.push_back(OwnedWindow{.sdl_id = sdl_id,
                                       .window = window,
                                       .fill = spec.fill,
                                       .cancellable_close = spec.cancellable_close});
  paint(impl_->windows.back());
  return WindowId{sdl_id};
}

PlatformCaps WindowManager::platform_caps() {
  // Measured, not assumed: doc/platform-notes.md's P1 spike ran
  // SDL_CreatePopupWindow on both the x11 and wayland SDL video drivers and
  // found a real OS window that escapes its parent's bounds on both. This
  // backend is compiled only for SDL3/Linux desktop (no #ifdef, per this
  // file's own top comment), so the answer is a constant here rather than a
  // runtime probe - the day a backend exists where it varies, THAT backend
  // measures its own answer.
  return PlatformCaps{.native_popup = true};
}

Expected<WindowId, WindowError> WindowManager::open_popup(WindowId parent, int offset_x,
                                                          int offset_y, int width, int height,
                                                          PopupWindowKind kind) {
  const auto parent_entry = impl_->find(parent.value);
  if (parent_entry == impl_->windows.end()) {
    return Unexpected{WindowError{"open_popup: no such parent window"}};
  }

  // Offsets and size are logical, matching what SDL_CreateWindow already
  // takes for width/height (see open() above) and what SDL_CreatePopupWindow
  // documents: "relative to the origin of the parent". Divided by the same
  // density warp_pointer() already divides by, so a popup anchored at a
  // physical-pixel rectangle this library computed lands where it was asked
  // to at any DPI, not only at density 1.
  const float density = SDL_GetWindowPixelDensity(parent_entry->window);
  const float scale = density > 0.0F ? density : 1.0F;
  const auto to_logical = [scale](int value) {
    return static_cast<int>(static_cast<float>(value) / scale);
  };

  // kMenu (SDL_WINDOW_POPUP_MENU) can gain keyboard focus, which is the only
  // way an Escape key ever reaches it; kTooltip (SDL_WINDOW_TOOLTIP) accepts
  // no input at all - doc/popup.md section 1's own measurement, exposed by
  // 7-5b (doc/menus.md section 6.2) instead of hardcoded.
  const SDL_WindowFlags flags =
      kind == PopupWindowKind::kTooltip ? SDL_WINDOW_TOOLTIP : SDL_WINDOW_POPUP_MENU;
  SDL_Window* popup =
      SDL_CreatePopupWindow(parent_entry->window, to_logical(offset_x), to_logical(offset_y),
                            to_logical(width), to_logical(height), flags);
  if (popup == nullptr) {
    return Unexpected{WindowError{sdl_failure("SDL_CreatePopupWindow")}};
  }

  const SDL_WindowID sdl_id = SDL_GetWindowID(popup);
  if (sdl_id == 0) {
    SDL_DestroyWindow(popup);
    return Unexpected{WindowError{sdl_failure("SDL_GetWindowID")}};
  }

  impl_->windows.push_back(OwnedWindow{.sdl_id = sdl_id, .window = popup, .fill = Color{}});
  return WindowId{sdl_id};
}

void WindowManager::close_popup(WindowId id) {
  const auto entry = impl_->find(id.value);
  if (entry == impl_->windows.end()) {
    return;
  }
  impl_->close(entry);
}

Expected<WindowId, WindowError> WindowManager::open_dialog(const WindowSpec& spec,
                                                           WindowId owner) {
  const auto owner_entry = impl_->find(owner.value);
  if (owner_entry == impl_->windows.end()) {
    return Unexpected{WindowError{"open_dialog: no such owner window"}};
  }

  SDL_Window* window = SDL_CreateWindow(spec.title.c_str(), spec.width, spec.height, 0);
  if (window == nullptr) {
    return Unexpected{WindowError{sdl_failure("SDL_CreateWindow")}};
  }

  // Real OS ownership/modality, in that order - SDL_SetWindowModal()'s own
  // documented precondition ("the window must currently be the child of a
  // parent"). Failure here fails open_dialog() outright (through the
  // ordinary WindowError path, matching open_popup()'s own precedent)
  // rather than silently degrading to an unowned/non-modal window -
  // measured, not assumed, that this happens under SDL_VIDEODRIVER=dummy
  // (see examples/23_menu_tooltip_dialog's own headless check, section
  // 6.3 of doc/menus.md). This engine's OWN modal focus trap
  // (dg::Focus::set_guarded()) is a separate mechanism either way - it does
  // not depend on this OS-level call succeeding.
  if (!SDL_SetWindowParent(window, owner_entry->window)) {
    SDL_DestroyWindow(window);
    return Unexpected{WindowError{sdl_failure("SDL_SetWindowParent")}};
  }
  if (!SDL_SetWindowModal(window, true)) {
    SDL_DestroyWindow(window);
    return Unexpected{WindowError{sdl_failure("SDL_SetWindowModal")}};
  }

  const SDL_WindowID sdl_id = SDL_GetWindowID(window);
  if (sdl_id == 0) {
    SDL_DestroyWindow(window);
    return Unexpected{WindowError{sdl_failure("SDL_GetWindowID")}};
  }

  impl_->windows.push_back(OwnedWindow{.sdl_id = sdl_id,
                                       .window = window,
                                       .fill = spec.fill,
                                       .cancellable_close = spec.cancellable_close});
  paint(impl_->windows.back());
  return WindowId{sdl_id};
}

std::size_t WindowManager::open_window_count() const {
  return impl_->windows.size();
}

void WindowManager::close_now(WindowId id) {
  const auto entry = impl_->find(id.value);
  if (entry == impl_->windows.end()) {
    return;
  }
  impl_->close(entry);
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

void WindowManager::post_pointer_button(WindowId id, bool down, int x, int y,
                                        PointerButton button) {
  const auto entry = impl_->find(id.value);
  if (entry == impl_->windows.end()) {
    return;
  }
  const float density = SDL_GetWindowPixelDensity(entry->window);
  const float scale = density > 0.0F ? density : 1.0F;

  SDL_Event event{};
  event.button.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
  event.button.windowID = id.value;
  event.button.button = from_pointer_button(button);
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

void WindowManager::start_text_input(WindowId id, const PixelRect& caret_rect) {
  const auto entry = impl_->find(id.value);
  if (entry == impl_->windows.end()) {
    return;
  }
  const SDL_Rect area{caret_rect.x, caret_rect.y, caret_rect.width, caret_rect.height};
  SDL_SetTextInputArea(entry->window, &area, 0);
  SDL_StartTextInput(entry->window);
}

void WindowManager::stop_text_input(WindowId id) {
  const auto entry = impl_->find(id.value);
  if (entry == impl_->windows.end()) {
    return;
  }
  SDL_StopTextInput(entry->window);
}

void WindowManager::clear_composition(WindowId id) {
  const auto entry = impl_->find(id.value);
  if (entry == impl_->windows.end()) {
    return;
  }
  SDL_ClearComposition(entry->window);
}

void WindowManager::post_key(WindowId id, bool down, Key key, bool shift) {
  const auto entry = impl_->find(id.value);
  if (entry == impl_->windows.end()) {
    return;
  }
  SDL_Event event{};
  event.key.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
  event.key.windowID = id.value;
  event.key.key = from_key(key);
  event.key.mod = shift ? SDL_KMOD_SHIFT : SDL_KMOD_NONE;
  event.key.down = down;
  SDL_PushEvent(&event);
}

void WindowManager::post_logical_key(WindowId id, bool down, LogicalKey key) {
  const auto entry = impl_->find(id.value);
  if (entry == impl_->windows.end()) {
    return;
  }
  SDL_Event event{};
  event.key.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
  event.key.windowID = id.value;
  event.key.key = from_logical_key(key);
  event.key.mod = SDL_KMOD_NONE;
  event.key.down = down;
  SDL_PushEvent(&event);
}

void WindowManager::post_text_input(WindowId id, const std::string& text) {
  const auto entry = impl_->find(id.value);
  if (entry == impl_->windows.end()) {
    return;
  }
  // SDL_TextInputEvent::text is a pointer, and SDL owns the string for a
  // REAL SDL_EVENT_TEXT_INPUT it generates internally, but a manually
  // SDL_PushEvent()'d one gives no such guarantee - the memory has to
  // outlive the call and something has to own it. impl_->posted_text does,
  // for the lifetime of this WindowManager, which is the same lifetime
  // every other resource this Impl owns already has.
  impl_->posted_text.push_back(text);
  SDL_Event event{};
  event.text.type = SDL_EVENT_TEXT_INPUT;
  event.text.windowID = id.value;
  event.text.text = impl_->posted_text.back().c_str();
  SDL_PushEvent(&event);
}

void WindowManager::post_text_editing(WindowId id, const std::string& text, int start,
                                      int length) {
  const auto entry = impl_->find(id.value);
  if (entry == impl_->windows.end()) {
    return;
  }
  // Same backing-storage requirement as post_text_input() above, and the
  // same deque - a real SDL_EVENT_TEXT_EDITING's `text` pointer has the
  // identical ownership shape as SDL_EVENT_TEXT_INPUT's.
  impl_->posted_text.push_back(text);
  SDL_Event event{};
  event.edit.type = SDL_EVENT_TEXT_EDITING;
  event.edit.windowID = id.value;
  event.edit.text = impl_->posted_text.back().c_str();
  event.edit.start = start;
  event.edit.length = length;
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
