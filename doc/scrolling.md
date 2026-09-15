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

**Update (slice 5-3, phase 5): closed.** `WidgetKind::kList` recycles a
fixed pool of nodes rather than windowing `local_bounds()`/`content_bounds()`
of a real, measured `kColumn` as this paragraph anticipated - a virtualized
list has no such column to window, which is exactly why it needed its own
mechanism rather than a policy layered on this slice's viewport. This
slice's own claims are otherwise unchanged by 5-3: `nodes_visited == 0` for
a plain offset (section 4 below) still holds exactly as measured here, and
5-3 both re-measures it and extends it to cover recycling too. See
`doc/list.md` for the full record; this paragraph is left as it was
written, not rewritten.

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

### 4.1 A finding the injection campaign forced, not predicted: damaging the OLD position is provably redundant for a CLIPPED scroll, and kept anyway

`set_scroll_offset` damages the old positions, updates the offset,
repositions, then damages the new ones - the same damage-then-move shape
`set_local_bounds` needs, written by direct analogy with it before this was
measured. Section 8's injection E removes the FIRST damage call, and it
**survives every test in the suite**, including the byte-identity pair that
exists specifically to catch a stale-pixel trail. That is not a test gap; it
is provable, the same way section 3's rounded-clip and layer-extent findings
in `doc/clipping.md`/`doc/compositing.md` were:

`invalidate()` calls `damage_subtree(id)` where `id` is the SCROLLING NODE
ITSELF, not merely its children - so the scrolling node's own
`visible_bounds()` is always one of the rectangles added, and that rectangle
is `intersect(node.absolute, node.clip_bounds)`, **neither half of which
moves when only a child's offset changes**. For a clipped scroll viewport
(`overflow: kClip`, which every scene this slice builds pairs with
`scroll_axis`, and which is the only configuration a "window onto oversized
content" means anything for), every child is confined inside that same
unmoving rectangle both before and after the scroll. So "damage the new
positions" already contains "damage the old positions" as a strict subset,
for any node whose scrollable content is clipped to its own box - the old
positions were never anywhere the new-position damage does not already
reach.

**Why it is kept rather than deleted, unlike `doc/compositing.md`'s
symmetrical case (the anti-alias slack on a layer's extent, which WAS
deleted once proven inert):** nothing in the type system stops a caller from
attaching `WidgetKind::kScrollView` to a node whose `overflow` is
`kVisible` - a misconfiguration, but not one this slice's boundary checks
reject, because `scroll_axis` and `overflow` are two independent properties
set independently. In that misconfigured case a scrolled child can move
outside the node's own box, and the "damage old, then new" shape is exactly
what keeps that case correct too. The redundant call costs one extra pass
over a small subtree per scroll event, which this project's stated position
on performance (not a gate) does not ask to be removed, and removing it
would trade a real (if minor) defence for a saving nobody measured needing.
Recorded here rather than silently kept, which is the same choice
`doc/clipping.md` section 7 made for the empty-clip guard.

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

Fourteen defects injected one at a time against the committed tree, each
built, each run through the full CTest suite, each reverted with
`git checkout --` and the source `touch`ed afterward (the standing lesson
about `tar`/`git checkout` leaving a stale mtime behind - doc/sizing.md
section 5.5 - applied from the start of this campaign rather than learned
from it again).

| # | injection | result |
| --- | --- | --- |
| A | `measure_leaf` ignores `scroll_axis` entirely (ordinary bounded child) | **caught** - `unit` and `scrolling.verify_demo_scene`'s composition/hit-testing checks |
| B | the WRONG axis is freed (`kVertical` frees `max_width` instead of `max_height`) | **caught** - same two |
| C | `reposition()` applies the offset to the node ITSELF, not only its children | **caught** - `scrolling.verify_demo_scene`'s hit-testing check (item 3 never appears where item 0 used to be) |
| D | the offset is applied with the wrong sign | **caught** - same |
| E | `set_scroll_offset` skips damaging the OLD position | **survived - provably, see section 4.1** |
| F | the re-assert no-op guard is missing | **caught** - `unit` |
| G | `descend()`'s clip check reads `node.local` instead of `node.absolute` (ignores the scroll shift) | **caught** - `unit` and `clipping.verify_demo_scene` (a general clip regression, not scroll-specific, since `descend()` is shared) |
| H | `WidgetSet::scroll_by` does not clamp at all | rejected by `-Werror=unused-variable` first (the now-unused clamp bounds); rewritten with `(void)max_x/max_y` to isolate it from that trap - **then caught** by `scrolling.verify_demo_scene`'s overscroll check |
| I | the clamp admits one extra pixel of overscroll (`max_y + 1`) | **caught** - exact-value assertion in the same check |
| J | `scroll_by` moves both axes regardless of `scroll_axis` | **survived on the demo scene** - see below; **caught** after a new unit test closed the gap |
| K | the `grow`-under-unbounded-axis diagnostic is dropped, `room.main` collapses to 0 again | **caught** - `unit`'s grow-diagnostic case, and the demo scene's every list item collapses to zero height, which the composition and hit-testing checks both catch too |
| L | `total_grow`/`item.grow` are not zeroed under an unbounded axis | rejected by `-Werror=unused-variable` first (the now-unread `main_bounded`); rewritten with `(void)main_bounded` - **then caught** by `unit`'s grow case |
| M | `scrollable_owner_of` climbs via `accepts_pointer` instead of its own `kind == kScrollView` check | **survived - provably, see below** |
| N | `scroll_by` starts from a zero offset every call instead of reading `RenderTree::scroll_offset()` back | **caught** - `scrolling.verify_demo_scene`'s overscroll check (repeated small scrolls never reach the far clamp) |

Nine were caught on the first attempt (A, B, C, D, F, G, I, K, N). Two (H, L)
were rejected by `-Werror` before a single test ran - the compiler naming an
now-unused clamp bound or an unread boolean, which this project has recorded
twice already (doc/clipping.md, doc/sizing.md) as evidence about the
compiler, not about the suite - and were rewritten into a form that
compiles, at which point both were caught. One (J) genuinely survived on the
scenes that existed at the time; the other two (E, M) survived and stay
survived, each for a different, specific, argued reason.

**J - the scene lacked the shape, and the demo's own scenes cannot supply
it.** `examples/10_scrolling`'s two viewports both have content that fills
the CROSS axis exactly - the vertical list's items are exactly as wide as
its viewport, the horizontal strip's chips are exactly as tall as its
viewport - which is an ordinary, realistic list layout, but it means
`max_x`/`max_y` on the axis a viewport does NOT scroll is already zero
before the injection does anything: `std::clamp(anything, 0, 0)` is 0
regardless of whether the clamp is reached through the right axis or the
wrong one. The bug had nothing to move even when it fired. The fix is
`tests/unit/test_scroll.cpp`'s new case, built with content deliberately
**larger than the viewport on BOTH axes** so a cross-axis delta has
somewhere to go - the general form of the lesson this project has recorded
before under a different name ("a diagnostic reachable only through a shape
nothing builds is a diagnostic with no caller"), applied here to a test
rather than to a diagnostic message.

**E - no observable consequence, and provable rather than merely
unobserved.** Section 4.1 above is the argument in full: `invalidate()`
already damages the scrolling node's own (unmoving, clip-confined)
`visible_bounds()`, which is a superset of anywhere a clipped child's old OR
new position can be. The code is kept anyway, as defence against a
misconfigured scene (`scroll_axis` set without a matching `overflow: kClip`)
that nothing currently prevents - recorded rather than deleted, which is the
opposite choice from `doc/compositing.md`'s symmetrical case (the anti-alias
slack on a layer's extent) and the reason for the difference is written down
in section 4.1 rather than asserted here.

**M - the term has no observable consequence today, and that is provable
rather than assumed.** No scene this slice built puts a clickable widget
inside a scrolling viewport - every list item is a plain panel - so
`scrollable_owner_of`'s climb and `owner_of`'s climb are asked about the SAME
set of nodes (none of which is ever `accepts_pointer()`) and necessarily
agree. The two functions are kept separate anyway, and the argument for
keeping them separate despite no test distinguishing them yet is written
into section 6 above and into the function's own doc comment: the day a
clickable item lands inside a scroller, the two climbs diverge, and sharing
one would silently break wheel routing for that scene without a single
existing test noticing. This is the same diagnosis as E, one section down -
a term whose current single reader cannot distinguish it, not a coverage gap
- so no test was invented to force a disagreement that does not exist yet.

In total: 9 caught immediately, 2 caught after being rewritten past a
compiler rejection, 1 (J) exposed a real, now-closed coverage gap, and 2
(E, M) are provably inert under every scene this slice builds, each with the
argument written down rather than a test invented to manufacture a
disagreement that does not exist. That is five distinct outcomes for
fourteen injections, none of them "nothing happened and nobody knows why" -
which is the standard this project's notepad has been holding injection
campaigns to since the first one found stale-mtime false negatives.

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
