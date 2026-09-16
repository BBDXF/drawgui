# Slice 7-3: IME composition — what SDL3 actually delivers, why the candidate window is not this engine's to draw, and the honest gap between a synthesized test and a real input method

4-9 built `WindowManager::start_text_input()`/`stop_text_input()` as a real
SDL3 passthrough and named `SDL_EVENT_TEXT_EDITING` — the composition
preview — as the one thing deliberately left unread, assigned to P7
(design.md line ~1719). 7-2b then made `TextField` edit arbitrary
well-formed UTF-8 by whole grapheme cluster. This slice is the last piece:
reading the composition event, rendering the preview, and committing
through 7-2b's own path. The task's acceptance bar is 中文输入可用 —
Chinese input works.

The short version:

- **SDL3 delivers `SDL_TextEditingEvent{text, start, length}` — a preedit
  string plus one byte/codepoint range within it.** That is the whole
  shape. `SDL_EVENT_TEXT_EDITING_CANDIDATES` also exists (a real, separate
  event type this project had not seen documented anywhere before this
  slice) but is read-and-dropped by name — section 4 explains why.
- **On this project's own development machine, with a real, running,
  correctly-configured IME (fcitx5 + rime), `SDL_EVENT_TEXT_EDITING` is
  never delivered at all.** Measured directly, three independent ways
  (section 2). The IME draws its own real X11 window instead, and
  positions it using exactly the caret rectangle 4-9's
  `start_text_input()` already reports — the single most important
  deliverable this slice's own task named in advance, and it was already
  built.
- **SDL's own header comment describes `start`/`length` as "in UTF-8
  characters"** — a third offset convention, neither of the two 7-2b
  already measured for Skia's editing surface (UTF-8-byte-native, the one
  actually called; UTF-16, the legacy one never called). Section 5.13.2's
  three-index-space question resurfaces, once, at exactly one seam — and
  is answered narrowly rather than by building the general type system
  (section 6).
- **The testing problem is solved the way the task itself pointed at**:
  `WindowManager::post_text_editing()`, a synthetic-injection sibling of
  `post_text_input()`/`post_pointer_button()`, drives a real
  `SDL_EVENT_TEXT_EDITING` through the real SDL queue and this project's
  own `pump()`/`dispatch()`. This proves the engine reads a well-formed
  event correctly. It does **not** prove end-to-end correctness against a
  live, composing IME — section 5 states the boundary as plainly as the
  claim itself.
- **No new node/RenderObject/`WidgetKind`.** `composition_underline` is a
  fourth plain positioned child, the same shape `caret`/
  `selection_highlight` already are. Line 622's streak (section 8) holds
  through an 18th consecutive slice.
- **The relayout claim is re-measured, not assumed, under the harder case
  the task named**: a composition preview changes LENGTH on every
  keystroke (unlike committed text, which only changes on insert/
  backspace) — `LayoutStats` is `nodes_visited == nodes_relaid_out == 0`
  across three different preedit lengths in a row (section 9).
- **10 defect injections against the new logic, 9 caught immediately, 1
  provably inert** (section 10) — one of the 9 is a genuine crash
  (`std::out_of_range`), not merely a wrong answer, confirming the
  composing-suppression guards are load-bearing rather than a behavioural
  nicety.

---

## 1. What design.md and 4-9 assumed, versus what this slice measured

design.md line ~1719 assigns IME to P7 with no further detail; 4-9's own
comment (`doc/text-input.md` section 1.3, `window_manager.h`'s own
`TextInputEvent` comment) says only that `SDL_EVENT_TEXT_EDITING` is
"deliberately not read anywhere" and stays P7's job. Neither document
names an actual SDL3 struct shape, a candidate-window mechanism, or an
offset unit — this slice is the first to read SDL3's own headers on the
question rather than infer from the general concept "IME composition".

**Measured, from `/usr/include/SDL3/SDL_events.h` and
`/usr/include/SDL3/SDL_keyboard.h` on this machine (SDL 3.4.2)**:

```c
typedef struct SDL_TextEditingEvent {
  SDL_EventType type;    /* SDL_EVENT_TEXT_EDITING */
  Uint32 reserved;
  Uint64 timestamp;
  SDL_WindowID windowID;
  const char *text;      /* the editing text */
  Sint32 start;          /* start cursor of selected editing text, or -1 if not set */
  Sint32 length;         /* length of selected editing text, or -1 if not set */
} SDL_TextEditingEvent;
```

with the header's own prose directly above it: **"The start cursor is the
position, in UTF-8 characters, where new typing will be inserted into the
editing text. The length is the number of UTF-8 characters that will be
replaced by new typing."** This is `TextEditingEvent`'s own comment
(`window_manager.h`) verbatim, quoted because section 5.13.2's own
question hinges on the exact words.

A second, sibling event exists that neither design.md nor 4-9 named at
all: `SDL_EVENT_TEXT_EDITING_CANDIDATES` / `SDL_TextEditingCandidatesEvent`
— a candidate LIST (`const char *const *candidates`, a `selected_candidate`
index, a `horizontal` layout hint). Section 4 is what this slice does with
it.

`SDL_SetTextInputArea(window, rect, cursor)` — 4-9's own `start_text_input`
already calls this — is documented as "native input methods may place a
window with word suggestions near the cursor, without covering the text
being entered." This is the one API surface design.md's own hook
(`start_text_input(rect)`) maps onto directly, and it was already correct
before this slice began.

## 2. Whether the candidate window is drawn by the IM or by this engine — investigated, not assumed

This machine has a real, running input-method stack: **fcitx5** (not a
stub — `pgrep` shows the daemon running, `fcitx5-remote` round-trips over
D-Bus) with **rime** (`fcitx5-rime`, `rime-data-luna-pinyin`) as its
default engine, `XMODIFIERS=@im=fcitx`/`SDL_IM_MODULE=fcitx` set in the
environment, and a real X11 display (`DISPLAY=:0`, the same WSLg sandbox
5-2's `doc/popup.md` measured its own real popup window against).

**Three independent probes, escalating, all against this real stack**:

1. A minimal SDL3 + XTest program (no drawgui code) that opens a window,
   calls `SDL_StartTextInput()`, and fakes real X11 key presses
   (`XTestFakeKeyEvent`) spelling out pinyin ("zhongguo") through the
   actual X server, polling every SDL event type in between. Result:
   **zero `SDL_EVENT_TEXT_EDITING` events, ever** — only a single
   `SDL_EVENT_TEXT_INPUT` carrying the fully-committed "中国" after the
   trailing space. rime correctly converted the pinyin (proving the IME
   chain itself works end-to-end for commits), but no preedit ever
   reached the client.
2. The same probe with `SDL_SetHint(SDL_HINT_IME_IMPLEMENTED_UI,
   "composition,candidates")` set before `SDL_Init` — the exact hint
   SDL3's own header says exists for precisely this ("the application
   handles `SDL_EVENT_TEXT_EDITING` events and can render the composition
   text"). **No change**: still zero composition events. This is
   evidence the negotiation failure is server-side (fcitx5's XIM bridge),
   not a missing client-side opt-in.
3. `xwininfo -root -tree` during composition, to settle the question
   empirically rather than by elimination. It shows a REAL, separate X11
   window: **`"Fcitx5 Input Window" ("fcitx" "fcitx")`**, sized
   `30x32` (during typing) sitting at a position measured to be **causally
   determined by the caret rectangle `start_text_input()` passed in** —
   two independent placements confirmed it: a window at `(766,467)` with
   `SDL_SetTextInputArea` rect `(10,10,100,30)` produced the fcitx window
   at `(776,517)` (x = window_x + rect.x exactly; y = window_y + rect.y +
   rect.height + a small margin, i.e. placed just below the caret, not
   over it); a second window at `(68,68)` with rect `(200,220,80,24)`
   produced the fcitx window at `(268,322)` — x again exact
   (`68+200=268`), y again `rect.y + rect.height + margin` below.

**Conclusion, stated as plainly as the task asked**: on this platform, the
candidate/composition window is drawn by the input method itself, as a
real, separate OS window, and it is correctly positioned using the exact
rectangle this engine already reports through `start_text_input()`. This
engine's job for the candidate-window half of IME is **already fully
discharged by 4-9's own code** — building an in-engine candidate popup
(a `PopupHost`-hosted one, the natural reach given 5-2) would duplicate a
window the platform already draws, which is precisely the caution the
task's own framing named in advance.

This is a genuine, load-bearing finding: it means `SDL_EVENT_TEXT_EDITING`
reading (scope items 1-3) is real, general-purpose code that is
**correct and complete but functionally silent on this exact
machine/IME/toolkit combination** — not because the code is wrong, but
because fcitx5's XIM implementation does not negotiate the callback/
over-the-spot preedit style this SDL3 build asks for, defaulting instead
to drawing its own window regardless. A different IME (a GTK/Qt native
app talking to ibus directly over its own D-Bus protocol, rather than
through legacy XIM) or a different platform (Windows TSF, which design.md
never assumes and this slice does not touch) could behave differently —
named as an open question this slice cannot answer without that platform
or IME in hand.

## 3. What was built

`include/drawgui/window/window_manager.h` gained:

- `TextEditingEvent{window, text, start, length}` — no SDL type, matching
  every other event struct in this header.
- `PumpResult::text_editing` — a fourth event vector, alongside
  `pointer`/`key`/`text_input`.
- `WindowManager::post_text_editing(id, text, start, length)` — the
  synthetic-injection sibling of `post_text_input()`, same shape, same
  backing-storage lifetime rule (`impl_->posted_text`, reused rather than
  a second deque).
- `WindowManager::clear_composition(id)` — `SDL_ClearComposition`,
  Escape's platform-side half (section 6).

`src/platform/sdl3/window_manager.cpp`: `SDL_EVENT_TEXT_EDITING` joins
`dispatch_keyboard()`'s existing switch (alongside `KEY_DOWN`/`KEY_UP`/
`TEXT_INPUT`, which is already split out of `dispatch()` for clang-tidy's
cognitive-complexity budget — 4-9's own precedent, unchanged shape);
`SDL_EVENT_TEXT_EDITING_CANDIDATES` is an explicit, named, dropped case
(matching `PointerAction`'s own "a button nothing routes is a promise
this engine does not keep" policy) rather than falling silently to
`default:`.

`include/drawgui/widget/widget_set.h` / `src/widget/widget_set.cpp`:

- `Widget` gained a fourth plain positioned child,
  `composition_underline`, and six new runtime fields: `composing`,
  `composition_text`, `composition_focus_start`/`_length` (byte offsets
  WITHIN the preedit — SDL's own start/length, converted), and
  `composition_replace_start`/`_end` (byte offsets WITHIN the COMMITTED
  `text` — captured once, at composition start, from whatever
  selection/cursor already existed).
- `WidgetSet::text_field_composition_update()` — the one entry point a
  `TextEditingEvent` reaches. Sanitizes through the exact
  `sanitize_insertable_text()` (`dg::sanitize_utf8()` + control-byte
  strip) `text_field_insert()` already uses — a composition event is
  exactly as untrusted as a keystroke or a paste, no third sanitization
  policy.
- `WidgetSet::text_field_cancel_composition()` / `is_composing()` /
  `composition_text()`.
- **The commit path is untouched, verified rather than assumed**:
  `text_field_insert()` gained exactly one guard — clear the stale
  composition state if any, then fall through to the SAME code that
  already existed. `widget.cursor`/`selection_anchor` are never written by
  composition itself (only read, to compute
  `composition_replace_start`/`_end` once), so the eventual commit
  replaces exactly the range that was there when composition began,
  selection included, with no parallel edit path.
- **Cancellation, all four named edge cases**: Escape
  (`text_field_cancel_composition()` + `WindowManager::
  clear_composition()`, both halves so neither side is left ahead of the
  other — `examples/12_text_input`'s `Runner::handle_key()`); focus loss
  (`text_field_set_focus(false)` now also resets composition state); a
  click, on the SAME field or triggering a blur to a DIFFERENT one
  (`text_field_click()` cancels before repositioning the cursor); an
  active selection at composition start (captured once into
  `composition_replace_start/_end`, never re-derived, restored intact if
  cancelled because it was never touched).
- **Arrow keys / Backspace / Delete are suppressed while composing** —
  matching the real behaviour a genuine IME already has on this platform
  (fcitx consumes those keys itself for candidate/clause navigation, so
  they do not reach the application at all while it is composing); this
  is also load-bearing for a reason found by injection, not merely
  argued for stylistically (section 10, injection D).

`examples/12_text_input`: `Runner::handle_text_editing()` routes a pumped
`TextEditingEvent` to `text_field_composition_update()`; Escape now
cancels an in-progress composition; `--preset-compose-b` shows the
underline + clause highlight deterministically for `--dump-png`;
`--script` drives a real `post_text_editing()` through the actual SDL
queue and prints the section-5 honesty caveat on the same line;
`text_field_check.cpp` gained Claim 7 (section 9).

## 4. `SDL_EVENT_TEXT_EDITING_CANDIDATES`: read, and declined by name

This slice does **not** build a candidate-LIST UI (a `PopupHost`-hosted
one, the obvious reach given 5-2's own infrastructure). Two reasons,
both real:

- Section 2's own finding: on the one real IME this project can measure
  against, the candidate window is *already* the IME's own — building a
  second one would be the exact duplicated-effort case the task warned
  about in advance, with no way to verify it does not visually collide
  with the real one.
- Nothing in this codebase would consume the data if it were plumbed
  through — the same "an interface no implementation has ever
  contradicted is a guess with a build rule" policy `window_manager.h`'s
  own top comment states, applied here to an event type rather than a
  platform capability. `SDL_EVENT_TEXT_EDITING_CANDIDATES` is read (it has
  an explicit, commented case in `dispatch()`) and dropped, matching
  `PointerAction`'s treatment of a non-primary mouse button.

## 5. The testing problem, and the honest gap

This project's every prior text/input slice drove REAL input: 4-8's
`--script` used `warp_pointer`/`post_pointer_button` through real X11;
4-9's used a real `SDL_EVENT_TEXT_INPUT`/`SDL_EVENT_KEY_DOWN`. Section 2
establishes this is not available for composition: a real IME on this
machine never produces `SDL_EVENT_TEXT_EDITING` at all, so there is no
real event to route through the queue.

**What was built instead, following the task's own pointer to precedent**:
`WindowManager::post_text_editing()` — the same shape
`post_text_input()`/`post_pointer_button()` already are: a real
`SDL_PushEvent()` onto the platform's own queue, observed back out through
the ordinary `pump()`/`dispatch()` path, "exactly like a user-initiated
one" (`window_manager.h`'s own words for the identical technique, quoted
verbatim because it is the precedent this slice reuses rather than
invents). `examples/12_text_input --script` demonstrates it, printed
alongside the exact honesty statement below.

**What this DOES prove**: the SDL3 event-dispatch plumbing
(`dispatch_keyboard()`'s new case, `PumpResult::text_editing`,
`WindowManager::post_text_editing()`) round-trips a well-formed
`SDL_TextEditingEvent` through the real SDL queue exactly as it would a
real one, and that `WidgetSet::text_field_composition_update()` (unit-
tested extensively, hand-derived, section 10's defect-injection subject)
correctly turns that event into the right model state and pixels.

**What this does NOT prove, stated exactly as plainly as the claim
itself**: that this code works end-to-end against a real, live, composing
IME. `fcitx5`/`rime` IS installed, running and correctly configured on
this machine (section 2's own probes commit correct Chinese text through
it), but its particular XIM negotiation with SDL3 never emits the
composition event this slice reads at all — so no run performed for this
slice has ever shown a genuine mid-composition preedit string reach this
engine's code. If a real IME framework that DOES negotiate the callback
preedit style becomes available (a different XIM server, ibus talking to
a toolkit through its native protocol rather than legacy XIM, or a future
SDL3/fcitx version that fixes the negotiation), that is the moment this
gap closes for real — named here as the exact prerequisite, not glossed
over as "should work."

Per the task's own standing instruction not to install system packages at
scale: **no additional package was installed** to chase this further —
fcitx5/ibus/rime were already present. If the owner wants a definitive
end-to-end real-composition proof, the concrete ask is: either a
different XIM-bridging IME package known to negotiate `XIMPreeditCallbacks`
correctly with SDL3 (not obviously available in this distro's default
fcitx5 packaging), or access to a platform where SDL3's IME path is known
to deliver `SDL_EVENT_TEXT_EDITING` (some ibus/GTK configurations,
reportedly, per SDL3's own issue tracker — not independently verified
here).

## 6. Did IME change 7-2b's byte-offset-native conclusion?

7-2b measured that every Skia call `TextField`'s editing surface needs
(`getGlyphClusterAt`/`getClosestGlyphClusterAt`) is UTF-8-byte-offset
native, and declined to build design.md section 5.13.2's
`ByteOffset`/`Utf16Offset`/`GraphemeIndex` type system because there was
no second index space a caller actually used. This slice's own
cross-reference instruction asked directly: does IME change that?

**Yes and no, precisely stated**:

- **No** — Skia's own editing surface is unchanged; `dg::Paragraph::
  caret_x()` is still called with byte offsets exactly as before, and
  `composition_replace_start`/`_end` (offsets into the COMMITTED `text`)
  are ordinary byte offsets, the same space `cursor`/`selection_anchor`
  already occupy.
- **Yes, at exactly one seam** — SDL's own `start`/`length` on
  `SDL_TextEditingEvent` are documented as "UTF-8 characters", read most
  literally as a CODEPOINT count, which is neither of 7-2b's two known
  units (UTF-8 bytes; UTF-16 code units). `composition_focus_bytes()`
  (`src/widget/widget_set.cpp`) is the one, narrow conversion function
  this forced — walking `dg::utf8_decode()` codepoint-by-codepoint to
  turn SDL's count into a byte offset inside the preedit string, exactly
  the kind of "the day a caller needs it, that is what justifies it"
  seam 7-2b's own document argued for rather than against.
- **The general type system is still declined, for the identical
  reason**: one caller, one seam, a five-line conversion function -
  `dg::Utf16Offset`/`GraphemeIndex`/`ByteOffset` as design.md section
  5.13.2 describes them (a whole strong-typed index-space family with
  banned implicit conversions) would be infrastructure built for a
  consumer this slice does not have. This is recorded as the trigger
  7-2b's own document said to watch for, and the finding is: it fired
  narrowly, not broadly.
- **Also unverified against a real IME** (section 5): since no real
  composition event was ever observed, whether SDL's "UTF-8 characters"
  literally means codepoints (as read here) or something else entirely
  (a grapheme count? — SDL's own docs do not say) could not be checked
  against a live sequence. `composition_focus_bytes()`'s own unit tests
  (`tests/unit/test_text_input.cpp`) pin the CODEPOINT reading against
  hand-constructed values, including SDL's documented `-1` sentinel and
  adversarial out-of-range inputs (`-DDG_SANITIZE=ON`'s own concern) —
  they cannot pin what a real IME would actually send.

## 7. Keeping SDL types out of `include/`

The exact pressure the task named: `TextEditingEvent`, `PumpResult::
text_editing`, `post_text_editing()`, `clear_composition()` are all
plain, SDL-free types/functions in `window_manager.h`, mirroring
`TextInputEvent`'s own precedent from 4-9 exactly — a `std::string` and
two `int`s, never an `SDL_TextEditingEvent*`. `SDL_TextEditingEvent`,
`SDL_TextEditingCandidatesEvent`, `SDL_ClearComposition`,
`SDL_HINT_IME_IMPLEMENTED_UI` all appear **only** in
`src/platform/sdl3/window_manager.cpp`, never in a header — the identical
discipline 5-2's `PopupHost` already held for `SDL_CreatePopupWindow`/
`SDL_WINDOW_POPUP_MENU`. `grep -rn "SDL_" include/` (run after this
slice) returns zero hits, unchanged from every prior slice's own check.

## 8. design.md section 5.6 line 622, re-verified

**Zero new `RenderObject`/node kinds.** `composition_underline` is a
fourth plain positioned `NodeId` child on `Widget` (kTextField only) —
the same shape `content`/`caret`/`selection_highlight` already are, moved
through the existing `RenderTree::set_local_bounds()`, never a new paint
primitive or node type. `WidgetKind` is unchanged at 8 enumerators
(`kPanel`/`kLabel`/`kButton`/`kCheckbox`/`kScrollView`/`kSlider`/
`kTextField`/`kList`). The streak `doc/completeness.md` measured through
4-9, extended through 5-1/5-2/5-3/6-1/6-2/6-3/7-1/7-2/7-2b, holds through
7-3 — **an 18th consecutive slice**.

## 9. The relayout claim, re-measured under the harder case

The task's own instruction: re-measure under a CHANGING preedit, which is
harder than committed text because its length changes on EVERY keystroke,
not only on insert/backspace. `examples/12_text_input`'s
`text_field_check.cpp::check_ime_composition_preview_and_commit()` (Claim
7) composes three DIFFERENT preedit strings in a row against the same
fixed-width field ("hi", then the longer "hell", then the shorter "h")
and calls `LayoutTree::layout()` after each:

```
composing "hi"   -> nodes_visited == 0, nodes_relaid_out == 0
composing "hell" -> nodes_visited == 0, nodes_relaid_out == 0   (longer)
composing "h"    -> nodes_visited == 0, nodes_relaid_out == 0   (shorter)
```

**Structurally, not coincidentally**: `text_field_composition_update()`
routes exclusively through `text_field_refresh_display()`, which (like
every other `text_field_*` mutator since 4-9) only ever calls
`RenderTree::set_text()`/`set_local_bounds()` — there is no code path from
a composition update to `LayoutTree::set_box()`, matching 4-9's own
"structural guarantee, not test coverage" finding, now confirmed to
extend past committed-text edits to a changing preedit as well. The exact
condition 4-9 named — a shrink-to-fit `TextField` whose OWN width is
derived from its content — is unbuilt for composition exactly as it was
for committed text, for the identical reason.

## 10. Defect injection: 10 injected, 9 caught immediately, 1 provably inert

Each injected against the working tree with the `edit` tool (never `git
checkout` — this project's own standing lesson about erasing a
newly-written test), built, run against `tests/unit/test_text_input.cpp`'s
new composition cases (sufficient isolation: every injection here touches
only composition-specific code no other test exercises), then reverted by
hand.

| # | injection | result |
| --- | --- | --- |
| A | `composition_focus_bytes()`'s start-boundary `>=` weakened to `>` | **caught immediately** — the clause-highlight test's exact pixel range moved |
| B | The `std::max<int64_t>(0, …)` clamp on negative `start_units`/`length_units` removed | **provably inert** — the walk's own `index >= start` (index always begins at 0) and the `byte >= text.size()` break independently reproduce the identical clamped result for every negative input, not just the one tested; kept for READABILITY/intent (a reader auditing "what happens on -1" should not have to trace the loop), documented here rather than deleted |
| C | The composing-state reset at the TOP of `text_field_insert()` removed | **caught immediately** — a commit left `is_composing()` true and the stale preview text behind |
| D | The composing-suppression guard in `text_field_backspace()` removed | **caught immediately, and worse than a wrong answer**: a REAL crash (`std::out_of_range` from `substr`) — `composition_replace_start/_end` are captured once at composition START and go stale the instant the underlying text is edited underneath an active preview, so the very next `text_field_refresh_display()` call substrings past the shrunk string's own end. This is not a stylistic suppression; it is load-bearing crash prevention |
| E | Composition cancellation in `text_field_click()` removed | **caught immediately** — clicking mid-composition left `is_composing()` true |
| F | Composition cancellation in `text_field_set_focus(false)` removed | **caught immediately** — losing focus mid-composition left `is_composing()` true |
| G | The underline's end-byte computed as `replace_start` instead of `replace_start + composition_text.size()` | **caught immediately** — the underline collapsed to zero width |
| H | The selection-capture branch at composition START skipped (always anchors `{cursor, cursor}`) | **caught immediately** — the "composing over a selection" preview showed the wrong splice (only the PREVIEW assertion failed; the separate COMMIT assertion, reading the untouched real `selection_anchor`, still passed — confirming the two paths are genuinely independent) |
| I | `sanitize_insertable_text()` skipped in `text_field_composition_update()` (raw text passed through) | **caught immediately** — both the malformed-UTF-8-repair and the control-character-strip assertions failed, exact-string checks per 4-9's own "assertion-too-weak" lesson |
| J | The composing-suppression guard in `text_field_move()` removed (backspace/delete left intact) | **caught immediately, and isolated correctly** — only the `move` assertion failed in the combined test, confirming the three guards are independent rather than one shared check |

**Tally**: 9 caught immediately (A, C, D, E, F, G, H, I, J); 1 provably
inert (B) — the second occurrence in this project's history of a
negative-input clamp being redundant with a loop's own monotonic
structure, diagnosed rather than merely noted as "survived."

## 11. What is declined, named

- **A candidate-list UI** (section 4) — the platform already draws one on
  the only real IME this project can measure against; building a second
  would duplicate it with no way to verify non-collision.
- **`SDL_HINT_IME_IMPLEMENTED_UI`, left at its default** — probing it
  (section 2) found no behaviour change on this machine's fcitx5/XIM
  bridge; setting it with no observable effect and no consumer for the
  events it would enable would be exactly the "field nobody reads"
  anti-pattern this project already declines elsewhere.
- **`design.md` section 5.13.2's full `ByteOffset`/`Utf16Offset`/
  `GraphemeIndex` type system** — one seam needed a conversion function,
  not a type system (section 6).
- **Windows/macOS IME** — genuinely different APIs (TSF on Windows, Cocoa
  `NSTextInputClient` on macOS), neither touched; this project's window
  layer is Linux/SDL3-only regardless (`window_manager.h`'s own top
  comment).
- **The per-window `FocusManager`/Tab order** — 7-4, immediately next.
  What IME needs from it, named for that slice: composition is currently
  scoped to whichever field `dg::Focus` (the one-`optional<NodeId>` model)
  already names, with no cross-window IME-focus tracking — the identical
  gap `doc/popup.md` section 5 already named for Escape/native-popup
  focus, now also true for composition. A future `FocusManager` should
  make sure a window-level focus change also ends any in-progress
  composition on that window, the way this slice's own
  `text_field_set_focus(false)` already does at the widget level.
- **Multi-line `TextField`/`TextArea` editing, rich-text, clipboard, undo/
  redo, word-boundary double-click, virtual keyboards, handwriting/voice
  input** — all named out of scope by the task, none touched.

## 12. Verification performed

- **Full CTest, once: 30/30** (unchanged count — no new CTest entry;
  `text_input.verify_demo_scene` now also exercises Claim 7).
- **Golden sha256 unchanged**:
  `f635028e6e1e92349d23f0575f1e39e7ca8b05e5974492641ee610fe520df12e` — this
  slice touches no default-rendered scene, confirmed on both g++ and
  clang++ builds.
- **g++ and clang++, one `-Werror` build each**: both clean. Pure-C
  `examples/19_c_client/main.c` unaffected, confirmed still compiled by
  `cc -x c -std=c11` in `compile_commands.json`.
- **`-DDG_SANITIZE=ON`, full 30-entry suite**: green — composition's
  string splicing (`substr`/`+=` inside `focused_display_projection()`)
  and the codepoint-walking `composition_focus_bytes()` are exactly where
  ASan/UBSan earn their keep, including the deliberately hostile
  `INT_MAX`/negative-offset test cases this slice's own unit tests
  construct.
- **clang-tidy `-p build` and clang-format**: exit 0 on every changed
  file, zero `NOLINT`. Two real findings surfaced and were fixed rather
  than suppressed: `text_field_check.cpp`'s `!= ""` rewritten to
  `!...empty()`, and `text_field_refresh_display()`'s cognitive complexity
  (27 against a 25 threshold) brought back under budget by extracting
  `focused_display_projection()` — the identical "split a function that
  crossed the tooling's budget" shape 4-9 already used for
  `dispatch_keyboard()`. **No files were added**, so the hand-maintained
  clang-tidy TU list in `.github/workflows/ci.yml` needed no update this
  slice — every touched file was already on it.
- **`props.*`/theme/ABI lock and drift tests**: unaffected, still green —
  this slice added no property and no ABI surface.
- **Defect injection, 10**: section 10, in full.

## 13. What this is not

No candidate-list UI (section 4, 11). No `ByteOffset`/`Utf16Offset`/
`GraphemeIndex` type system (section 6, 11). No Windows/macOS IME (section
11). No `FocusManager`/Tab order — 7-4's job, with IME's own dependency on
it named (section 11). No multi-line editing, rich text, clipboard, undo/
redo, word-boundary selection, virtual keyboards, handwriting/voice input
— all named out of scope by the task, none touched. **No claim that IME
works end-to-end against a real composing input method** — section 5 is
the honest record of exactly what was and was not verified, and exactly
what would need to change (a different IME/toolkit combination, or a
different platform) to close that gap for real.
