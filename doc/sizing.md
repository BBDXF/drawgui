# The second sizing stage: `basis`, `shrink`, `main_size`, `aspect_ratio`

Piece 4 of `doc/properties.md` section 4.4. It is the piece that was left last
on purpose, because it is the one that threatens the invariant every other
slice has been able to take for granted:

> **L3** - in one layout pass every node is laid out **exactly once**.
> (design.md section 5.4.1, and `doc/layout.md` for what this project bought
> with it.)

Slice 2 declined intrinsic sizing and wrote down why so that a later slice
would know what it was inheriting. This is that slice. **Section 1 settles the
argument before a line of the feature was written**, which is the order the
work was done in; sections 2 onwards are what came out of it.

---

## 1. The argument, settled

### 1.1 The question

A shrink or a basis resolution wants each child's *preferred* main-axis size
before it decides what to allocate. The obvious implementation measures the
child under a loose constraint to learn that preferred size, computes the
allotment, and measures it again under a tight constraint. That is a second
`layout()` of the same node, which is L3 gone.

`doc/wrapping.md` kept L3 through a genuinely order-dependent arrangement by
measuring every child under a **loose constraint on both axes**, so that a
child's size could not depend on anything that closing a run would change. The
first thing to establish is whether that trick generalises here.

### 1.2 It does not generalise, and here is why

The wrapping trick works because a run's membership is a function of sizes
already taken, and a size taken under `0 .. room` does not change when a run
closes. **Shrinking is the opposite shape.** The allotment a child receives is
a function of *every sibling's* base size, so no constraint computable before
the siblings are measured can be the one the child is finally laid out under.
The loose measurement is not merely *an* extra measurement here - it is the
input to the arithmetic that decides the real one.

Concretely: measuring child `i` under `0 .. room` yields `m_i`. The deficit is
`D = Σ m_j + gaps - room`, which needs every `m_j`. The allotment is
`m_i - D · w_i / Σ w_j`. It cannot be known before `m_i` is known, and it is
not equal to `m_i`.

### 1.3 What that costs if you just do it anyway

Let a container be *pressing* when it overruns its main axis and holds at least
one shrinkable child. A pressing container measures each such child twice: once
loose, once tight at the allotment.

Nesting is where this stops being a factor of two. Let `M(k)` be the number of
times a node at depth `k` of a chain of pressing containers is laid out:

```
M(0) = 1
M(k) = 2 · M(k-1)          each ancestor lays its subtree out twice
M(k) = 2^k
```

A chain of `d` pressing containers has `n ≈ 2d` nodes and costs `2^(d+1) = 2^(n/2)`
layouts. **That is exponential in the node count, not the O(n²) design.md
section 5.4.6 quotes** - section 5.4.6's figure assumes the caching it also
mandates, and this is what the same code costs without it.

The tree does not have to be contrived to be deep. A chain is narrow: thirty
nested rows, each holding one nested row and one leaf, is sixty nodes and
2^30 layouts.

### 1.4 Why the obvious cache does not fix it

design.md section 5.4.6 says: cache `(input constraints → intrinsic size)` per
node, invalidated by `mark_needs_layout`. `LayoutNode` already has exactly that
cache with one slot, keyed on `BoxConstraints`. Give it two slots and the loose
measurement and the tight one stop evicting each other.

**It still does not bound the recursion, and the reason is worth writing down
because it is the whole crux.** Container `C` measures child `D` twice:

| pass | constraint `C` hands `D` |
| --- | --- |
| basis | `main: 0 .. room_C`, `cross: X` |
| allotment | `main: s .. s`, `cross: X` |

`D`'s own room on the main axis differs between the two, so the basis
constraints `D` hands to *its* children differ between them too. Every cache
slot below `D` misses. The cache only pays off if the basis constraint is
**independent of the room the container has**, which means measuring the basis
with an **unbounded main axis** - which is exactly what design.md section 5.4.3
step 1 writes, and it is not a stylistic detail. With an unbounded main axis
`D`'s basis query is byte-identical in both passes, its children hit their
basis slot, and the total becomes `2n`.

So a fully general shrink has a known price, and it is not the second
measurement - it is **making unbounded main-axis constraints work everywhere**.
`doc/layout.md` records that no unbounded constraint is constructible in this
engine today; the root is tight to the viewport and every rule derives a
child's maximum from its parent's. Three arrangements have never executed their
unbounded branch:

- `measure_flex` sets `room.main = 0` when the main axis is unbounded, which
  collapses every child to nothing;
- `measure_wrap` puts every child in one run, which is right for max-content
  but has never been exercised;
- a `grow` weight against an unbounded axis is design.md section 5.4.7's first
  named layout error, and the code to report it was written and *deleted* in
  slice 2 because nothing could reach it.

That is a slice of work in its own right, and it lands scrolling's diagnostics
with it.

### 1.5 The decision

**Nothing is measured twice. L3 is preserved literally, not weakened.**

The four properties were re-derived from the question "what does this need
*before* a child is laid out?", and the answer turned out to be:

| property | needs a second measurement? | why not |
| --- | --- | --- |
| `main_size` | **no** | it changes the *container's* own extent after its children already have sizes. Children are sized before placement and placement never re-measures |
| `aspect_ratio` | **no** | it is a pure function of `(constraints, box)` and resolves *before* the first child is touched, or - when neither axis is settled - it only ever **grows** the finished box, which moves no child out of it |
| `basis` | **no** | an explicit basis is a *declared* number. A declared base needs no measurement at all, which is strictly better than the loose-constraint trick rather than a weaker version of it |
| `shrink` | **no, given a declared base** | the allotment is computed from declared bases before any of those children is laid out, so each is laid out exactly once, tight to its allotment |

So the generalisation of slice 4-3's trick is not "measure under a constraint
that cannot change". It is one step further along the same line:

> **A child whose base main-axis size is declared does not have to be measured
> to find it out, so the allotment can be computed first and the child laid out
> once, already at its answer.**

### 1.6 What that costs, stated plainly

`shrink` applies to a child whose base is known before it is laid out - it has
an explicit `basis`, or a definite size on the container's main axis
(`width` in a row, `height` in a column). A child with **neither** has a base
that is only knowable by measuring it, and it is **placed at its natural size
and reported**, in the node-path form design.md section 5.4.7 requires:

```
layout: shrink was not applied to child #7; its base main size is its measured
    natural size, and shrinking from a measured base needs a second layout of
    that subtree (invariant L3, design.md section 5.4.1). Give the child a
    `basis` or a definite width/height on the main axis.
    at root(column) > #2(row)
```

This is the same shape `doc/wrapping.md` chose for `align: stretch` inside a
wrap: accepted at the property boundary, degraded at layout, named out loud.
The boundary keeps accepting `shrink` because whether it can be honoured
depends on the *child's other properties*, which may be set in either order -
refusing it at set time would make `shrink` then `basis` fail where `basis`
then `shrink` succeeded.

### 1.7 The four questions the slice was asked, answered

**What exactly gets measured twice, and under what constraint each time?**
Nothing. Every node is laid out exactly once per pass, before and after this
slice. `stats.nodes_relaid_out` is still bounded by `nodes_total`, and
`tests/unit/test_sizing.cpp` asserts that directly for a tree that shrinks,
grows, derives an aspect and fills its main axis all at once.

**Why does that not make the result depend on measurement order?**
Because the only quantities that flow between siblings are declared numbers and
prefix sums over them. A base is read from `BoxStyle`, never measured; the
deficit is a sum, which is order-free; and the per-child share is
`share_upto(pool, weight_so_far, weight_total)`, whose value for child `i`
depends only on the multiset of weights and on `i`'s position, exactly as
`grow` already does. Children measured on today's path (`shrink == 0`, no
`basis`) are measured under the constraints they were measured under before
this slice, in the order they were measured in before this slice - which is
what keeps every existing baseline byte-identical.

**Are results cached, what is the key, what invalidates it?**
No new cache exists, because no result is computed twice. The one cache in the
engine is unchanged: `LayoutNode` keys `(needs_layout, constraints,
relayout_boundary) → size`, invalidated by `mark_needs_layout`. Two things had
to be *taught* to it rather than added to it:

- `limits_for()` now resolves `main_size` and `aspect_ratio`, and
  `layout_node()`'s `settled` test reads `limits_for()`. So a node whose main
  axis is filled, or whose second axis is derived, becomes a relayout boundary
  automatically and the cache key stays a complete description of its inputs.
  Had the resolution lived in `measure()` instead, `settled` would have
  disagreed with reality and the boundary would have been wrong in the
  direction that is silent.
- `shrink` and `basis` are **parentData**: the value is on the child and the
  row is what reads it. `differs_in_parent_data()` gained both, for exactly the
  reason `doc/properties.md` section 3.7 records for `grow` - a flexible,
  stretched child is tight on both axes, so a change to its own box is absorbed
  at the child and never reaches the container that hands out space.

**What is the complexity in the bad case, and can a pathological tree explode
it?** `O(n)` layouts per pass, unchanged. Per container the new arithmetic is
`O(k)` in its child count with a single pass of prefix sums, so a pass is still
`O(n)` overall. No tree can make it worse, because there is no shape whose
existence causes a node to be entered twice. The exponential of section 1.3 is
not reachable by any input; it is the cost of the design that was **not**
chosen, and it is written down so the slice that needs a measured base knows
what it is buying.

### 1.8 Where this contradicts design.md

1. **Section 5.4.3 step 1** - "lay out the `grow == 0 && shrink == 0` children
   with an **unbounded main axis** and record their natural sizes". Not done.
   Those children are measured under `0 .. room`, as they were before this
   slice. The reason is section 1.4: an unbounded main axis is what a *cached*
   two-pass design needs, and this design has no second pass to cache. Adopting
   it would change the size of every child that is naturally wider than its
   container, for the benefit of a mechanism that is not here.
2. **Section 5.4.3 step 4** - "`free < 0` and some `shrink > 0` → shrink
   weighted by `shrink × basis` (as CSS)". Done, with two deviations:
   `basis` means the *declared* base (section 1.6), and there is **no
   freeze-and-redistribute loop**. CSS re-runs the distribution when an item
   hits its minimum; here a child that floors at its `min_width` /
   `min_height` simply stops absorbing, and the residual overrun is reported by
   the diagnostic that already exists for it. The loop is pure arithmetic and
   would not have cost a measurement - it was left out because a second
   distribution rule needs its own semantics tests, and the residual is
   *visible* rather than silent.
3. **Section 5.4.3 step 3** - a `grow` child's base. CSS resolves
   `flex-basis: auto` to the item's own `width`. Here a `grow` child's base is
   its explicit `basis` or **zero**, never its `width`. That is not new: it is
   what the engine has always done, and changing it would move every flexible
   child in every existing scene. `basis` is how a caller asks for the CSS
   behaviour explicitly.
4. **Section 5.4.5** - "`aspect_ratio` as an optional property of `RenderBox`:
   once one dimension is determined, derive the other." Done, with the case the
   sentence does not cover made explicit: when *neither* dimension is
   determined, the content decides the box and the ratio then grows the
   deficient axis (section 4.3). Growing rather than shrinking is what keeps
   children inside the box they were laid out against.
5. **Section 5.4.6** - the intrinsic-sizing policy. Neither implemented nor
   contradicted: no intrinsic query exists, so there is nothing to cache and
   nothing to count. Section 1.4 is this project's derivation of *why* that
   section's cache is mandatory rather than advisory, which is more than the
   section itself says.

### 1.9 What is still not unlocked

`doc/properties.md` section 4.4 predicted that piece 4 would also unlock
`align: stretch` inside a wrap and `align: baseline` everywhere. **Neither is
unlocked, and both fail for the same reason as a measured base:**

- a run's cross extent is known only after every child in it has been measured,
  so stretching to it is a second measurement of a child that has one already;
- a baseline is a *measured* property of a child, so aligning on it needs the
  child before it is placed.

The prediction was right that they need the same machinery; it was wrong that
this piece would bring it. What they need is section 1.4's unbounded-axis
intrinsic pass, and that is now costed rather than hand-waved.

---

## 2. `main_size`

`min` (the default) hugs the content within the node's limits - today's
behaviour, unchanged. `max` makes the container take the whole main axis it was
offered.

It is implemented **entirely in `limits_for()`**, in four lines, and that is
the interesting part rather than an economy:

```cpp
if (arranges_children(box.kind) && box.main_size == MainSize::kMax) {
  // the main axis is settled at the maximum the parent permits
  low_main = high_main;             // when high_main is bounded
}
```

Three consequences fall out and none needed code:

- `border_box_size()` already clamps into `[low, high]`, so the container's
  size is the room it was offered.
- `place_flex_children()` already computes `leftover = inner_main - used` from
  the *final* size, so `justify` starts doing something on a container that
  used to shrink-wrap and therefore never had leftover space.
- `layout_node()`'s `settled` test reads `limits_for()`, so a `main_size: max`
  container whose cross axis is also settled becomes a **relayout boundary**
  without anybody declaring it one.

### What `main_size` is NOT, learned while building the demo

`max_height` on a `grow` child does not cap it. A `grow` child is handed a
**tight** main constraint, and `box.h`'s rule - the parent's constraint wins
over the child's own style, because the parent has already reserved that space
from ITS parent - means the maximum is ignored. The demo's ratio row was first
written that way and came out 370 tall, whereupon its 16:9 thumbnail derived a
width of 658 in a 604-wide row.

That is not a defect in `main_size` or in `aspect_ratio`; it is the constraint
protocol working as designed, and the fix is to express the intent as a
**weight** (the panel below it takes three quarters) rather than as a bound the
protocol is entitled to overrule. Recorded because the mistake is an easy one
to make twice, and because the failure it produced was instructive: two real
layout diagnostics fired AND the byte-identity check went red, because a full
repaint draws an overrunning child while a damage repaint clips it to the
viewport.

No child is re-constrained, because no arrangement reads the main-axis
*minimum* of its inner constraints: `measure_flex` and `measure_wrap` read
`main_max` and `cross_max`, `measure_leaf` loosens, and `measure_absolute`
reads a minimum only on an axis that is unbounded. That was checked rather than
assumed, and it is why the change is safe at four lines.

---

## 3. `basis` and `shrink`

### 3.1 Where a base comes from

```
basis set                    -> that value, clamped by the child's own
                                min/max on the main axis
else definite main size      -> width in a row, height in a column, resolved
                                the same way limits_for() would
else                         -> unknown; the child is measured, as before
```

A base is **not** clamped to the container's room. That is the whole point: a
base larger than the room is what produces the deficit `shrink` exists to
absorb. Clamping it would make `shrink` unreachable through `basis`, which is
the one route that needs no measurement.

### 3.2 The order of the passes, and why the default case is bit-identical

`size_flex_children` walks its children **once, in child order**, and puts each
into one of three groups:

| group | when | when is it laid out |
| --- | --- | --- |
| measured | `grow == 0`, no `basis`, `shrink == 0` | immediately, under `0 .. room` - the call this engine has always made |
| declared | `grow == 0`, and `basis` set or `shrink > 0` with a definite main size | after the deficit is known, tight at its allotment |
| grown | `grow > 0` | after the free space is known, tight at `base + share` |

Both new groups are entered only by a property that did not exist before this
slice, so a tree that sets neither takes exactly the calls, in exactly the
order, that it took before. That matters for more than pixels: `report()`
appends to a vector whose order `tests/unit/test_props_parity.cpp` compares
between the two API paths, so re-ordering the measurement of existing children
would have been observable even where the geometry was not.

The `used` accounting keeps one asymmetry from before the slice, deliberately:
a `grow` child contributes its **base** to `used` and **not its margin**,
because the margin comes out of the share (`main_extent = base + share -
margin`). With a base of zero that is the arithmetic the engine already had,
digit for digit.

### 3.3 The deficit distribution

```
free = room.main - used
free > 0 and Σgrow > 0    -> grow, unchanged
free < 0 and Σweight > 0  -> deficit = -free, distributed by weight
weight_i = shrink_i x base_i
extent_i = max(floor_i, base_i - share_i)
```

`shrink × base` is CSS's scaled shrink factor and design.md section 5.4.3 asks
for it by name. It is the right rule rather than a compatibility gesture: with
`shrink` alone as the weight, a 50 px item and a 500 px item both give up the
same number of pixels and the small one reaches zero while the large one is
barely touched.

**Shares are prefix sums, exactly as `grow` is.** `share_upto(pool, units,
total)` is the running total and each child takes the difference between two of
them, so the shares add up to the deficit exactly - no pixel is invented and
none is lost - and the extra pixels land on the earlier children rather than
disappearing. That discipline is not decorative: `doc/layout.md` records the
injected defect where a per-child division turned 100 across three equal
weights into 33/33/33.

`floor_i` is the child's declared `min_width` / `min_height` on the main axis.
A child that floors stops absorbing and the container reports the residual
overrun through the diagnostic it already had. See section 1.8 item 2.

### 3.4 The weight arithmetic, and the one place it is reduced

`weight_i = shrink_i × base_i` is a product of two quantities each bounded at
`2^24` by the property boundary, so a weight can reach `2^48` and a total can
exceed what `pool × units` may safely hold in 64 bits. The distribution
therefore **reduces every weight by one common divisor** so that the total fits
in 31 bits:

```cpp
const std::int64_t divisor = (total >> 31) + 1;   // 1 in any real scene
```

The divisor is a pure function of the weights, so an incremental pass and a
full pass reduce identically, which is the property that had to hold. In every
scene anybody will build the divisor is 1 and the arithmetic is untouched; the
reduction exists so that a hostile input degrades a ratio instead of
overflowing, and `tests/unit/test_sizing.cpp` drives it with weights that force
a divisor above 1 and requires the shares still to sum to the deficit exactly.

---

## 4. `aspect_ratio`

The ratio is `width / height`, as the table says.

### 4.1 One axis settled - the common case, and the sharp one

Resolved inside `limits_for()`, so it happens before the first child is
touched:

- width settled, height not → `height = round(width / ratio)`, clamped into the
  height limits, and the height becomes settled too;
- height settled, width not → `width = round(height × ratio)`, likewise.

The second row is the one the slice brief calls the sharpest test, and it is
worth being precise about why it is *not* a problem. A child of a row with
`align: stretch` receives a **tight cross constraint** - its height - computed
by the container from its own room, before the child is laid out. Deriving the
width from it is arithmetic on a constraint, not a measurement, so the child is
laid out once, with both axes already decided. The dependency runs
cross-axis-to-main-axis and stays entirely inside `limits_for(box,
constraints)`, which is a pure function of the node's own style and the
constraints handed down - never of a sibling and never of a child.

That is also what keeps the cache honest. `layout_node()` decides `settled`
from the *same* `limits_for()` call, so an aspect child in a stretched row is
recognised as a relayout boundary. Resolving the ratio in `measure()` instead
would have left `settled` reading the pre-ratio limits, and the boundary would
have been wrong in the direction nothing observes until something moves.

### 4.2 Both axes settled - the constraints win, and say so

A node whose width and height are both fixed by its constraints cannot honour a
ratio that disagrees with them. The constraints win, because the parent has
already reserved that space, and the disagreement is **reported** with the node
path rather than absorbed:

```
layout: aspect_ratio 1.6 cannot be honoured; both axes are already fixed at
    120x100 by the incoming constraints, which would need 160x100
    at root(column) > #3(row) > #9(leaf)
```

The check compares the derived extent against the fixed one, so a ratio that
*agrees* with the constraints is silent - a stretched square with
`aspect_ratio: 1` does not produce a message every frame.

### 4.3 Neither axis settled - grow, never shrink

A shrink-to-fit node has no settled axis until its content is measured. The
ratio is then applied to the finished border box, and it may only **grow** it:

```
w < h x ratio  ->  w = h x ratio
otherwise      ->  h = w / ratio
```

which is the smallest box of the requested ratio that contains the box the
content produced. Growing is not an aesthetic preference. Children were laid
out against the constraints derived from the *pre-ratio* limits and placed
inside the resulting box; a box that only ever grows still contains every one
of them, so nothing is pushed outside a rectangle it was measured against and
no node paints outside the rectangle it declared - which is the invariant the
whole damage system rests on. Shrinking the box afterwards would break exactly
that, and it is why this case is not "resolve the ratio and re-measure".

The grown result is clamped into the node's own limits, so a `max_width` still
wins.

---

## 5. Verification

`examples/09_sizing` puts the four properties on four rows whose behaviour the
window's own size drives, and `--verify-sizing` is the headless check against
that same scene. The on-screen result was verified by hand against the same
derived numbers, matching to the pixel.

### 5.1 What the byte-identity gate can and cannot say

`layout.incremental_equals_full` and the render-level identity cases drive
**two copies of the same code**. A wrong sizing decision corrupts both equally
and the comparison still passes - `doc/widgets.md` recorded the same structural
limit for widget semantics, and it applies here word for word. So the two
kinds of test do different jobs and both are required:

- **identity** answers "does doing less produce the same answer as doing
  everything" - four new cases, listed below;
- **semantics** answers "is that answer right", by pinning **hand-derived
  numbers**, computed on paper before the code was run.

### 5.2 The identity cases the brief names

| case | what it moves | why it is not covered by the existing scene |
| --- | --- | --- |
| surplus → deficit | the container narrows until `used > room` | the demo scene never overruns |
| grow → shrink | a child's `grow` is taken off and `shrink` put on, in one frame | flips which distribution runs, on an unchanged tree |
| aspect child, cross axis changes | a stretched row changes height | drives the cross-to-main derivation through the cache |
| nested inside a wrapped run | a shrinking row is a child of a wrapping band | composes the new arithmetic with the loose-constraint arrangement |

### 5.3 Hand-derived numbers

Every new rule is pinned to figures worked out by hand, including where the
integer remainder lands. The worked examples are in
`tests/unit/test_sizing.cpp` next to their assertions; the two that were most
worth checking on paper:

- **the deficit remainder.** Three children with bases 100 / 100 / 100 and
  shrink weights 1 / 1 / 1 in a room of 200 have a deficit of 100 and equal
  weights of 100 each. Prefix sums give cumulative shares 33 / 66 / 100, so the
  per-child shares are 33 / 33 / 34 and the extents are 67 / 67 / 66. The
  container ends at exactly 200: the extra pixel lands on the last child rather
  than being lost, which is the mirror image of `grow`'s 33/33/34.
- **the scaled shrink factor.** Bases 300 and 100 with shrink 1 and 3 give
  weights 300 and 300 - equal - so a deficit of 100 splits 50/50 and the
  extents are 250 and 50. Weighting by `shrink` alone would have given 25 and
  75, taking three quarters of the small item and a twelfth of the large one.

### 5.4 Order independence, tested rather than asserted

Section 1.7 claims the result does not depend on the order children are
measured in. The test that would expose it if it were false builds the same
row twice with the children **added in the opposite order** and requires each
child's extent to be the mirror of its counterpart. A distribution that leaked
state between children - a running remainder carried the wrong way, a base read
after a sibling was mutated - does not survive it.

A second case pins where the remainder lands and shows it is **positional**:
with equal weights the sequence is 67/67/66 read either way round, because the
extra pixel belongs to the last slot rather than to a particular child.

### 5.5 Seventeen defects injected, and what each one proved

A test that has never failed is not known to work.

| # | injected | result | caught by |
| --- | --- | --- | --- |
| A | the deficit split by a per-child division instead of prefix sums | caught | `unit`, `sizing.verify_demo_scene` |
| B | the deficit weighted by `shrink` alone, not `shrink x base` | caught | `unit` |
| C | a shrinking child no longer stops at its declared minimum | caught | `unit` |
| D | the declared base clamped to the room, so no deficit can form | caught | `unit` |
| E | a `grow` child's base becomes its own width | **survived**, see below | `unit`, after a test was added |
| F | `shrink` and `basis` dropped from `differs_in_parent_data` | caught | `unit` |
| G | `main_size` fills the cross axis instead of the main one | caught | `unit` |
| H | `main_size` never fills anything | caught | `unit` |
| I | `aspect_partner` multiplies where it should divide | caught | `unit`, `sizing.verify_demo_scene` |
| J | the both-settled half of the aspect guard removed | **survived, and provably nothing can catch it** | - |
| K | the ratio shrinks a finished box instead of growing it | caught | `unit` |
| L | the derived extent truncated instead of rounded to nearest | caught | `unit`, `sizing.verify_demo_scene` |
| M | the ratio resolved BEFORE `main_size` settles the main axis | **survived**, see below | `unit`, after a test was added |
| N | a declared child's base counted twice into `used` | caught | `unit`, `sizing.verify_demo_scene` |
| O | free space measured without the `grow` children's bases | caught | `unit` |
| P | the shrink weight reduction removed | caught | `unit` |
| Q | the measured-base shrink refusal stops being reported | caught | `unit` |

Fourteen were caught on the first run. The three that were not came apart into
the three diagnoses this project has learned to tell apart before acting.

**E and M - the scene lacked the shape.** Both are real rules with no instance
anywhere in the suite or the demo:

- E only matters when a `grow` child ALSO declares a size on the main axis, and
  it takes **two** flexible children to see even then, because with one the
  total is conserved either way: `extent = base + (room - used - base)`. The
  case built for it is a rigid child of 100 in a room of 300 beside weights 1
  and 3, one of which declares a width of 80: the shares are 50 and 150, where
  counting the width as a base would have made them 110 and 90 - a different
  split of the same total.
- M only matters on a node that asks for `main_size: max` **and** an
  `aspect_ratio`, which nothing did. Resolved in the right order a row filling
  a 400-wide page at 2:1 is 400x200; resolved the wrong way round the ratio
  finds neither axis settled, declines, and the row shrink-wraps its absent
  content to 400x0 - a row that has vanished.

**J - the term has no observable consequence, and that is provable.** Deriving
into an axis that is already settled clamps the result into `[low, high]` where
`low == high` by definition, so it can only produce the value already there.
The guard's both-settled half therefore cannot change any output, and no test
can be written that it changes. It is kept for what it SAYS rather than for
what it prevents - the clamp is an accident of how the derivation happens to be
written, and a later derivation that stopped clamping would need the line back -
and the proof is now in the comment above it so the next reader stops where
this one did.

**One measurement error, of the kind this project has now made four times.**
Injection N reported `ANCHOR MISS` rather than a result: `clang-format` had
rewrapped the line the probe was anchored on, so the replacement never landed.
Re-anchored, it was caught immediately. The harness fails loudly on a missed
anchor precisely because "not caught" and "never applied" are indistinguishable
from the outside, and the harness also `touch`es every source after both
injecting and restoring, because `tar` preserves mtimes and ninja would
otherwise skip the rebuild.

### 5.6 The verified matrix

g++ 15.2.0 and clang++ 21.1.8, Debug and Release, `-Werror`, plus
`-DDG_SANITIZE=ON` on both compilers - six configurations, 14 CTest entries
green in each. `drawgui_render_png` output is
`f635028e6e1e92349d23f0575f1e39e7ca8b05e5974492641ee610fe520df12e` in all six,
unchanged from before the slice, and the four golden baselines are untouched at
zero tolerance. clang-format and clang-tidy clean, the latter through the CI's
`clang-tidy -p build` form - a relative path would have silently swallowed the
header diagnostics as non-user code.

clang-tidy found four real defects in this slice's first draft, none of them
suppressed: two functions over the cognitive-complexity threshold (split), an
unchecked `std::optional` access where the compiler could not see that a
`basis` implies a declared base (restructured into `DeclaredBase`, which is
clearer anyway), and a float widened to double inside a `doctest::Approx`.
