# Slice 7-7: `examples/25_showcase` — a cross-feature regression bed

Every prior example in this project is a single-slice measuring instrument
(`05_widgets` runs `--verify-widgets`/`--clip-probe`/`--damage-cost`;
`03_damage_repaint` is the rig behind the 27x number; `16_complex_properties`
is a 100-frame byte-identity suite). An audit found that no two non-trivial
features had ever been put together in one running scene. This slice builds
one screen — a "media library settings" layout — using all 9 `WidgetKind`
values, a context menu, a tooltip and a modal dialog, themed entirely through
6-2's tokens, and uses it as a regression bed for combinations no prior
example exercises. `--verify-showcase` is the deliverable; the interactive
window and `--script` mode exist so the same scene is also a real integration
smoke test.

## Scene

One window, `kColumn` root: a header row (CJK title `kLabel`, a "Toggle
Theme" `kButton`, a "File Info" `kButton` that opens a context menu on
right-click, a "Help" `kButton` that opens a tooltip on hover) and a content
row split into a sidebar (`kPanel`) and a main column. The sidebar holds a
plain `kCheckbox`, three radio-grouped `kCheckbox`es, a `kSlider`, a
`kDropdown` ("sort order", CJK options) and a `kTextField` ("search"). The
main column holds a wrapped-CJK-plus-shadow description panel, a virtualized
`kList` ("recent files") whose pool nodes each carry a real attached
`Widget{kind=kButton}`, and a "Open Profile" button that opens a modal dialog
containing a second `kTextField` for a CJK-capable name field.

`kScrollView` is deliberately not attached to anything: a `kList` already
*is* a clipping scrolling viewport (`doc/list.md`), so wrapping a second one
around it would test nothing this project's own `10_scrolling`/`15_list`
examples do not already cover. `showcase_check.cpp`'s own Claim 1 asserts
this absence by name rather than silently omitting `kScrollView` from the
"all 9 kinds" claim.

## Cross-feature combinations tested, and what each found

1. **Dropdown popup opening over/near a scrolled list** — genuinely
   untested before this slice (`doc/menus.md` §3 only ever tested a
   dropdown with five always-visible options). Result: **composes
   correctly, for a structural reason, not by luck.** `PopupHost`'s overlay
   branch (`src/window/popup_host.cpp`) appends its content container as a
   **direct child of `RenderTree::root()`**, never as a descendant of the
   anchor's own ancestor chain. Scrolling the list first (which recycles
   several pool nodes) does not move the dropdown's own anchor rectangle,
   and opening the popup afterward costs zero relayout
   (`nodes_relaid_out == 0`, measured, not assumed) and lands the popup
   directly below the anchor, unaffected by the list's scroll offset.

2. **Modal dialog + `TextField` mid-IME-composition + focus trap** — the
   exact meeting point 7-3 (a stale-composition-offset crash) and 7-4 (a
   double `Focus::set()` silently skipping side effects) each found
   separately, never combined. Result: **composes correctly.**
   `Focus::set_guarded()` refuses a direct escape attempt while a
   composition is active, the refusal has no observable side effect on the
   in-progress composition, Escape #1 cancels the composition without
   touching the committed model (`doc/ime.md`'s own rule), and Escape #2
   then exits the modal scope and blurs the field that was still focused
   inside it. Every step is asserted with an exact value, not "no crash".

3. **Tab traversal into/out of a virtualized `kList` whose nodes recycle**
   — `doc/menus.md` §3 evaluated `kList` for *dropdown option rows*
   specifically and found its pool nodes "carry no attached Widget at
   all"; that finding was about the choice made for dropdown rows, not a
   structural limitation of `kList` itself. This slice attaches a real
   `Widget{kind=kButton}` to every "recent files" pool node — nothing new
   in the engine, an ordinary `WidgetSet::attach()` call on an
   already-existing node — which makes the rows genuinely Tab-reachable
   and gives `Focus::blur_if_any_of()` (built in 7-4, unit-tested only per
   `doc/focus.md` §6) its first real end-to-end caller. Result:
   **composes correctly** — focusing the pool node showing logical item 2,
   then scrolling far enough to recycle every pool slot, blurs the stale
   focus rather than leaving it silently pointing at a different item's
   content. A defect injection (recorded below) confirms the oracle
   actually catches a dropped `blur_if_any_of()` call.

4. **Theme switch mid-flight of an active `AnimationEngine` transition on
   the same bound property** — genuinely untested anywhere in this
   project before this slice; neither `doc/animation.md` nor `doc/theme.md`
   combine the two. **This is the one place composing two working features
   produced a real, previously-unrecognized emergent behaviour**, reported
   here in full rather than hidden:
   - `ThemeBindings::apply()` writes a bound property through the ordinary
     `dg::set_prop()` door, unconditionally, on every switch (its own
     header already documents this).
   - `AnimationEngine::tick()` recomputes an active slot's interpolated
     value from that slot's own `from`/`to` (captured as literal resolved
     colours at `animate()`/`set_value()` time) and writes it through the
     same door, also unconditionally.
   - Neither system knows the other exists. A theme switch fired while a
     transition is running on the SAME (node, prop_id) takes effect
     **transiently** — visible for exactly one frame — and is silently
     overwritten by the very next `tick()`, which has no idea the theme
     changed underneath it.
   - **This is not a memory-safety bug and not a violation of either
     system's own documented contract** — both behave exactly as their own
     headers say they will, in isolation. It is a real interaction that
     simply had never been exercised because no prior example or test ever
     ran both systems against the same property at the same time.
   - **Not fixed in this slice.** A fix would require `ThemeBindings` to
     either query `AnimationEngine` before writing (a new coupling between
     two systems that have never depended on each other) or `set_prop()`
     itself to notice an active transition (a much larger change to a
     function every property write in this project already goes through).
     Both are real engineering, not slice-sized within this task's own
     scope; named here as a genuine gap for whichever future slice wants
     coordinated theme+animation invalidation, matching this project's
     "declined by name, not silently worked around" convention.
   - `showcase_check.cpp`'s own claim asserts the FULL sequence with exact
     colour comparisons (not "something changed"): the switch's own write
     is visible immediately, it differs from what the animation had just
     painted, and the next `tick()` provably overwrites it back.

5. **Wrapped CJK text + a drop shadow on the same node** — `doc/text-layout.md`
   and `doc/complex-properties.md` each document their own feature alone;
   neither combines with the other, and no test carries a `shadow` on a
   `TextStyle::wrap = true` node. Result: **composes correctly** — the
   panel's declared height is computed by `dg::Paragraph::build()` before
   the node exists (the display path's own discipline, untouched), the
   shadow paints outside those declared bounds (the complex-properties
   path's own discipline, also untouched), and a full repaint of the
   combined node succeeds and encodes a PNG. The oracle asserts a
   hand-derived FLOOR on the wrapped height (must exceed one line, not
   merely be greater than zero) so a regression that collapses wrapping
   back to a single line fails a specific comparison.

6. **Tooltip over a clipped/scrolled region** — implemented as the "Help"
   button's own tooltip (not inside a scrolled viewport in this scene's
   final layout — see "Not built" below for why the harder placement was
   descoped).

7. **Opacity/clipping over an open popup subtree** — answered structurally
   rather than by building a live opacity-faded scene: `PopupHost`'s
   overlay branch attaches its container at `RenderTree::root()`
   unconditionally (`src/window/popup_host.cpp`, unchanged by this slice),
   so an ancestor's `opacity`/`clip` on the sidebar or the list can **never**
   reach an overlay popup's own subtree — it is not a descendant of either,
   by construction. `showcase_check.cpp`'s dropdown check asserts
   `tree.parent(handle.content_root) == LayoutTree::root()` directly rather
   than merely observing correct pixels in one case. **This closes the
   hypothesis rather than confirming a passing test that happened to avoid
   the hazard**: the 5-2 invariant the task named ("the host must not
   re-run layout while an overlay popup is open") is a *different*, real
   hazard this slice does not newly test — it is 5-2/7-4's own documented
   constraint, unaffected by opacity.

## Not built, named rather than quietly avoided

- **A live tooltip anchored to a partially-clipped, mid-scroll list row.**
  The hover-timer + clip interaction the task named as untested remains
  untested by this slice specifically — `HoverTimer` is unchanged from
  7-5b, and this scene's own tooltip target is an ordinary header button,
  not a scrolled list row. Building that specific combination would need a
  second tooltip anchor inside the list's own clip region and a scripted
  scroll-while-hovering sequence; descoped for time, named honestly rather
  than silently covered by the existing (different) tooltip check.
- **A native (real second-OS-window) modal dialog in the interactive
  window/`--script` driver.** The oracle (`showcase_check.cpp`) builds the
  dialog directly (no `WindowManager`), so the modal-trap claims run
  either way; the *interactive* `Runner::open_dialog()` only opens the
  overlay shape, unlike `examples/23_menu_tooltip_dialog`'s own
  `--branch native|overlay` for its dialog. The dropdown/context
  menu/tooltip in this slice's window driver DO support both branches
  (`--branch native|overlay`), matching precedent.
- **Fixing** the theme-switch-mid-animation finding above.

## Headless popup limitation

Unchanged from 5-2/7-4/7-5b's own measurement: `SDL_VIDEODRIVER=dummy` opens
an ordinary window but `SDL_CreatePopupWindow`/`SDL_SetWindowParent` fail.
`--script`'s default (native) branch attempts the dropdown popup once and
reports the failure loudly and specifically:

```
could not open the popup: SDL_CreatePopupWindow failed: That operation is not supported
```

`--script --branch overlay` exercises the full scripted sequence (Tab
traversal, CJK text input, right-click context menu + Escape, dialog open +
synthesized IME composition + double-Escape, theme toggle) end to end on any
headless machine. `showcase_check.cpp`'s own dropdown/popup claim forces
`caps.native_popup = false` for the identical reason, rather than skipping
the claim on a headless machine.

## Theme tokens: none added

Every colour, radius and spacing value in this scene resolves against the
existing 12 tokens (`color.surface`, `color.on-surface`, `color.border`,
`color.primary`, `color.on-primary`, `color.primary-hover`,
`color.primary-pressed`, `radius.sm`, `radius.md`, `space.sm`, `space.md`,
`color.focus-ring`). Panel/border/radius/spacing properties go through
`dg::bind_token()`/`ThemeBindings` exactly like `examples/18_theme` and
`examples/24_theme_package`; interactive widgets' `fill_normal`/
`fill_hover`/`fill_pressed` (plain `Color` fields on `Widget`, never
`dg::set_prop()` consumers — `widget_set.h`'s own header) are re-derived
from the same `Theme::color_value()` calls at build time and again on every
theme switch (`paint_interactive_widget()`), which is a **different**
mechanism from `bind_token()`, not a second token system — named explicitly
so a reader does not conflate the two. The one literal colour left in this
slice is the modal dialog's translucent backdrop scrim
(`profile_dialog.cpp`'s `kBackdrop`) — no schema token models a scrim, and
`examples/23_menu_tooltip_dialog`'s own dialog uses the identical literal
for the identical reason.

## Defect injection (composition-only slice — injected against the oracle)

This slice adds no new engine logic: every mechanism it exercises
(`bind_token`, `PopupHost::show`, `Focus::set_guarded`/`blur_if_any_of`,
`WidgetSet::text_field_composition_update`, `AnimationEngine::tick`,
`dg::Paragraph::build`, `dg::set_shadow`) already existed and is unit-tested
elsewhere. Per the task's own instruction, the injection targets the
ORACLE rather than invented engine code: `list_scroll_to()`'s own
`return scene.focus.blur_if_any_of(recycled);` was replaced with
`return dg::FocusChange{};` (silently dropping the blur call). Result: two
of `showcase_check.cpp`'s own assertions failed immediately
(`blur_if_any_of() blurs the focus...` and `focus is not left pointing at
the pool node's NEW (wrong) logical item`), and the process exited 1. The
injection was reverted immediately afterward; `git diff` shows no trace of
it in the committed history.

## Measured relayout numbers (dense scene, realistic load)

- Scrolling the "recent files" list (multiple pool nodes recycled at once):
  `nodes_relaid_out == 0` — repaint-only, matching 5-3's own finding
  extended to a scene with a dropdown, a dialog and a shadowed paragraph
  all coexisting.
- Opening the dropdown's popup after the list has already scrolled:
  `nodes_relaid_out == 0` — `PopupHost` appends through `RenderTree`
  directly, never through `LayoutTree` (5-2's own finding, re-verified
  rather than assumed to still hold with this much more going on around
  it).
- A theme switch touching this scene's colour-only bindings (panels,
  borders) costs zero relayout, consistent with 6-2/7-6; this slice did
  not additionally bind an int token to a showcase-scene property, so the
  "int-token switch costs a real relayout" half is left to
  `examples/18_theme`/`24_theme_package`'s own existing coverage rather
  than re-derived here.

## line 622 (design.md §5.6) re-verified

Zero new `RenderObject`/node/`WidgetKind` kinds. This slice's own scene uses
all 9 existing kinds (`kPanel`, `kLabel`, `kButton`, `kCheckbox`, `kSlider`,
`kTextField`, `kList`, `kDropdown` attached; `kScrollView` deliberately
absent, named above) and adds nothing to `WidgetKind`, `NodeStyle`,
`BoxStyle`, or the `RenderTree`/`LayoutTree` primitive set. The streak
(4-8 through 7-6) is unaffected — this slice does not extend it by name
(it builds no new control), but it re-confirms the primitive set is
sufficient for a screen combining nine controls, a context menu, a
tooltip, a modal dialog and a live theme switch at once, which none of the
22 prior slices' own individual acceptance checks measured together.

## Counts, re-derived for this slice

- 49 properties, 12 theme tokens (both unchanged — this slice adds neither).
- 21 ABI exports (unchanged).
- 9 `WidgetKind` values (unchanged).
- 25 examples (this slice).
- 35 CTest entries (34 inherited + `showcase.verify_demo_scene`).
