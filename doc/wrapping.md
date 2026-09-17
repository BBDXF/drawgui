# Wrapping, per-child alignment, and per-side borders

This slice built the two pieces `doc/properties.md` section 4.4 called 1 and 5:
a wrapping arrangement, and the per-child / per-side overrides. Between them
they moved six properties out of the gap report - `run_gap`, `align_content`,
`align_self` and the paint half of `border_width_l/t/r/b`.

The interesting part is not the feature. It is that wrapping is the first
arrangement whose child positions depend on **other children's sizes in a
non-prefix way**, and this project's acceptance technique is byte-identity
between an incremental pass and a full one. Most of this document is about why
that still holds.

---

## 1. The shape of the problem

Every arrangement before this one had a property that made incremental layout
easy without anybody having to say so: a child's position depended only on the
children **before** it. Change one child and every earlier sibling is still
right; every later one shifts by a known amount.

A run boundary breaks that.

- A run's cross extent is the tallest child **in that run**, which is not known
  until the run is closed.
- Which children are in that run depends on sizes that appear **later** in the
  list than the change.
- Closing a run early or late moves children that come **after** the change onto
  a different line entirely.
- Changing how many runs there are resizes the container, which moves
  everything **below** it.

So a single-pixel change to one chip can relocate half the band and resize its
parent. That is the case the byte-identity gate had to be extended to cover, and
it is covered from both ends: a hand-built scene in `tests/unit/test_wrap.cpp`
with cases that name the two transitions explicitly, and a new mutation in the
demo scene (`kWrapRebreak`) that sweeps a chip through a 150 px triangle wave so
the band spills an extra run and takes it back, on every frame of
`layout.incremental_equals_full`.

## 2. Why exactly-once survives: loose constraints

design.md section 5.4.1 invariant L3 says every node is laid out exactly once
per pass, and section 5.4.4 says `RenderWrap` preserves it. Both hold here, and
the reason is one line in `size_wrap_children`:

```cpp
const PixelSize size = layout_node(
    child, axis.constraints_of(0, shrink_bound(room.main, axis.main_total(margin)), 0,
                               shrink_bound(room.cross, axis.cross_total(margin))));
```

**Loose on both axes.** A child is measured against the room the CONTAINER
offers, never against the run it turns out to land in. Its size is therefore a
pure function of `(container room, own style, own children)` - which contains no
term that closing a run can change. Runs are then formed from sizes that are
already final, and closing one never invalidates a measurement already taken.

That is the whole argument, and everything awkward in the rest of this document
follows from refusing to weaken it.

### The incremental path needs no new machinery, and that is a consequence

There is no cached run assignment anywhere. `WrapLayout` is built inside
`measure_wrap` and dies with it. This is not frugality - it is what the loose
constraint buys: since runs are a pure function of the children's sizes and the
container's room, and both are already part of `layout_node`'s cache key, a
container re-entered with the same constraints and no dirty mark **cannot** have
different runs. There is nothing to invalidate because there is nothing stored.

The mark still has to REACH the container, and that is where the work was:

- A child's own style change never absorbs at the child under a wrap, because
  the constraints it was handed are loose and `mark_needs_layout()` only absorbs
  on a tight one. It climbs to the container automatically.
- A change to a grandchild that resizes the child does the same, provided the
  child is not itself settled - and if it IS settled its size cannot change, so
  neither can run membership. Both branches are correct for the same reason.
- **parentData is the exception**, exactly as `doc/properties.md` section 3.7
  records for `grow`. `align_self` joins `margin`, `grow` and the four stack
  insets in `differs_in_parent_data`, so a change to it also marks the parent.

## 3. Where design.md and this engine disagree

### 3.1 `align: stretch` cannot mean what it means in a flex

design.md section 5.4.3 step 5 says stretch gives the child a **tight cross
constraint**. Section 5.4.4 says a wrap lays every child out **exactly once**.
Section 5.4.4 does not say what stretch means on a wrap, and **the two
requirements it does state are incompatible there**:

the extent to stretch to is the child's RUN's extent; a run's extent is the
tallest child in it; that is not known until every child in the run has reported
a size; so honouring stretch means laying those children out a second time.

Flutter's `Wrap` resolves this by not having stretch at all. CSS resolves it by
being multi-pass. This engine does neither: **`align: stretch` and
`align_self: stretch` under a wrapping container are placed as `start`, and the
arrangement says so at layout time** with the node path attached, in the form
section 5.4.7 asks for.

The value is still **accepted** at the property boundary rather than refused
with `kUnsupported`, which is a deliberate departure from how the gap report
treats an unimplemented value. The reason is that the answer depends on the
CONTAINER, and a child's `align_self` outlives any particular parent kind - a
node whose container is a wrap today may be a flex tomorrow, and discarding its
stretch at set time would silently lose a value that becomes valid. The struct
API has no return channel at all, so a layout diagnostic is the only place the
two paths can say the same thing. They do, and
`tests/unit/test_props_parity.cpp` compares `diagnostics()` between them.

This is the one place the slice was tempted into piece 4 (a second sizing
stage). It is left alone deliberately; section 4.4 of the gap report now records
that stretch-inside-a-wrap is one of the things piece 4 would unlock.

### 3.2 `grow` under a wrap is refused, and design.md agrees

Section 5.4.4: "不支持 wrap 下的 `grow`（CSS 里这个组合的语义本身就令人困惑）".
The table agrees - `grow` is `consumed_by = ["flex"]`. So a `grow` on a child of
a wrapping container is `kNotApplicable` at the property boundary and a layout
diagnostic through the struct API. The mechanism is the same one stretch hits:
a weight would have to be resolved against the run the child lands in, and the
run is not known until it is closed.

### 3.3 `align_self` is consumed by wrap as well as flex

The table said `consumed_by = ["flex"]`. That is now `["flex", "wrap"]`, which
is a metadata change to `props/drawgui.props.toml` - no id moved, no name
changed, and `prop_lock.py --check` is untroubled. A run aligns children on the
cross axis in exactly the way a single-line flex does, and there is no argument
for a child being able to override one and not the other.

### 3.4 Node kind is still not a property

`kWrapRow` and `kWrapColumn` are `LayoutKind` values, chosen at `add_child`
time, exactly as `kAbsolute` is. `direction` flips a wrap between its two axes
and a flex between its two, and turns neither into the other. This extends
`doc/properties.md` section 3.1 rather than changing it, and
`tests/unit/test_props_parity.cpp` pins that `direction` on a wrapping container
leaves it wrapping - the failure mode that folding direction into the kind
invites.

### 3.5 An overrun is reachable only through margins

`measure_flex` reports children that overrun the main axis. The wrapping
equivalent looks like it should be unreachable, and very nearly is:

a child's definite width is clamped by `limits_for` to the constraint it was
given, and the constraint is the container's room. So a child asking for 260
inside 200 becomes 200, wraps normally, and overruns nothing.

**A margin escapes that.** The constraint is reduced by the margin while the
space the child occupies adds it back, so a margin wider than the container
yields a zero-width child that still occupies more than there is. That is the
only reachable overrun, it is reported, and
`tests/unit/test_wrap.cpp` covers both halves - the margin case fires the
diagnostic, and the width case asserts that nothing is said.

The first draft of that test used a width of 260 and failed, because the
clamp made it unreachable. Recording it because the general form is worth
carrying: **a diagnostic written by analogy with a sibling arrangement is a
diagnostic whose reachability nobody has checked.**

## 4. Integer distribution, per run rather than once

The existing flex distribution hands out space by prefix sums, so shares add up
to exactly the pool and the extra pixels land on the later children - 100 across
three equal weights is 33, 33, 34. Wrapping multiplies the places that has to
happen: main-axis slack is distributed **per run**, and cross-axis slack is
distributed **across runs**.

Cross-axis distribution is by POSITION rather than by weight - "how much belongs
before run 3 of 5" - so it needed `share_upto(pool, units, total)`, the running
total on its own. `run_lead` returns a **cumulative** figure and the caller
advances by the difference between consecutive ones. A per-gap value rounded
independently loses up to a pixel per gap.

`space_around` is the case that shows it. Half a share outside the first and
last run, a full share between adjacent ones, so the gaps are 1, 2, 2, ..., 2, 1
in units of half a share and run `i` is preceded by `2i + 1` of them. With 54
pixels across two runs that is 13 before, 27 between and 14 after - the odd
pixel lands at the end rather than vanishing. `tests/unit/test_wrap.cpp` and
`tests/unit/test_props_parity.cpp` both pin the literal 13 and 66.

An injection that replaced the cumulative form with `(leftover / (2 * count)) *
(2 * index + 1)` was caught by both.

## 5. Per-side borders: two paint routes, and why

`BoxStyle::border` always reserved space per side. `NodeStyle::border_width` was
a single `float`, because the painter insets by half the width and strokes once,
so `border_width_*` set the painted stroke to the **minimum of the four**. That
was exact whenever the four agreed and wrong otherwise, and
`doc/properties.md` section 3.3 recorded it as a table-versus-engine
disagreement.

`NodeStyle::border_width` is now a `BorderWidths`. The painter takes one of two
routes:

| case | route |
| --- | --- |
| four widths equal | the original centred stroke, inset by half |
| four widths unequal | `drawDRRect` over the ring between the border box and the box the four widths inset it to |

**The uniform route is kept deliberately, not out of caution.** Every measured
pixel this project holds - four golden baselines at zero tolerance, the
`f635028e...` hash of `drawgui_render_png`, three demos' byte-identity gates -
was produced by it. A feature about UNEQUAL borders has no business rerouting
the equal case.

One filled annulus rather than four stroked edges, because the table gives all
four sides one colour: mitre joints are invisible, and four overlapping bands
would double-blend a translucent border at the corners while a single fill
cannot.

The inner shape's corner radii are `max(0, r - max(w_a, w_b))` for the two
adjacent sides, and the choice of `max` is a containment proof rather than a
preference. `drawDRRect` requires the inner shape inside the outer one; with
that formula the inner corner's centre sits `|w_a - w_b|` from the outer
corner's centre, and

```
|w_a - w_b| + (r - max(w_a, w_b)) <= r
```

holds for every non-negative pair, because `|a - b| <= max(a, b)`.

### The test that had to exist

`tests/unit/test_props_parity.cpp` asserts the four widths reach `NodeStyle`
intact. That is a **different claim** from "the painter draws four widths", and
the gap between them is precisely where the old behaviour lived - the four
insets were always stored per side and it was the painter that collapsed them.
A test reading the style back would have passed throughout.

`tests/unit/test_border_paint.cpp` therefore measures the frame: it walks inward
from each edge and counts pixels carrying the border colour. Everything is
integer-aligned with square corners, which slice 1 measured to be bit-exact
under any clip, so a pixel is either border or fill with no blend in between.
It takes both colours from the rendered frame rather than from a constant, so it
does not depend on knowing whether the surface is BGRA or RGBA - the trap slice
2 recorded against `kN32_SkColorType`.

An injection collapsing the painter back to a uniform border was caught by it,
including the case that the minimum rule could never express: a border on two
sides only, where one zero side used to zero every side.

## 6. On screen

`examples/04_layout` gained a wrapping band whose width follows the window, so
resizing re-breaks the runs in front of a human rather than only in a test. The
band has no height of its own, so a change in the number of runs resizes it and
moves everything below.

Captured under WSLg at four window sizes:

| window | runs | band height |
| --- | --- | --- |
| 1500x700 | 1 | 44 |
| 1100x760 | 2 | 75 |
| 820x760 | 2 | 75 |
| 640x900 | 4 | 131 |

The band's border is `{6, 2, 10, 4}`. Measured off the captured frames by
walking inward from each edge, at **every one of the four window sizes**: left
6, top 2, right 10, bottom 4. That is the per-side painting confirmed on a real
X11 window rather than only in an offscreen surface.

The `align_self` chip is deliberately the shortest one in the band. The band
centres its children, so an override is only visible by as much as the chip's
own slack, and the first version of the demo made it a chip near the run's
tallest - where the override moved it by **one pixel**. At height 12 against a
run of 26 it moves seven, and the capture confirms it sits at its run's top
(y 182) where centring would put it at 189.

### Hit testing after a reflow

`layout_check.cpp` gained `verify_hit_after_rewrap`, which runs in CTest as part
of `layout.verify_demo_scene`. At six window widths it resizes the scene, then
requires `hit_test` to agree with a brute-force paint-order oracle at **every
pixel of the band**. The oracle is built from the parent links alone, so it
shares no code with the thing it checks.

It also asserts that the ladder produced at least three DISTINCT run counts.
Without that, six passes over one arrangement would look exactly like six passes
over six, which is the notepad's "the scene may lack the shape" lesson made
automatic instead of left to whoever chose the widths. It currently reports 1,
2, 2, 2, 3, 3 runs.

## 7. Proving the tests can fail

Twelve defects injected, each built and run against the full CTest list, each
reverted afterwards.

| # | injected defect | verdict |
| --- | --- | --- |
| 1 | run break at `>=` instead of `>`, so a child that exactly fills the room starts a new run | CAUGHT |
| 2 | `align_content: space_around` divides per gap instead of by prefix sum | CAUGHT |
| 3 | `align_self` honoured while placing but ignored while sizing | CAUGHT |
| 4 | `align_self` dropped from `differs_in_parent_data` | **SURVIVED** - see below |
| 5 | `align` applied across the container instead of within each run | CAUGHT |
| 6 | painter collapses four border widths back to one | CAUGHT |
| 7 | `run_gap` not applied between runs | CAUGHT |
| 8 | `apply_bounds` reports only the destination | **SURVIVED** - see below |
| 9 | `align_content: stretch` grows every run by the whole leftover | rejected by `-Werror` |
| 10 | (4) retried against a hand-built flex shape | CAUGHT |
| 11 | `set_local_bounds` stops damaging the vacated box | CAUGHT |
| 12 | (8) retried against a one-rectangle damage cap | CAUGHT |

### Injection 4: the scene lacked the shape, and the shape was not in a wrap

Dropping `align_self` from `differs_in_parent_data` left every test green,
including the wrapping test written specifically for parentData delivery.

The cause is structural and is worth stating because it is the opposite of
intuition: **the wrapping tests cannot see this defect, because a wrapping
container makes the delivery redundant.** Children of a wrap are handed loose
constraints, so a child's own style change never satisfies
`mark_needs_layout()`'s absorb rule and climbs to the container anyway.

The shape that needs the delivery is a child that is TIGHT on both axes, and
only a flex row produces one: a flexible, stretched child. `test_layout.cpp`
gained that case, beside the three that already existed for `grow`, `margin` and
the stack insets. The retry was caught.

Generalisable form: **when a field is shared by two containers, the container
where a rule is REDUNDANT is not evidence about the container where it is
load-bearing** - and it is the redundant one that is most likely to be where the
new feature put the test.

### Injection 8: the right defect, aimed at the wrong copy

Removing the vacated-bounds term from `apply_bounds` survived, and the reason is
that `apply_bounds` is not what makes the pixels correct.
`RenderTree::set_local_bounds` damages the old subtree on its own account,
before it moves anything. The `add_subtree` calls in `apply_bounds` only feed
`LayoutStats::damage_area` - the number the demo REPORTS.

So there were two claims hiding under one injection, and they needed separate
tests:

- the pixels are right - injection 11 aims at `set_local_bounds` and is caught
  by the byte-identity test over a re-break;
- the reported number is right - injection 12 is the original defect, now caught
  by a test that reads `LayoutStats` under a **one-rectangle damage cap**.

The cap is what makes that assertion possible at all. Under the default cap of
eight the region keeps its rectangles disjoint, so its total area is deliberately
far smaller than any bounding box and nothing can be concluded by comparing the
two - the first version of the test failed for exactly that reason, 4200 against
7820. At a cap of one the region IS the bounding box.

### Injection 9: `-Werror` got there first

The first form of the align_content stretch defect left a parameter unused and
failed to compile, which is the trap the notepad records from slice 4-1: a
compiler diagnostic is not the test suite. It was rewritten into a form that
compiles (`share_upto(leftover, index > 0 ? count : 0, count)`) and then caught
by the stretch case.

## 8. Honest limits

- **No stretch inside a run**, and no baseline anywhere. Both need piece 4.
- **No `grow` inside a wrap**, matching design.md, so a wrapping band cannot
  have a child that fills the rest of its line.
- **Runs are formed greedily and never rebalanced.** A last run holding one
  chip stays that way; CSS does the same, but a `balanced` mode would be a
  second sizing stage.
- **`main_size` is still unimplemented**, so a wrapping container always shrinks
  to its content on the main axis within its limits.
- **The run-count heuristic in `verify_hit_after_rewrap`** groups children whose
  tops differ by 8 or less into one run. That is derived from the demo's chip
  heights rather than from anything structural; a scene with taller alignment
  slack would need it revisited.
- **Performance was explicitly not a gate for this slice** and nothing here was
  tuned or benchmarked. `size_wrap_children` allocates three vectors per
  wrapping container per pass, which is the obvious thing to remove first if a
  measurement ever asks for it.
