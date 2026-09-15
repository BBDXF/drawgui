# The animation clock, curves and implicit transitions: one subsystem, five blocked features

Slice 6-1, phase 6's opening move. design.md section 5.16.1 names this "本节
最关键的约束" - the C++ core owns the clock, a host only declares endpoints and
a curve, and interpolation never leaves C++. Section 5.15.1's on-demand frame
loop is the other half: idle blocks at (measured) near-zero CPU, an active
animation drives a short frame loop, completion returns to blocking. This
slice builds both, plus the explicit and implicit animation APIs, over three
real clients.

The short version:

- **The clock is a value, not a virtual interface.** `AnimTime` is a plain
  `{ int64_t ms }` struct; `AnimationEngine::tick(LayoutTree&, AnimTime)` takes
  one as an ordinary parameter. Production calls `steady_anim_time()`
  (`std::chrono::steady_clock` underneath); every test in
  `tests/unit/test_animation.cpp` hands `tick()` hand-chosen `AnimTime` values
  advancing by exact, explicit deltas - no sleep anywhere. This is the same
  seam technique every prior slice used for a varying input
  (`WindowManager::warp_pointer(x, y)`, `RenderTree::set_scroll_offset(offset)`):
  make the value an explicit argument, not an injected strategy object. Zero
  `virtual` was added; none was needed.
- **`curve_id` is a plain constant set, not generator-backed - yet.** Unlike
  `prop_id`, which drives four independent consumers today (a C++ header, a
  dispatch switch, an ABI lock, and eventually `.d.ts` types), `curve_id` has
  exactly one reader: `evaluate_curve()`. Standing up
  `props/drawgui.props.toml`-style generator/lock machinery for a
  single-consumer table would be scaffolding built ahead of the reader that
  needs it - this project's own standing rule (window_manager.h's own header:
  "an interface is extracted from at least one working implementation, never
  written ahead of one") argues against it. Revisit when 6-3's ABI generator
  exists and can fold `curve_id` in as a fourth generated family alongside
  `prop_id`/`token_id`/`action_id`.
- **Handle lifetime uses a generation counter - a genuinely new mechanism, not
  5-3's "never free" reused.** 5-3's list pool never dangles because a pool
  node is never destroyed - sound for render nodes, whose count is bounded by
  UI complexity. An animation's count is NOT bounded that way: a button
  hovered ten thousand times has created ten thousand short-lived slots, so
  the engine MUST reclaim a finished slot for reuse, and reuse is exactly what
  makes a bare index ambiguous. `AnimHandle{index, generation}` is the
  ordinary generational-index answer - `doc/widgets.md`'s own comment on
  `WidgetSet` already named this technique as "the day removal arrives", and
  this is that day, for animation slots specifically.
- **Retargeting mid-transition uses the CURRENT interpolated value, recomputed
  from the slot's own state, not read back from the tree and not the original
  `from`.** This is the subtlety the task named by name, and it is also the
  single defect injection that would have been easiest to get wrong and
  hardest to notice on a still screenshot - section 5 has the argument and the
  test that pins it.
- **The idle-CPU claim is measured, not assumed**: `--idle-probe-ms 2000`
  blocks in `WindowManager::pump()` for a genuine two-second span with nothing
  animating and reports **0.009% CPU utilisation** (0.19 ms of process CPU
  time across 2002 ms of wall time, `getrusage`-measured) - see section 6.
- **Two real clients were built**: an implicit `background_color` transition
  on hover (the transition API's most direct demonstration, exactly as the
  task suggested) and a text-cursor blink built from four chained explicit
  animations (the discrete two-state client 4-9 declined by name). A third,
  an explicit `left` slide with pause/reverse/cancel, exists specifically to
  exercise the explicit handle API end to end. Caret blink, popup animation
  and scroll fling/rebound are the four the task allowed picking from; see
  section 8 for exactly what each of the five originally-blocked features
  still needs.
- **Zero new node/RenderObject kinds - a 13th consecutive slice.** Section 9.
- **§5.15.2's three-level invalidation model is NOT implemented as a cost
  model** - every animated property write still goes through the same
  `dg::set_prop()` → `RenderTree::set_style()`/`set_box()` path every other
  property write does, which repaints (or relays out) exactly as much as a
  hand-written call would. An `opacity` animation does not skip to
  recomposite-only, and a `left`/`width` animation does not get a cheaper path
  than a one-off change. Section 7 is the honest measurement, not a claim.

---

## 1. Scope: what this slice builds, and what it declines by name

design.md section 5.16.1's own two signatures:

```c
dg_anim_t* dg_animate(dg_node_t*, uint16_t prop_id, const dg_value* from,
                      const dg_value* to, uint32_t duration_ms, uint16_t curve_id);
int dg_node_set_transition(dg_node_t*, uint16_t prop_id,
                           uint32_t duration_ms, uint16_t curve_id);
```

This slice builds the C++ shape of both - `dg::AnimationEngine::animate()` and
`dg::AnimationEngine::set_transition()`/`set_value()` - over `LayoutTree&`/
`NodeId`, exactly the way `dg::set_prop()` already stands in for
`dg_node_set_prop`. The C ABI itself (`dg_animate`'s actual FFI surface,
`dg_anim_t*` as an opaque handle, the `dg_value` union) is slice 6-3's job and
is not built here.

Declined, by name, matching the task's own list:

| what | why |
| --- | --- |
| the gesture arena (§5.16.3, P4) | out of phase, and unrelated to a clock |
| overscroll rebound's platform-specific behaviour | needs the gesture arena's velocity source, itself out of scope |
| nested scroll delta propagation, scroll anchoring | §5.16.2 items 4-7's own scope, untouched here |
| GPU-driven vsync | **there is no GPU backend in this project** (doc/cpu-raster-findings.md, doc/compositing.md); this engine's "vsync" is necessarily a CPU-side frame pacer (a fixed `pump()` timeout while something is animating) - said plainly rather than called vsync it is not |
| `SkPicture` recording for recomposite-only animation | already evaluated and rejected project-wide - doc/damage-repaint.md's own finding, "**`SkPicture` 未获益**，直接遍历快 15–20%" (`SkPicture` produced no benefit; direct traversal is 15-20% faster), recorded again here rather than re-litigated |
| the FFI/ABI surface itself | 6-3 |
| spring/physics curves | the four named-curve set (linear, ease-in, ease-out, ease-in-out) suffices for every client this slice built; a spring curve needs no new mechanism in `AnimationEngine::tick()` (it is still `t -> f(t)`), only a fifth named formula, and nothing here forecloses adding one |

Also declined, discovered while designing rather than named in the task:

- **`transform` animation.** `dg::set_transform()` (doc/complex-properties.md
  section 4) always reports `kUnsupported` - there is no working destination
  to animate INTO. `TransformDesc`'s decomposed translate/scale/rotate fields
  exist for exactly this day, per that file's own comment, but the day has not
  arrived; nothing here changes that verdict.
- **A generic `dg::get_prop()`.** Implicit transitions need to read a
  property's CURRENT value on its very first trigger (before any slot exists
  to remember it - see section 5). `AnimationEngine::read_current()` is a
  small, explicitly bounded switch covering exactly the properties this
  slice's clients use - `background_color`, `border_color`, `opacity`
  (`NodeStyle`, read via `RenderTree::style()`) and `left`/`top`/`width`/
  `height` (`BoxStyle`, read via `LayoutTree::box()`). Both accessors are
  already public; nothing new was added to read them. Extending the table to
  another float/length/color property is one added `case`, following the same
  shape - it was not built out to all ~29 other interpolatable properties
  because nothing in this slice exercises them, and a generic reader would be
  new machinery built ahead of a second consumer (6-2's theme system is the
  most likely one, and it has not landed). This is a deliberately narrow scope
  boundary, named rather than hidden.

## 2. The clock: time source and determinism without `virtual`

design.md requires the clock to be C++-owned and vsync-driven. This project's
own acceptance bar - byte-exact goldens, hand-derived assertions, a test suite
this slice was explicitly told must never sleep - requires the opposite: a
clock a test can drive by hand, to an exact instant, with no wall-clock
variance at all.

**The seam is a value, not an interface.** `include/drawgui/anim/clock.h`
defines:

```cpp
struct AnimTime { std::int64_t ms = 0; };
[[nodiscard]] AnimTime steady_anim_time();
```

and every clock-consuming function - just one, `AnimationEngine::tick(LayoutTree&, AnimTime now)`
- takes `AnimTime` as an ordinary parameter. Production code calls
`tick(tree, steady_anim_time())` every iteration of the on-demand frame loop;
`tests/unit/test_animation.cpp` calls `tick(tree, AnimTime{250})`,
`tick(tree, AnimTime{500})`, and so on, with each value chosen so the expected
interpolated result is exact (a 0..100 `left` animation over 1000ms ticked at
250 reads exactly 25, no `Approx()` needed for the float cases; the color
cases use `Approx()` only because `std::lround` rounding is being asserted
against, not because the clock is inexact).

**Why this is the established technique rather than an invention.** This
project has zero `virtual` in `src/`/`include/` across twelve prior slices,
and every seam that could have been "inject a strategy object" was instead
built as "make the varying input an explicit argument, and read it back
through an ordinary accessor":

| prior seam | what varies | how it is injected |
| --- | --- | --- |
| `WindowManager::warp_pointer(id, x, y)` | where the pointer goes | a plain `int, int`, not a `PointerDevice` interface |
| `RenderTree::set_scroll_offset(id, offset)` | where a scrolled viewport sits | a plain `PixelPoint`, not a `ScrollSource` interface |
| `WidgetSet::slider_value_at(tree, id, pointer_x)` | where a drag put the thumb | a plain `int`, not a `DragSource` interface |

`AnimTime` continues the pattern exactly: the varying thing (time) is a value
argument to the one function that needs it, not a polymorphic dependency
`AnimationEngine` would otherwise have to own, mock, or inject through a
constructor. **No seam was invented for testability that the rest of this
project does not already use elsewhere.**

**The real clock**, `src/anim/clock.cpp`, is three lines around
`std::chrono::steady_clock` - chosen over `SDL_GetTicks()` because the engine
never touches SDL (`include/drawgui/anim/` has zero SDL leakage, matching
every other header in `include/`), and over a frame counter because a frame
counter cannot express "this animation takes 200ms" without also fixing a
frame rate the on-demand loop deliberately does not have.

## 3. `curve_id`: a plain constant set, and why not the generator

design.md's `dg_animate` signature carries a `uint16_t curve_id` - the same
transport shape as `prop_id`, which raises the question of whether it should
ride `props/drawgui.props.toml`'s generator/lock machinery the way `token_id`
(6-2) and `action_id` (6-3) are expected to.

**Decided: not yet.** The generator's entire value is preventing ONE fact from
drifting across SEVERAL independent consumers - `prop_id` already drives a
C++ header, a dispatch switch, an ABI lock, and will drive `.d.ts` types and
ABI code once 6-3 lands. `curve_id` has exactly ONE consumer today:
`evaluate_curve()` in `src/anim/curve.cpp`. Standing up generator/lock
infrastructure for a single-reader table is scaffolding built for consumers
that do not exist yet - directly against this project's own standing rule
(quoted in full in `include/drawgui/window/window_manager.h`'s top comment:
"an interface no implementation has ever contradicted is a guess with a build
rule"). `include/drawgui/anim/curve.h` instead defines four plain
`constexpr std::uint16_t` constants (`kCurveLinear`, `kCurveEaseIn`,
`kCurveEaseOut`, `kCurveEaseInOut`), the identical shape a property's enum
`values` constants already have before anything in this project has ever
locked an enum ordinal.

**When this should change**: the moment 6-3's ABI generator exists, `curve_id`
becomes the fourth ABI-numbered family beside `prop_id`/`token_id`/
`action_id`, and folding it into that generator at that point is the right
call - not before.

**The four curves themselves** are plain polynomials, each mapping `[0,1]` to
`[0,1]` with no overshoot (deliberately - an overshooting curve would need
`interpolated_value()`'s colour-channel math to clamp, which it currently does
not, and nothing here needs overshoot):

```cpp
linear:      t
ease_in:     t * t
ease_out:    1 - (1-t)^2
ease_in_out: t < 0.5 ? 2t^2 : 1 - (-2t+2)^2/2
```

`tests/unit/test_animation.cpp` hand-derives all four at `t = 0.5` and checks
all four pass through both endpoints unchanged (split into one `TEST_CASE`
per curve rather than a loop - see the comment there for why: a loop wrapping
`doctest::CHECK`'s own try/catch expansion pushed `readability-function-cognitive-complexity`
over threshold for what is otherwise eight straight-line assertions).

## 4. Explicit animation: `animate()`, and why handle lifetime needed a new mechanism

`AnimationEngine::animate(tree, node, prop_id, from, to, duration_ms, curve_id)`
returns an `AnimHandle`, which can be `pause()`d, `resume()`d, `reverse()`d
(flips playback direction from wherever the animation currently is, not a
`from`/`to` swap) or `cancel()`led (freezes the node at its current value;
never a silent jump to `to`).

**The task's own framing put handle lifetime at the centre, pointing at 5-3's
solution to the analogous dangling-`NodeId` problem**: "look at what it did
before inventing something new." Checked before writing a line of this
slice's code:

> `doc/list.md` section 1: "*a recycled `NodeId` never dangles anywhere - it
> is never freed*." The pool is fixed-size, allocated once, and a node is
> restyled and repositioned, never destroyed.

**That precedent does not transfer, and the reason is a real difference
between the two resources.** A render tree's node count is bounded by UI
complexity - a screen has at most a few thousand widgets, so "never free"
costs a bounded amount of memory forever. An animation's count is NOT bounded
that way: a hover transition retriggers on every mouse-enter/leave, a blinking
caret completes and relaunches itself every few hundred milliseconds, and a
long-running application creates an unbounded NUMBER of animations over its
lifetime even though only a handful are ever alive at once. "Never free a
slot" would leak unboundedly in exactly the case 5-3's render nodes do not:
time, rather than screen complexity, is what keeps producing new ones.

**So `AnimSlot` storage IS reused** - `AnimationEngine` keeps a
`std::vector<AnimSlot>` plus a free list, and a finished or cancelled slot's
index goes back onto that list for the next `animate()` call to claim. Reuse
is exactly what makes a bare index ambiguous (the classic ABA problem: handle
A names slot 3; slot 3 finishes and is freed; a new animation reuses slot 3;
handle A, still held by some caller, now silently controls the WRONG
animation). The fix is the ordinary generational-index technique:

```cpp
struct AnimHandle {
  std::uint32_t index = 0;
  std::uint32_t generation = 0;
};
```

`generation` is bumped every time a slot is freed (natural completion or
`cancel()`), and every handle-taking method compares BOTH fields before
touching the slot; a mismatch reports `AnimControlStatus::kStaleHandle` rather
than acting on someone else's animation, mutating memory that has moved on, or
invoking undefined behaviour. `tests/unit/test_animation.cpp`'s
"a stale handle (reused generation) is reported, never acted on" test pins
this end to end: cancel a handle, create a second animation that reuses the
same `index`, and confirm the ORIGINAL handle is rejected by every one of
`pause()`/`resume()`/`reverse()`/`cancel()` while the NEW handle (same index,
different generation) works.

**This is not 5-3's technique reapplied - it is the technique
`doc/widgets.md`'s own comment on `WidgetSet` already named as the one a
future removal path would need**: "*THE DAY REMOVAL ARRIVES that stops being
true [index-as-identity], and doc/widgets.md names the generation counter it
will need.*" That day is this slice, for animation slots specifically -
render nodes still never need it, because they still never get removed.

**Implicit transitions never expose a handle at all** - `set_transition()`
returns nothing, and a transition's in-flight slot is looked up internally by
`(node, prop_id)`, not by a handle a host holds. `AnimEvent::handle` is
`AnimHandle{}` (generation 0, a value no real `animate()` call ever produces)
for a transition's own completion event, which is the honest way to say "no
host-visible handle exists for this one."

## 5. Implicit transitions, and the retarget-mid-flight case

`set_transition(node, prop_id, duration_ms, curve_id)` declares, once, that
future writes to that `(node, prop_id)` interpolate. `set_value(tree, node,
prop_id, target)` is the write itself - a drop-in replacement for
`dg::set_prop()` a transition-aware caller uses unconditionally, whether or
not a transition happens to be declared for that property:

- **No transition declared** → falls straight through to `dg::set_prop()`.
- **Transition declared, nothing currently running** → reads the property's
  CURRENT value through the bounded `read_current()` table (section 1), and
  starts a slot from there to `target`.
- **Transition declared, ALREADY running** (the retarget case) → the new
  `from` is the slot's CURRENT interpolated value, computed via the exact same
  `interpolated_value()` function `tick()` itself uses, evaluated at THIS
  instant's `progress_ms` - not re-read from the tree (which would only be
  current as of the LAST `tick()` call, not this exact moment between ticks)
  and emphatically not the slot's ORIGINAL `from`.

**Why the restart-from-original version is a real, visible defect and not a
simplification**: consider a button whose hover-in transition (grey → blue,
200ms) is 50% done when the pointer leaves. A caller that restarts from the
ORIGINAL grey would make the button visibly SNAP backward to pure grey for one
frame before beginning to fade back to grey a second time - a stutter exactly
at the moment of interaction, which is the least forgivable place for a UI
animation to glitch. Retargeting from the CURRENT blended colour instead
continues smoothly from wherever the eye already is.

**This was the single most valuable defect injection this slice ran** (see
section 10): reverting `set_value()`'s retarget branch to read
`slots_[index].from` (the original) instead of `interpolated_value(slots_[index])`
(the current blend) was caught immediately by both
`tests/unit/test_animation.cpp`'s dedicated retarget test and
`examples/17_animation`'s `--verify-animation` hover oracle - the exact
subtlety the task named by name, verified to actually be load-bearing rather
than asserted on faith.

## 6. The on-demand frame loop, and the measured idle-CPU number

design.md section 5.15.1:

```
空闲                → wait_events() 阻塞，CPU 占用 0%
存在脏区或活跃动画   → 进入 vsync 驱动的帧循环
动画结束且无脏区     → 退回阻塞
```

`WindowManager::pump(int timeout_ms)` was already shaped for exactly this -
this slice's seam, per the task's own framing - because `SDL_WaitEventTimeout`
already has the property the design needs: a NEGATIVE timeout blocks
indefinitely (SDL's own "wait forever" convention), while a positive one
blocks up to that many milliseconds. `examples/17_animation/anim_window.cpp`'s
`Runner::run()` is the whole state machine, in four lines:

```cpp
const int timeout_ms = engine_.has_active() ? kFramePacerMs : -1;
const dg::PumpResult pumped = manager_->pump(timeout_ms);
```

`engine_.has_active()` is true from the instant `animate()`/`set_value()`
creates a live, unpaused slot until the instant the last one completes or is
cancelled - exactly the signal the frame loop needs, and it is tested
end-to-end at the engine level in `tests/unit/test_animation.cpp`'s
"has_active() state-machine" tests and again on the real demo scene in
`examples/17_animation --verify-animation`'s fourth oracle.

**"没有 GPU，所以没有真正的 vsync" - said plainly.** `kFramePacerMs = 16` is a
fixed CPU-side interval, not a signal from a display driver; this project has
no GPU backend (doc/cpu-raster-findings.md, doc/compositing.md both record
why), so there is no real vsync event to wait on. Calling this "vsync-driven"
without the caveat would misrepresent what is actually happening, which is a
periodic poll dressed as a frame pacer - functionally adequate for a CPU
raster engine, but not the thing design.md's own prose describes for a GPU
backend.

**Per-window dirty flags**: `examples/17_animation` opens ONE window with
three panels sharing one `RenderTree`/`AnimationEngine` pair, so this slice
does not build a second window to demonstrate cross-window isolation
directly - `examples/01_sdl3_multi_window` already demonstrates independent
windows at the `WindowManager` level, and nothing about `AnimationEngine`
changes that: each window would own its own `RenderTree` and its own
`AnimationEngine` (the class has no static or global state), so an idle
window's `RenderTree::damage()` stays empty regardless of what an animating
sibling window is doing, and `RenderTree::repaint()`'s existing
"skip when damage is empty" check (already exercised by every prior
interactive demo's `draw()`) is what keeps an idle window from being
repainted or presented. What IS new here is the TIMEOUT decision when
multiple windows share one `WindowManager::pump()` call: a real multi-window
host would take `timeout = any_window_animating ? kFramePacerMs : -1`, the OR
of every window's own `AnimationEngine::has_active()` - a small aggregation
this slice's single-window demo does not need to build, and is named here
rather than silently assumed solved.

**The measurement**, `examples/17_animation --idle-probe-ms N`: opens a real
window, drains its own startup events, then blocks in `WindowManager::pump()`
for a genuine `N`-millisecond span with nothing animating and nothing else
posting an event, using `getrusage(RUSAGE_SELF, ...)` before and after to
measure actual process CPU time (user+sys) consumed across the block. A
single `pump()` call is not reliable on a real desktop (the window system
itself delivers occasional focus/expose events that end the block early), so
the probe loops `pump()` with the REMAINING time on every early wakeup until
the requested wall-clock span has genuinely elapsed.

Measured on this machine, real X11/Wayland session, i5-1145G7:

| requested block | actual wall time | process CPU (user+sys) | utilisation |
| --- | --- | --- | --- |
| 500 ms | 500.5 ms (2 pump() calls) | 0.224 ms | **0.045%** |
| 2000 ms | 2002.1 ms (2 pump() calls) | 0.189 ms | **0.009%** |

**This is the first time this project has directly measured design.md's own
"CPU 占用 0%" phrase**, rather than assuming it follows from `pump()`'s shape.
It does not read as a literal zero - `getrusage`'s own bookkeeping and the
kernel's wakeup/reschedule cost are real, nonzero work - but at
0.01-0.05% utilisation over genuine multi-second idle spans, the claim is
true to the precision anyone would reasonably read it at.

## 7. §5.15.2's three-level invalidation: an honest gap report

design.md section 5.15.2 proposes three invalidation levels of increasing
cost - recomposite (opacity/transform, replays a cached `SkPicture`), repaint
(colour/text, re-records one subtree), relayout (size/layout properties,
re-measures) - and the task asked this slice to check which of the three an
ANIMATED property actually gets, rather than assume the model is implemented.

**Checked, not assumed: it is not implemented as a cost model at all.**
`AnimationEngine::tick()` writes every interpolated value through
`dg::set_prop()`, the exact function an ordinary, non-animated write already
goes through, and `dg::set_prop()` has never been taught to distinguish "this
write is part of an animation" from "this write is a one-off change" - nor
should it need to, for this to matter, since the cost is determined entirely
by WHICH property is written, exactly the way it already was before this
slice:

- An `opacity` animation calls `RenderTree::set_style()` every tick, which
  damages the node (doc/compositing.md's existing rule) - it does NOT recomposite
  a cached layer without re-walking the subtree, because this engine
  (`RenderTree::paint_mode()`'s `kPicture` option, `examples/03_damage_repaint`)
  has TWO paint modes and neither one is selected automatically per-property;
  `kDirect` (the default, and what every example here uses) re-walks and
  re-issues draw calls under the damage clip on every repaint regardless of
  which property changed.
- A `background_color` animation calls `set_style()` too, and repaints -
  correctly, matching the "repaint" tier, though not because anything
  distinguishes it from the opacity case above.
- A `left`/`width` animation calls `LayoutTree::set_box()`, which marks the
  node dirty up to its relayout boundary exactly the way a one-off `set_prop()`
  call already does - `examples/17_animation`'s slide panel genuinely
  relayouts every tick it moves, which is the CORRECT and EXPECTED cost for a
  layout property under this engine's existing model, not evidence the
  three-tier split exists.

**The honest verdict**: this slice adds no new invalidation granularity and no
per-property-type dispatch inside the damage/layout system. Every animated
property costs EXACTLY what a hand-written `dg::set_prop()` call to the same
property already cost before this slice existed - which is a property of
`dg::set_prop()`'s own design (doc/properties.md), not of anything
`AnimationEngine` adds. Building the recomposite-only fast path design.md
describes for `opacity`/`transform` would need `RenderTree` to cache and
replay a subtree's paint independent of a full repaint - `kPicture` mode is
the closest existing machinery, but it replays the WHOLE tree, not one
node's layer, and nothing here wires animation frame-writes to it
specifically. That is future work, named rather than quietly claimed done.

## 8. Which of the five originally-blocked features are unblocked, and what each still needs

design.md's own roadmap (echoed in `doc/scrolling.md`'s own decline) named
five features blocked on the absence of an animation clock:

| feature | unblocked? | what remains |
| --- | --- | --- |
| **scroll fling/inertia** | Partially. The CLOCK and the explicit `animate()` API are both here - a fling could be built as one `animate()` call per axis, decaying `scroll_offset` from a release velocity over a computed duration. What is STILL missing is the velocity itself: design.md's own design puts that in the gesture arena (§5.16.3, P4, explicitly out of scope this slice and named as such in the phase plan), and this slice did not attempt the "estimate velocity from raw pointer events without the arena" shortcut the task floated - `RenderTree`/`WidgetSet` currently discard everything about a pointer sequence except its position, so recovering a velocity would need new bookkeeping in `dg::Interaction` this slice did not add, kept out to avoid quietly building a piece of the arena under a different name. |
| **overscroll rebound** | Unblocked for the "spring back" motion itself (an `animate()` call from the overscrolled offset back to the clamped one, any curve). NOT unblocked for platform-correct behaviour - design.md gives two different platform defaults (macOS rubber-band vs Windows hard-stop) and this slice built neither, matching the task's explicit exclusion of "overscroll rebound platform-specific behaviour." |
| **text caret blink** | **Unblocked and built.** `examples/17_animation`'s caret panel is a real, working two-state blink, chained from four explicit `animate()` calls via completion events. It is a STANDALONE caret, not wired into the real `TextField` widget (`WidgetKind::kTextField`, 4-9) - doing that is mechanical (call the same four-phase pattern from `TextField`'s own focus/blur handling) but was not done here, to keep this slice about the clock rather than about re-touching 4-9's widget. |
| **popup animations** | Unblocked at the mechanism level (an implicit `opacity`/`background_color` transition on a popup's root node is exactly the pattern the hover panel here demonstrates) but NOT built - `PopupHost` (5-2) was not touched this slice, and no popup demo animates. |
| **implicit property transitions** | **Unblocked and built** - this is the CSS-transition-model client this slice's hover panel demonstrates directly, including the retarget case. |

Two of five are fully built (caret blink, implicit transitions); two are
mechanically unblocked but not built out (overscroll rebound's motion,
popup animation); one (fling) needs a second, still-missing piece
(gesture-derived velocity) before the clock this slice built can drive it.

## 9. design.md §5.6 line 622, re-verified: the streak holds

Line 622's acceptance bar: a new capability that needs a new `RenderObject`/
node kind is a sign layer 3's primitive set has a gap. Twelve consecutive
prior slices (4-1 through 4-9, 5-1 through 5-4) needed none. This slice adds
none either:

- **No new `WidgetKind`.** The count is still 8
  (`kPanel`/`kLabel`/`kButton`/`kCheckbox`/`kScrollView`/`kSlider`/
  `kTextField`/`kList`) - `AnimationEngine` is not a widget and touches no
  node kind at all; it writes EXISTING properties (`left`, `opacity`,
  `background_color`, `width`) through the EXISTING `dg::set_prop()` door.
- **No new `RenderObject`/node structure.** `RenderTree::Node` is unchanged;
  animation is entirely a caller-side concern that happens to call
  `set_prop()` on a timer instead of once.
- **No new property.** The property table is unchanged at 49 entries (38
  fully implemented, 10 partial, 1 not yet) - every property this slice
  animates (`left`, `top`, `width`, `height`, `opacity`, `background_color`)
  already existed.

The streak now holds for a 13th consecutive slice.

## 10. Defect injection: interpolation math, the retarget case, handle lifetime, the frame-loop state machine

Ten single-line injections against `src/anim/animation_engine.cpp` and
`src/anim/curve.cpp` - the NEW engine logic this slice added, per the task's
own scope - each applied, tested against BOTH
`tests/unit/test_animation.cpp` and `examples/17_animation --verify-animation`,
then reverted before the next one:

| # | injection | caught? |
| --- | --- | --- |
| 1 | `kCurveEaseOut` duplicated `kCurveEaseIn`'s formula (`t*t`) | **immediately** - curve unit test |
| 2 | `tick()`'s progress clamp used `duration_ms - 1` as its upper bound | **immediately** - animation never completed; 3 unit tests + the example's caret/state-machine oracles all failed |
| 3 | `AnimationEngine::valid()` dropped the generation comparison | **immediately** - the dedicated stale-handle unit test |
| 4 | `set_value()`'s retarget branch used the slot's ORIGINAL `from` instead of its current interpolated value | **immediately** - the dedicated retarget unit test AND the example's hover oracle both failed; this is the task's own named subtlety |
| 5 | `has_active()` stopped excluding paused slots | **immediately** - the pause/resume unit test |
| 6 | `free_slot()` stopped bumping `generation` | **immediately** - the stale-handle unit test (a second, independent assertion inside it) |
| 7 | `animate()` stopped forcing `duration_ms` to zero under reduced motion | **immediately** - the reduced-motion unit test |
| 8 | `tick()` stopped flipping the delta's sign for a reversed slot | **immediately** - the dedicated reverse unit test |
| 9 | `lerp_color()` dropped the alpha channel, defaulting to fully opaque | **SURVIVED** both suites - every existing colour test interpolates between two fully-opaque endpoints, and `Color::rgba()`'s own default alpha is 0xFF, so an opaque-to-opaque test cannot tell a correct alpha mix apart from one that was never computed at all. This is this project's fifth documented failure mode (场景缺形状 - the scene lacks the shape to expose it), and the fix is a new test, not a hedge: `"explicit animate(): the alpha channel interpolates too, not just RGB"` interpolates a FULLY TRANSPARENT colour toward an opaque one and asserts the midpoint alpha is exactly `0x80` - re-injecting the same bug against this test now fails it immediately. |
| 10 | `read_current()`'s `DG_PROP_LEFT` case tagged its result `PropValue::length()` instead of `PropValue::number()` (the table declares `left` as `float`, not `length`) | **SURVIVED**, and PROVABLY SO rather than merely unnoticed: `interpolated_value()` branches on `slot.type` (correctly set from `dg::prop_type()`, never from the mistagged value's own tag) and calls `.scalar()`, which reads the same underlying float regardless of which `PropType` a `PropValue` claims to be - the mistag is never inspected again after `read_current()` returns it. Reverted anyway on the grounds every other typed value in this codebase is grounds for suspicion the day something new inspects the tag (a future generic `get_prop()`, most plausibly) - correct hygiene now costs nothing and forecloses a latent bug for a reader who has not read this paragraph. |

**8 of 10 caught immediately; 1 survived and was closed with a new,
deliberately-shaped regression test; 1 survived and was diagnosed as provably
inert** (the third and sixth of this project's now-six catalogued injection
failure modes, per `.omo/plans/drawgui-kernel.md`'s running list, are both
represented here: "场景缺形状" for #9, and the already-known
"该项无可观测后果，能证明" pattern - 4-7/4-9's own precedent - for #10).

## 11. Counts

- **49 properties**, unchanged: 38 fully implemented, 10 partial, 1 not yet
  (`transform`).
- **22 CTest entries** (21 before this slice + `animation.verify_demo_scene`).
- **18 examples** (00 through 17; `17_animation` is new).
- **8 `WidgetKind`s**, unchanged.
- **Zero new node/`RenderObject` kinds** - 13th consecutive slice (section 9).
- **Zero `virtual`** added, in `src/` or `include/` (section 2).
- **Zero SDL leakage** into `include/drawgui/anim/` (clock.h/curve.h/animation_engine.h
  name no SDL type).

New files: `include/drawgui/anim/{clock,curve,animation_engine}.h`,
`src/anim/{clock,curve,animation_engine}.cpp`,
`tests/unit/test_animation.cpp`, `examples/17_animation/` (six files plus
`CMakeLists.txt`).
