# Incremental layout: what it costs, and what makes it correct

Sub-step 2 of step 3. Sub-step 1 built damage-driven repaint and measured it at
27x; this builds the layout engine that feeds it, and makes changing one leaf
re-lay-out only what that change can reach.

All numbers are Release, g++ 15.2.0, i5-1145G7, WSLg/x11. On-screen figures are
paced to a 16 ms budget; the offscreen ladders are tight loops and are labelled
as such.

Reproduce with:

```sh
./build/examples/drawgui_layout                          # the demo
./build/examples/drawgui_layout --scope --size 1280x800  # the scope table
./build/examples/drawgui_layout --bench --frames 500     # the timing ladders
ctest --test-dir build -R layout                         # the correctness gate
```

---

## The headline

| | |
|---|---|
| Changing one leaf in a 92-node tree | **4 nodes recomputed (4.3%)**, 8 entered |
| Changing one cell in a 40,001-node tree | **2 nodes recomputed**, 0.023 ms vs 2.70 ms full - **119x** |
| Layout time on the demo scene, paced | 0.0076 ms incremental vs 0.0213 ms full - 2.8x |
| Incremental bounds vs full bounds | **identical, node for node, every frame** |
| A 10x10 dot repainted inside a square container | **144 px** |
| The same dot inside a rounded container | **5,760 px - 40x** |

The last two lines are the finding sub-step 3 has to design against, and they
are not a layout cost at all - see "The corner radius bill arrives at repaint
time" below.

---

## The constraint model, and why

**Flutter-style single pass: constraints down, sizes up, every node laid out
exactly once.** design.md section 5.4.1 asks for this by name (invariants
L1-L3) and this slice implements it. The alternative considered was CSS-style
multi-pass reflow.

The decision was made on one property, and it is specific to this project.
Sub-step 1 measured partial repaint at 27x, which makes incremental re-layout a
precondition rather than an optimisation. In a protocol where a node's size is
a pure function of `(constraints, own style, children)`, "this subtree's inputs
did not change, so its outputs cannot have" is a two-line check:

```cpp
if (!node.needs_layout && node.constraints == constraints) {
  return node.size;          // subtree never visited
}
```

In a reflow model where a later sibling can retroactively change an earlier
one, there is no such check. Every attempt to add one is a cache with a
correctness obligation nobody can discharge, and the acceptance bar for this
slice is exactly that obligation discharged.

### Everything is integer device pixels - a deliberate deviation from 5.4.9

design.md section 5.4.9 keeps layout in float logical pixels so that a DPI
change repaints without re-laying-out. This implementation uses integer device
pixels throughout.

The reason is measured, not stylistic. Sub-step 1's damage system is correct
only when the rectangle a node declares is exactly the rectangle it paints, and
a float layout has to round somewhere. A node whose left edge slides from 10.4
to 10.6 rounds from 10 to 11, so for one frame its damage rectangle and its
painted rectangle disagree by a pixel - which is precisely the class of bug the
byte-identity test exists to catch, and it would catch it only on the frames
where the fraction happened to cross a boundary. That is a test that fails once
a fortnight, which is worse than one that never runs.

**What the deviation costs**: a DPI change re-lays-out rather than only
repainting. That is close to free, because a DPI change also changes the
framebuffer size, which forces a full repaint anyway; the relayout rides along
on a frame that was already the most expensive in the session. Section 5.4.9's
stated benefit - cross-screen window movement without relayout - is preserved
in substance, because 0.008 ms of relayout is not what makes that frame slow.

Integer flex weights follow from the same argument. `BoxStyle::grow` is an
`int`, distributed by prefix sums so the shares add up to exactly the space
available (100 across three equal weights is 33/33/34, not 33/33/33 with a
pixel lost). Float ratios would make the incremental-equals-full comparison a
coin toss on the frames where a sum landed near a half.

### What is implemented

Box model per design.md section 5.9.4: always border-box, `width`/`height`
include border and padding and exclude margin, **margin is applied by the
parent**, gap and margin stack rather than collapsing. Six arrangements:
`kLeaf` (shrink-to-fit), `kRow`, `kColumn` (gap, integer grow, four main
alignments, four cross alignments, plus per-child `align_self`), `kWrapRow`,
`kWrapColumn` (the same, minus `grow` and minus `stretch`, plus `run_gap` and
six `align_content` values), `kAbsolute` (every row of design.md section
5.4.2's table - two near edges, two far edges, two opposite edges for a stretch,
and unanchored).

Wrapping arrived a slice later than the rest and `doc/wrapping.md` is its
record. The one thing worth repeating here is why it did not disturb anything
above: a wrapping container measures every child under a **loose** constraint,
so a child's size never depends on which run it lands in, and closing a run
cannot invalidate a measurement already taken. That is what keeps L3 - and it is
also why `align: stretch` cannot be honoured inside a run, since the extent to
stretch to is not known until the run closes.

**No intrinsic sizing, deliberately.** `intrinsic_width(height)` and friends are
speculative layout: they break the exactly-once invariant and section 5.4.6 puts
them at O(n^2) worst case. Nothing here needs them - a row obtains its
inflexible children's natural sizes by laying them out with the room available,
which is the ordinary pass and not a second one. **Sub-step 3 should assume they
do not exist.** A layout that genuinely needs a child's size before allocating
(table column auto-fit, baseline alignment) has to add them, pay the caching
section 5.4.6 requires, and measure what the second pass costs.

---

## Two invalidation mechanisms, working in opposite directions

**Upward, a relayout boundary stops the dirty mark.** A node whose size is
settled before its children are consulted cannot change size however its
contents rearrange, so its parent has nothing to reconsider.

**Downward, constraint equality stops the recursion.** A node re-entered with
the constraints it was last laid out under, and not itself dirty, returns its
cached size and its subtree is never touched.

The demo reports both numbers - nodes *entered* and nodes *recomputed* -
because reporting only the second would flatter the design. A pass that walked
the whole tree and cheaply decided to do nothing would have a small second
number and a large first one, and calling that incremental would be a claim the
numbers do not support.

### The measured defect: tight constraints alone are not enough

The first implementation made a node a boundary only when its incoming
constraints were tight. That is one of Flutter's two conditions, and on the
92-node demo scene it looked fine - the scope table was already small, because
the downward mechanism was absorbing the difference.

The tree-size ladder is what exposed it. A list is a column of fixed-height rows;
a row's *size* is fixed by its own style, but the constraint it receives from
the column is `0..available` on the main axis, which is not tight. So the mark
climbed past every row to the root, and the cost of changing one cell grew
linearly with the whole list:

| nodes | incremental, boundary = tight constraints only | after adding the second condition |
|---|---|---|
| 109 | 0.0011 ms (3x) | 0.0006 ms (8x) |
| 1,001 | 0.0026 ms (15x) | 0.0040 ms (10x) |
| 10,001 | 0.0103 ms (44x) | 0.0105 ms (45x) |
| 40,001 | 0.0223 ms (115x) | 0.0226 ms (119x) |

The second condition is: **a node whose own style fixes both dimensions inside
what its parent permits is also a boundary**, because its size is settled before
any child is consulted.

That condition has a trap, and getting it wrong is silent. A size determined by
a style *can* change when the style changes, so a mark raised by the node's own
box changing must not stop there - only a tight incoming constraint, which the
style cannot override, still absorbs it. `mark_needs_layout` therefore takes
`own_style_changed` and applies a different rule to the first node in the walk
than to the rest.

The remaining growth in the "after" column is not tree size: it tracks the width
of the row that contains the change (8 cells at the top of the ladder, 159 at
the bottom). That is O(the boundary), which is the claim.

### Re-layout scope on the demo scene, 92 nodes, 1280x800

| change | entered | recomputed | % of all | moved | damage px | repaint px |
|---|---|---|---|---|---|---|
| leaf resizes its parent, siblings shift | 8 | **4** | 4.3% | 6 | 21,400 | 22,043 |
| leaf resizes inside a pinned card | 4 | **3** | 3.3% | 2 | 62,274 | 62,274 |
| leaf inside a row inside a column | 3 | **2** | 2.2% | 1 | 1,248 | 1,248 |
| box reassigned its own value | 4 | 2 | 2.2% | **0** | **0** | **0** |
| fill colour only, no geometry | **0** | **0** | 0.0% | 0 | 0 | 144 |
| all five in one frame | 15 | 9 | 9.8% | 9 | 83,520 | 83,982 |
| full layout, for comparison | 92 | 92 | 100.0% | - | - | 1,024,000 |

The two rows that matter most are the last two of the five. A box assigned the
value it already had does real work - it is marked dirty and its boundary is
laid out again - and moves nothing and damages nothing, which is the case that
catches a pass writing bounds unconditionally. A fill colour enters layout zero
times, which is the case that catches layout invalidation leaking out of the
render tree's.

---

## Layout-induced damage, and why it was not re-derived here

`LayoutTree` owns its `RenderTree` rather than sitting beside one. Layout's
entire output is "where every box is" and the render tree's entire input is
"where every box is"; a second structure holding the same rectangles is a second
structure that can disagree. Node indices are shared - layout node `i` IS render
`NodeId{i}` - so there is no mapping table to keep in step.

Movement damage then falls out for free. `RenderTree::set_local_bounds` already
damages the pixels a node vacated *and* the pixels it now occupies; that is what
sub-step 1 built and what its byte-identity test verifies. Layout's job is only
to notice which boxes moved and call it for exactly those. Re-deriving the
old-plus-new rule here would have been a second implementation of the thing most
likely to leave trails.

That this was the right call is measurable: deleting the vacated-bounds damage
(injected defect 2 below) fails the pixel-identity test at frame 1, and the fix
is in one place rather than two.

---

## The corner radius bill arrives at repaint time, not at layout time

Sub-step 1 established that Skia's anti-aliased rounded rectangles are not
clip-invariant, so a rounded node must be repainted whole and any damage
rectangle touching one grows to contain it plus a pixel of slack.

**The consequence for layout is not visible in layout's own numbers.** Compare
the two columns of the scope table with the containers rounded:

| change | damage px | repaint px, square | repaint px, rounded | factor |
|---|---|---|---|---|
| leaf inside a row inside a column | 1,248 | 1,248 | **5,760** | 4.6x |
| fill colour only (one 10x10 dot) | 0 | **144** | **5,760** | **40x** |
| leaf resizes its parent | 21,400 | 22,043 | 44,016 | 2.0x |
| all five together | 83,520 | 83,982 | 132,439 | 1.6x |

The `damage px` column is identical in both cases, because layout damages node
bounds and node bounds do not depend on a radius. The whole cost appears after
the repaint has expanded the damage. **A layout that reported its own damage
figure and stopped would report a corner radius as free.** The demo therefore
reports repainted pixels, not damaged pixels, and prints both container styles
side by side.

The paced on-screen figure over a whole session at 1280x820 is 21,223 px/frame
square against 49,776 px/frame rounded - 2.3x. That is smaller than the 40x
above and than sub-step 1's 30x, and the reason is that this scene already
follows sub-step 1's advice: the small accents are rounded in *both* modes, so
the switch only changes the containers, and the session average mixes cheap
frames with expensive ones. **The 40x single-node figure is the one sub-step 3
should design against**, because a hover highlight or a caret is exactly a small
node inside a container.

The rule for sub-step 3, restated: a rounded container makes its entire area the
smallest unit of damage anything inside it can produce. Round the leaves, not
the containers, and make the theme layer say so.

---

## Correctness: the gate, and the proof that it can fail

`layout.incremental_equals_full` is a CTest entry with no display dependency, so
CI gates it on every configuration. It builds two identical trees, applies the
same mutation to both, lays one out incrementally and the other from scratch,
and requires **every node's computed bounds to be identical** - at three
viewport shapes (including odd ones, so the surface pads its rows) and both
container styles, 180 frames each.

The same comparison runs inside `unit`, which additionally renders both trees
and compares the framebuffers byte for byte, composing this slice with
sub-step 1's verification.

Covered, one case per class the acceptance bar names: a leaf changing size; a
leaf whose change does not escape its card; a change inside a row nested in a
column; a change that affects nothing; a fill colour that layout must ignore; a
resize mid-script; and all of them landing in the same frame, which is the case
a dirty list that assumes a single root gets wrong.

Two further cases are built by hand in `test_layout.cpp` rather than taken from
the demo scene, because the scene has no instance of either: a relayout
boundary nested inside another one, and an ancestor that stops being a boundary
while its descendants' constraints do not move. Both arrived from defect
injection and are covered under defects 5 and 6 below.

### Six defects injected, four caught outright, two that exposed real gaps

A test that has never failed is not known to work.

| # | injected | caught by | symptom |
|---|---|---|---|
| 1 | `is_boundary()` returns true for any laid-out node - the mark stops one level too early | `layout.incremental_equals_full` (exit 1) and 6 doctest cases | 4 nodes differ at frame 0; node 40 at x=332 instead of 312 |
| 2 | movement damages only the new bounds, not the vacated ones | the pixel-identity case in `unit` | diverges at frame 1 - the trail |
| 3 | the constraint-equality early-out ignores `needs_layout` | `layout.incremental_equals_full` (exit 1) and 8 doctest cases | 6 nodes differ; a stale 90x28 where the full pass says 70x28 |
| 4 | flex shares divided per child instead of by prefix sum | `test_layout.cpp` | 33/33/33 instead of 33/33/34 - one pixel lost |
| 5 | the dirty-root sort comparator reversed to deepest-first | **nothing, until a test was added for it** | now `test_layout.cpp`: `dirty_roots == 2` where the ordering promises 1 |
| 6 | the relayout-boundary term deleted from the early-out key | **nothing, and provably nothing can** | none - the term is inert under today's single reader |

Defect 4 is the reason `test_layout.cpp` exists alongside the identity test.
Identity says the two paths agree; it cannot say they agree on the right answer,
because both use the same distribution. The semantics tests pin the
arrangements to concrete numbers, where a wrong answer is a wrong answer rather
than a consistent one.

A seventh attempt - replacing the prefix-sum distribution with a plain division -
never reached the tests: `-Werror=unused-parameter` rejected it at compile time.
Worth recording as evidence that the warning configuration is doing work.

### Defect 5: the ordering was real, the scene could not see it

`run()` sorts the dirty roots shallowest-first so that a relayout boundary
nested inside another one is already clean when its turn comes, and is dropped
rather than laid out a second time. Reversing that comparator entirely left
every test green.

The reason is that **the demo scene contains no nested boundaries**. Its four -
the toolbar, the card, the sidebar item, the badge layer - sit in four
different subtrees, and no order of four independent subtrees is wrong. A
scene cannot exercise an ordering rule it has no instance of, however many
frames it runs for.

The fix is a tree built by hand in `test_layout.cpp`: a column with a definite
height inside a stretched root (settled on both axes, therefore a boundary),
containing a row with a definite height (settled again, therefore a boundary
nested inside the first) and a shrink-to-fit sibling. Dirtying one leaf under
each on the same frame puts two roots in the list, one an ancestor of the
other. With the sort as written the pass reports **one** dirty root; reversed,
it reports two.

The investigation also corrected the comment above the sort. It claimed the
ordering was "what the incremental-equals-full comparison needs", and that is
**not true**: under the reversed comparator every bounds assertion still
passed, including the byte-identity render. A nested boundary laid out too
early is simply laid out again on the way down from its ancestor and its
rectangle overwritten, so the order is a cost property, not a correctness one.
The claim has been narrowed to what the injection actually demonstrated.

The index tie-break within a depth is load-bearing for a different reason,
now also written down: it makes the comparator a total order, which the
`std::unique` on the next line needs. A node whose own constraints are tight
absorbs its own style change and is pushed on *every* `set_box`, not only the
first, so duplicate roots do occur.

### Defect 6: a cache-key term that no test can justify

The early-out in `layout_node()` keys on `(needs_layout, constraints,
relayout_boundary)`. Deleting the boundary term leaves every test green, and
the comment that justified it was wrong in a way worth recording: it said the
divergence "only happens on a resize, which re-lays-out everything anyway".

That is false. The state is reachable from an ordinary style change, and a
probe build proved it: with a `kRow` root switched from `kStretch` to `kStart`,
a flexible child stops being settled - it keeps its tight main axis from the
grow weight and loses its tight cross axis - while the shrink-to-fit node under
it is handed byte-identical constraints. The probe fired on exactly that node.

But the divergence is unobservable, and that is a property of the code rather
than of the test suite. The stored boundary is only ever read by
`is_boundary()`, which compares it against the node's own index, and
`boundary == index` holds precisely when `index == 0 || settled` - a pure
function of the box and the constraints the key has *already* pinned equal. The
probe confirmed it: on every divergence, the stale and the fresh value were
both non-self. A stale boundary and a fresh one therefore agree on the only
question anybody asks of them.

So no test was added for the term, because none can exist while there is one
reader. What was added is a test for the *state*, which had no coverage at all:
an ancestor that stops being a relayout boundary while its descendants'
constraints do not move. It passes with the term and without it, and it is
there because the scenario is a real one that nothing else in the suite
constructs.

The term stays. The equivalence that makes it inert is a property of today's
single reader, and the obvious next optimisation - having `mark_needs_layout()`
jump straight to `nodes[index].relayout_boundary` instead of climbing to it -
reads the stored value for its own sake. Removing the term now would buy a
handful of cache hits and leave a trap for that change; the reasoning is in the
comment so that the next reader does not repeat the hunt.

---

## Diagnostics: one is reachable, the rest are not yet

design.md section 5.4.7 requires constraint conflicts to be reported with a node
path rather than turned into a plausible-looking wrong answer. One such conflict
is reachable in this slice and is implemented:

```
layout: children overrun the main axis by 30 px (130 needed, 100 available)
    at root(column) > #1(row)
```

Overflow is the conflict a shrinking window produces first, and it is genuinely
silent: each child fits the room on its own, so nothing upstream catches it, and
the frame simply has children running past the edge of the box containing them.

**Every other conflict section 5.4.7 lists is downstream of an unbounded
constraint, and no unbounded constraint is constructible here.** The root is
laid out tight to the viewport and every rule derives a child's maximum from its
parent's, so boundedness is inherited by induction. Two such diagnostics were
written, and both were deleted when the tests for them could not be made to
fire - a message nothing can reach is a message written for a caller that does
not exist, which is the mistake this project already made once with the twelve
platform headers. The unbounded axis arrives with `RenderViewport` and
scrolling, and the diagnostics arrive with it.

What remains is the arithmetic that makes `kUnbounded` safe (`shrink_bound`,
`is_bounded`), because `BoxStyle::max_width` defaults to it and it is `INT_MAX`,
so a plain subtraction silently turns "no maximum" into a very specific large
number. Those have live consumers and unit tests.

---

## Where this contradicts design.md

1. **Section 5.4.9, layout in logical pixels.** Contradicted, and the reasoning
   is above. Integer device pixels; a DPI change re-lays-out, and that is
   cheaper than the rounding bug the alternative buys.
2. **Section 5.4.7, node-path diagnostics for constraint conflicts.** Partially
   implemented and honestly so - one reachable conflict, the rest deferred to
   the code that can produce them.
3. **Section 5.4.2, Stack sizing from its non-positioned children.** Not
   implemented. Every child of a `kAbsolute` node here is positioned, so there
   are no non-positioned children to measure, and the container fills the room
   it is offered. That is both the single-pass answer and the useful one - an
   overlay wants to cover what it overlays. The two-step sizing arrives with the
   first non-positioned child.
4. **Section 5.4.11's line estimate.** The document estimates the whole layout
   subset at under 1,000 lines. This slice implements roughly the Flex + Stack +
   BoxConstraints part of it (`RenderWrap` is not built) in about 700 lines of
   `src/layout/` plus 250 of headers, which is consistent. *`RenderWrap` landed
   a slice later, in about 180 further lines - see `doc/wrapping.md`.*
5. **Sections 5.4.3 and 5.4.4 together, on `align: stretch` inside a wrap.**
   Section 5.4.3 defines stretch as a tight cross constraint; section 5.4.4
   requires a wrap to lay each child out exactly once; and a run's extent is not
   known until every child in it has been measured. The two cannot both hold, so
   stretch under a wrapping container is placed as `start` and reported as a
   layout diagnostic. `doc/wrapping.md` section 3.1.
6. **Section 5.15.7, "single-frame layout under 2 ms for ~500 nodes".** Not
   contradicted but wildly conservative for this workload: a *full* layout of
   10,001 nodes is 0.48 ms and of 40,001 nodes is 2.70 ms, tight-loop. Layout is
   not where this project's frame budget goes; presentation is (sub-step 1
   measured 12.7x there against 63x for raster). **A future slice should not
   spend effort optimising layout on the strength of that target.**

## Honest limits

- **The demo scene is 92 nodes.** The tree-size ladder is synthetic - a column
  of rows of leaves - and is the right shape for a list, which is what
  virtualisation (section 5.15.4) is about, but it is not a real application.
- **No text.** A layout that has to measure a string will re-enter `SkFont`, and
  section 5.15.5 says text shaping is the CPU cost driver. None of the timings
  here include any of that.
- **No scrolling, so no unbounded constraints**, which is why a third of section
  5.4.7 is unimplemented rather than unimplementable.
- **`--bench` is a tight loop.** Sub-step 1 measured a tight loop understating a
  paced frame by 2.7x for the same work. The demo's on-screen figures are the
  paced ones and are the ones to quote.
- **WSLg is not a native desktop.** The presentation half of every on-screen
  number needs re-measuring on native X11, Wayland and Windows.
