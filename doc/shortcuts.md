# The shortcut/intent system: `action_id`, `LogicalKey`, four-level routing

Slices 8-1, 8-2, 8-3, 8-3b, 8-3c, 8-3d. design.md section 5.5 names the whole
mechanism up front: application code declares an intent (`Intent::Copy`), a
per-platform table resolves it to a real chord, and a fixed routing order
(IME, then bubble from focus, then window, then app) decides which level
consumes a key. This batch is the slice that built it.

The short version:

- A fourth generated-id family (`input/shortcuts.toml` + `tools/gen_shortcuts.py`
  + `input/action_ids.lock` + `tools/shortcut_lock.py`), same shape as
  `props/` and `themes/` before it: `action_id` is a plain, ABI-crossing
  `uint16_t` constant, append-only, locked.
- `LogicalKey` is a real generated `enum class`, deliberately not the same
  kind of id as `action_id` - and deliberately not the same enum as `Key`.
- The `Mod` pseudo-modifier lets one table row cover three platforms with
  only a Linux backend to test it against.
- Every `[[action]]` requires a `consumer` field. The generator refuses to
  build without one.
- `Home`/`End` collide between an app-scoped action and a `TextField`'s own
  editing key, and the resolution is not scope priority - it is a bypass
  that never lets the router see the key at all.
- IME isolation is an early return proven by defect injection, not an `if`
  someone has to remember to check.
- `KeyEvent` gained `Modifier mods` and `LogicalKey logical_key`,
  replacing a single `bool shift` across roughly 22 call sites.
- Declined by name: routing level 3 (window table), `dg_app_bind_shortcut`,
  physical scancode chords, horizontal keyboard scrolling.

---

## 1. Two id types, two different reasons

`action_id` and `LogicalKey` both come out of the same generator run, and
they are deliberately not the same kind of thing.

`action_id` is a plain `uint16_t` constant (`DG_ACTION_COPY`, `DG_ACTION_
SCROLL_PAGE_UP`, ...), matching `prop_id` and `token_id` before it
(design.md section 5.5.3: "同源同构" with the other two families). The
reason is the same reason those two are constants and not enums: it
crosses the C ABI. A host language that only speaks C has no way to name a
C++ `enum class` value; a `uint16_t` with a generated `#define` is the only
shape every host can hold. `dg_node_scope_action(dg_node_t*, uint16_t
action_id)` takes exactly this type for exactly this reason.

`LogicalKey` is a real `enum class`. It is never handed across the ABI as
an arbitrary host-supplied number the way `action_id` is - a `KeyEvent`
carries one, produced entirely inside `dg::WindowManager` from an SDL
scancode/keycode pair, and consumed entirely inside `dg::route_key_event()`.
Nothing external ever needs to construct one from an integer it invented,
so nothing forces it into a plain-constant shape. Being closed and internal
is also what lets the generator emit only the key names something in
`input/shortcuts.toml` actually references - "no enumerator without a real
consumer" becomes mechanical for `LogicalKey` the same way it is enforced
by hand for `Key` (`dg::Key` already declines to carry a key nothing
routes; `LogicalKey` gets the identical discipline for free from the
generator instead of by convention).

## 2. `Key` and `LogicalKey` stay separate enums

design.md section 5.5.2 states the rule plainly: "文本编辑按键不是快捷键"
- a text-editing key is not a shortcut. `dg::Key` (`window_manager.h`)
names the small, fixed set of keys a focused `kTextField` consumes directly
as editing intents: `kLeft`/`kRight`/`kHome`/`kEnd`/`kBackspace`/`kDelete`,
plus a few structural ones (`kTab`, `kEscape`, `kEnter`, `kUp`/`kDown`,
`kOther`). `LogicalKey` names the (larger, generated) set of keys the
shortcut table binds chords to.

Unifying them was considered and declined. The two enums answer different
questions - "what should this focused text field do with this key" versus
"what intent does this chord resolve to" - and design.md's own routing
order (section 5.5.2) already has these two questions asked by different
code at different points in the pipeline, never both against the same
value. Merging them would not remove a real duplication; it would force
one enum to serve two call sites that need to evolve independently (a new
generated action can add a `LogicalKey` value with no `TextField` editing
behaviour ever caring, and vice versa).

## 3. The `Mod` pseudo-modifier

Most rows in `input/shortcuts.toml` need to express exactly one thing:
"the platform's primary modifier plus this key," covering Windows/Linux
(`Ctrl`) and macOS (`Cmd`) in one line. `Mod+C`, `Mod+X`, `Mod+V`, `Mod+A`
are the four clipboard actions; every one is written once.

`tools/gen_shortcuts.py` substitutes `Mod -> Ctrl` for the `linux`/`windows`
columns and `Mod -> Cmd` for `macos`. This is a **data substitution rule
inside a generator that already has to exist for the one backend this
project builds** - not hand-written, untested macOS application code. That
distinction is what makes it honestly testable today with no macOS machine
anywhere in this project: `tests/unit/test_shortcuts.cpp` exercises the
substitution directly, checking what the generator produces for the
`macos` column even though nothing runs on macOS.

What the substitution cannot do is stand in for a macOS override that
is not derivable from a shared `Mod+<key>` row at all. Two are named and
declined in `input/shortcuts.toml`'s own header comment, by name:

- macOS's `Ctrl+A`/`Ctrl+E` meaning line-start/line-end (a system-level
  Emacs binding, entirely unrelated to this project's `select_all`).
- macOS's `Option+Delete` for "delete previous word," where Windows/Linux
  use `Ctrl+Backspace`.

Both are `TextField` editing-intent overrides (design.md section 5.5.2's
own category), not app/clipboard shortcuts, so neither has a row in this
table to override in the first place - and there is no macOS backend yet
to test either override against. They are recorded so a future macOS
backend slice does not have to rediscover them from scratch.

## 4. The mandatory `consumer` field

Every `[[action]]` in `input/shortcuts.toml` requires a `consumer` string.
`tools/gen_shortcuts.py` refuses to generate anything if one is missing.

This project's standing rule against speculative vocabulary - do not add
an enumerator, a property, a token nothing consumes - has so far lived as
a convention someone has to remember and a reviewer has to catch. The
`consumer` field turns it into something the generator itself checks: an
action with no named consumer is a build failure, not a code-review
comment. `input/shortcuts.toml`'s own rows name their consumers plainly -
`select_all`'s consumer field names both "8-4 clipboard" and "TextField,"
which is also why `apply_clipboard_action()` (`clipboard_actions.cpp`) is
where `select_all` is implemented rather than in a router-adjacent file of
its own (see `doc/clipboard.md`).

## 5. The `Home`/`End` collision, and its real resolution

This is the most interesting finding in the batch.

`scroll_to_start` (`DG_ACTION_SCROLL_TO_START`) is bound to `Home` at
`ActionScope::kApp`. A `TextField` also treats `Home` as "move caret to
line start" - and does so today, unconditionally, independent of any
generated table at all (`examples/12_text_input`'s `Runner::handle_key`
already dispatches on `Key::kHome` directly, long before this batch).
With a `TextField` focused, both readings of the same physical key are
live at once.

The resolution is **not** scope priority. It would be tempting to say
"level 2 (the focused node's own scope) wins over level 4 (the app table),
so a `TextField`'s own line-start binding beats `scroll_to_start`" - but
that framing is wrong, because a `TextField` never registers a
`Home`-bound action at any scope in the generated table at all. There is
no competing binding for the router to arbitrate between.

The real mechanism is design.md section 5.5.2's own rule, restated:
"文本编辑按键不是快捷键" - a text-editing key is not a shortcut. When the
focused widget is a `kTextField` and the key is one of the fixed editing
set, `route_key_event()` (`src/shortcuts/router.cpp`) returns
`KeyRouteOutcome::kEditingIntent` in an early return, **before**
`resolve_action()` - the function that would otherwise find and fire
`scroll_to_start` - is ever called. The shortcut table is never consulted.
This is a structural bypass, not a priority rule, and the two are tested
in both directions in `test_shortcut_routing.cpp`:

- A focused `TextField` receiving `Home`: `kEditingIntent`, no action
  resolved, `scroll_to_start` never fires.
- Nothing focused, or a focused node that is not a text field: `Home`
  routes normally and fires `DG_ACTION_SCROLL_TO_START` exactly once.

## 6. The limitation already recorded in `router.cpp`, and worth repeating here

`is_text_editing_intent()` (the function that implements the bypass above)
reads `Key key` and never looks at `event.mods`. That means a focused
`TextField` swallows `Mod+Home` exactly the same way it swallows bare
`Home` - the bypass fires on the key alone, unconditionally on any
modifier state.

This costs nothing today: nothing in `input/shortcuts.toml` binds a
modified form of `Home`/`End`/`Left`/`Right`/`Backspace`/`Delete` at any
scope, so there is no `Mod+Home`-shaped action for the omission to
silently break. But the day a future slice binds an app-scoped `Mod+Home`
- a "jump to document start" distinct from `Home`'s own line-start - it
will silently never fire while any `TextField` has focus, because the
bypass will still trigger on the bare key and never inspect the modifier
that would have disambiguated the two. `router.cpp`'s own comment names
the fix in advance: consult `mods` inside `is_text_editing_intent()` when
that day comes, not a special case at the call site.

## 7. IME isolation as control flow, not a checkable `if`

Level 1 of the routing order - "IME 组字中？→ 全部事件交给 IME" - is
implemented as the first thing `route_key_event()` does: if the focused
node is composing (`ctx.widgets.text_field_is_composing(*focused)`), the
function returns `KeyRouteOutcome::kIme` immediately, before
`resolve_action()` is named anywhere below it in the source.

The reason this is an early return rather than a flag `resolve_action()`
itself consults is that it makes the router **structurally unreachable**
during composition, rather than merely correct as long as every call site
remembers to check a flag first. This was not merely asserted - it was
shown: `test_shortcut_routing.cpp`'s defect-injection test scopes
`DG_ACTION_SELECT_ALL` onto a composing field, sends the exact `Mod+A`
chord that scope would resolve, and confirms the outcome is `kIme` with no
action resolved. Deleting the early return by hand and re-running the same
test turns it red - the router visibly reaches level 2 and fires
`select_all` mid-composition, which is exactly the class of bug this
isolation exists to prevent. The same test also checks `Escape`, which
carries no shortcut binding of its own but must still be swallowed by the
candidate window during composition rather than falling through to a
no-op router pass.

## 8. `KeyEvent`'s shape change (8-2)

`KeyEvent` gained `Modifier mods` (a bitmask covering `kNone`/`kShift`/
`kMod`/etc.) and `LogicalKey logical_key`, replacing a single `bool shift`.
The compiler, not a search-and-replace, found every call site: removing
`shift` from the struct turned every read of it into a build error, which
is how roughly 22 call sites across the widget/window/example layers were
migrated - each one converted to read `mods` (or the specific bit it
actually needed) rather than left half-migrated with a stale boolean
sitting next to the new field.

## 9. What this batch declined, by name

- **Routing level 3 (the window-level table).** design.md's own routing
  order names it - a window menu, a dialog's default button - but no
  action in `input/shortcuts.toml` is scoped to a window today and no
  caller needs one; `router.cpp`'s own comment marks the gap rather than
  building `ActionScope::kWindow` speculatively. Prerequisite: a real
  window-scoped action.
- **`dg_app_bind_shortcut`** (design.md section 5.5.3 names four ABI
  functions; this batch built three). Prerequisite: a host application
  needing to bind a chord the generated table does not already cover -
  nothing in this project's own examples does yet.
- **macOS-specific non-`Mod`-derivable overrides** (section 3 above),
  prerequisite: a macOS backend to test either override against.
- **Physical scancode chords.** design.md section 5.5.1 mentions layout-
  independent scancode binding as a future option for WASD-style games;
  every binding in this table today matches on the layout-dependent
  logical key, and nothing in this project needs the alternative yet.
- **Horizontal keyboard scrolling.** 8-3c's `apply_keyboard_scroll()`
  only ever touches the vertical axis (`PageUp`/`PageDown`/`Home`/`End`);
  no scene in this project has a horizontally-scrolling viewport a key
  could usefully target.
