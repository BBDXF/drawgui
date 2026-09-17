# Clipboard: copy/cut/paste/select_all as intents

Slice 8-4. design.md section 5.5.1's own worked example for the intent
system is copy/paste - "应用代码不得直接书写 `Ctrl+C`", it declares
`Intent::Copy` instead. This slice is where that specific intent gets a
real clipboard behind it.

The short version:

- The clipboard seam is `WindowManager::set_clipboard_text()`/
  `get_clipboard_text()` - methods, never free functions.
- `SDL_GetClipboardText()` hands back heap memory the caller must free, and
  returns `""` rather than NULL for every absence case; `std::string` by
  value is what keeps SDL's own allocator entirely out of `include/`.
- Copy/Cut/Paste/SelectAll are the router's third consumer
  (`clipboard_actions.h`), resolved as `action_id`s, never as a hardcoded
  chord anywhere in application code.
- `text_field_insert()` drops ASCII control characters, so pasting a
  multi-line clipboard string into this single-line field silently loses
  the newlines - pre-existing, correct for a single-line field, and this
  slice is simply the first one where a user can trigger it trivially.
- Copy (and cut) with no active selection is a deliberate no-op.
- The X11/Wayland clipboard-ownership model is accepted as-is; a
  clipboard-manager daemon workaround is declined by name.
- Verified empirically under `SDL_VIDEODRIVER=dummy`, including that the
  dummy driver's own clipboard resets cleanly across `SDL_Quit()`/
  `SDL_Init()`; a defect injection returning `""` from
  `get_clipboard_text()` turned 8 of 11 test cases in
  `test_clipboard.cpp` red, which is what makes "these tests are real"
  a checked fact rather than an assumption.

---

## 1. Why the seam is `WindowManager` methods, not free functions

`set_clipboard_text()`/`get_clipboard_text()` live on `WindowManager`
rather than as standalone functions taking no object at all. SDL's own
clipboard calls (`SDL_SetClipboardText`/`SDL_GetClipboardText`) require the
video subsystem to already be initialised, and `WindowManager` is the only
thing in this codebase that guarantees that (its own constructor is what
calls `SDL_Init`). A free function would compile fine and, called before
any `WindowManager` existed, would either crash or silently do nothing
depending on SDL's own uninitialised-subsystem behaviour - a **runtime**
failure a caller could easily not notice until it hit exactly that ordering
bug. A method on `WindowManager` makes the dependency a **structural**
fact instead: there is no way to call `set_clipboard_text()` at all without
already holding a live `WindowManager`, so the ordering question the free
function would have left silent cannot arise.

## 2. `SDL_GetClipboardText`'s ownership, and why `std::string` by value

`SDL_GetClipboardText()` returns a `char*` the caller owns and must
`SDL_free()` - documented in `SDL_clipboard.h` itself, and this is the one
call in `window_manager.cpp` that takes ownership of memory the SDL
allocator, not this project's own, handed out. `get_clipboard_text()`
copies it into an owned `std::string` and frees the SDL buffer inside the
same function, before returning - the freeing happens exactly once, at the
one call site that knows the allocator responsible for it, rather than
being pushed onto every caller as an "and now free it" step they could
forget.

Returning `std::string` by value, rather than the raw pointer or a
`std::string_view` over it, is also what keeps SDL's allocator entirely
out of `include/drawgui/window/window_manager.h`: nothing in this header
mentions `SDL_free`, an SDL type, or an ownership contract a caller has to
honour. The header's own comment on `set_clipboard_text()` states the
matching half of this rule directly - `std::string_view` in, never a
`const char*` reinterpreted from an SDL type, so no SDL type crosses this
boundary in either direction.

`SDL_GetClipboardText()`'s documented behaviour is to return an empty
string, not NULL, when there is nothing to paste - verified against the
installed SDL3 header rather than assumed from SDL2's differently-shaped
API (SDL2 could return NULL; SDL3 does not). `get_clipboard_text()` still
guards against a null return defensively, but the case it actually has to
handle in practice is the empty string SDL3 hands back for every "nothing
here" case: nothing was ever copied, or an X11/Wayland source application's
clipboard ownership has lapsed (see section 5). A caller gets the
identical empty answer for every one of these, never a crash or a null
dereference - which is the actual guarantee this method exists to make.

## 3. Intents, never a hardcoded chord

Copy, Cut, Paste and SelectAll are four generated `action_id`s
(`DG_ACTION_COPY`/`CUT`/`PASTE`/`SELECT_ALL`, `input/shortcuts.toml` ids
1-4), resolved by `dg::route_key_event()` and applied by
`apply_clipboard_action()` (`clipboard_actions.h`) - the router's third
consumer, after 8-3c's keyboard scrolling. Nowhere in this path does any
code compare a key against `Ctrl+C` directly; a chord resolves to an
`action_id`, and the `action_id` is what every consumer downstream acts on.

design.md's own reason for this indirection is not aesthetic. macOS's real
chord for copy is `⌘C`, not `Ctrl+C`, and the platform's own binding table
- not application code - is what is supposed to know the difference. A
project that hardcoded `Ctrl+C` would be wrong by construction the moment
it ran on macOS, and the failure would not show up as a crash or a test
failure on the platform where it was written; it would show up as a
silently unreachable feature on the one platform where it mattered. The
`Mod` substitution (`doc/shortcuts.md` section 3) is what lets this
project state the intent once and get both chords for free, tested today
against the Linux backend that exists.

All four share `ActionScope::kTextField` - they only mean something with a
focused text field - which is also why `select_all` is implemented inside
`clipboard_actions.cpp` alongside copy/cut/paste rather than in a file of
its own: `input/shortcuts.toml`'s own `consumer` field for `select_all`
names both "8-4 clipboard" and "TextField," and this is where that pairing
is realised as one function, one call site, over the one scope all four
register under.

## 4. `text_field_insert()` drops control characters - and paste is the first slice that can trigger it trivially

`WidgetSet::text_field_insert()` filters ASCII control bytes (`0x00`-
`0x1F`, `0x7F`) out of whatever it inserts. This is **not** new behaviour
this slice introduces - it predates 8-4 entirely, and is the correct
choice for a field that is, and has always been, single-line
(`doc/text-input.md`'s own scope boundary). A newline typed by hand simply
cannot arrive at this filter in the first place, because nothing before
8-4 offered a way to type or paste one.

Paste changes that. `apply_clipboard_action(DG_ACTION_PASTE, ...)` reads
the platform clipboard's text unfiltered and hands it to
`text_field_insert()` unchanged - so pasting text copied from a real
multi-line source, with a real `\n` in it, silently loses every line break
the same filter always dropped, and what remains is every line
concatenated with none of the separators. This is correct for a
single-line field: there is nowhere for a second line to go. It is named
here, and asserted by an explicit test case in `test_clipboard.cpp`, rather
than left to be rediscovered as a surprising defect later, because 8-4 is
the first slice where a user can trigger it in one paste rather than by
typing control characters no keyboard sends. Fixing it needs a multi-line
text field, which this project does not have; there is nothing this slice
can do differently that would change the outcome without building one.

## 5. Copy (and cut) with no selection: a deliberate no-op

`apply_clipboard_action(DG_ACTION_COPY, ...)` and the `CUT` case both check
`text_field_selection()` first; with no active selection, both return
`false` and leave the clipboard exactly as it was.

The alternative some toolkits implement - copying the whole field's text
when nothing is selected - is real and considered, but declined by name:
design.md names no such rule, and this project already gives a user an
explicit way to select everything (`select_all`, the fourth action in this
same table) when that is what they want. Inventing a second, implicit
"select everything" behaviour behind Copy would surprise a user who
already has the explicit tool for it, for no behaviour design.md actually
asks for. Cut shares the identical reasoning: no selection, nothing
happens, the clipboard is untouched - the same "was this worth doing"
boolean signal `WidgetSet::scroll_by()`/`set_slider_value()` already give a
caller for their own no-op cases.

## 6. The X11/Wayland ownership model, and the daemon workaround declined by name

The clipboard on X11/Wayland is served lazily: the "owner" of the
clipboard selection is whichever process last copied something, and that
process is asked to hand over the text only when something else pastes.
If the copying process has since exited, the clipboard silently has
nothing to serve - `get_clipboard_text()` returns the same empty string it
returns for "nothing was ever copied," because from this project's side of
the API, both are indistinguishable and both need the identical safe
answer (section 2).

A **clipboard-manager daemon** (a background process that intercepts every
copy and re-serves it even after the original application exits, the way
`xclip -selection clipboard -o` chains or a desktop's own clipboard history
service work) would close this gap, but is declined by name for this
slice: it is a system-level service this project does not own, would add
a real runtime dependency this project's own build has no reason to carry
for a GUI kernel, and no example or test in this project actually needs
clipboard content to survive past the copying process's own lifetime.

## 7. Verified under `SDL_VIDEODRIVER=dummy`, and proven non-vacuous

`test_clipboard.cpp` forces `SDL_VIDEODRIVER=dummy` before constructing
every `WindowManager` in the suite, so no test in this project ever
touches the machine's own real desktop clipboard. This slice's own
empirical finding, checked against the installed SDL3 before writing the
test file, is that the dummy driver's own clipboard state resets cleanly
across an `SDL_Quit()`/`SDL_Init()` cycle - each `TEST_CASE` constructs its
own `WindowManager`, so "nothing was ever set" in one test case and "a
stale value left behind by the previous test case" cannot be confused with
one another, which sharing one process-wide clipboard across the suite
would have risked.

That the suite is real, and not eleven test cases that would pass even
against a broken implementation, was checked directly rather than assumed:
patching `get_clipboard_text()` to unconditionally `return {};` and
rebuilding turned **8 of the 11** test cases in `test_clipboard.cpp` red
(round-trip, both copy variants, both cut variants, both paste variants,
and the control-character paste case), with only the three cases that
never read the clipboard back (the empty-clipboard check, and both
`select_all` cases) still passing. That is the same defect-injection
discipline `doc/shortcuts.md` records for the router's own IME isolation -
a claim of test reality checked by breaking the thing under test and
watching red, not merely asserted.
