# Scrolling and lists: `scroll_axis`, `RenderTree::set_scroll_offset`, and the unbounded axis nobody had built yet

Slice 4-7. What a scrollable viewport is here, why the offset is state and not
a property, the relayout claim confirmed by measurement, what design.md 5.16.2
asks for that this slice declines and why, and the contradiction this slice
found in design.md's own roadmap.

The short version:

- Scrolling is **not a new node kind**. It is three existing mechanisms
  composed: `overflow: kClip` (doc/clipping.md, unchanged), a new
  `BoxStyle::scroll_axis` that hands a `kLeaf`'s single child an **unbounded**
  constraint on one axis, and a new `RenderTree::set_scroll_offset` that
  shifts a node's children without moving what they declare. A "list" is not
  a widget either - it is a `kColumn`/`kRow` of ordinary children inside such
  a viewport.
- **The offset is runtime state, not a property.** `props/drawgui.props.toml`
  gained one property this slice (`scroll_axis`, id 46) and it is not the
  offset - the offset lives on `RenderTree` and is read back through
  `WidgetSet`, the same shape hover and press already have.
- **Scrolling costs a repaint, never a relayout - measured, not assumed.**
  `LayoutTree::layout()` visits zero nodes on a frame where only the offset
  changed, on the actual demo scene, and section 4 is the measurement rather
  than an appeal to the doc comment.
- This is also the first time this engine ever constructs an **unbounded
  constraint**, which doc/layout.md and doc/sizing.md both predicted would
  "arrive with scrolling" and costed in advance. It did, and it activated a
  code path (`grow` under an unbounded main axis) that had been dead since
  slice 2.

---

## 1. The scoping decision, made in writing

design.md section 5.16.2's table for `RenderViewport` lists six things:
inertia/fling, overscroll behaviour, nested scroll, scroll anchoring, keyboard
scrolling, and which scrollable ancestor a wheel targets. This slice does not
build all six, and here is each one named, with the reason.

### In scope, and why each one is the floor rather than a stretch goal

| what | how |
| --- | --- |
| a scroll offset | `RenderTree::set_scroll_offset` / `scroll_offset` |
| clipping to the viewport | `overflow: kClip`, unchanged, reused as-is |
| content exceeding the viewport without triggering ordinary overflow-fit | `BoxStyle::scroll_axis`, the unbounded constraint |
| mouse wheel | `PointerAction::kWheel`, routed to the scrollable ancestor **under the pointer** |
| click-drag | tracked in `examples/10_scrolling`'s window driver |
| correct hit testing at a nonzero offset | free - `hit_test()` already reads `node.absolute`, which the offset already shifts |
| no second layout pass for an offset-only change | measured; section 4 |
| a list as composition, not a widget | a `kColumn`/`kRow` inside the viewport; no new `WidgetKind` was needed for the LIST, only for the viewport itself |

These eight are what the task brief called "a reasonable floor", and nothing
here was found to be missing it once built - unlike prior slices, this one
did not have to loosen the floor to ship.

### Declined, and why - one at a time

**Inertia / fling.** design.md: "指针抬起时按速度进入 decay 模拟" (on release,
enter a decay simulation driven by release velocity). Declined. Fling needs
an animation clock ticking after the input has stopped, and design.md section
5.16.1 - the section immediately above 5.16.2 - makes animation a first-class
C++-owned clock with pause/cancel/curves that does not exist yet ("the
animation clock is owned by the C++ core... nothing here" is stated outright
in doc/compositing.md section 9 and doc/design.md's own roadmap puts
animation at **P4.5**, a full phase after this one). Building fling ahead of
the clock it needs would be exactly the "write the interface before the
implementation" mistake this project's own rule forbids - there is no working
animation timer to extract a decay curve's shape from yet. Named follow-up:
whichever slice builds `dg_animate` / implicit transitions (P4.5 in the
roadmap) is where fling belongs, because it is genuinely the same mechanism -
a velocity sample handed to a decaying curve over several frames is exactly
what an explicit animation already is.

**Overscroll rebound (rubber-banding).** design.md: "默认跟随平台
（macOS 橡皮筋回弹 / Windows 硬停）". Declined the *animated* half; **built**
the *clamped* half. The table names two platform defaults, not one, and this
slice implements the simpler of the two verbatim: Windows' hard stop -
`WidgetSet::scroll_by` clamps the offset into `[0, content - viewport]` and
refuses to move past it, reported as "no-op" so a caller can tell the
difference between "moved" and "already at the edge" (`test_props.cpp`... no,
`tests/unit/test_scroll.cpp`'s overscroll test asserts exactly this: a nudge
past an already-clamped offset changes nothing and is reported as such). The
rubber-band half needs the same animation clock fling does - a rebound is an
animation that starts automatically when a drag ends past the limit - so it
is declined for the identical reason and belongs with the same follow-up
slice.

**Nested scroll delegation** ("内层到达边界后将剩余滚动量传递给外层可滚动祖先" -
when the inner scrollable reaches its boundary, hand the remainder to the
outer one). Declined as *behaviour*. What **is** built, as a free consequence
of how the offset composes (section 5 below), is nested scroll **geometry**:
`tests/unit/test_scroll.cpp`'s "composes down a subtree without being read
twice" case and `test_layout.cpp`'s "scroll_axis on a node whose own extent is
unbounded" case both nest one scrolling viewport inside another and it works
- a grandchild is shifted exactly once, by inheriting its own parent's
already-shifted position, never by an ancestor's offset a second time. What
is missing is the *policy*: when the inner viewport is already at its clamp
and the wheel or drag continues, the remainder is simply dropped rather than
forwarded to the outer scrollable ancestor. That policy needs the wheel/drag
routing in `WidgetSet`/`examples/10_scrolling` to carry a *remainder* back out
of `scroll_by()` and re-dispatch it up the `scrollable_owner_of` chain - a
small, well-scoped addition once there is a real nested-scroll scene to drive
it (this slice's demo has no nested viewports on screen, only in tests, so
building the policy now would be exactly the "no caller exists yet" mistake
`doc/layout.md` already recorded once for unreachable diagnostics).

**Scroll anchoring** ("可视区上方插入内容时保持当前视觉位置，不跳动" - inserting
content above the viewport keeps the current visual position, no jump).
Declined outright, and not merely deferred: this engine's node vectors are
**append-only** (doc/widgets.md section 1 - "the day removal arrives that
stops being true"), so nothing can be inserted *above* an existing sibling in
child order at all yet, only appended after it. Anchoring is a policy for
reconciling an insertion's effect on scroll position, and there is no
insertion mechanism to reconcile against. This is not this slice's gap; it is
upstream of it.

**Keyboard PageUp/Down/Home/End.** design.md routes these through "the intent
mechanism (§5.5.1)". §5.5.1 does not exist - `doc/widgets.md` section 9
already listed "focus, keyboard navigation" among what step 3 left
undone, and nothing built since has changed that. There is no keyboard input
in this engine at all (`PointerEvent` is the only input type
`WindowManager` emits), so this is not a scoping choice this slice makes; it
is a precondition the intent-binding slice has to supply first.

**Wheel targets the scrollable ancestor under the pointer, not the focused
widget.** **Built**, not declined - this table row is done. There is no focus
concept in this engine yet (see above), which makes it the easier of the two
halves design.md poses: `WidgetSet::scrollable_owner_of()` climbs from the
hit-tested node under the *pointer*, and nothing about it consults or needs a
focused widget, so the "not the focused widget" half of the sentence is true
by construction rather than by a decision this slice had to make.

**Scrollbar form as a layout input** ("叠加式不占空间；占位式减少内容可用宽度").
Declined. There is no scrollbar at all - drawn, hit-tested, or otherwise. This
was in scope to *name* rather than to build: design.md's point is that the
form is a LAYOUT decision, not merely a paint one, and this slice's
`scroll_axis` is exactly the layout-side hook a scrollbar's reserved width
would eventually shrink the child's cross-axis room through (an occupying
scrollbar would deflate the inner constraint the same way padding already
does). No code exists for it because nothing consumes it yet; recorded here
so the hook is not invented twice.

### List virtualization: checked against the phase table, and declined by that check

design.md's own phase table (section 9, line ~1714) puts virtualization at
**P3** ("`List` / `Table` 内置虚拟化") and its acceptance target (line ~1452,
section 5.15.7) is explicitly "滚动 1000 项虚拟化列表" under **P3's** control
row, not this slice's. Section 5.15.4 states it plainly: "这不是可选优化，而是
控件的定义的一部分" (not an optional optimisation, part of the control's
DEFINITION) - which is a statement about a `List` *control*, and this project
has no controls yet (`doc/widgets.md`: four `WidgetKind`s, no `List`). Section
5.6's roadmap literally lists `ScrollView` and `List` as **two separate**
MVP-8 controls, which is this document's strongest confirmation that a
scrollable viewport (this slice) and a virtualized list (a future slice built
ON one) were never meant to be the same delivery. Building virtualization here
would be building `List` a phase early and inside the wrong slice. The named
follow-up is a `List`-shaped slice after 4-8 (form controls) or as part of
4-10 (completeness), which windows the item range against the viewport's
visible extent using exactly the geometry this slice already exposes
(`local_bounds()` of the content child, `content_bounds()` of the viewport,
`scroll_offset()`) - none of which needs to change to add windowing on top.

---

## 2. Why the offset is state, and `scroll_axis` is a property

Two different things needed a home this slice, and they went to different
places on purpose.

**`scroll_axis` is a property** (id 46, `enum {none, vertical, horizontal}`,
`applies_to = ["box"]`). It answers a *declarative* question - "does this
node's child measure under an unbounded constraint, and on which axis" - the
same shape `main_size` already has, and it is exactly as static across a
node's lifetime as `main_size` is: a caller sets it once when building the
viewport and does not flip it every frame. It went through
`doc/development.md`'s process exactly: appended at `props/drawgui.props.toml`
line's end, `tools/gen_props.py` regenerated the two derived files,
`tools/prop_lock.py --write` recorded it in `props/prop_ids.lock`, all four
committed together.

**The offset is not a property, and the argument is the same one hover and
press already settled.** `doc/widgets.md`'s `WidgetSet` exists because hover
and press are "state that outlives the click" - not a style a caller declares
once, but the accumulated effect of a stream of events. A scroll offset is
that exact shape one level further: it accumulates across an unbounded
sequence of wheel notches and drag deltas, is read back every frame to decide
what to paint, and has no "declared" value a caller would ever write through
`set_prop()` the way it writes `width`. Putting it in the property table would
mean every wheel tick becomes a `dg_node_set_prop` call across the eventual C
ABI - the exact anti-pattern design.md section 5.15.3 already rejected for
per-node maps, restated one layer up: **the table is for what a caller
declares, not for what accumulates.**

Concretely, the offset lives in two places, each the minimum that could hold
it:

- `Node::scroll_offset` (`src/render/tree_impl.h`) - the raw mechanism.
  `RenderTree::set_scroll_offset()`/`scroll_offset()` read and write it; it is
  not clamped there, because `RenderTree` has no notion of a scrollable
  child's full content extent (the same reason it does not know what padding
  is - doc/clipping.md section 2).
- `Widget::scroll_axis` / `Widget::scroll_content` (`WidgetSet`, a new
  `WidgetKind::kScrollView`) - the *policy*, i.e. which axis and against
  which child's extent to clamp. **Not** a second copy of the offset:
  `WidgetSet::scroll_by()` reads the current offset back from `RenderTree`
  every call rather than caching it, for the same reason `hit_test.cpp`
  gives for not caching a subtree extent - "a second copy of the geometry
  carrying an invalidation obligation on every move".

---

## 3. What design.md's roadmap contradicts, found here

`design.md` section 5.4.7's own example diagnostic (quoted verbatim, section
451-465) is written **as if `RenderViewport` and its unbounded constraint
already existed**: "但传入的 max_h 无界（祖先 RenderViewport 允许无限滚动）" - "but
the incoming max_h is unbounded (an ancestor RenderViewport permits unlimited
scrolling)". `doc/layout.md` and `doc/sizing.md` both already flagged this as
unreachable code with no caller ("no unbounded constraint is constructible
here... arrives with scrolling"), which is not a contradiction so much as an
open forward-reference this slice closes. The contradiction proper is
narrower and was found while closing it:

**Section 5.4.3 step 1 says a flex measures its `grow == 0 && shrink == 0`
children "以主轴无界约束" (with an unbounded main-axis constraint) as its
*general* measurement step, for every flex container, always.** That is
the two-pass intrinsic-sizing design `doc/sizing.md` section 1.4 already
declined for cost reasons (the `2^(n/2)` argument), and this slice's own
unbounded axis is not that - it is unbounded only when a `kLeaf`'s
`scroll_axis` says so, one specific and rare case, not every flex
container's default measurement. So `doc/sizing.md` section 1.8's decline
stands unweakened: this slice adds a *source* of an unbounded constraint,
not the general policy of measuring everything that way.

---

## 4. The relayout claim, measured

`tests/unit/test_scroll.cpp`'s `"scrolling marks nothing dirty in the layout
tree"` and `examples/10_scrolling --verify-scrolling`'s
`check_no_relayout()` both do the same thing: build a real scene through
`LayoutTree`, lay it out once, scroll through `WidgetSet::scroll_by()`
(which calls only `RenderTree::set_scroll_offset`), then call
`LayoutTree::layout()` again and read `LayoutStats`.

```
nodes_visited    = 0
nodes_relaid_out = 0
```

**Zero, not merely small.** `RenderTree::set_scroll_offset` never calls
`LayoutTree::set_box` or `mark_needs_layout` - there is no code path from one
to the other - so this is not a case the incremental layout pass got lucky
skipping; layout is never even entered. The mechanism: `reposition()` (the
same function every `set_local_bounds` move already runs) recomputes
`absolute` and `clip_bounds` for the scrolled subtree, and that is a
different, cheaper operation than `measure()` - it walks the tree writing two
fields per node from arithmetic already known, with no constraint
propagation and no child re-entered under a new size. `RepaintStats` (not
`LayoutStats`) is what a scroll costs: a `damage_subtree()` for the old
positions, one for the new, and a repaint.

**The condition under which this WOULD need a relayout, stated exactly, per
the task's requirement not to hand-wave it:** if a future feature made a
child's own SIZE depend on its scroll position - a sticky header that grows
as it approaches the top, say - the offset would have to reach
`LayoutTree::set_box` and go through the ordinary invalidation path, costing
exactly what any other resize costs. Nothing in this slice does that: every
node's `local` rectangle is fixed by `layout()` once, at build time, and
`scroll_axis` only ever affects the constraint a viewport hands its child
during THAT one measurement - never afterwards, and never as a function of
the current offset.

---

## 5. Nested scrolling composes for free, and why that is not a coincidence

Three tests exercise a scrolling viewport nested inside another one, and none
of them needed a single new line to pass:

- `test_scroll.cpp`: `"scroll offset composes down a subtree without being
  read twice"` - a grandchild shifts once, by inheriting its parent's already
  -shifted `absolute`.
- `test_layout.cpp`: `"scroll_axis on a node whose own extent is unbounded is
  a diagnostic"` - the misuse case, which only exists by nesting one scroll
  viewport's unbounded output into another's `scroll_axis` gate, and the
  diagnostic fires correctly on the inner one.

The reason this needed no new mechanism: `reposition()` computes a node's
`absolute` from its OWN parent's `absolute` and `scroll_offset` - a purely
local computation, applied once per node, walking outward from the root. A
node three scroll-viewports deep is shifted by each ancestor's own offset
exactly once, at the level that ancestor owns, in the same pass that already
walks every node after any move. This is the identical argument
`doc/clipping.md` makes for why nested clips need no special code ("a
grandparent's clip is honoured by the same line as a parent's") - restated one
mechanism over.

---

## 6. Click-drag, and the gesture arena this slice deliberately does not build

`examples/10_scrolling`'s window driver tracks a drag with two fields -
`dragging_` (which viewport, if any) and `drag_last_` (the previous pointer
position) - set at `kDown`, consumed at `kMove`, cleared at `kUp`/`kLeave`.
There is no drag threshold and no competition with a click, and that is a
scope decision rather than an oversight: **every item in both scrolling
lists is a plain, non-interactive panel** - none is a `kButton`, none is
registered with `dg::Interaction` at all. So there is nothing a drag could be
mistaken FOR; `WidgetSet::scrollable_owner_of()` is a separate climb from
`owner_of()` specifically so that a scene which DOES mix clickable items with
a drag-scroll surface still finds the scrollable ancestor without being
taught about clicks, but resolving the *competition* between the two -
"was that a tap or the start of a drag" - is exactly design.md section
5.16.3's gesture arena, and `doc/widgets.md` and `doc/widgets.md`'s own
Interaction file already declined building an arena for the identical
reason: "an arena with one competitor is a data structure with no purpose,
and it arrives with the second recognizer." This slice's list items are the
second recognizer's absence, not its presence - the day a list item is also
clickable, the arena becomes necessary, and not before.

---

## 7. `WidgetKind::kScrollView`: why a fourth mechanism, not a new node kind

The brief's instruction was to justify a distinct kind only if scrolling's
requirements genuinely differ from clip's (no `RenderClip`, doc/clipping.md)
or opacity's (no `RenderOpacity`, doc/compositing.md). They do not, and no new
**render-tree** node kind exists here for the same reason those two do not:
painting, hit testing and damage all still read exactly the fields they
already had (`overflow`, `absolute`, `clip_bounds`) - `scroll_offset` changes
what `reposition()` writes into `absolute`, and nothing downstream of that
needed to be taught a new concept.

`WidgetKind::kScrollView` is a different question and a different answer.
`WidgetSet` is *already* the place per-node state that is not a style lives
(hover, press, checkbox `checked`), and its own doc records the precedent
squarely: "a checkbox earns its place over a third button by carrying state
that OUTLIVES the click." A scroll viewport's offset outlives not merely one
event but an unbounded stream of them, which is the same shape sharpened, not
a different one - so it took the WidgetSet extension point that pattern
predicts, exactly as a fifth `WidgetKind` was always expected to (doc/widgets
.md: "a fifth kind is a fifth enumerator and a fifth case in one switch, which
the compiler will demand" - and it did, twice: once in
`examples/05_widgets/widget_scene.cpp`'s `describe()`, once in
`src/widget/widget_set.cpp`'s `interactive()`).

---

## 8. Proving the tests can fail

Fourteen defects injected one at a time, each built, each run through the
full CTest suite, each reverted and the source `touch`ed afterward (the
fourth-recorded lesson about `tar`/`git checkout` leaving a stale mtime behind
- doc/sizing.md section 5.5 - is now standing practice for this project's
injection harness, not merely this slice's).

| # | injection | caught by |
| --- | --- | --- |
| A | `measure_leaf` ignores `scroll_axis` entirely (ordinary bounded child) | `test_layout.cpp`'s scroll_axis case, `scrolling.verify_demo_scene`'s composition check |
| B | the WRONG axis is freed (`kVertical` frees `max_width` instead of `max_height`) | same two |
| C | `reposition()` applies the offset to the node ITSELF, not only its children | `test_scroll.cpp`'s "does not move itself" case |
| D | the offset is applied with the wrong sign (content moves the same way the pointer scrolls, not opposite) | `test_scroll.cpp`'s geometry case (exact expected coordinate) |
| E | `set_scroll_offset` skips damaging the OLD position | `test_scroll.cpp`'s byte-identity pair (frame 1) |
| F | `set_scroll_offset` is missing the no-op guard (re-asserting the same offset re-damages) | `test_scroll.cpp`'s "re-asserting is a no-op" case |
| G | `descend()`'s clip check is skipped for a scrolled subtree (hit testing ignores the clip once an offset is nonzero) | `test_scroll.cpp`'s "click just outside the clip" case |
| H | `WidgetSet::scroll_by` does not clamp at all | `scrolling.verify_demo_scene`'s overscroll check |
| I | the clamp uses `<=` instead of `<`, admitting one extra pixel of overscroll | same, exact-value assertion |
| J | `scroll_by` moves the axis IT WAS NOT ASKED to move (a vertical delta nudges a horizontal-only viewport) | `scrolling.verify_demo_scene`'s horizontal-axis case |
| K | the `grow`-under-unbounded-axis diagnostic is silently dropped, `room.main` collapses to 0 instead (the pre-existing dead branch) | `test_layout.cpp`'s grow-diagnostic case - **and every plain no-grow list item collapses to zero height**, which the byte-identity pair on the demo scene also catches |
| L | `total_grow`/`item.grow` are NOT zeroed under an unbounded axis (the dangerous half-fix) | `test_layout.cpp`'s grow case - the flexible child's height stops being 0 and starts being a multi-million-pixel number |
| M | `scrollable_owner_of` climbs through `owner_of`'s interactive check instead of its own kind check (a click on a button inside a scrolling list finds the button, not the scroller, for wheel purposes) | **nothing - see below** |
| N | `WidgetSet::scroll_by` reads a CACHED offset instead of `RenderTree::scroll_offset()` fresh each call | **nothing - see below**, then fixed by inspection |

**M - the term has no observable consequence today, and that is provable
rather than assumed.** No scene this slice built puts a clickable widget
inside a scrolling viewport - every list item is a plain panel - so
`scrollable_owner_of`'s climb and `owner_of`'s climb are asked about the SAME
set of nodes (none of which is ever `accepts_pointer()`) and necessarily agree.
The two functions are kept separate anyway, and the argument for keeping them
separate despite no test distinguishing them yet is written into
`doc/scrolling.md` section 6 above and into the function's own doc comment:
the day a clickable item lands inside a scroller, the two climbs diverge, and
sharing one would silently break wheel routing for that scene without a
single existing test noticing. This is the second diagnosis this project's
notepad already named - a term whose current single reader cannot
distinguish it, not a coverage gap - so no test was invented to force a
disagreement that does not exist yet; the separation is justified in the
comment instead, matching `doc/layout.md`'s defect 6 precedent for the same
shape of finding.

**N - caught by inspection before it reached a build, which is itself worth
recording.** Deliberately trying this injection surfaced that `WidgetSet`
holds no offset field to go stale in the first place (section 2 above records
why) - there was no cache to inject a staleness bug into. The candidate
injection was rewritten into "N: `scroll_by` computes the new offset from
`Widget::scroll_axis` alone and forgets to read `RenderTree::scroll_offset()`
as the STARTING point (always scrolls from zero)" and re-run; **this one WAS
caught**, by `scrolling.verify_demo_scene`'s overscroll check, because a
repeated small scroll would never reach the far clamp if every call started
over from zero. Recorded because the near-miss is the useful part: an
injection that cannot even be built because the bug's precondition does not
exist is evidence the architecture removed the bug's precondition, which
`doc/compositing.md`'s reasoning about `RepaintStats::layers` already
established as worth stating explicitly rather than silently skipping.

Twelve were caught immediately, one (N, in its corrected form) was caught
after being rewritten to be buildable, and one (M) is the provable-inert
case with the argument written down rather than a test invented for it -
14 total, 13 with a positive verdict one way or the other, matching this
project's now-five-times-established taxonomy of what a "did not catch it"
result actually means before concluding the tests are missing something.

---

## 9. On screen

`examples/10_scrolling`, offscreen via `--dump-png` (this machine's CPU raster
is byte-identical to what a real window would show, per
`doc/cpu-raster-findings.md` - a dump is not a mockup of the on-screen result,
it is the on-screen result, unpresented) and via a real SDL3/X11 window for
interactive use. Evidence under `.omo/evidence/drawgui-kernel/scrolling-*.png`.

| what | result |
| --- | --- |
| idle | the vertical list's first nine chips (a blue-to-purple ramp) and the horizontal strip's first chips visible, both at offset 0 |
| scrolled (vertical, +300 px) | the SAME nine-chip window now shows chips whose ramp position has shifted by ~6 items - a visibly different, contiguous slice of the same 24-item list |
| overscroll, scrolled far past the bottom | clamps at exactly `content_height - viewport_height` (708 px on this scene); the last (brightest) chip's bottom edge sits flush with the viewport's bottom edge, no gap, no stale pixel from a further scroll that did not happen |
| horizontal strip scrolled | the first chip is visibly cropped at the strip's left edge, chips shifted right versus the unscrolled frames - independent of the vertical list, which is unchanged in the same frame |

`--script` opens a real window and drives one wheel event through
`WindowManager::warp_pointer()` + the new `post_wheel()` - a real
`SDL_EVENT_MOUSE_WHEEL` on the platform's own queue, reported back through
the ordinary `pump()` path with `wheel_y = -3`, confirming the event travels
the same route a hardware wheel would rather than being synthesized past the
dispatcher. (The reported pointer *position* on this WSLg host is `0,0`
rather than the warped point - `SDL_GetMouseState` appears not to have
picked up mouse focus for a window this freshly opened under WSLg's rootless
X server in the single pump this mode allows; the same class of WSLg quirk
`doc/font-fallback.md` and `doc/widgets.md` already recorded for this
platform, not a defect in the scroll mechanism, which the delta value
confirms arrived correctly regardless of the reported position.)

`--probe 40,30` after `--scroll-vertical 300` reports `hit vertical item 6` -
`300 / (44 + 4) = 6.25`, so item 6 is exactly the item whose top edge has
scrolled up to that row, matching the hand-derived arithmetic rather than
merely "some plausible item".

---

## 10. What this does not do

Fling/decay, overscroll rebound animation (the clamp itself is built - see
section 1), nested-scroll delegation of a remainder to an outer scrollable
ancestor, scroll anchoring, keyboard scrolling, and scrollbar geometry as a
layout input - all named in section 1 with the specific reason and, where one
exists, the follow-up slice. Also: no gesture arena (section 6), no list
virtualization (section 1's phase-table check), no `List`/`Table` control -
this slice built the viewport a `List` will eventually stand on, matching
design.md section 5.6's own separation of `ScrollView` and `List` into two
MVP controls.

## 11. Property status after this slice

33 of the 46 properties fully implemented, 10 partial, 3 unimplemented.
`scroll_axis` (id 46) is new and implemented in full - both non-`none`
ordinals, refused where a node arranges its own children, refused on an
out-of-range ordinal. The scroll OFFSET itself was never a property
candidate; section 2 is the argument.
