# List virtualization: a permanent, recycled pool instead of one node per item

Slice 5-3. design.md line 617 puts virtualization inside `List`'s own
**definition**, not beside it as an optional optimization - quoted in full in
`doc/completeness.md` section 1 because that row's verdict rests on this one
sentence. This is the record of closing it: a fixed, permanently-allocated
pool of item nodes, recycled as the visible range moves, so that a 1000-item
list never has more than a handful of real nodes at once.

The short version:

- **No node-removal path was added, and none was needed.** `RenderTree`/
  `LayoutTree` are still append-only, exactly as `doc/widgets.md` section 1
  left them. Virtualization does not need to destroy a node when an item
  scrolls out of view - it needs to **repaint the same node as a different
  item** - so the highest-risk question the task poses (does a recycled
  `NodeId` dangle in `WidgetSet`, in `dg::Focus`, in a queued damage
  rectangle?) has a structural answer: **it never dangles, because it is
  never freed.** Section 1 below is the argument in full.
- **`kList` is an 8th `WidgetKind`**, not a mode on `kScrollView` and not a
  field on `NodeStyle`. Section 2 argues this from the project's own
  precedent rather than from preference.
- **Only the fixed-extent case is built.** Every item is exactly
  `list_item_extent` device pixels along `list_axis`. Variable-height rows
  are declined by name, in section 3, with the estimation/jitter argument the
  task asked for rather than a bare "out of scope".
- **The data-source seam needed no interface at all**, `virtual` or
  otherwise. `WidgetSet::list_sync()`/`list_scroll_by()` report which pool
  node now needs which logical item's content (a `NodeId, int` pair,
  `ListSlot`); the caller writes that content through the exact same
  `RenderTree::set_style()` an unrecycled node already uses. Section 4.
- **Recycling costs a repaint and never a relayout - measured, not assumed**,
  the same acceptance shape `doc/scrolling.md` section 4 already used for a
  plain offset. Section 6 is the measurement, and it directly answers the
  task's central question: 4-7's `nodes_visited == 0` invariant does **not**
  lapse. It holds for both halves of what a virtualized scroll now does -
  the continuous offset (already true since 4-7) and the discrete "swap
  identity" recycle (newly true, and not for free - section 6 explains why
  it had to be engineered that way rather than assumed).
- **design.md line 622's acceptance bar holds for an 11th consecutive
  slice**: zero new node/`RenderObject` kinds. `kList`'s pool nodes are
  ordinary `kLeaf` children carrying `NodeStyle::fill`/`text`/`image` -
  fields every node already had before this slice, none of them new.
- **The 1000-item measurement**, against a real pre-virtualization baseline
  (1000 permanently-allocated real nodes), is section 7. Both clear 60fps
  comfortably at 1000 items on this CPU-raster engine; the honest finding is
  that virtualization's payoff at THAT scale is real but modest, and the
  decisive number shows up further out, where the baseline's per-scroll cost
  grows with item count and the virtualized one does not.

---

## 1. Node lifecycle: removal was not added, and the pool is why

The task's own framing put this at the center: *"Check before designing. If
`RenderTree`/`LayoutTree` have no removal path, adding one is a significant
and genuinely new capability... whether a recycled NodeId can dangle... is
the highest-risk part of this slice."*

Checked before writing a line of `kList` code, not assumed:

```
$ grep -n "remove\|erase\|destroy" include/drawgui/render/render_tree.h \
                                    include/drawgui/layout/layout_tree.h
(no matches other than comments already explaining append-only design)
```

`doc/widgets.md` section 1 already recorded this as the standing invariant -
"nothing is ever removed, so an index never shifts under a widget that is
holding it... THE DAY REMOVAL ARRIVES that stops being true" - and
`src/render/tree_impl.h`'s own comment on `Node` repeats it: "Children hold
indices rather than pointers: nothing is ever removed, so an index stays
valid for the life of the tree." Every widget kind built through 5-2
(checkbox, radio, slider, scrollview, textfield, popup's overlay branch) is
consistent with this: none of them ever removes a node, only repositions or
restyles one.

**The decision this slice makes is to keep that invariant rather than break
it.** A virtualized list's whole *purpose* is that a node stops representing
one item and starts representing a different one as the visible range moves
- and the mechanism this slice builds for that (`WidgetSet::list_sync()`,
section 4) works by **restyling and repositioning a node that continues to
exist**, never by destroying the item that scrolled away and allocating a
new one for the item that scrolled in. The pool (`Widget::list_pool`, sized
once at `attach()` time to the viewport's extent, `kPoolSize` in the demo) is
never grown or shrunk after it is built. This is not a workaround for the
missing removal path - it is the correct design regardless: a real removal
capability would let a caller build a *different*, node-per-item list that
still had to destroy and recreate ~1000 nodes per full scroll, which is
exactly the cost virtualization exists to avoid. Fixed recycling is strictly
cheaper than "remove the old node, add a new one" even where removal is
available, because it reuses the render tree's own storage instead of
churning it.

**What this buys, stated as the risk the task asked to see addressed
explicitly:**

- **Dangling `NodeId` in `WidgetSet`**: impossible. `list_pool`'s `NodeId`s
  are assigned once, at `attach()`, and never change value or become
  invalid - there is nothing in this slice's design that could produce a
  `NodeId` naming a node that used to exist and no longer does.
- **Dangling `NodeId` in `dg::Focus` / interaction state**: not applicable to
  this slice at all. Pool item nodes carry no `Widget` of their own (they are
  plain content-bearing children, not registered with `WidgetSet` or
  `dg::Interaction`), and `dg::Focus` holds at most one `NodeId` for a
  `kTextField`, a kind this slice does not touch. There is no widget-level
  state anywhere that could outlive a pool node, because the pool nodes never
  die.
- **A stale damage rectangle from a previous frame naming a node that has
  since been recycled**: this is the one place recycling COULD have
  introduced a real defect, and the reason it does not is the same reason a
  slider's thumb move or a scroll offset already do not: `RenderTree::
  set_local_bounds()`/`set_style()` damage the node's OLD extent and its NEW
  one unconditionally, every call, exactly the shape `doc/damage-repaint.md`
  and `doc/scrolling.md` section 4 already established for an ordinary move.
  A damage rectangle queued for "this node, in its old position, showing its
  old content" is retired the instant the next `repaint()` runs, the same as
  it always was - recycling adds a NEW reason a node's bounds/style might
  change on a given frame, not a new RULE about what happens when they do.

**The genuinely new risk this slice's own design introduces, named rather
than hidden**: `Widget::list_assigned` (which logical index each pool slot
currently shows) is bookkeeping state that has no counterpart in any
previous `WidgetKind`, and a caller-visible bug class exists if it desyncs
from what is actually painted - not a dangling pointer, but a **dangling
ASSIGNMENT** (a slot whose bookkeeping says "logical 40" while its node still
shows logical 39's content, because something touched the node without
going through `list_sync()`). `list_sync()` is the only place `list_assigned`
is written, and it is also the only place a pool node's position OR content
identity changes correctness-relevantly - the residue tests in section 6
are what would catch this class of bug, and the defect-injection campaign in
section 8 deliberately targets exactly this bookkeeping.

---

## 2. Why `kList` is an 8th `WidgetKind`, argued from precedent

The task's instruction was explicit: argue this from the project's own
precedent, not from preference, and note that MVP-8 membership alone does
not make something a `WidgetKind` (`Box`/`Row`/`Column` are in MVP-8 and are
`LayoutKind`s, not `WidgetKind`s).

**The precedent, read straight**: 4-4 (`overflow`) and 4-5 (`opacity`) became
FIELDS on `NodeStyle`, not kinds, because painting, hit testing and damage
all had to read the SAME rule, and a field is the only shape that guarantees
agreement (`doc/clipping.md` section 6, `doc/compositing.md` section 5). 5-1
(`Image`) extended that argument to a THIRD reader (layout, uniquely, for the
size-before-decode rule) and reached the identical conclusion: a field
(`ImageStyle`), not a kind.

**4-7/4-8/4-9 (`kScrollView`/`kSlider`/`kTextField`) went the other way**,
and the reason is different in kind from the overflow/opacity/image
argument, not merely a different answer to the same question:
`WidgetSet` is *already* the place per-node state that OUTLIVES an event
lives (`doc/widgets.md`: "a checkbox earns its place... by carrying state
that outlives the click"). A scroll offset, a slider's value and a text
field's model all accumulate across an unbounded stream of events rather
than being declared once, which is the shape that earns a new `WidgetKind`
rather than a new `NodeStyle` field - the state does not belong on the node
struct every reader sees, because painting/hit-testing/damage do not need to
agree about it; only the ONE widget that owns it does.

**`kList` is squarely the second kind of case, sharpened further.** It needs
state no existing kind carries:

- `list_assigned` (which logical index each pool slot shows) - genuinely new
  bookkeeping, not a repurposed field.
- `list_pool` itself - a collection of nodes one widget owns and repositions
  as a unit, which no existing kind does (`kCheckbox`'s `indicator`,
  `kSlider`'s `thumb` and `kTextField`'s three children are each a SINGLE
  node with a fixed role; `kList`'s pool is `kPoolSize` interchangeable
  nodes whose ROLE - which item they represent - itself changes over time).
- `list_item_count`/`list_item_extent` - the arithmetic model that replaces
  a measured child entirely (section 1's design decision), which is not
  something `kScrollView`'s `scroll_content` (a real, measured node) has any
  use for.

**Could `kList` have been a MODE on `kScrollView` instead of its own kind?**
Argued against directly: `kScrollView`'s defining shape is "a clipping node
whose SINGLE child is measured under an unbounded constraint"
(`BoxStyle::scroll_axis`, a `LayoutTree` concept). `kList` has no such
child - there is nothing for `LayoutTree` to measure, because the whole
point is that 1000 items are never turned into 1000 real nodes for it to
measure. Folding `kList` into `kScrollView` would mean `kScrollView` sometimes
means "layout measures my one real child" and sometimes means "layout
measures nothing, I do my own arithmetic" - the same kind answering two
structurally different questions depending on a hidden mode flag, which is
the exact ambiguity a fifth/sixth/seventh kind existing at all is supposed to
prevent (`doc/widgets.md`: "a fifth kind is a fifth enumerator... which the
compiler will demand").

**design.md line 622, re-verified for this slice specifically**: does `kList`
need a new node/`RenderObject` kind? **No.** Every pool item is an ordinary
`kLeaf` `RenderTree` node using fields (`fill`, `text`, `image`, `overflow`)
that existed before this slice. The streak `doc/completeness.md` measured
through 4-9, and 5-1/5-2 each extended by one more slice, **extends through
5-3**: eleven consecutive slices (4-1 through 4-9, 5-1, 5-2, 5-3) that needed
zero new node kinds. This is worth stating plainly because virtualization was
the task's own candidate for "the item most likely to finally break the
streak", and it did not: the thing that changes for `kList` is entirely at
the `WidgetSet` layer (a new kind THERE) and at the `RenderTree` call-site
level (which existing setters get called, and when) - never at the level of
what a node fundamentally IS.

**A second thing `kList` needed that no prior `WidgetKind` did, and did NOT
need**: unlike `kScrollView`, `kList` needed **zero new `BoxStyle`/`NodeStyle`
fields**. `scroll_axis` was new in 4-7. `kList` reuses `NodeStyle::overflow`
(unchanged since 4-4) and `RenderTree::set_scroll_offset()` (unchanged since
4-7) for the clip and the continuous pixel offset, and needs no
`BoxStyle::scroll_axis` at all, because there is no single child to hand an
unbounded constraint to - every pool item gets an ordinary, tight box
(section 1's arithmetic model, section 5 below). Virtualization is, in this
narrow sense, CHEAPER in new primitives than the plain scrolling viewport it
sits beside.

---

## 3. Fixed extent only - variable-height rows declined by name

The task asked this be decided explicitly and declined by name if not built,
with the reasoning rather than a bare scope note.

**Built**: every item is exactly `list_item_extent` device pixels along
`list_axis`. This is what makes the whole mechanism arithmetic instead of
measurement - `content_extent = list_item_count * list_item_extent` answers
"how far can this list scroll" without ever laying out an off-screen item,
and `top_index = offset / list_item_extent` answers "which item is at the
top" the same way. Both are O(1) regardless of `list_item_count`, which is
the property the 1000-item bar (section 7) needs.

**Declined**: variable/dynamic item heights. The task named the two ways
this is usually attempted and asked for a decision between them, or a better
third option:

- **Measuring every off-screen item's height up front** defeats virtualization
  outright - it requires laying out the very items the pool exists to avoid
  creating, which is not a smaller version of the problem, it IS the
  unvirtualized problem with extra bookkeeping on top.
- **Estimate-then-correct** (assume a height, measure the real one once the
  item scrolls into view, adjust the scroll position to compensate) is a
  real, shippable technique (it is what several production virtualized-list
  implementations do), but it has a well-known, structural failure mode:
  every correction changes the mapping between scroll offset and content
  position for everything below the corrected item, which is visible to a
  user as the scrollbar (or the content under the pointer) jumping - "jitter"
  - exactly when the correction fires. Building it would also need a
  measurement pass this slice's fixed-extent model specifically avoids,
  reopening the O(item_count) cost virtualization exists to close.
- **No third option was found that avoids both costs.** A commonly proposed
  middle ground - a per-item height CALLBACK the caller supplies without an
  actual measurement (e.g., from cached metadata) - still needs the
  estimate-then-correct reconciliation the moment the callback's answer is
  wrong for even one item, which real content always eventually makes it be.

**This is declined, not merely deferred, and the reason is structural rather
than a matter of unspent budget**: fixed-extent virtualization proves the
mechanism (recycling, the seam, the residue discipline, the no-relayout
property) completely, on its own terms. Variable-height virtualization is a
different problem - a scroll-position reconciliation policy under
imperfect information - layered ON TOP of this one, not a generalization of
it. `Table` (design.md pairs it with `List` at line 617) is explicitly the
same kind of "different problem, not a generalization" and is named
out-of-scope for the identical reason: it needs its own column-layout model,
which this slice does not touch.

---

## 4. The data-source seam: built concrete first, extracted after

The standing rule this project has followed since its very first abstraction
was deleted (`README.md`: "an interface is extracted from at least one
working implementation, never written ahead of one") applies here at the
exact point the task predicted someone would reach for a callback interface:
something has to produce content for a logical item on demand, since a
virtualized list cannot own its items as nodes.

**Built first, in `examples/15_list/list_scene.cpp`**: `style_for(int index,
FontId, ImageId, ImageId, bool) -> NodeStyle` is a plain, `virtual`-free
function that computes a fresh `NodeStyle` for any logical index, and
`refresh()` calls `RenderTree::set_style()` with it for whichever nodes
changed. This is the ONE concrete, working implementation this slice's seam
was extracted from, exactly in that order - `style_for()` existed and was
exercised by `--verify-list` before `ListSlot` was named as "the seam" in
this document.

**What the seam turned out to be, once extracted**: `WidgetSet::list_sync()`/
`list_scroll_by()` return `std::vector<ListSlot>` - pairs of `(NodeId,
logical_index)` naming exactly which pool nodes now represent which items.
That is the WHOLE of the boundary between the engine and the application:
the engine never calls back INTO application code, and the application never
implements an interface the engine calls. The caller reads the vector and
writes content through the identical `RenderTree` setters an ordinary,
unrecycled node already uses (`set_style()`, or `set_fill()`/`set_text()`/
`set_image()` individually).

**This is the established technique named again, not a new one**:
`doc/popup.md` section 2 already recorded it for `PopupHost::show()` ("write
the concrete thing first, branch on a plain data value, do not write an
interface ahead of a second implementation that would justify one") and
`doc/form-controls.md`/`doc/scrolling.md` recorded the identical shape for
`WidgetSet::group_members()`/`scrollable_owner_of()` - a function that
reports FACTS about what changed, leaving the caller to act on them, rather
than a callback the engine invokes. `list_sync()` is that same shape one
level further: it does not just report a fact, it reports the MINIMUM set
of facts (only the slots that actually changed identity) a caller needs to
do the least possible work, exactly the way `group_members()`/
`scrollable_owner_of()` already do.

**Why this is a stronger answer than the callback the task expected someone
to reach for**: a `void(*)(int index, void* context, NodeStyle* out)`
function-pointer seam (the natural non-`virtual`, C-ABI-shaped alternative)
would still require this ENGINE layer to know the SHAPE of "content" (a
`NodeStyle`), coupling `WidgetSet` to `RenderTree`'s style struct beyond what
it already needs. The `ListSlot`-returning design needs no such coupling: the
engine's contribution ends at "this NodeId now means this integer", and
absolutely nothing about how that integer becomes pixels is engine-visible -
which is what a future C ABI binding would want anyway, since a host
language's own "produce content for row N" logic has no reason to be
expressed as a C function pointer INTO this engine at all; it is ordinary
host-language code calling `dg_node_set_text`/`dg_node_set_fill` the same way
any other row-count-independent code already would.

---

## 5. Recycling correctness: designing residue to be visible, not merely absent

The task named this the classic virtualization bug and asked for the demo
scene to be shaped so residue would be VISIBLE, scrolled in both directions
and jumped - and flagged 4-9's catalogued "missing scene shape" and
"assertion too weak" failure modes as exactly where this bites.

**The scene, shaped so every recycled field differs from its neighbours**:
`examples/15_list/list_scene.cpp`'s 1000 items cycle three INDEPENDENT
periods - fill (`index % 12`, twelve fixed colours), text (`"item #NNNN"`,
the zero-padded index itself, so a stale label shows a visibly WRONG NUMBER,
not a plausible-looking one) and image (`index % 3`: a cyan square, a
magenta square, or the placeholder colour with no source at all - three
states a viewer or an assertion can tell apart without decoding a pixel).
Because the periods (12, none, 3) share no common small factor with
`kPoolSize` (12) other than coincidentally matching the fill period exactly -
worth naming since it means TWO ring-buffer neighbours (nodes `kPoolSize`
apart in logical index, which is the shape any residue bug would actually
produce) show the SAME fill but DIFFERENT text and image, which is what
makes fill-only residue insufficient to catch on its own and text/image
residue independently diagnostic.

**The oracle is exact equality, not a loose shape.** `examples/15_list/
list_check.cpp`'s `check_exact_content()` reads a node's CURRENT logical
index back from where `list_sync()` actually placed it
(`local_bounds().y / item_height`, independent of `WidgetSet`'s own
bookkeeping) and compares its `fill`/`text.text`/`image.source`/
`image.placeholder` against `style_for()` of THAT index, field by field,
with `==`/`!=` rather than a substring or "is non-default" check - the
precise failure mode 4-9's own campaign found once already (`ellipsize()`'s
`.ends_with("...")` passing a whole family of wrong lengths).

**Scrolled forward, jumped, and backward - all three, not just forward.**
`check_residue()` scrolls 40 steps forward (ordinary recycling), jumps 900
items forward in one call (the "fast scrollbar drag" shape that recycles the
whole pool at once), scrolls 60 steps back (recycling the SAME slots a
SECOND time, in the opposite direction - the shape most likely to expose an
assignment that was only ever tested moving one way), and finally jumps back
to the very top. `check_exact_content()` runs after every one of the four
states, not only at the end.

**Why residue is structurally impossible here, not merely checked for**: the
task's phrasing - "carry NO residue... no stale text, fill, image,
hover/press state, or focus" - describes a discipline a careless
implementation could still violate even with a scene built to expose it, if
the fix were "remember to clear each field". This slice's `style_for()`
builds a **brand new `NodeStyle` from nothing** on every call, and
`refresh()`/`paint_item` write it via `RenderTree::set_style()` - which
replaces the node's ENTIRE style in one call, never the field-at-a-time
`set_fill()`/`set_text()`/`set_image()` an incremental update would use.
There is no old field for a forgetful call to leave behind, because nothing
here ever reads a node's PREVIOUS style before overwriting it. This is
provable rather than merely tested: injection F (section 8) demonstrates
what a REAL bug in this file looks like, and it is a geometry bug (wrong
axis), not a residue bug, because the residue class is closed by
construction.

**`hover/press state` and `focus`, named and explicitly out of scope for a
structural reason, not an oversight**: pool item nodes carry no `Widget` of
their own at all - they are plain content-bearing children, exactly the
shape `examples/10_scrolling`'s (non-virtualized) list items already are
("every item in both scrolling lists is a plain, non-interactive panel...
none is registered with `dg::Interaction` at all" - `doc/scrolling.md`
section 6). `kList` itself (the one node WITH a `Widget`) does not
`accepts_pointer()` either. So hover/press residue cannot occur because
there is no hover/press STATE anywhere in this slice's design for it to
reside in - the same diagnosis `doc/scrolling.md` section 8's finding "M"
already gave for `scrollable_owner_of`/`owner_of` agreeing vacuously.
Building clickable list items would reopen the gesture-arena question
`doc/scrolling.md` section 6 already declined for the identical reason
(click vs. drag-to-scroll competing for the same pointer stream) and is
out of this slice's scope by the same argument, not a new one.

---

## 6. The central tension: does recycling reintroduce relayout?

This is the question the task named as "the intellectual core of this
slice" and asked to be answered head-on, with measurement, not hand-waved.

**The tension, stated exactly**: `doc/scrolling.md` section 4 measured that
an ordinary scroll costs a repaint and never a relayout, because
`RenderTree::set_scroll_offset()` never calls anything in `LayoutTree`.
Virtualization adds a genuinely new kind of change on top of that - a pool
node's IDENTITY (which item it represents) changes as the visible range
moves, which sounds exactly like the shape that would need real layout: a
node's SIZE could plausibly depend on which item it now shows (a longer
label, a taller thumbnail), which is precisely the case `doc/scrolling.md`
section 4 named as the one that WOULD force a relayout ("if a future
feature made a child's own SIZE depend on its scroll position... the offset
would have to reach `LayoutTree::set_box`").

**The resolution: recycling is engineered to never touch a node's SIZE, only
its POSITION and STYLE - both are `RenderTree`-only concepts.** This slice's
fixed-extent decision (section 3) is what makes that possible: every pool
node's box (`width`/`height`) is set ONCE, at `attach()`-time, to
`list_item_extent` and the list's cross-axis extent, and NEVER changes
again, because every item - whichever logical index a slot currently shows -
is declared to be exactly the same size. `WidgetSet::list_sync()` therefore
only ever calls `RenderTree::set_local_bounds()` (repositioning, reusing the
node's own already-correct `width`/`height` read back from
`tree.local_bounds()`) and the caller only ever calls
`RenderTree::set_style()` (restyling) - NEITHER of which is a `LayoutTree`
method. This is the identical technique `kSlider`'s thumb and `kCheckbox`'s
indicator already used to move without triggering a relayout, applied to a
node whose IDENTITY changes rather than merely its value.

**Measured, per the task's own requirement, not merely argued from the code
shape**: `tests/unit/test_list.cpp`'s `"recycling costs a repaint and never
a relayout"` builds a REAL `LayoutTree` (not the raw-`RenderTree` shortcut
most of this file's other tests use), lays it out once, then does the
LARGEST possible recycle this slice can produce - a jump of 777 items,
which reassigns every one of the pool's 12 slots in a single call - and
reads `LayoutStats` back:

```
nodes_visited    = 0
nodes_relaid_out = 0
```

`examples/15_list --verify-list`'s `check_no_relayout()` measures the
identical property on the actual demo scene (1000 items, the design.md
number), for BOTH a small sub-item scroll and a 777-item jump, and both
report zero. **`4-7`'s `nodes_visited == 0` invariant does not lapse - it is
extended, not merely preserved**, to cover a kind of change (identity
recycling) that did not exist when it was first measured. The condition
under which it WOULD lapse is named exactly, matching `doc/scrolling.md`'s
own precedent for stating this precisely rather than gesturing at it: the
day an item's own SIZE becomes a function of which logical index it shows
(the variable-height case section 3 declines), recycling would have to
reach `LayoutTree::set_box()` and pay an ordinary relayout, exactly as a
future shrink-to-fit `TextField` would (`doc/text-input.md`'s identical,
named exception).

---

## 7. The 1000-item measurement, against a real baseline

design.md's P3 acceptance row (~line 1714) names "1000 项列表 60fps" as an
explicit criterion. Per the task's instruction, this is answered with a
measurement and its pre-virtualization baseline, not a target the numbers
were tuned to hit - performance is not a gate for this project.

`examples/15_list --bench [N]` builds BOTH the virtualized scene (a
`kPoolSize`-node pool) and `list_scene::build_baseline()` - `N`
permanently-allocated real `kLeaf` nodes stacked in a `kColumn` inside an
ordinary `kScrollView`, exactly `examples/10_scrolling`'s own shape
generalized past 24 items, painted with the identical `style_for()` content
so the comparison is content-for-content rather than "flat rectangles vs.
decorated rows" - then scrolls each through 300 steps (one item's extent per
step), repainting a real `RasterSurface` after every step. 300, not `N`,
because the baseline's own `repaint()` cost is `O(node_count)` per call (no
spatial index - `doc/damage-repaint.md`'s own recorded limit), so looping
`N` times at `N = 100000` would be `O(N^2)` and never finish; `N` still
governs how many real nodes the baseline allocates, so the node-count
comparison stays honest at any scale even where the timing loop is capped.

Measured on this host (Intel i5-1145G7, Release, `-O3 -DNDEBUG`, one run
each - `doc/cpu-raster-findings.md`'s own caveat about run-to-run variance on
this laptop under WSLg applies here too, so read these as "the right order
of magnitude" rather than three significant figures):

| | virtualized (pool) | baseline (real nodes) |
| --- | --- | --- |
| `item_count = 1000`, `node_count` | 14 | 1003 |
| `item_count = 1000`, build+`layout_full()` | 5.22 ms | 4.54 ms |
| `item_count = 1000`, 300 scroll+repaint steps | 37.44 ms (0.125 ms/step) | 50.02 ms (0.167 ms/step) |
| `item_count = 100000`, `node_count` | 14 | 100003 |
| `item_count = 100000`, build+`layout_full()` | 8.58 ms | 113.56 ms |
| `item_count = 100000`, 300 scroll+repaint steps | 38.57 ms (0.129 ms/step) | 4180.14 ms (13.93 ms/step) |

The 60fps frame budget is 16.67 ms/frame.

**The honest reading, stated plainly per the task's instruction not to tune
for a target**: **at 1000 items, BOTH approaches clear 60fps comfortably** -
0.125 ms and 0.167 ms per step are both roughly two orders of magnitude
under budget, and the ~1.3x gap between them is real but not decisive at
this scale, because 1003 flat, already-styled nodes is still cheap for this
engine's undamaged-region-bounded `repaint()` to walk. Virtualization's
measured payoff at exactly the number design.md names is therefore modest,
not dramatic, and reporting anything else would be tuning the story to the
number rather than reporting what was measured.

**The decisive number is `node_count`'s independence from `item_count`,
which shows up as `repaint()` cost the moment `item_count` grows past what
this CPU-raster path's un-indexed traversal can shrug off.** The virtualized
scene's per-step cost is FLAT (0.125 ms at 1000 items, 0.129 ms at 100000 -
unmeasurable difference, exactly the property a fixed pool is supposed to
buy) while the baseline's grows linearly with `node_count` (0.167 ms → 13.93
ms, a 108x increase for a 100x increase in item count, tracking `node_count`
almost exactly). **At 100000 items the un-virtualized baseline is still,
barely, inside the 60fps budget (13.93 ms of 16.67), but extrapolating the
same linear trend puts it OVER budget somewhere in the neighbourhood of
120000-130000 items on this host** - a real, attributable, and honestly
reported ceiling, not a number invented to make virtualization look
necessary. The cost is correctly attributed to `doc/damage-repaint.md`'s own
already-recorded limit ("no spatial index... will not be [fine] at 5000, a
real widget tree needs culling before it needs anything else") rather than
to anything specific to scrolling or to CPU rasterization in general - it is
the SAME `O(node_count)` traversal cost every repaint on this engine has
always had, made visible here because a real (as opposed to a synthetic)
list is the first scene this project has built with enough real content to
reach it.

---

## 8. Defect injection, against this slice's new engine logic only

Per the task's scoping instruction, only the NEW logic this slice adds -
`list_sync()`'s ring-buffer assignment and `list_scroll_by()`'s clamp/axis
arithmetic - was targeted, not previously-existing code this slice merely
calls. Six injections, each built, run against `tests/unit/test_list.cpp`
and `examples/15_list --verify-list`, then reverted with `cp`+`touch` (the
stale-mtime lesson `doc/sizing.md` section 5.5 already recorded, applied
from the first injection rather than rediscovered).

| # | injection | result |
| --- | --- | --- |
| A | ring-buffer slot computed as `logical / pool_size` instead of `logical % pool_size` | **caught immediately** - `unit` (wrong node/position) and the demo itself **crashes** (`std::vector::operator[]` out of range once `logical` exceeds `pool_size`) |
| B | the idempotency guard (`if (list_assigned[slot] == logical) continue;`) removed | **caught by `unit`** (four cases assert an EMPTY vector when nothing should have changed; all four now report stale slots); **survived on `--verify-list`** - content is recomputed identically to what was already there, so the residue check sees no wrong VALUE, only wasted work invisible to a content-equality oracle |
| C | `list_scroll_by`'s axis selection swapped (`kVertical` reads/writes `offset.x`, uses `dx`) | **caught immediately** - `unit`'s axis tests and `--verify-list`'s overscroll-clamp check (the vertical demo list never reaches its clamp because every scroll call moves the wrong, always-zero, axis) |
| D | the clamp floor (`std::max(0, ...)`) dropped from `max_offset` | **survived, provably** - every scene this slice builds has `item_count * item_extent >> viewport_extent`, so the floor is never reached; **closed** by a new test (`"a list shorter than its own viewport clamps its max offset to zero, not negative"`, a 3-item list in a 90px viewport) which crashes the injected build (`std::clamp`'s own debug assertion, `!(hi < lo)`) and passes cleanly once reverted |
| E | the in-range guard (`if (logical < 0 \|\| logical >= item_count) continue;`) removed | **survived, and for the "assertion too weak" reason 4-9's campaign already catalogued once**: at the scrolled-to-maximum offset, the pool's spare row asks for logical index `item_count` itself (one past the last real item) - `style_for()`'s deterministic formulas answer ANY integer with an equally well-formed-looking result, so no content-equality check notices. **Closed** by a new test asserting every VISIBLE logical index is inside `[0, item_count)`, which fails immediately against the injected build (`CHECK( 1000 < 1000 )`) |
| F | the horizontal/vertical branch in `list_sync`'s placement swapped (`kVertical` takes the HORIZONTAL branch) | **caught immediately and dramatically** - every pool node collapses to `y = 0`, so `--verify-list`'s residue check reports essentially every node as "showing logical 0" (because `logical_index_of()` reads back `y / item_height`, and every node now shares `y = 0`) - the exact shape the residue design in section 5 exists to make visible |

Four caught immediately (A, C, F - two of them dramatically, via a crash or
wholesale visible collapse) or by the unit suite alone (B). Two (D, E)
survived the FIRST pass and are now closed with regression tests that assert
the specific property each gap needed - D a scene shape none of this file's
other cases built (a list shorter than its own viewport, the identical
lesson `doc/scrolling.md`'s injection J already recorded under a different
name), E a bound no existing assertion checked (every visible index is
inside `[0, item_count)`, the identical lesson `doc/text-input.md`'s
`ellipsize()` case already recorded: a scene that CAN expose a bug is not
the same as an assertion precise enough to notice it). `tests/unit/
test_list.cpp` therefore ends this campaign with two more cases than it
started with, both real coverage gaps closed rather than merely logged.

---

## 9. Verification run, once each

- `ctest --test-dir build`: **20/20 passed** (19 from before this slice plus
  `list.verify_demo_scene`).
- `./build/examples/drawgui_render_png` → sha256
  `f635028e6e1e92349d23f0575f1e39e7ca8b05e5974492641ee610fe520df12e` -
  unchanged since 4-6, matching the value this task was given as ground
  truth.
- One `-Werror` build each, g++ 15.2.0 and clang++ 21.1.8, Debug: both clean,
  zero warnings.
- `-DDG_SANITIZE=ON` (clang++, ASan+UBSan): **20/20 passed**, ~213 seconds
  total (widget/clip/opacity scenes dominate, as they already did before this
  slice; `list.verify_demo_scene` itself is 0.14s under sanitizers). Node
  recycling with pooled, permanently-reused storage is squarely ASan's
  domain - see section 1's argument for why nothing here should trip a
  use-after-free, and the sanitized run corroborates it.
- clang-tidy `-p build` and clang-format, on every file this slice touched
  (`include/drawgui/widget/widget_set.h`, `src/widget/widget_set.cpp`,
  `examples/05_widgets/widget_scene.cpp`, `tests/unit/test_list.cpp`, all
  four `examples/15_list/*.cpp`): exit 0, zero `NOLINT`. Three real findings
  were fixed rather than suppressed along the way - `list_scene.cpp`'s
  `std::move()` into a `std::pair`'s by-value member (removed; `ImageCatalog`
  is a cheap reference-counted handle, so a plain copy was already correct
  and the move bound to the wrong overload), `Runner::handle_wheel` marked
  `static` (it touches no member state, unlike `handle_move`/`handle_up`
  which do), and `main.cpp`'s `parse_options` split into a `apply_valued()`
  helper mirroring `examples/10_scrolling`'s own precedent, to bring its
  cognitive-complexity score back under the configured threshold.
- CI's hand-maintained clang-tidy TU list (`.github/workflows/ci.yml`)
  updated with `tests/unit/test_list.cpp` and all four
  `examples/15_list/*.cpp`.

---

## 10. What this does not do

Named explicitly, matching the task's own out-of-scope list:

- **Variable/dynamic item heights.** Section 3, argued in full, including
  why the two usual techniques were rejected and why no third option closes
  the gap without reopening one of their costs.
- **`Table`.** design.md pairs it with `List` at line 617, but it needs its
  own column-layout model - a different control, not a generalization of
  this one.
- **Sticky headers / section indexes, item insert/remove animations, pull-
  to-refresh, incremental/lazy data loading, selection models and multi-
  select, drag-to-reorder.** None of these interact with the recycling
  mechanism this slice builds; each would be a feature layered on top of it,
  the same relationship `doc/scrolling.md` already drew between "a scrolling
  viewport" and "a `List`" one slice earlier. Insert/remove animation
  specifically shares fling's and caret-blink's already-declined
  precondition: no animation clock exists project-wide (`doc/scrolling.md`
  section 1, `doc/text-input.md` section 9).
- **Horizontal virtualization as a second demonstration.** `kList` supports
  `ScrollAxis::kHorizontal` in the mechanism itself (`list_axis` is not
  hardcoded to vertical anywhere in `WidgetSet`, and `tests/unit/
  test_list.cpp`'s `"the horizontal axis works the same way"` case proves
  it) - only a second, horizontal DEMO scene is declined, because vertical
  alone already proves the mechanism and a second axis would be the same
  argument twice.

## 11. Property status after this slice

Unchanged: **49 properties, 35 implemented / 10 partial / 4 not-yet.**
`kList`'s runtime state (`list_assigned`, the scroll offset it shares with
`kScrollView`) is not a property candidate, for the identical reason a
scroll offset and a slider's value already are not
(`doc/scrolling.md` section 2, `doc/form-controls.md` section 1.3): it
accumulates across an unbounded stream of wheel notches rather than being
declared once. No new property was needed or added.

`WidgetKind` values: **8** (`kPanel`, `kLabel`, `kButton`, `kCheckbox`,
`kScrollView`, `kSlider`, `kTextField`, `kList`). `LayoutKind` values:
unchanged, **6**. CTest entries: **20** (+1). Examples: **16** (+1,
`examples/15_list`).
