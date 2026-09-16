# Dropdown, and everything found while trying to build Menu/Tooltip/Dialog alongside it

Slice 7-5. `doc/form-controls.md` section 2 declined Dropdown outright, naming
one prerequisite: a `PopupHost` abstraction with a `Popup` window kind and a
`PlatformCaps` capability query. 5-2 (`doc/popup.md`) built exactly that,
named Dropdown as the next slice's job, and deferred Context menu, Tooltip
and `Dialog` alongside it. This is the record of building Dropdown, settling
design.md section 12 open question 6 (`Table`/`RenderGrid`) before touching
any code, and finding - not assuming - that the other three controls each
carry a genuinely NEW prerequisite of their own, none of which existed
before this slice looked.

The short version:

- **design.md section 12 question 6, settled**: `Table` composes from
  `kRow`/`kColumn` with zero new primitives when column widths are
  caller-declared; it genuinely needs two-dimensional layout only for
  auto-sized columns that must agree in width across every row - the one
  thing Flex's per-node, sibling-blind sizing cannot express. No working
  caller needs the second case (not this slice's four controls, not any of
  the 19 slices before it), so `Table` is not built. See section 1.
- **Dropdown is the 9th `WidgetKind`, `kDropdown`** - an anchor plus a
  caller-declared option list plus a selected index that outlives the
  click that set it, the identical "earns a kind by needing state no
  existing kind has" argument `doc/form-controls.md` made for `kSlider`.
  Zero new `RenderObject`/node kinds - the streak now extends to a
  **twentieth** consecutive slice. See section 2.
- **`kList` was evaluated and NOT reused for the option rows.** Its pool
  nodes carry no attached `Widget` at all, so they cannot be focused,
  clicked, or keyboard-highlighted without inventing a second interaction
  layer on top of it - the option rows are ordinary `kButton` widgets in a
  plain `kColumn`-shaped stack instead, `doc/form-controls.md` section
  2.3's own settled finding applied literally. See section 3.
- **Keyboard highlight navigation (Up/Down) is `dg::Focus::focus_next()`/
  `focus_previous()`, unchanged** - the identical traversal Tab already
  performs over a popup's scope (7-4), routed by two new keys instead of
  Tab/Shift-Tab. No highlight-cursor field was added anywhere. See
  section 4.
- **Context menu, Tooltip and `Dialog` are NOT built this slice.** Each was
  checked, not assumed buildable, and each turned out to need a real,
  previously-nonexistent prerequisite of its own - the identical shape
  `doc/form-controls.md` used to decline Dropdown itself. Named in full,
  with the concrete follow-up, in section 6.
- Two small, contained platform additions were needed and are shared
  infrastructure now: `Key::kUp`/`kDown`/`kEnter` (menu navigation had no
  keys to navigate with at all before this slice - `Key::kEscape`/`kTab`
  were the only "non-text-editing" keys that existed). See section 5.

---

## 1. design.md section 12 question 6: `Table`/`RenderGrid`, decided before writing any widget code

The task required a decision here before anything else, matching the
project's own precedent (7-1 resolved section 12 question 5 the same way -
in place, with a strikethrough and a resolution note, not a new document).
The resolution is now in `design.md` section 12 itself; this section
restates the reasoning in full, because it is what actually governs whether
`Table` gets built in this slice (it does not).

**The check, run the same way `doc/form-controls.md` ran it for Dropdown**:
does this project's existing primitive set already answer the question, or
is a genuinely new mechanism required? Two sub-cases, and they have
different answers:

- **Fixed column widths, declared by the caller.** A table row is an
  ordinary `kRow`; each cell is an ordinary child with an explicit `width`
  matching whatever the caller declared for that column. This is `Row`/
  `Column` composition, the identical shape `doc/scrolling.md` section 1
  already proved for `List` ("a list is not a widget, it's a `kColumn`/
  `kRow` of ordinary children") - `Table`'s fixed-width case needs nothing
  a widget author cannot already build today, with zero new primitives.
- **Automatic column widths that must AGREE across every row** (the
  `<table>` element's own default behaviour: column N's width is the
  widest cell in column N, over every row). This is genuinely NOT
  expressible by Flex, and the reason is structural rather than a missing
  feature: `design.md` section 5.4.1's own invariant L1 ("a node's size
  depends only on its own constraints and its own properties, never on a
  sibling's content") holds for each row's `kRow` in isolation, but
  "column N's width = max over every row's cell N" is a computation that
  has to see every row's content BEFORE any row can be given its final
  column widths - a shared pre-pass across siblings that a per-node,
  exactly-once layout pass (`design.md` section 5.4.1's L3) cannot express
  without becoming two-dimensional. This is precisely the CSS Grid problem
  `design.md` section 12 question 6 already named, not a gap in this
  project's own Flex implementation.

**Decision: `Table` is not built this slice.** The fixed-column-width case
needs no new primitive and is deferred only because nothing in this
slice's own scope (Dropdown/Menu/Tooltip/Dialog) asked for it - a future
slice builds it the day a real caller needs it, per this project's own
standing rule against building ahead of a working caller. The
auto-column-width-agreement case genuinely needs two-dimensional layout,
and `design.md` section 5.4.11's own trigger condition applies verbatim:
"if CSS Grid is genuinely needed, re-evaluate Taffy rather than
self-build" - not something to rush into this slice's already-large scope,
and not something any of this project's 19 prior slices, or this one, has
had a real caller ask for. Deferred with a decided answer, not left open.

## 2. Dropdown: the 9th `WidgetKind`, argued the same way `kSlider`/`kList` earned theirs

### 2.1 Why a new kind, not a field on `kButton`

`doc/form-controls.md` section 1.2's own bar, restated and applied here:
"a fifth [now ninth] kind is earned by needing state or behaviour no
existing kind has." A dropdown needs exactly one thing `kButton` has
nowhere to keep: **which option is currently selected, once the popup that
showed the choice has already closed.** This is the identical shape
`kCheckbox`'s `checked` and `kSlider`'s `value` already are - state that
OUTLIVES the interaction that produced it, which is precisely
`doc/form-controls.md`'s own stated reason `kSlider` was not folded into
`kCheckbox`. `Widget::options` (the caller-declared choice list) and
`Widget::selected_index` (`std::optional<int>`, runtime state for the
identical reason `checked`/`value` already are - it accumulates across a
click, not declared once) are the whole of the new state; `Widget::label`
is a plain positioned child exactly the shape `kTextField::content`/
`kCheckbox::indicator`/`kSlider::thumb` already are.

**What is deliberately NOT on `Widget`**: which popup is currently open (
`PopupHost`'s own handle already owns that, matching every other popup
client - `doc/popup.md`), the option ROW node ids (built fresh every time
the popup opens, the same "content is the caller's job" boundary `kList`'s
pool already draws a line at), and a keyboard-highlight cursor (section 4
below - `dg::Focus` already tracks it, so a second copy would disagree
with the first the day they diverge).

### 2.2 Zero new `RenderObject`/node kinds - the streak

`kDropdown`'s anchor is a plain box (`kLeaf`) plus one plain text child,
identical in shape to `kButton`'s own label convention. Opening a popup
routes entirely through `PopupHost` (5-2), which already proved it needs
no new `RenderObject` either. The streak `doc/completeness.md` measured
through 4-9 and every slice since (7-4's own count: nineteen consecutive
slices) extends to a **twentieth**: `Box`/`Text`/`Button`/`Checkbox`/
`Radio`/`ScrollView`/`Slider`/`TextField`/`Image`/`PopupHost`/`List`/
gradient+shadow+`set_image`/`AnimationEngine`/theme tokens/the C ABI/
multi-line text/grapheme-cluster editing/IME composition/focus/
**Dropdown**.

### 2.3 `doc/form-controls.md` section 2.4's three prerequisites, checked one at a time

Named precisely by 4-8, confirmed satisfied one at a time rather than
assumed satisfied because 5-2 landed:

1. **"`WindowSpec` (or an equivalent) gaining a `kind` field, with `Popup`
   at minimum."** Satisfied by 5-2: `WindowManager::open_popup()` is a
   dedicated entry point (not a `WindowSpec::kind` field, but the
   functional equivalent - a caller never constructs an ordinary window
   and asks it to behave like a popup) that creates exactly this kind of
   window. Confirmed still true today: `open_popup()`'s own signature is
   unchanged by this slice.
2. **"The SDL3 backend implementing whatever `SDL_WINDOW_POPUP_MENU`/
   positioning API a borderless, owner-relative popup window needs."**
   Satisfied by 5-2 and re-confirmed by this slice's own
   `dropdown_check.cpp`: a real `SDL_CreatePopupWindow` attempt is made
   (and, as expected under `SDL_VIDEODRIVER=dummy`, fails loudly and
   specifically - section 7 below), and `examples/22_dropdown_menu`'s
   interactive mode drives the real thing against an actual display.
3. **"A `PlatformCaps`-shaped capability query... so a future mobile
   backend... can report that and let a `PopupHost` fall back to an
   overlay layer."** Satisfied by 5-2: `WindowManager::platform_caps()`
   and `PlatformCaps::native_popup`, both unchanged and both exercised by
   this slice's own overlay/native branch split (`--branch overlay`
   forces the fallback path this prerequisite exists for, on the same
   desktop, exactly as `examples/14_popup`/`examples/21_focus` already
   established).

All three: satisfied by 5-2, confirmed rather than assumed by this slice's
own tests. `doc/form-controls.md`'s own decline is now closed in full, not
merely "unblocked in principle."

## 3. Why `kList` was evaluated and not reused for the option rows

design.md names `List` and `Table` together as the MVP controls requiring
"built-in virtualization... part of the control's own definition" (section
5.6 line 617), which invites the question directly: is a dropdown's option
list the client `kList`'s own doc named as the obvious next consumer
(`doc/list.md`'s closing line)? Checked, not assumed:

- **`kList`'s pool nodes carry no attached `Widget` at all.** 5-3's own
  recycled slots are plain, content-bearing children - never registered
  with `dg::Interaction`, never `is_focusable()`. A dropdown's rows need
  to be clicked, Tab/Up/Down-reachable, and keyboard-highlightable - none
  of which a `kList` pool slot can do without a second, parallel
  interaction layer built on top of it. That second layer is exactly the
  "new machinery this slice's acceptance criteria did not ask for"
  `doc/form-controls.md` section 8 already declined once, for a slider's
  hover glow, for the identical reason.
- **The measured virtualization threshold does not apply here anyway.**
  `doc/list.md` measured the unvirtualized baseline staying under a 60fps
  frame budget up to roughly 100,000 items, with the crossing point
  somewhere past that. A dropdown's option count in any of this project's
  own scenes, and in any realistic UI, is nowhere near six orders of
  magnitude close to that line.
- **The right composition is the one `doc/scrolling.md` section 1 already
  settled**: "a list is not a widget, it's a `kColumn`/`kRow` of ordinary
  children" - `examples/22_dropdown_menu/dropdown_options.cpp` builds
  exactly that, one ordinary `kButton` row per option, stacked in a plain
  `kColumn`-shaped layout (device-pixel arithmetic, not `LayoutTree`,
  matching `PopupHost`'s own overlay-branch precedent of building popup
  content directly on `RenderTree`).

Scrolling a long option list, should one ever need it, composes for free
the identical way `doc/scrolling.md` already proved for any other content
- nothing about this decision forecloses it, and nothing in this slice's
own scene needed it (five options, all visible without scrolling).

## 4. Keyboard navigation: Up/Down are Tab's own traversal, not a new mechanism

The task asked for arrow-key navigation and Enter to activate, and the
obvious naive design is a new "highlighted option index" field somewhere.
Built instead, after checking what already exists: **`dg::Focus::
focus_next()`/`focus_previous()`, called with Up/Down instead of Tab/
Shift-Tab, over the popup's own scope** (`enter_scope(popup->content_root)`,
7-4's own mechanism, unchanged). This is not an analogy - it is the
IDENTICAL function call:

```cpp
if (event.key == dg::Key::kDown) {
  active_focus().focus_next(active_tree(), active_widgets(), handle.content_root);
}
```

Three consequences fall out for free, none of them separately built:

- **Wraparound in both directions** - `focus_next()`/`focus_previous()`
  already wrap at both ends (7-4's own documented, deliberate choice,
  "design.md names no wrap-or-stop choice... every desktop toolkit this
  project's MVP-8 is modelled on wraps, so this does too"). Up from the
  first option reaching the last, and Down from the last reaching the
  first, needed zero new code - `dropdown_check.cpp`'s own wraparound
  assertions are checking 7-4's mechanism from a new caller, not new
  logic.
- **The focus ring already highlights the option under keyboard
  navigation** - `dg::update_focus_ring()` is called after every Up/Down,
  the same call every other focus change in this project already makes.
  No second "highlighted" visual state was built.
- **Enter reads `dg::Focus::current()` directly** - there is no
  highlight-cursor field to read instead, so there is nothing for the two
  to disagree about.

**What this means for "does modal trapping use 7-4's scopes"**: Dropdown's
OWN Tab confinement is `enter_scope()`/`exit_scope()`, unchanged from 7-4 -
this is not new. `Dialog`'s MODAL trap (refusing a click or a
programmatic `set()` from escaping, not merely Tab) is a strictly larger
mechanism 7-4 explicitly did not build and named as this slice's job
(`doc/focus.md` section 4: "the smaller mechanism a modal trap could be
built ON TOP of, not the trap itself") - and section 6 below is why this
slice does not build it either, rather than silently reusing `enter_scope`
as if it already were one.

## 5. Two small platform additions, now shared infrastructure

**`Key::kUp`/`kDown`/`kEnter`.** Before this slice, the only "non-text-
editing" keys this engine's `Key` enum carried were `kEscape` (`PopupHost`'s
own dismissal key, 5-2) and `kTab` (7-4). Arrow-key menu navigation had
literally nothing to navigate with - the identical "only what has a real
consumer" policy this enum's own header comment already states, applied
here as the reason these three were absent before a real consumer
(Dropdown) existed for them, not an oversight. Added to
`include/drawgui/window/window_manager.h`'s `Key` enum and
`src/platform/sdl3/window_manager.cpp`'s `to_key()`/`from_key()` (mapping
`SDLK_UP`/`SDLK_DOWN`/`SDLK_RETURN`/`SDLK_KP_ENTER`), and to every existing
EXHAUSTIVE switch over `Key` this addition made non-exhaustive
(`examples/21_focus/focus_scene.cpp`, `examples/12_text_input/
text_field_window.cpp`) - each gained a `break;` case, no behaviour change,
confirmed by the full CTest suite staying green throughout.

**Nothing else in the platform layer changed.** `PopupHost`, `WindowManager
::open_popup()`, `PlatformCaps` are all byte-for-byte what 5-2 built -
Dropdown needed none of them extended.

## 6. Context menu, Tooltip, `Dialog`: checked, not assumed, each declined by name with its own real prerequisite

The task asked, explicitly, whether the pointer plumbing even reports a
right mouse button before assuming a context menu could be built. It was
checked, and the answer generalizes to all three remaining controls: each
one, examined the same way `doc/form-controls.md` examined Dropdown itself,
turns out to need a genuinely new, previously-nonexistent prerequisite -
not merely "more of the same composition Dropdown just proved."

### 6.1 Context menu: the pointer plumbing has no button identity AT ALL

`include/drawgui/window/window_manager.h`'s own `PointerEvent`/
`PointerAction` carry no button field whatsoever, and the header says why,
verbatim: "Four actions, and no button identity. Only the PRIMARY button
produces an event at all - the backend drops the others, because nothing
routes them... **a right-click cannot activate a widget here, not because
the state machine checks, but because the event does not exist.**"
Confirmed in `src/platform/sdl3/window_manager.cpp`: `if (event.button.
button != SDL_BUTTON_LEFT) { /* dropped here */ }`, and
`post_pointer_button()`'s own synthetic-injection helper hardcodes
`SDL_BUTTON_LEFT` unconditionally.

This is the IDENTICAL shape `doc/form-controls.md` section 2.4 used to
decline Dropdown itself: not "harder to build," but "the platform-layer
event this needs does not exist yet." A context menu is not a widget-layer
gap - it needs, at minimum: a button-identity field on `PointerEvent`/
`PointerAction` (or an equivalent), the SDL3 backend passing
`SDL_BUTTON_RIGHT` through instead of discarding it, and a routing
decision for right-click (does it change hover/press state the way a left
click does, or is it a parallel, non-activating channel that only ever
opens a menu - a genuine design question this slice did not settle because
it never got past the first, purely mechanical blocker). **Named as a
platform-layer prerequisite for a follow-up slice**, matching `doc/
form-controls.md`'s own precedent for Dropdown's `PopupHost` prerequisite
exactly - closing this is a full task in its own right, not a
`PopupHost`-only composition the way Dropdown turned out to be.

### 6.2 Tooltip: `SDL_WINDOW_TOOLTIP` is a real, un-plumbed flag, and the hover delay needs a decision `WidgetSet` does not have a home for yet

`doc/popup.md` section 1 already measured, and named by name, that
`SDL_WINDOW_POPUP_MENU` (what `WindowManager::open_popup()` always passes)
and `SDL_WINDOW_TOOLTIP` are genuinely different SDL capabilities -
`kPOPUP_MENU` can gain keyboard focus (which is what lets Escape reach it,
the exact mechanism Dropdown/the existing `Popup` kind depend on);
`kTOOLTIP` accepts **no input at all**, which is the CORRECT choice for a
tooltip and the WRONG one for a menu (5-2's own words, restated because
this slice re-confirmed rather than re-derived them: `doc/popup.md`
section 6 already declined the tooltip variant by name for exactly this
reason). `open_popup()` hardcodes `SDL_WINDOW_POPUP_MENU` with no way for
a caller to ask for the other flag - **this is a real, contained platform
gap**, smaller than context menu's (no new event type, just a second flag
choice threaded through `WindowManager`/`PopupHost`), but a gap
nonetheless, checked directly in `src/platform/sdl3/window_manager.cpp`
rather than assumed absent.

**The hover-delay timing question was investigated, not left unexamined**:
6-1's `dg::AnimationEngine`/`AnimTime` clock DOES serve this need - a
tooltip's "show after N ms of continuous hover" is a plain `AnimTime`
timestamp comparison (record `steady_anim_time()` at hover-start, compare
against it on each subsequent frame), needing no new engine feature at
all, the identical conclusion 6-1's own caret-blink client already proved
for a different chained-animation use. **What is missing is not the
clock** - it is the SDL popup-flag plumbing above, plus a home for "which
widget is currently hovered long enough to show its tooltip" (a new,
small piece of per-scene state this slice did not build because it has
nowhere natural to live yet without a real Tooltip client to shape it
around, matching this project's own "an interface is extracted from a
working implementation" rule).

### 6.3 `Dialog`: modal focus trapping is a genuinely NEW mechanism, not an extension of `enter_scope()`

7-4 named this precisely and handed it to this slice: "`enter_scope()`/
`exit_scope()` bound Tab traversal only... not `Dialog`'s modal focus trap
... a `Dialog` additionally needs to refuse losing focus to anything
outside it at all (including a click)." Checked directly against `dg::
Focus`'s current implementation before deciding: `Focus::set(target)` -
the function EVERY click routes through (`dispatch_pointer()`'s own
`apply_focus_change()`, in every example this project has) - takes any
`NodeId` unconditionally and has no concept of "refuse this because it is
outside the active scope." Making it refuse would require either a second
`set()`-shaped entry point every existing caller would have to be
migrated to consult, or a trap flag threaded through `Focus` itself that
every one of its five existing call sites (Dropdown included, as of this
slice) would need to newly reason about - a real design decision with
several honest shapes, not a one-line extension of `enter_scope()`.
Alongside it, design.md section 5.2's own `on_close_request` being
cancellable (for unsaved-changes prompts) needs a real `Dialog` window
kind with its own close-request event, which does not exist in
`WindowManager` today either (only `request_close()`'s own fire-and-forget
shape, observed later from `pump()`'s `closed` list with no cancellation
point). **Both are real, contained, but genuinely new mechanisms** - named
as this slice's own honest non-delivery rather than a rushed,
under-designed trap that the next slice would have to redo.

### 6.4 The follow-up

`.omo/plans/drawgui-phase7.md` records **7-5b** as the named follow-up for
all three, in the register `doc/form-controls.md`'s own Dropdown decline
and 7-2's own split into 7-2b already established: a well-evidenced subset
landed now, the remainder named with its real prerequisite rather than
silently narrowed.

## 7. `SDL_VIDEODRIVER=dummy` and the headless test design

Matching `doc/popup.md` section 4 and `doc/focus.md`'s own headless check
exactly: `examples/22_dropdown_menu --verify-dropdown` sets
`SDL_VIDEODRIVER=dummy` before touching `WindowManager`. This is enough to
open an ordinary window and drive the OVERLAY branch for real (every
keyboard-nav/selection/wraparound/mouse-click assertion in section 8 runs
against a real, dummy-backed `WindowManager` + `PopupHost` + `dg::Focus`),
but **not** enough for `SDL_CreatePopupWindow`, which fails under that
driver with "That operation is not supported" - the identical measurement
5-2 made and every popup-touching slice since has re-confirmed rather than
assumed still true. The check ATTEMPTS the native branch once and reports
the failure loudly and specifically (`attempt_native_popup()`), never
silently skips it - real coverage of the native branch (a genuine second
OS window, its own `dg::Focus`/`WidgetSet`, Up/Down/Enter actually driving
it) comes from `examples/22_dropdown_menu`'s own interactive mode against
this sandbox's real display (WSLg, `DISPLAY=:0`), the same precedent every
prior popup-touching example already established.

## 8. Verification performed

- **Full CTest, once: 32/32** (31 inherited + `dropdown_menu.verify_demo_
  scene`), including the new `tests/unit/test_dropdown.cpp` (8 `TEST_CASE`s,
  folded into the existing `drawgui_unit_test` binary).
- **Golden sha256 unchanged**: `f635028e6e1e92349d23f0575f1e39e7ca8b05e5974492641ee610fe520df12e`
  - confirmed on both g++ and clang++ builds. Dropdown touches no
  default-rendered scene.
- **g++ 15 and clang++ 21, one `-Werror` build each**: both clean, across
  the whole tree (not only the new files - `Key::kUp`/`kDown`/`kEnter`
  made two pre-existing exhaustive switches non-exhaustive, and both had
  to be fixed for either compiler to build at all).
- **`-DDG_SANITIZE=ON`**: full 32/32 suite green, including
  `dropdown_menu.verify_demo_scene`'s own six open/close cycles and five
  selection changes.
- **clang-tidy `-p build` and clang-format**: exit 0 on every changed/new
  file (`examples/22_dropdown_menu/*`, `src/widget/widget_set.cpp`,
  `src/widget/focus.cpp` [rebuild only, unchanged content], `src/platform/
  sdl3/window_manager.cpp`, `examples/21_focus/focus_scene.cpp`,
  `examples/12_text_input/text_field_window.cpp`,
  `examples/05_widgets/widget_scene.cpp`, `tests/unit/test_dropdown.cpp`),
  zero `NOLINT`. Two real findings fixed rather than suppressed:
  `bugprone-unchecked-optional-access` on `active_widgets()`/
  `active_focus()`'s ternaries (rewritten as explicit `if` guards, the
  identical "explicit `has_value()` before every access" style 7-4's own
  `focus_window.cpp` already established for the native popup branch) and
  on three `popup_->` dereferences inside `handle_pointer()`/`handle_key()`
  (rewritten to bind one local `dg::PopupHandle&` reference per guarded
  block rather than re-dereferencing the optional repeatedly).
  `.github/workflows/ci.yml`'s hand-maintained TU list gained all five
  `examples/22_dropdown_menu/*.cpp` files.
- **`props.*`/theme lock/ABI lock/drift tests**: unaffected, still green -
  this slice added no property and no theme token (section 9).
- **Defect injection, against this slice's own new logic**: three attempts,
  detailed in section 2's own commit history -
  (A) `dropdown_select()`'s lower-bound check weakened (`index <= 0`
  instead of `index < 0`) - **caught immediately** by both the headless
  demo check and the new unit test ("Enter at the FIRST option... commits
  it" failed exactly as predicted);
  (B) `dropdown_set_options()`'s upper-bound check weakened (`>` instead
  of `>=`) - **survived against the tests that existed at injection time**
  (a real coverage gap: `dropdown_set_options()` had no test at all before
  this slice, because `examples/22_dropdown_menu`'s own scene sets
  `Widget::options` directly at construction rather than calling it) -
  closed by adding the exact boundary case (a selected index exactly ONE
  past the new list's end) to `tests/unit/test_dropdown.cpp`, which then
  **caught** the same injection when re-applied;
  (C) not pursued further within this slice's own test surface: a swapped
  `Key::kUp`/`kDown` routing inside `dropdown_window.cpp`'s own
  `handle_key()` would NOT be caught by `dropdown_check.cpp`, because that
  file drives `dg::Focus::focus_next()`/`focus_previous()` directly rather
  than through the window driver's own key-to-call mapping - named here
  as an honest gap in this slice's own test surface (the window-driver
  routing layer is exercised only by the interactive mode against a real
  display) rather than silently left unexamined.
- **line 622 (design.md section 5.6) re-verified**: zero new `RenderObject`/
  node kinds, a twentieth consecutive slice.

## 9. Property and token status after this slice

**Unchanged: 49 properties (38 implemented / 10 partial / 1 not-yet), 12
theme tokens.** `kDropdown` added no property (`options`/`selected_index`
are construction-time/runtime `Widget` fields, the identical argument
`doc/form-controls.md` section 1.3 already made for `kSlider`'s `value`)
and reused every existing colour field (`fill_normal`/`fill_hover`/
`fill_pressed`) rather than introducing a new one - `doc/properties.md`
and `doc/theme.md` need no edit for counts, and this document is their
cross-reference.

## 10. Final counts

49 properties (unchanged); 12 theme tokens (unchanged); 9 `WidgetKind`s
(8 + `kDropdown`); 32 CTest entries (31 + `dropdown_menu.verify_demo_
scene`); 22 examples (21 + `22_dropdown_menu`); zero new node/
`RenderObject` kinds, a twentieth consecutive slice.

## 11. What this slice explicitly does not build, named

Context menu (right-click - no button identity in the pointer plumbing at
all, section 6.1), Tooltip (`SDL_WINDOW_TOOLTIP` flag threading plus a home
for hover-delay state, section 6.2), `Dialog` and modal focus trapping
(a genuinely new `Focus` mechanism plus a cancellable close-request event,
section 6.3) - all three named with their own real, checked (not assumed)
prerequisite, and split into `.omo/plans/drawgui-phase7.md`'s own **7-5b**.
`Table`/`RenderGrid` - decided (section 1), not built, because no working
caller in this slice's own scope needs even the fixed-column-width case.
Nested/cascading submenus, menubars, toolbars, tab controls, tree views,
date pickers, colour pickers, drag-and-drop, a11y trees, the remaining
~20 widgets of design.md's full target set (§5.6) - all named by the
task itself as out of scope, untouched here. Popup/dropdown animations
(open/close fade or slide) - `doc/popup.md` section 6's own decline,
unrevisited by this slice: nothing about Dropdown specifically needed one,
though 6-1's clock would serve it the identical way section 6.2 describes
for a tooltip's fade-in.
