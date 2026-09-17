// WindowManager - several OS windows at once, and the routing that keeps them
// independent.
//
// This header was extracted from src/platform/sdl3/window_manager.cpp after
// that implementation was running, and it declares only what
// examples/01_sdl3_multi_window/main.cpp actually calls. Nothing here is a seam for a
// second backend: the project's previous attempt wrote twelve abstract
// platform headers before any backend existed, and an interface no
// implementation has ever contradicted is a guess with a build rule. When a
// second platform is measured, whatever the two genuinely share can be
// extracted then - from two working implementations rather than from a
// document.
//
// Nothing from the underlying windowing library appears below, and nothing
// from it ever should. A caller includes this header and links drawgui; that
// is the entire contract.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "drawgui/base/expected.h"
#include "drawgui/base/pixel_geometry.h"
#include "drawgui/graphics/types.h"
#include "drawgui/shortcuts/chord.h"

namespace dg {

// Why an operation failed, in a form fit for a diagnostic.
//
// A message rather than an error code, because nothing branches on the
// reason - every caller so far prints it - and the platform's own text
// ("Couldn't find matching GL context", "No available video device") is the
// part that makes a failure debuggable. A code would discard exactly that and
// buy a switch statement nobody writes. It can become a code the day some
// caller needs to act differently on one.
struct WindowError {
  std::string message;
};

// Identifies one open window.
//
// A struct rather than a bare integer so that it cannot be passed where a
// count, an index or a size was meant. The value is only meaningful to the
// WindowManager that issued it, and only while that window is open.
struct WindowId {
  std::uint32_t value = 0;

  friend bool operator==(WindowId, WindowId) = default;
};

// What a window should be when it appears.
//
// Grouped rather than passed as four arguments: title, width and height and
// fill describe one thing, and a call site reading open(title, 480, 320, c)
// invites the two ints to be swapped.
struct WindowSpec {
  std::string title;
  int width = 0;
  int height = 0;

  // Filled flat, so that "which window is this" is answerable by looking at
  // it. The alpha channel is ignored; a window is opaque.
  Color fill;

  // design.md section 5.2's own `on_close_request` being cancellable (the
  // unsaved-changes-prompt case) - 7-5b's own prerequisite for `Dialog`
  // (doc/menus.md section 6.3). False, the default, preserves every
  // existing window's behaviour byte-for-byte: request_close()/the user's
  // own close button still destroy the window immediately and report it
  // through PumpResult::closed, exactly as before this field existed. True
  // routes the SAME close-requested event through PumpResult::
  // close_requested instead - the window is NOT destroyed automatically -
  // and the caller decides, by calling close_now() or doing nothing at
  // all, which is the whole of what "cancellable" has to mean: refusing to
  // act until told to.
  bool cancellable_close = false;
};

// What this platform can do, so a layer above never has to assume.
//
// design.md section 5.1 defines PlatformCaps as several fields
// (multi_window/native_popup/native_menubar/system_tray/window_transparency/
// file_dialog); this slice's actual consumer is PopupHost, which reads only
// `native_popup`, so only that one is here - the same "an interface no
// implementation has ever contradicted is a guess with a build rule" policy
// window_manager.h's own top comment already states. The other five are
// unclaimed by any caller and are not speculatively added.
struct PlatformCaps {
  // True on this SDL3/Linux desktop backend: doc/platform-notes.md's P1
  // spike measured SDL_CreatePopupWindow producing a real, bounds-escaping
  // OS window on both x11 and wayland. A mobile backend would report false
  // here, which is the whole reason PopupHost takes this as a value rather
  // than querying it implicitly - see PopupHost's own header for why a
  // caller can also override it to exercise the overlay branch on a desktop
  // that does have native popups.
  bool native_popup = false;
};

// Which of the two genuinely different SDL popup-window capabilities
// open_popup() should create - doc/popup.md section 1's own measurement,
// closed by 7-5b (doc/menus.md section 6.2): SDL_WINDOW_POPUP_MENU (kMenu)
// CAN gain keyboard focus, which is the only way Escape ever reaches it;
// SDL_WINDOW_TOOLTIP (kTooltip) accepts NO input at all, which is exactly
// what a tooltip needs (nothing should ever be able to click or type into
// one) and exactly wrong for a menu (Escape/arrow-key navigation would
// never arrive). Two enumerators rather than a bool, because "which flag"
// reads at a call site the way `PopupPlacement::kBelow`/`kAbove` already do
// on the same signature's neighbour, not because a third value is
// anticipated - SDL itself has exactly two borderless-popup-shaped window
// flags, not three.
enum class PopupWindowKind : std::uint8_t {
  kMenu,
  kTooltip,
};

// How the bytes of one pixel are arranged in memory.
//
// One enumerator, because one is what has been measured. A window surface on
// this platform is a 32-bit XRGB word, which on a little-endian machine is
// the bytes blue, green, red, unused - the same order a CPU-rasterized
// drawgui frame comes out in, so a frame reaches the screen by copying rather
// than converting. surface_format() reports a failure rather than a second
// enumerator when a window disagrees, because nothing can convert yet and
// quietly presenting the wrong channels is worse than saying so.
enum class PixelFormat : std::uint8_t {
  kBgra8888,
};

// Somebody else's pixels, borrowed for the duration of one call.
//
// This is the whole of what the window layer knows about rendering: an
// address, a shape, a row stride and a channel order. It names no surface
// type, no renderer and no graphics library, so the window layer stays
// ignorant of how the image was produced - which is the property that let
// this header survive the previous attempt's deletion.
struct ImageView {
  const std::uint8_t* pixels = nullptr;
  int width = 0;
  int height = 0;

  // Bytes between the starts of consecutive rows. Passed rather than assumed
  // to be width * 4, because a rasterizer is free to pad rows.
  std::size_t row_bytes = 0;

  PixelFormat format = PixelFormat::kBgra8888;
};

// Which physical button produced a kDown/kUp PointerEvent - 7-5b's own
// prerequisite for a context menu (doc/menus.md section 6.1): before this
// slice, `PointerEvent` carried no button identity anywhere, and the SDL3
// backend dropped every button except the primary one before a PointerEvent
// was ever constructed, so "a right-click cannot activate a widget here" was
// a fact about a missing FIELD, not a routing decision.
//
// A FIELD ON PointerEvent, NOT A NEW PointerAction ENUMERATOR - the
// project's own standing preference, restated: PointerAction names WHAT
// HAPPENED (a move, a press, a release, a leave, a wheel notch), and every
// one of those four things can happen with any button held; WHICH button
// is an orthogonal question a sibling field answers without multiplying
// PointerAction's own cases by three. This also means every existing
// EXHAUSTIVE switch over PointerAction (examples/22_dropdown_menu's own
// dispatch_pointer(), examples/21_focus's, and every prior one) needs no
// fixup at all - the identical exhaustive-switch-fixup cost 7-5 paid for
// extending Key (kUp/kDown/kEnter) does NOT recur here, because nothing was
// added to the enum a switch already covers.
enum class PointerButton : std::uint8_t {
  kPrimary,
  kSecondary,
  kMiddle,
};

// What the pointer did.
//
// Four actions. Until 7-5b, no button identity at all was carried anywhere
// on this path - only the PRIMARY button produced an event, and the backend
// dropped every other one before a PointerEvent was ever constructed,
// because nothing routed them and an enumerator naming a button no widget
// could receive would have been a promise. `PointerEvent::button` (below)
// is the fix: the SECONDARY button now also produces kDown/kUp events - for
// a context menu, doc/menus.md's own follow-up (7-5b) - and the MIDDLE
// button is passed through for the identical "measured, not assumed"
// reason (SDL reports it with the same enum, and dropping a value nothing
// asked for yet is a decision a future caller can still make, not one this
// slice has to make FOR them by omission). Every other SDL button (X1/X2 -
// "back"/"forward" side buttons) is still dropped, unchanged from before
// this slice: nothing routes them and this project's own standing rule
// (`Key::kOther`'s identical comment) is that an enumerator/field value
// nothing consumes is a promise this engine does not keep.
enum class PointerAction : std::uint8_t {
  kMove,
  kDown,
  kUp,

  // The pointer left the window. It carries NO position - `x` and `y` are
  // zero and mean nothing - because the platform does not report where the
  // pointer went, only that it is no longer here. A caller reading them would
  // be hit-testing the top-left corner and clearing hover for the wrong
  // reason, which is a bug that looks exactly like correct behaviour.
  kLeave,

  // A mouse wheel or trackpad scroll. Carries a position - the pointer's, at
  // the moment of the event - because design.md section 5.5 routes a wheel to
  // "the scrollable ancestor under the pointer", not to whatever last had
  // focus or hover; a caller hit-tests `x, y` to find it. `wheel_x`/`wheel_y`
  // hold the scroll amount, which `x`/`y` do not.
  kWheel,
};

// One thing the pointer did to one window.
//
// The position is in the window's PHYSICAL pixels - the same coordinates
// drawable_size() reports and present() copies into - not the logical ones the
// platform delivers. The conversion happens once, in the backend, because a
// pointer coordinate and a framebuffer coordinate that differ by a display
// scale is how clicks land near a widget instead of on it, and every consumer
// would otherwise have to remember to apply it.
struct PointerEvent {
  WindowId window;
  PointerAction action = PointerAction::kMove;
  int x = 0;
  int y = 0;

  // Meaningful only for kDown/kUp - which physical button changed state.
  // kMove/kLeave/kWheel carry no real button of their own (a motion or a
  // leave is not "a button doing something"), so the default kPrimary on
  // those three is a don't-care value, never read by anything this project
  // ships: doc/menus.md section 6.1 names the routing decision this field
  // exists to answer - a secondary-button kDown does NOT feed
  // dg::Interaction's hover/press/click machine the way a primary-button one
  // does (it is a PARALLEL, non-activating channel that only ever opens a
  // context menu), so every caller that already only ever constructed a
  // primary-button PointerEvent by hand keeps doing exactly that with no
  // change in observed behaviour.
  PointerButton button = PointerButton::kPrimary;

  // kWheel only. Positive scrolls up / left, matching the platform's own sign
  // convention (SDL3 already flips SDL_MOUSEWHEEL_FLIPPED for the backend, so
  // a caller never sees that platform detail). FLOAT, unlike every other
  // measurement in this library: a wheel amount is not a device pixel, it is
  // an abstract "how many notches", and a trackpad reports fractional
  // notches that rounding to an int would silently zero.
  float wheel_x = 0.0F;
  float wheel_y = 0.0F;
};

// Which key changed. Not a full keyboard map - only the editing intents
// design.md section 5.5.2 names as belonging to a text field rather than to
// a shortcut table (`MoveCaretLineStart`, `DeleteWordBackward`, and this
// slice's smaller set of them). Everything else is `kOther`, the identical
// policy PointerAction already has for a non-primary mouse button: an
// enumerator naming a key nothing consumes as an EDITING INTENT would be a
// promise this engine does not keep. `kOther` no longer means "dropped
// before it becomes a KeyEvent" as of 8-2, though: a key with no editing
// intent can still carry a real `LogicalKey` (KeyEvent::logical_key below) -
// 'C' is `kOther` here and `LogicalKey::kC` there, and 8-3's router is what
// will eventually read the second field, not this one.
enum class Key : std::uint8_t {
  kOther,
  kLeft,
  kRight,
  kHome,
  kEnd,
  kBackspace,
  kDelete,

  // PopupHost's own dismissal key (design.md section 5.2's Popup window
  // kind: "click outside or Escape"). Not a text-field editing intent like
  // the six above it, but the same "only what has a real consumer" policy
  // this enum's own comment states applies to it too.
  kEscape,

  // 7-4's Tab/Shift-Tab (`mods` on KeyEvent below already carries the
  // distinction). design.md section 5.5.2 lists Tab under "焦点树 + 显式
  // tab_index" (section 5.5), not under a text field's own editing intents
  // - it is the one keyboard input this project routes through
  // `dg::Focus::focus_next()`/`focus_previous()` rather than through a
  // widget's model at all.
  kTab,

  // 7-5's Dropdown/menu keyboard navigation (design.md section 5.6's
  // Dropdown/Menu, doc/menus.md): arrow-key highlight movement and Enter to
  // activate the highlighted option. Neither existed before this slice
  // because nothing here had a list of options to move a highlight through
  // - the identical "only what has a real consumer" policy this enum's own
  // comment already states for kEscape/kTab. kUp/kDown are deliberately NOT
  // named kBackward/kForward or folded into a generic "menu navigate"
  // action: a dropdown's own vertical option list is the only consumer so
  // far, matching this project's own standing rule against a speculative
  // vocabulary.
  kUp,
  kDown,
  kEnter,
};

enum class KeyAction : std::uint8_t {
  kDown,
  kUp,
};

// One key changing state on one window.
//
// `mods` is design.md section 5.5.1's own bitmask (dg::Modifier,
// drawgui/shortcuts/chord.h) - Shift/Mod/Alt, not just Shift, because 8-3's
// router needs the full set to build a dg::Chord and match it against
// dg::all_shortcut_bindings(). Replacing the single `shift` bool this field
// used to be, rather than adding `mods` alongside it: two fields carrying
// overlapping truth (`shift` and `has(mods, Modifier::kShift)`) could
// disagree, and this struct's own prior comment already argued against
// exactly that ("a field nobody reads is a field nobody can trust stayed
// correct" - the same reasoning applies to two fields that both claim to
// answer the same question).
//
// `logical_key` is design.md section 5.5.2's OTHER routing level: `key`
// above is an editing intent a widget consumes directly, and `logical_key`
// is the shortcut-table half a dg::Chord is built from - two separate
// fields for two separate levels, per chord.h's own comment and
// LogicalKey's own generated header. `LogicalKey::kInvalid` means this key
// has no entry in input/shortcuts.toml at all, the identical "no
// enumerator/mapping without a real consumer" policy `Key::kOther` already
// states for itself.
struct KeyEvent {
  WindowId window;
  KeyAction action = KeyAction::kDown;
  Key key = Key::kOther;
  Modifier mods = Modifier::kNone;
  LogicalKey logical_key = LogicalKey::kInvalid;
};

// Committed text from the platform's text-input mechanism, UTF-8 as SDL
// delivers it. This is ALSO design.md's IME hook point (line ~142-143's
// `start_text_input`/`stop_text_input`) doing its ordinary ASCII job: SDL3
// generates no SDL_EVENT_TEXT_INPUT at all until start_text_input() has been
// called on the window, IME or not, so this plumbing is required for plain
// ASCII typing to work in the first place - it is not a placeholder built
// "just in case" for a later slice. A composed, non-ASCII character an IME
// commits still arrives here as ordinary committed text - this library does
// not distinguish "typed" from "IME-committed", and a TextField widget's
// text_field_insert() (doc/text-input.md, doc/ime.md) is the single place
// either kind lands, unchanged by which one it was.
//
// 7-3 (doc/ime.md) is what now also reads TextEditingEvent below for the
// PREVIEW half of composition - this struct's own job (the COMMIT half) is
// untouched by that: a real IME still delivers a TextInputEvent exactly like
// this at the moment it commits, whether or not any TextEditingEvent ever
// preceded it.
struct TextInputEvent {
  WindowId window;
  std::string text;
};

// The IME's in-progress, NOT YET COMMITTED composition/preedit string -
// SDL_EVENT_TEXT_EDITING, unread by 4-9 and named there as P7's own job
// (design.md line ~1719); this is 7-3 reading it. doc/ime.md section 2 is
// the full investigation; the short version:
//
// `text` is UTF-8, exactly like TextInputEvent::text - empty means
// composition just ended WITHOUT committing anything (SDL's own convention:
// a real commit is a SEPARATE, later TextInputEvent, never folded into this
// one).
//
// `start`/`length` are SDL's OWN documented unit, quoted verbatim because it
// is a THIRD offset convention this project had not measured before this
// slice: "the start cursor is the position, in UTF-8 CHARACTERS, where new
// typing will be inserted... length is the number of UTF-8 characters that
// will be replaced by new typing" (SDL3's own SDL_events.h). This is neither
// of the two units 7-2b already measured for Skia's own editing surface
// (UTF-8-byte-native, the API this project actually calls; UTF-16, the
// legacy API it never calls) - "UTF-8 characters" reads as a codepoint
// count, not a byte offset, which is exactly the trigger 7-2b's own
// cross-reference said to watch for. `-1` is SDL's documented sentinel for
// "not set" on either field. doc/ime.md records plainly that this could NOT
// be verified against a genuine composition sequence on this project's own
// development machine - every real IME probed here never delivers this
// event at all (section 2) - so WidgetSet::text_field_composition_update()
// converts these units to a byte offset by walking codepoints (the only
// literal reading of SDL's own words available), clamped defensively
// against exactly the "absurd offset from an untrusted source" case
// `-DDG_SANITIZE=ON` is meant to catch, but the conversion itself is
// unverified against a live IME - named, not hidden.
struct TextEditingEvent {
  WindowId window;
  std::string text;
  int start = -1;
  int length = -1;
};

// Everything one pump() turned up, split by what the caller has to do about
// it.
struct PumpResult {
  // Windows that closed during this call, in the order they closed. A
  // cancellable_close window (WindowSpec) is never in here directly from a
  // user's close request - see close_requested below.
  std::vector<WindowId> closed;

  // A cancellable_close window's own close request - `on_close_request`,
  // design.md section 5.2 - reported HERE INSTEAD OF closed above, and the
  // window is NOT destroyed: it stays exactly as open as it was before this
  // pump() call, with nothing else about it changed. A caller runs whatever
  // veto logic it wants (an "unsaved changes" prompt, most concretely) and
  // then either calls close_now(id) to actually destroy it or does nothing
  // at all, which is what makes doing nothing the same thing as vetoing -
  // there is no separate "cancel" call to remember to make.
  std::vector<WindowId> close_requested;

  // Windows whose contents are now stale - newly exposed, or resized. A
  // resize is not reported separately because the only correct response to
  // either is the same: ask for the drawable size again and draw a frame at
  // it. Assuming the old size is still valid is how a resize turns into a
  // torn frame or a heap overflow.
  std::vector<WindowId> needs_repaint;

  // In the order they happened, which is the whole of what makes them usable:
  // a press and the release that ends it are only a click if nothing was
  // reordered between them.
  //
  // A caller must handle `needs_repaint` BEFORE these. A resize changes where
  // every widget is, and a pointer event that arrived in the same pump would
  // otherwise be hit-tested against the layout the window no longer has.
  std::vector<PointerEvent> pointer;

  // Keyboard and committed-text events, in the order they arrived. Routing
  // either to a specific widget (which one has FOCUS) is the caller's job -
  // this layer only reports that a window received them, the same
  // window-scoped granularity every event above already has.
  std::vector<KeyEvent> key;
  std::vector<TextInputEvent> text_input;

  // Composition-preview events (7-3, doc/ime.md) - SDL_EVENT_TEXT_EDITING.
  // Ordinary on every platform this project has measured; on the one it
  // develops on, doc/ime.md section 2 records that a real IME never
  // produces one at all (it draws its own composition window instead), so
  // this vector is empty in every real run so far and is exercised by
  // WindowManager::post_text_editing()'s synthetic injection instead.
  std::vector<TextEditingEvent> text_editing;
};

class WindowManager {
 public:
  // Initializes the platform's video subsystem. One at a time per process.
  [[nodiscard]] static Expected<WindowManager, WindowError> create();

  WindowManager(WindowManager&&) noexcept;
  WindowManager& operator=(WindowManager&&) noexcept;
  WindowManager(const WindowManager&) = delete;
  WindowManager& operator=(const WindowManager&) = delete;

  // Closes every window still open and shuts the video subsystem down.
  ~WindowManager();

  // Opens a window immediately. Windows already open are unaffected, and the
  // new one is filled with spec.fill before this returns.
  [[nodiscard]] Expected<WindowId, WindowError> open(const WindowSpec& spec);

  // What this backend can do, queried rather than assumed. See PlatformCaps.
  [[nodiscard]] static PlatformCaps platform_caps();

  // Opens a real popup window owned by `parent`, positioned `offset_x`,
  // `offset_y` from parent's own top-left - SDL_CreatePopupWindow's own
  // coordinate system, verified by doc/platform-notes.md's P1 spike to place
  // and size the popup exactly as asked and to let it overhang parent's
  // bounds on both the x11 and wayland drivers. `parent` must already be
  // open; an id naming no window, or one that is itself a popup's ancestor
  // beyond what SDL accepts, fails through the ordinary WindowError path
  // rather than asserting.
  //
  // The window this returns can gain keyboard focus (SDL_WINDOW_POPUP_MENU),
  // which is what lets Escape reach it - the tooltip variant
  // (SDL_WINDOW_TOOLTIP, no input at all) is a real, different SDL flag
  // `kind` (below) now exposes, doc/popup.md's own named decline, closed by
  // 7-5b (doc/menus.md section 6.2).
  //
  // `kind` defaults to kMenu, so every existing call site (5-2's own
  // PopupHost, and every dropdown/menu example built on it since) keeps
  // opening the exact SDL_WINDOW_POPUP_MENU window it always has, with no
  // source change required. kTooltip opens SDL_WINDOW_TOOLTIP instead - a
  // window that, per doc/popup.md section 1's own measurement, accepts NO
  // input at all, which is why PopupHost::handle_pointer()/handle_key()
  // dismissal is not how a tooltip is ever closed (see popup_host.h).
  [[nodiscard]] Expected<WindowId, WindowError> open_popup(
      WindowId parent, int offset_x, int offset_y, int width, int height,
      PopupWindowKind kind = PopupWindowKind::kMenu);

  // Destroys a popup window immediately, synchronously, unlike
  // request_close(). A popup has no title bar and no user-driven close
  // button, so there is no "same route a user takes" to imitate; the
  // decision to dismiss it is the caller's (PopupHost's), made in response to
  // a click outside or an Escape key, and it must take effect before the next
  // frame is drawn rather than round-tripping through pump(). An id naming no
  // open window is ignored, matching request_close()'s idempotence.
  void close_popup(WindowId id);

  // Opens an ordinary top-level window that is also `owner`'s CHILD - real
  // SDL_SetWindowParent()/SDL_SetWindowModal() ownership, not a convention
  // this engine invents on top of an unrelated window - design.md section
  // 5.2's `Dialog` window kind ("有 owner，可模态"), 7-5b's own prerequisite
  // (doc/menus.md section 6.3). `owner` must already be open. This is the
  // window's OS-level shape only: the modal FOCUS trap this engine's own
  // dg::Focus enforces (set_guarded(), focus.h) is a separate, necessary
  // mechanism, because SDL's own modal enforcement (blocking input to the
  // owner at the platform level) and this engine's own click/set() routing
  // are two different layers - a headless/dummy-driver backend, in
  // particular, has no platform-level modal enforcement to fall back on at
  // all, matching open_popup()'s own measured "dummy can open ordinary
  // windows but not this" limitation (doc/popup.md section 1) - see
  // doc/menus.md section 6.3 for the measurement on THIS call specifically.
  [[nodiscard]] Expected<WindowId, WindowError> open_dialog(const WindowSpec& spec,
                                                            WindowId owner);

  [[nodiscard]] std::size_t open_window_count() const;

  // Asks for one window to close, by the same route the window manager's own
  // close button takes. It has not closed when this returns - the close is
  // observed from pump(), exactly like a user-initiated one. An id that names
  // no open window is ignored, which is what makes asking twice harmless.
  //
  // A window opened with WindowSpec::cancellable_close does NOT close from
  // this alone: the request surfaces through PumpResult::close_requested
  // instead (see its own comment), and close_now() below is what actually
  // destroys it.
  void request_close(WindowId id);

  // Destroys a window immediately and synchronously - close_popup()'s own
  // shape, applied to an ordinary/dialog window instead of a popup, for the
  // identical reason: once a caller has decided (perhaps after running its
  // own on_close_request veto logic) that a close should actually happen,
  // there is no "same route a user takes" left to imitate, because the
  // route a user took is exactly what already produced the
  // close_requested entry this is answering. An id naming no open window is
  // ignored, matching close_popup()'s/request_close()'s idempotence. Safe
  // to call on a window that was never cancellable_close at all - it is
  // simply request_close() without the round trip through pump().
  void close_now(WindowId id);

  // Waits up to timeout_ms for something to happen, then handles everything
  // queued. Empty on a timeout.
  [[nodiscard]] PumpResult pump(int timeout_ms);

  // Moves the actual pointer, in the window's physical pixels.
  //
  // This is the real thing: the display server moves the cursor and delivers
  // the motion event it would have delivered had a hand done it. Together with
  // post_pointer_button() it lets a demo drive itself along a scripted path
  // THROUGH the ordinary event queue, hit testing and state machine - never
  // around them. A scripted mode that called the state machine directly would
  // prove nothing about the path a user takes, which is the only path that can
  // be wrong.
  void warp_pointer(WindowId id, int x, int y);

  // Puts a button press or release on the platform's own event queue, by the
  // same route request_close() uses for a close. `button` defaults to
  // kPrimary, so every call site written before 7-5b keeps posting exactly
  // the primary-button event it always did with no source change; a caller
  // driving the secondary-button context-menu path (doc/menus.md section
  // 6.1) passes kSecondary explicitly.
  //
  // Pushed rather than synthesized at the device, because no windowing system
  // offers a "press the button" call - warping the pointer is as far as the
  // real hardware path goes. The event is indistinguishable from a physical
  // one once queued, so everything downstream of pump() is exercised exactly
  // as it is for a user.
  void post_pointer_button(WindowId id, bool down, int x, int y,
                           PointerButton button = PointerButton::kPrimary);

  // Puts a wheel scroll on the platform's own event queue, same route as
  // post_pointer_button(). `dx`/`dy` are notches, matching
  // PointerEvent::wheel_x/wheel_y; the position at which the wheel is
  // reported to have happened is the window's LAST warped/real pointer
  // position, which is what a real wheel event also does - a wheel has no
  // position of its own, it reports wherever the cursor already is.
  void post_wheel(WindowId id, float dx, float dy);

  // design.md line ~142-143's IME hook point, `IWindow::start_text_input(rect)`
  // / `stop_text_input()`, implemented here as a direct SDL3 passthrough -
  // required for SDL_EVENT_TEXT_INPUT to be generated AT ALL (SDL3 gates it
  // on this call regardless of whether an IME is active), so it is real
  // plumbing this slice needs for plain ASCII typing, not a placeholder
  // reserved for a later one. `rect` is the on-screen caret rectangle IMEs
  // use to position a candidate window - doc/ime.md section 3 confirms,
  // empirically, on this project's own development machine, that this rect
  // is exactly what the real IME (fcitx5) reads to place its OWN, real X11
  // composition window: moving `caret_rect` moves that window one-for-one.
  // No composition PREVIEW is read here - see TextEditingEvent's own
  // comment (doc/ime.md) for that half, which 7-3 built as a separate
  // pump()-reported event rather than folding it into this call.
  void start_text_input(WindowId id, const PixelRect& caret_rect);
  void stop_text_input(WindowId id);

  // SDL_ClearComposition - tells the platform's own IME to abandon whatever
  // it is composing, without committing it. 7-3's own cancellation path
  // (Escape mid-composition, doc/ime.md section 6) calls this alongside
  // WidgetSet::text_field_cancel_composition() so both halves - this
  // engine's model and the platform's own IME state - agree; calling only
  // one would leave the other one ahead, showing (or expecting) a
  // composition the other side has already forgotten.
  void clear_composition(WindowId id);

  // Puts a real key event on the platform's own event queue, same route
  // post_pointer_button() uses - so a scripted run exercises the actual
  // SDL event queue and this manager's own dispatch(), not a shortcut around
  // either.
  void post_key(WindowId id, bool down, Key key, bool shift);

  // The identical route post_key() uses, for a key that carries a
  // `LogicalKey` (8-3c's own shortcut keys - PageUp/PageDown, which
  // input/shortcuts.toml binds and `Key` (this file's own editing-intent
  // enum, above) has no case for) rather than a `dg::Key` editing intent.
  // Kept a separate overload rather than widening `Key` with a PageUp/
  // PageDown enumerator: `Key`'s own comment already states every
  // enumerator needs a real editing-intent consumer, and neither
  // `is_text_editing_intent()` (router.cpp) nor any other switch over `Key`
  // would ever gain a case for a key that is a shortcut, not an editing
  // intent - the identical "no enumerator without a real consumer" policy
  // `LogicalKey`'s own generated header already states for itself. Gives
  // `to_logical_key()`'s reverse mapping (SDL3's own window_manager.cpp)
  // its first real caller - previously declined by name for exactly that
  // reason ("nothing calls it yet").
  // `mods` defaults to Modifier::kNone, so 8-3c's own existing call site
  // (examples/10_scrolling, PageUp/PageDown - neither binding carries a
  // modifier) keeps posting exactly the unmodified event it always did with
  // no source change. 8-4's own real end-to-end clipboard check
  // (examples/12_text_input) is what makes a real modifier worth carrying
  // here: `Mod+C`/`Mod+X`/`Mod+V`/`Mod+A` cannot be posted onto the actual
  // SDL event queue at all without one.
  void post_logical_key(WindowId id, bool down, LogicalKey key,
                        Modifier mods = Modifier::kNone);

  // Puts committed text on the platform's own event queue as a real
  // SDL_EVENT_TEXT_INPUT, the same route post_key() uses.
  void post_text_input(WindowId id, const std::string& text);

  // Puts a synthetic SDL_EVENT_TEXT_EDITING on the platform's own event
  // queue - the SAME precedent post_text_input()/post_pointer_button()
  // already establish for driving a real event through the real queue and
  // this manager's own dispatch() deterministically. This is 7-3's answer
  // to the testing problem doc/ime.md section 5 states plainly: a real IME
  // on this development machine never produces a genuine
  // SDL_EVENT_TEXT_EDITING at all (section 2), so this is the only
  // deterministic way this project has to exercise the composition-preview
  // code path in a CTest entry - a synthesized event, observed from
  // pump() exactly like a user-initiated one, but NOT a substitute for
  // verifying against a live composing IME. `start`/`length` are passed
  // through verbatim as SDL's own ints - TextEditingEvent's own comment is
  // where the unit question is recorded.
  void post_text_editing(WindowId id, const std::string& text, int start, int length);

  // Puts UTF-8 text on the platform clipboard (8-4, design.md section
  // 5.5.1's clipboard seam). A METHOD on WindowManager, not a free
  // function: SDL's own clipboard calls require the video subsystem to be
  // initialised, and this manager is the only thing in this codebase that
  // guarantees that (this file's own top comment) - a free function would
  // compile fine and silently do nothing without a live WindowManager,
  // where a method makes the dependency a structural fact the caller
  // cannot get around. `std::string_view` in, never a `const char*`
  // reinterpreted from SDL's own type: no SDL type crosses this header,
  // matching every other method on this class.
  //
  // Returns false on an SDL-reported failure (SDL_SetClipboardText itself
  // returns bool, true on success - SDL3's own convention, not SDL2's
  // 0-on-success int, the same distinction WindowManager::create() already
  // states for SDL_Init). No WindowError is threaded through: nothing here
  // has a caller that branches on the failure REASON specifically, only on
  // whether it happened at all - see WindowError's own comment for why a
  // message rather than a code is this project's default, and see this
  // method's own boolean return for why even a message is not built ahead
  // of a caller that reads it.
  bool set_clipboard_text(std::string_view text);

  // The platform clipboard's current text, decoded as UTF-8 - SDL's own
  // encoding (SDL_GetClipboardText's documented return). Empty for every
  // "nothing to paste" case this project has measured: nothing was ever
  // set, an X11/Wayland source application's own clipboard ownership has
  // lapsed (the clipboard is served lazily by whichever process last
  // copied, and that process can exit), or any other SDL-reported failure -
  // SDL_GetClipboardText's own documented behaviour is to return an empty
  // string rather than NULL for "nothing here", verified against the
  // installed SDL3 header (SDL_clipboard.h) rather than assumed from
  // SDL2's differently-shaped API. A caller therefore gets the identical
  // answer for every absence case rather than a second one to handle -
  // never a hang, a crash, or a null dereference, which is the actual
  // guarantee this method exists to make (a lazily-served, since-exited
  // source process is a normal condition on this platform, not a bug to
  // detect).
  [[nodiscard]] std::string get_clipboard_text() const;

  // The size a frame for this window must be rasterized at, right now.
  //
  // Asked for per frame rather than remembered from open(): the window
  // manager, the user and the compositor all resize windows, and a cached
  // size is stale from the moment one of them does.
  [[nodiscard]] Expected<PixelSize, WindowError> drawable_size(WindowId id) const;

  // The channel order this window's surface expects. An error means the
  // window is in a format drawgui has never seen, not that the window is
  // broken - see PixelFormat.
  [[nodiscard]] Expected<PixelFormat, WindowError> surface_format(WindowId id) const;

  // Copies `dirty` out of `image` onto the window and puts it on screen.
  //
  // `image` must cover the whole window - `dirty` selects the part of it that
  // has actually changed, in the window's own pixel coordinates, and is
  // clipped to the window before anything is copied. A caller repainting
  // everything passes the full bounds; a caller repainting a hover highlight
  // or a blinking caret passes just that, and pays for just that.
  //
  // Presenting is what stops the window showing its WindowSpec::fill: that
  // flat colour is what a window displays until its owner draws something,
  // and never again afterwards.
  [[nodiscard]] Expected<void, WindowError> present(WindowId id, const ImageView& image,
                                                    const PixelRect& dirty);

  // The same, for a caller that has several disjoint damage rectangles.
  //
  // Each call to the single-rectangle form is its own round trip to the
  // display server, and at 1080p that round trip costs far more than the
  // pixels do. Measured over six runs each, presenting a frame's two to four
  // damage rectangles one call at a time took 0.57 ms; one call carrying all
  // of them took 0.42 ms.
  //
  // Overlapping rectangles would be copied twice, so the set is expected to
  // be disjoint. dg::DamageRegion produces one.
  [[nodiscard]] Expected<void, WindowError> present(WindowId id, const ImageView& image,
                                                    std::span<const PixelRect> dirty);

 private:
  struct Impl;

  explicit WindowManager(std::unique_ptr<Impl> impl);

  std::unique_ptr<Impl> impl_;
};

}  // namespace dg
