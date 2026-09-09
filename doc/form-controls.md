# Slice 4-8: form controls — radio as a checkbox field, a slider that needs no new RenderObject, and why dropdown is declined

What was built (radio, slider), the scoping decisions and their justification,
what was declined (dropdown) and why, a real engine constraint found while
building the slider, the defect-injection campaign, and screen verification.

The short version:

- **Checkbox**: unmodified from step 3-3. Verified end-to-end again here as a
  regression, not rebuilt.
- **Radio is not a new `WidgetKind`.** `Widget::group` is one new
  `std::optional<int>` field on the existing `kCheckbox`, read by `toggle()`
  (select-and-clear-siblings instead of flip) and a new `group_members()`
  scan. No new render-tree node, no new layout kind, no new property.
- **Slider is a sixth `WidgetKind`** — a track (the widget's own node) and a
  thumb (its one child), positioned by a paint-time formula over the
  **existing** `RenderTree::set_local_origin`. No new RenderObject kind,
  satisfying design.md section 5.6 line 622's acceptance bar directly: this
  slice's whole point, per that line, is that a slider must be buildable from
  the existing primitive set or the primitive set is broken. It is.
- **The slider's value is runtime widget state, not a property** — the
  identical argument doc/scrolling.md section 2 made for the scroll offset,
  extended one control over.
- **Dropdown is declined outright.** `include/drawgui/window/window_manager.h`
  has no window-kind concept whatsoever — one `open()`, one implicit "normal"
  window, zero occurrences of `Popup`, `WindowKind` or `PlatformCaps` anywhere
  in `include/` or `src/`. design.md section 5.2 calls the `PopupHost`
  abstraction that would be needed "本设计最关键的一处" (one of the most
  critical decisions in the whole design) for exactly this reason: building a
  dropdown as an in-window overlay now and discovering later that it needs a
  real OS popup window means rewriting the whole menu/dropdown/tooltip
  subsystem — the trap design.md cites stonegui's retrospective 2.1 for.
- **A real engine constraint was found, not invented**: a `kLeaf` child is
  clamped to its parent's own resolved size even when "loosened" — loosening
  drops the *minimum*, not the *maximum*. A thumb declared taller than its
  track is silently measured back down to the track's height. This is why the
  demo's track and thumb share one height, and why the centering formula's
  only genuine test is built directly on `RenderTree`, bypassing `LayoutTree`.

---

## 1. The scoping decisions, made in writing

### 1.1 Radio: "checkbox plus one field", checked against the same bar 4-7 used for scrolling

The brief's question was where group membership should live: a new field on
`Widget`, a separate `GroupSet` keyed by group id, or reuse of `kCheckbox`
outright. The answer follows doc/scrolling.md section 1's own method
precisely — that slice checked whether scrolling's requirements genuinely
differed from clip's or opacity's before building anything, and found they did
not (no `RenderClip`, no `RenderOpacity`, no fourth mechanism — three existing
ones composed). Applying the identical check here:

A radio button's requirements differ from a checkbox's in exactly one place:
**the effect of a click**. Everything else — the two-fill (`fill_normal` /
`fill_hover` / `fill_pressed`) cycle, the indicator child whose colour flips
on state change, hover/press through `dg::Interaction`, hit testing through
`owner_of()` — is verbatim identical. A `kRadio` `WidgetKind` would duplicate
every one of those and diverge only in `toggle()`'s body, which is exactly the
shape that argues **against** a new enumerator: a fifth (now sixth, for
slider) `WidgetKind` earns its place by needing machinery a checkbox does not
have, per `doc/widgets.md`'s own rule ("a fifth kind is a fifth enumerator and
a fifth case in one switch, which the compiler will demand" — stated as
inevitable, not as license to add one pre-emptively). Radio needs **one
number**: which group it belongs to.

So: `Widget::group` is `std::optional<int>`, unset for an ordinary checkbox.
`WidgetSet::toggle()` branches on whether it is set:

- unset — the existing behaviour, unchanged: flip `checked`.
- set — **select**, not toggle: if already checked, no-op (a real radio
  button is not un-checked by clicking it again, only by another option in
  its group being selected); otherwise set `checked = true` and clear
  `checked` on every *other* `kCheckbox` sharing the same group id, found by
  a linear scan of the widget table (there is no secondary index — this
  project has already measured, in `doc/widgets.md`'s hit-testing section,
  that a spatial index bought by a measurement rather than a guess is the
  standing policy, and a form has at most a few dozen widgets, not the tens
  of thousands a scan would need to matter for).

**What toggle() cannot do**: repaint. It has no `RenderTree` parameter, so it
cannot refresh the siblings it just deselected. `group_members()` is the
answer — a read-only scan returning every other same-group `kCheckbox`,
which a caller (here, `form_scene::dispatch()`) uses to refresh exactly the
widgets whose appearance may have changed. This mirrors `scrollable_owner_of()`
being a *separate* climb from `owner_of()` rather than a shared one
(doc/scrolling.md section 7): two different questions over the same table,
not one function trying to answer both.

**No new property.** `props/drawgui.props.toml` gained nothing this slice.
`group` is a construction-time field a scene author sets once when building
the widget, the same way `indicator`/`indicator_on`/`indicator_off` already
are — none of `kCheckbox`'s existing fields are properties either, and
`group` is the same kind of thing one field over.

### 1.2 Slider: checked against design.md line 622 before writing a line of code

Design.md section 5.6's acceptance standard, quoted in full because this
slice is what it is testing: **"如果实现 Slider 需要新增 RenderObject，说明第 3
层的原语集设计有缺陷"** — if implementing Slider needs a new RenderObject, the
layer-3 primitive set is flawed. The MVP eight controls exist specifically to
prove this.

**It does not need one.** A slider is:

- a **track** — the widget's own node, an ordinary box with a fill, a border
  and (optionally) `radii`. Nothing new; every widget already has one of
  these.
- a **thumb** — its single child, an ordinary box. Also nothing new — a
  checkbox already has exactly this shape (`indicator` is a child the
  checkbox widget moves the *colour* of; a slider's thumb is a child the
  widget moves the *position* of).
- **positioning** — `RenderTree::set_local_origin(thumb, x, y)`, a primitive
  that already existed before this slice (used generically for moving any
  node). No new render-tree method, no new layout kind, no new node kind.

The brief specifically asked to reuse the pattern `scroll_by()` established:
a parameter for content the caller already has and this file would otherwise
need a `LayoutTree` to derive. `set_slider_value()` deliberately does **NOT**
take such a parameter, and section 2.2 below explains why that is a
considered omission rather than an oversight.

**A sixth `WidgetKind` is earned, not reused into `kCheckbox`,** because a
slider's requirements genuinely differ from a checkbox's: dragging
accumulates a value across an unbounded stream of pointer deltas (the same
shape scroll's offset already has, doc/scrolling.md section 2), which a
checkbox's binary `checked` cannot represent, and the drag mechanism itself
bypasses `dg::Interaction` entirely (section 1.4 below) — a checkbox is
purely `dg::Interaction`-driven. This is the opposite conclusion from radio's
for a specific, stated reason, not an inconsistency.

### 1.3 The slider's value: state, not a property — the same argument as the scroll offset, one control over

`Widget::value`/`min_value`/`max_value`/`step` are **not** in
`props/drawgui.props.toml`. `doc/scrolling.md` section 2 gives the argument
for the scroll offset in full; restated for the slider because it applies
identically:

A scroll offset accumulates across an unbounded stream of wheel notches and
drag deltas, is read back every frame to decide what to paint, and has no
"declared" value a caller would ever write through `set_prop()` the way it
writes `width`. A slider's `value` is the same shape one control over: it
accumulates across an unbounded stream of drag deltas during a live drag, and
putting it in the property table would mean every drag pixel becomes a
`dg_node_set_prop` call across the eventual C ABI — the exact anti-pattern
design.md section 5.15.3 already rejects for per-node maps.

`min_value`/`max_value`/`step` are the interesting middle case: they **are**
declarative and long-lived, set once when a scene is built and never mutated
by a drag — closer to `scroll_axis` (a property) than to the offset (state).
They are kept as plain construction-time fields, not properties, for the
reason `doc/properties.md`'s own standard states for every property already
implemented: **a representation is not added before the code that consumes
it exists.** Nothing sets `min_value` through `set_prop()` today because there
is no C ABI yet at all; the day one exists and a host language needs to set a
slider's range through it, `min_value`/`max_value`/`step` are exactly the
shape that becomes three new properties, following `doc/development.md`'s
process. Building that representation now, with no consumer, is the mistake
`doc/widgets.md` already named once for platform headers ("an interface no
implementation has ever contradicted is a guess with a build rule").

### 1.4 Dragging bypasses `dg::Interaction` entirely, and `kSlider` is deliberately not `accepts_pointer()`

`WidgetSet::interactive()` (private to `widget_set.cpp`) returns `false` for
`kSlider`, in the same group as `kPanel`/`kLabel`/`kScrollView`. This means
`owner_of()`/`widget_at()` never resolve to a slider, and `dg::Interaction`
never sees a press on one — mirroring `kScrollView`'s identical choice
(doc/scrolling.md section 6): "every item in both scrolling lists is a plain,
non-interactive panel... there is nothing a drag could be mistaken FOR."

A new, separate climb — `slidable_owner_of()` — exists for exactly the reason
`scrollable_owner_of()` does: a drag starting on the thumb (a plain child, not
itself `accepts_pointer`) still has to find the slider that owns it, and this
is a *different* question from "what does a click activate" that a shared
climb would answer wrong the day a slider sits inside something clickable.

The window driver (`examples/11_form_controls/form_window.cpp`) tracks a drag
exactly the way `examples/10_scrolling/scroll_window.cpp` does: a
`std::optional<NodeId> dragging_` field set at `kDown` (via
`slidable_owner_of()`, not `dg::Interaction`), consumed at `kMove`, cleared at
`kUp`/`kLeave`. No drag threshold, no gesture arena (design.md section
5.16.3) — the identical decline doc/scrolling.md section 6 already recorded,
for the identical reason: "an arena with one competitor is a data structure
with no purpose, and it arrives with the second recognizer." This slice's
sliders are, again, the second recognizer's absence.

**One difference from scroll's drag worth naming**: scroll's drag reports a
*delta* (how far the pointer moved since the last event); a slider's drag
reports an *absolute position* (where the pointer is, mapped through the
track's geometry to a value) — because a scroll offset is a running
accumulator with no natural "home" position for the pointer to correspond to,
while a slider's value has an exact geometric correspondence to a pixel on
the track at every instant. `slider_value_at()` is this mapping, and it is
click-to-position: pressing anywhere on the track (not only on the thumb)
immediately jumps the value there, which is ordinary slider behaviour and
requires no separate "did the user grab the thumb or click the track" case.

## 2. What was declined, and why — dropdown, in full

### 2.1 The check the brief asked for, run first

Before writing anything: does `include/drawgui/window/window_manager.h` have
*any* window-kind or popup support today?

```
$ grep -rn "Popup\|WindowKind\|native_popup\|PlatformCaps" include/ src/
(no output)
```

**Zero occurrences.** `WindowManager::open()` is the only way to create a
window, taking a `WindowSpec` (title/size/fill) with no `kind` field at all —
every window this engine can open is an ordinary top-level window. There is
no `PlatformCaps`, no `native_popup` capability query, no `Popup`/`Dialog`/
`Tooltip` enumerator anywhere in this codebase. design.md section 5.2's table
of window kinds (`Normal`/`Dialog`/`Popup`/`Tooltip`) and its `PopupHost`
abstraction are design-stage text with no implementation whatsoever.

### 2.2 Why this settles the question, per design.md's own words

design.md section 5.2, verbatim: **"PopupHost 抽象 —— 本设计最关键的一处"**
("the PopupHost abstraction — the single most critical decision in this
whole design"). The passage's own argument:

> 下拉框、右键菜单、tooltip 需要能超出父窗口边界。桌面上正确做法是**真 OS 窗口**；
> 移动端只能是**应用内 overlay 层**。因此 Widget 层不直接创建窗口，而是向
> `PopupHost` 请求展示 ... **这个抽象必须在 MVP 就存在**。如果先用应用内模态层
> 实现菜单，后期改成真窗口等于重写整个菜单/下拉/tooltip 子系统 —— 这正是
> stonegui 复盘 2.1 描述的困境。

(Dropdowns, context menus and tooltips need to escape their parent window's
bounds. The correct desktop implementation is a *real OS window*; mobile can
only do an in-app overlay layer. So the widget layer never creates a window
directly — it asks a `PopupHost` to show content, and PopupHost decides the
real form. **This abstraction must exist at MVP.** Building menus as an
in-app modal layer first and later needing a real window means rewriting the
entire menu/dropdown/tooltip subsystem — exactly the trap stonegui's
retrospective section 2.1 describes.)

This passage is not merely a suggestion this slice could reasonably defer; it
is design.md **naming its own trap** and describing precisely the shape a
naive dropdown implementation would take. Building a dropdown as an
in-window absolute-positioned overlay now — the obvious approach given only
`kAbsolute` layout and no window-kind concept — **is** that trap, verbatim.

### 2.3 The narrow exception considered, and why it is declined too

The brief allows a narrower case: a windowless placeholder built purely to
prove the RenderObject-primitive-sufficiency question for a closed/open
*list* portion, without attempting the escape-parent-bounds problem. This was
considered and declined, for two reasons specific to this codebase rather
than a generic "no" —

1. **The list portion needs nothing new to prove.** A dropdown's open list is
   a `kColumn` of plain rows inside a container — exactly the "list is not a
   widget, it's a `kColumn`/`kRow` of ordinary children" finding
   doc/scrolling.md section 1 already established for `ScrollView`, and (if
   the list overflows) exactly what `scroll_axis` already composes. There is
   no new primitive-sufficiency question a closed/open list would answer that
   `examples/10_scrolling` has not already answered. Building one anyway
   would be building a demonstration of an already-settled fact, which
   `doc/layout.md` has already named a category for and rejected ("a
   diagnostic reachable only through a shape nothing builds is a diagnostic
   with no caller" — restated here as "a demo proving what is already
   proven has no reader either").
2. **A windowless placeholder still teaches the wrong lesson if it looks
   like a dropdown.** design.md's trap is specifically about a menu/dropdown
   that *looks and works* well enough in-window that nobody revisits the
   decision until a real popup is needed elsewhere in the app (a right-click
   menu that must escape a small embedded panel, say). Any implementation
   that opens and closes a list under a button, however honestly scoped in
   its own comments, invites exactly that reuse the day someone needs a menu.
   The safer artifact is not a placeholder but the written decision itself
   — this document, plus the following named prerequisite.

### 2.4 What has to exist before dropdown is buildable

Named precisely, matching every prior slice's decline shape (doc/scrolling.md
section 1 names the animation clock as fling's prerequisite the identical
way):

**A `PopupHost` abstraction, which needs at minimum a `Popup` window kind in
`WindowManager`.** This is a **platform-layer** feature (layer 1, per
design.md section 4's architecture table), not a widget-layer one — it
requires:

- `WindowSpec` (or an equivalent) gaining a `kind` field, with `Popup` at
  minimum (no border, no focus steal, click-outside auto-close) alongside
  today's implicit `Normal`.
- The SDL3 backend implementing whatever `SDL_WINDOW_POPUP_MENU`/positioning
  API a borderless, owner-relative popup window needs — unbuilt today; only
  ordinary top-level windows exist.
- A `PlatformCaps`-shaped capability query (or its equivalent) so a future
  mobile backend without OS popup windows can report that and let a
  `PopupHost` fall back to an overlay layer — again, `PlatformCaps` does not
  exist in this codebase in any form yet.

This is a full slice's worth of platform work, upstream of anything a widget
layer can compose — matching the shape doc/scrolling.md already used for its
own three declines that had a genuine prerequisite (fling needs the animation
clock; scroll anchoring needs an insertion mechanism; nested-scroll
delegation needs a real nested scene to drive the policy). Named follow-up:
whichever slice builds `WindowManager`'s popup-window support is where
dropdown belongs, and the `kColumn` list-composition half is already proven
and needs no further slice of its own.

## 3. Layout involvement: neither radio nor slider need any

Confirmed, not assumed, per the task's requirement:

- **Radio** needs *zero* layout involvement. It reuses `kCheckbox`'s existing
  box+indicator shape verbatim; the only change is in `WidgetSet::toggle()`'s
  *state* logic, which never touches `BoxStyle`, `LayoutTree` or any layout
  kind.
- **Slider's thumb position is a paint-time offset, never a layout concern**
  — confirmed by `tests/unit/test_form_controls.cpp` (`set_slider_value` calls
  produce a thumb move through `RenderTree::set_local_origin` alone) and by
  `examples/11_form_controls/form_check.cpp`'s
  `check_no_relayout_during_drag`, which asserts `LayoutStats::nodes_visited
  == 0` and `nodes_relaid_out == 0` after a slider-value-only change on the
  actual demo scene — the identical measurement shape doc/scrolling.md
  section 4 used for the scroll offset, applied here rather than assumed by
  analogy.

**The one place layout genuinely re-enters the picture**: a *window resize*
that changes a `grow`-weighted slider's track width. That resize was always
going to trigger a real layout pass on its own account (the track's own size
changed); nothing about the slider *causes* a relayout that would not have
happened anyway. What resize DOES require, and what section 4 covers, is a
**caller-side resync obligation** distinct from layout itself.

## 4. A real engine constraint, found rather than designed around

The first version of this slice's track/thumb pair used a thin track (8px)
under a taller, round, overlapping thumb (20px) — a common slider aesthetic.
Empirically, the thumb's rendered height came out as **8px, not 20px**, and
tracing it down: `LayoutTree::Impl::measure_leaf()` hands each of a leaf's
children `child_bound = inner.loosened().deflate(margin)`. `loosened()`
(in `include/drawgui/layout/box.h`) is defined as `BoxConstraints{0, max_width,
0, max_height}` — it drops the *minimum* to zero, but the *maximum* is
untouched, and that maximum is the leaf's own resolved bound. A leaf whose
own height is a *tight* 8px (declared via `BoxStyle::height`) therefore hands
its child a constraint whose `max_height` is **also** 8, and the child's
own declared height (20) is clamped down to fit — silently, with no
diagnostic, because a leaf shrinking its child to fit is exactly what a leaf
is specified to do for a child with no intrinsic size opinion of its own.

**This is a real, load-bearing property of `LayoutTree`, not a bug**: a
`kLeaf`'s child cannot organically exceed the leaf's own resolved size. It
explains why `examples/11_form_controls`'s track and thumb share one height
(`kSliderTrackHeight == kThumbSize == 20`) rather than the originally-intended
thin-rail-under-a-round-thumb look, and it is why
`tests/unit/test_form_controls.cpp`'s slider scenes are built directly on
`RenderTree` (bypassing `LayoutTree` entirely, the same construction choice
`tests/unit/test_scroll.cpp` already made for the identical reason) — the one
path this constraint does not apply to, and therefore the only place the
thumb-centering formula `(track.height - thumb.height) / 2` can be exercised
with a non-degenerate (non-zero) answer. This is recorded here rather than
silently worked around, matching every prior slice's practice of writing
down a design.md gap or engine surprise the moment it is found.

## 5. Property status after this slice

**Unchanged: 33 implemented / 10 partial / 3 not-yet, 46 total.** Neither
radio nor slider added a property — section 1.1 and section 1.3 are the
arguments for each. `doc/properties.md` needs no update.

## 6. Verification performed

### 6.1 Automated

- **16 CTest entries green** (15 inherited + new `form_controls.verify_demo_scene`),
  `unit` (doctest) includes 13 new test cases in
  `tests/unit/test_form_controls.cpp` covering radio exclusivity/no-op
  re-selection/group independence, slider clamping/snapping/re-clamping,
  `slider_value_at()`'s pointer-to-value mapping, `slidable_owner_of()`'s
  climb, the `accepts_pointer()` invariant, and `resync_sliders()`'s
  unconditional repositioning.
- **Golden image unchanged**: `drawgui_render_png`'s sha256 is still
  `f635028e6e1e92349d23f0575f1e39e7ca8b05e5974492641ee610fe520df12e` — this
  slice touches no default-rendered scene.
- **g++ and clang++, Debug and Release, `-Werror`**: all green.
- **`-DDG_SANITIZE=ON`, both compilers, full suite**: green, plus a manual
  sanitized run of `--dump-png`, `--probe` and `--script` (the real-X-input
  mode) with zero diagnostics.
- **clang-tidy `-p build` and clang-format**: exit 0 on every changed and new
  file, added to `.github/workflows/ci.yml`'s hand-maintained TU list.

### 6.2 Hand-derived geometry, not byte-identity alone

`examples/11_form_controls/form_check.cpp`'s `expected_thumb_x()` is an
**independent** re-implementation of the value-to-pixel formula
`WidgetSet::reposition_slider()` uses, not a read-back of the tree — matching
`doc/compositing.md`'s standing argument that byte-identity "verifies the
damage system, not the widget semantics", extended here to widget geometry:
a second copy of the same arithmetic proves nothing, an *independently
written* one does. On the demo's default 760x420 scene:

| what | hand-derived | measured |
| --- | --- | --- |
| volume slider track width (grow=1, row content 692px minus brightness's 240px base minus 24px gap) | 428px | 428px |
| brightness slider track width (fixed) | 240px | 240px |
| volume thumb x at value 30 of [0,100] (travel 408px, fraction 0.3) | 122px | 122px |
| brightness thumb x at value 3 of [0,10] step 1 (travel 220px, fraction 0.3) | 66px | 66px |
| volume thumb x after widening the window by 400px (new track 828px, travel 808px, same 0.3 fraction) | 242px | 242px |

### 6.3 On screen

`.omo/evidence/drawgui-kernel/form-controls-{idle,radio-group,slider-dragged}.png`,
via `--dump-png` (byte-identical to a real window's output on this CPU-raster
backend, per doc/cpu-raster-findings.md) and a real SDL3/X11 window for
interactive use.

| what | result |
| --- | --- |
| idle | checkbox and every radio indicator unfilled (dim); both slider thumbs at their built-in initial values |
| `--preset-checkbox --preset-radio-a 1` | checkbox filled (accent); radio group A's *second* option filled, first and third unfilled; radio group B **untouched** (still both unfilled) — confirmed by an independent visual read, not merely "the pixels differ" |
| `--preset-volume 72` | volume slider's thumb visibly moved right (x≈166 → x≈338 on this scene); brightness slider's thumb unmoved; both radio groups and the checkbox unmoved |

`--script` opens a real window and drives a checkbox click, a radio
selection and a slider drag through `WindowManager::warp_pointer()` +
`post_pointer_button()` — real `SDL_EVENT_MOUSE_BUTTON_DOWN/UP` and
`SDL_EVENT_MOUSE_MOTION` events on the platform's own queue, reported back
through the ordinary `pump()` path:

```
clicked the checkbox at 50,50: 1 real down event(s), 1 real up event(s) delivered
clicked radio a/1 at 94,130: 1 real down event(s), 1 real up event(s) delivered
dragged the volume slider from 44 to 452: 1 real move event(s) delivered
```

**A real defect was found and fixed via this exact screenshot process**: the
demo's own `--preset-*` flags originally called `toggle()`/`set_slider_value()`
without following through with `refresh()`, so a preset changed the stored
state but painted nothing — confirmed by comparing `--dump-png` output
before and after the fix (file size 8684 → 9157 bytes once the checkbox and
a radio option actually rendered as checked). Section 7 below is the
regression test this produced.

## 7. Proving the tests can fail: 14 injections against `src/widget/widget_set.cpp` and `examples/11_form_controls/form_scene.cpp`

Each injected one at a time against the committed tree, built, run through
the full CTest suite, reverted with `git checkout --` and the source
`touch`ed afterward — the standing lesson about `tar`/`git checkout` leaving
a stale mtime behind (doc/sizing.md section 5.5, doc/scrolling.md section 8),
applied from the start rather than rediscovered.

| # | injection | result |
| --- | --- | --- |
| A | `toggle()`'s sibling-clearing condition flipped (`slot->group != group` instead of `==`) — clears the WRONG group | **caught immediately** — `unit` and `form_controls.verify_demo_scene` |
| B | `group_members()` drops the group-id comparison, returns every checkbox | **caught by `unit` only** — provably no visual consequence: `refresh()` always reads the *actual* `widget.checked` bit rather than assuming one, so an over-broad member list only wastes idempotent refresh calls, never paints a wrong colour |
| C | `clamp_slider_value()` skips the initial `std::clamp` | **caught immediately** — both |
| D | `clamp_slider_value()` skips the re-clamp AFTER step-snapping | **survived** — the only stepped slider in the scene (`[0,10]` step 1) has a max that is already an exact step boundary, so snapping never overflows it. Closed by adding a `[0,11]` step-3 case (11 is not a multiple of 3, so the nearest step below it overflows past it) — then **caught** |
| E | `reposition_slider()`'s travel computed from `track.height` instead of `track.width` | **caught immediately** — both |
| F | `reposition_slider()`'s centering divisor (`/2`) dropped | **caught by `unit` only** — the demo scene's track and thumb are necessarily the same height (section 4's `kLeaf` clamp), so the centering formula is provably a no-op there; the unit test's RenderTree-direct scene (which bypasses the clamp) is the only shape that can observe this |
| G | `set_slider_value()`'s no-op early-return removed | **caught by `unit` only** — re-asserting the same value repositions the thumb to the identical pixel, so byte-identity cannot see it; only the explicit "did this report a change" contract test can, the same blind spot doc/compositing.md and doc/scrolling.md already recorded for other properties |
| H | `slidable_owner_of()` climbs via `accepts_pointer()` instead of `kind == kSlider` | **caught by `unit` only** — the headless `--verify-form-controls` path never exercises the interactive drag route this climb serves |
| I | `resync_sliders()` drops its `kind == kSlider` guard | **survived, then a real bug in the new regression test itself survived too** — a non-slider `Widget`'s `thumb` field defaults to `NodeId{0}` (the ROOT), so the bug repositions the root once per non-slider widget, visibly corrupting the whole scene (confirmed by `--dump-png`: the top two control rows vanish). No existing check caught it — every geometry assertion reads LOCAL bounds (unaffected) and the byte-identity gate compares two equally-corrupted scenes. The first version of the new test snapshotted root's bounds AFTER `build()` had already run the buggy resync once, so calling it a second time reproduced the identical corruption and the check passed regardless. Fixed by comparing against the viewport rectangle root MUST have, not a snapshot — then **caught** |
| J | `interactive()` returns `true` for `kSlider` | **survived** — no existing scene nests a slider inside a clickable container, so nothing distinguished the correct answer from the wrong one. Closed by adding a direct `accepts_pointer()`/`owner_of()` assertion — then **caught** |
| K | `toggle()`'s radio-select path skips `widget->checked = true` (clears siblings but never marks itself selected) | **caught immediately** — both |
| L | `form_scene::dispatch()`'s `group_members()` refresh loop removed — the ORIGINAL bug from section 6.3, re-injected deliberately | **caught immediately** — `form_controls.verify_demo_scene`, by the regression test (`check_radio_click_repaints_both_widgets`) built specifically to guard it |
| M | `slider_value_at()` drops the `half_thumb` centering offset | **caught by `unit` only** — the demo's headless verify never calls `slider_value_at()` directly, only the interactive `Runner` does |
| N | `clamp_slider_value()`'s step-snap uses `std::floor` instead of `std::round` | **caught immediately** — both, via the `4.6 -> 5` snap-up assertion (`floor(4.6) = 4`) |

**11 of 14 caught on the first attempt** (A, B, C, E, F, G, H, K, L, M, N).
**3 survived and required genuinely new test coverage before being caught**
(D, I, J) — D needed a scene shape (a non-step-aligned max) nothing built
yet; I and J needed assertions that simply did not exist, and I's first
attempt at one was itself wrong for a subtle, specific reason (recorded in
its row above and in the `tests/unit/test_form_controls.cpp` commit
message) — a fourth documented failure mode for this project's running list:
**a regression test that snapshots "before" AFTER the code path it is
testing has already run once, so an idempotent defect looks like no change
at all.**

## 8. What this does not do

Dropdown (section 2, declined with the platform-layer prerequisite named),
menus, tabs, tables, trees, dialogs, tooltips, progress bars, spinners,
splitters, date pickers — everything design.md section 5.6's ~30-control
target set names beyond MVP-8 that this slice does not touch. Also: no
keyboard activation of a checkbox/radio/slider (no keyboard input exists in
this engine at all, the identical precondition doc/scrolling.md section 1
already named for keyboard scrolling), no vertical slider orientation (this
slice built horizontal only — a slider's `value`-to-pixel mapping is
symmetric under a 90-degree turn and the extension is small, but nothing
in this scene needs it, so it is not built ahead of a caller), no hover/press
visual feedback on the slider thumb (the checkbox/radio pair already prove
`dg::Interaction`'s fill-cycling; a slider's thumb glow-on-hover would need
`fill_hover`/`fill_pressed` wired through a *second* appearance-holder that
does not receive clicks, which is new machinery this slice's acceptance
criteria did not ask for).
