# The property system, and what it can and cannot do yet

`props/drawgui.props.toml` and its generator were written in P0 and then frozen
for four slices, because nothing included either generated file. Slices 2 and 3
went on to build a layout engine and a widget layer with their own concrete
types. This slice connects the two, which meant first deciding what the
relationship between them actually is.

This document records that decision, the disagreements the reconciliation
exposed, and a per-property gap report. The gap report is the input to the next
layout slice, so it is the part that matters most.

---

## 1. The reconciliation: which vocabulary is which

There were two descriptions of the same box:

- the **table** - 45 properties, numeric ids, float logical pixels, CSS-ish
  names (`justify`, `align`, `direction`, `background_color`);
- the **structs** - `BoxStyle` and `NodeStyle`, integer device pixels, engine
  names (`main_align`, `cross_align`, `kind`, `fill`).

Two descriptions that can drift are worse than either alone, so one of three
things had to be true: the table drives the structs, the table replaces them, or
they sit beside each other under something that forces them to agree.

**The decision: the table is the external vocabulary, the structs are the
internal storage, and `dg::set_prop()` is the single seam between them.**

Neither could replace the other, and the reasons are specific rather than
diplomatic:

**The table cannot be the storage.** It describes float logical pixels with
percentage lengths. Slice 2 measured its way to integer device pixels
deliberately - `include/drawgui/layout/box.h` records why: a damage rectangle is
only correct when the rectangle a node declares is exactly the rectangle it
paints, and a float layout has to round somewhere. Generating `BoxStyle` from
the table would undo a measured decision on the strength of a document.

**The structs cannot be the vocabulary.** The whole reason this project exports
a C ABI is that a host language sets a property by NUMBER. It cannot name a C++
struct member. Those numbers are already an ABI contract with a lock guarding
them.

What removes the ambiguity is that **the seam is generated**. `set_prop()` is
implemented by including `src/render/prop_dispatch.generated.inc` twice, and the
dispatch spells `apply_<field>` from the TOML's `field` key. So:

- a property appended to the TOML grows a case automatically;
- that case names a handler that does not exist, so the build fails, naming it;
- the handler cannot forget to type-check, because the check is applied by the
  generated macro using the generated `type` token.

A property therefore cannot be added to the table and silently ignored by the
engine. That is the property the two-vocabulary problem needed.

### What was NOT done, and why

`BoxStyle` and `NodeStyle` were left alone. Renaming `main_align` to `justify`
to match the table was considered and rejected: it would touch every layout
file, every test and three examples, to make two vocabularies look like one
while leaving them exactly as separate as before. The mapping is one switch in
one file, it is compiler-checked, and it is where a reader should look.

---

## 2. The boundary: no enumeration, no cast

The notepad predicted this slice would detonate
`clang-analyzer-optin.core.EnumCastOutOfRange` - the check that rejects casting
a host-supplied `uint16_t` into a scoped enum.

**It measured otherwise, and that is worth recording.** That checker is an
`optin` static-analyzer checker; it is not armed by clang-tidy's defaults, and
it produced nothing even when selected explicitly against a deliberate
out-of-range cast:

```
$ clang-tidy --checks='-*,clang-analyzer-optin.core.EnumCastOutOfRange' scoped.cpp -- -std=c++20
(no output)
```

**Two other checks fired instead, and they were real.** Against a translation
unit that merely `#include`d the old `enum dg_prop_id : std::uint16_t`, under
the project's own `.clang-tidy` and the CI invocation `clang-tidy -p build`:

```
include/drawgui/render/prop_ids.generated.h:21:6: error: enum 'dg_prop_id' is unscoped,
    use 'enum class' instead [cppcoreguidelines-use-enum-class]
include/drawgui/render/prop_ids.generated.h:21:6: error: enum 'dg_prop_id' uses a larger
    base type ('std::uint16_t', size: 2 bytes) than necessary for its value set, consider
    using 'std::uint8_t' (1 byte) [performance-enum-size]
```

Both are errors under `WarningsAsErrors: '*'`, and **obeying either one makes
the ABI worse**:

- `enum class` makes casting a host-supplied `uint16_t` the only way to dispatch
  on it, which is the very thing the notepad entry warns is unrepresentable;
- `std::uint8_t` narrows an id the ABI transports as 16 bits. That is not
  hypothetical - it is the defect already recorded against the platform service
  ids, where `performance-enum-size` forced `uint8_t` and an ABI value of
  `0x0101` folded onto a valid enumerator, so a query for a service nobody had
  was answered with the system tray.

So the landmine was real, the specific detonator was not, and the two live ones
pull in opposite directions. The fix is in the design:

**The generated header declares no enumeration.** It emits
`using dg_prop_id = std::uint16_t;` and one `inline constexpr dg_prop_id`
per property. `case DG_PROP_WIDTH:` still works in the generated switch, the ABI
width stays honest at 16 bits, and all three checks become structurally
inapplicable rather than suppressed. No `NOLINT` anywhere.

Validation is then not a range test followed by a cast - **it IS the dispatch**.
The generated `default:` arm is the single place that decides an id is unknown,
which means the decision cannot disagree with the table.

`tests/unit/test_props_boundary.cpp` sends **all 65536 values a `uint16_t` can
hold** and requires the recognised set to be exactly the 45 the table defines.
`0x0101` is in that sweep by name.

### Enum values are ABI too

An enum-typed property travels as an **ordinal** into its `values` list, so the
ORDER of that list is as much a contract as the id is. Hand-copying those lists
into C++ would have been precisely the drift this generator exists to remove, so
the generator now emits them as `DG_<PROPERTY>_<VALUE>` constants, with a
collision check that names both offending TOML entries.

**Known gap:** `props/prop_ids.lock` covers ids and names. It does **not** cover
enum value ordering, so reordering a `values` list today is an undetected ABI
break. Closing that is a small change to `prop_lock.py` and a lock format
change; it is left for the slice that first needs to reorder one.

---

## 3. Disagreements between the table and the engine

Recorded rather than quietly resolved, because every slice so far has
contradicted `doc/design.md` somewhere and it has been useful each time.

### 3.1 Node kind is not a property

The table models `box` / `flex` / `wrap` / `stack` as node **kinds** and gives
`direction` to flex and wrap. The engine's `LayoutKind` merges the two:
`kLeaf`, `kRow`, `kColumn`, `kAbsolute`.

So `direction` can turn a row into a column, but **no property can make a node a
leaf or an absolute container**, and there is no `wrap` kind at all. Node kind
stays a construction-time argument to `add_child`. `direction`, `justify`,
`align` and `gap` are refused with `kNotApplicable` on a node that arranges
nothing, which is the table's `applies_to` enforced.

### 3.2 There is no percentage length

The table's `length` has three modes: absolute, percentage of the incoming
`max_*`, and error-under-unbounded. `PropValue::length()` carries only the
absolute one. A percentage constructor was deliberately not written: this
project does not add a representation before the code that consumes it exists,
and no arrangement can resolve one today.

### 3.3 One border in the table, two in the engine — RESOLVED

**This was a disagreement and is not one any more.** `doc/wrapping.md` records
how it was closed; what follows is what it used to say, kept because the shape
of the argument is the reusable part.

`BoxStyle::border` reserved space per side, exactly as the table describes.
`NodeStyle::border_width` was a **single uniform stroke**, because that is what
the painter implemented — it insets by half the width and strokes once.

`border_width_*` therefore wrote the per-side layout inset exactly, and set the
painted stroke to the **minimum of the four**. The alternatives were worse:
rejecting unequal sides makes setting them one at a time impossible, since the
first write is what makes them unequal; painting the maximum would put stroke
outside the space some side reserved, and a node painting outside the rectangle
it declared is exactly what damage tracking cannot survive. The minimum is
always inside the box and is exact whenever the four agree.

`NodeStyle::border_width` is now a `BorderWidths` carrying all four, and the
painter takes one of two routes: a **uniform** border still strokes once, inset
by half, so every pixel this project has already measured is unchanged; an
**unequal** one fills the ring between the border box and the box the four
widths inset it to, with `drawDRRect`. Both stay inside the declared rectangle.
`tests/unit/test_border_paint.cpp` measures the painted thickness of each side
in pixels rather than reading the field back, because the field was always
right — it was the painter that collapsed it.

### 3.4 `grow` is an integer weight

The table types it `float`. `BoxStyle::grow` is an `int`, and `box.h` records
why: the free-space split is exact integer division with the remainder handed to
the earliest children, so a re-run produces the same pixels rather than the same
pixels up to rounding - and byte-identity between an incremental and a full
layout is this project's acceptance technique.

A fractional weight is therefore **rejected**, not rounded. Rounding 0.5 down
would silently delete a child's flexibility; rounding it up would silently
double it against a sibling weighted 1.

### 3.5 parentData is validated at set time, not layout time

`design.md` section 5.8 decision 7 defers the check because a node may be
configured before it is mounted. **This engine cannot reach that state**:
`LayoutTree::add_child` takes the parent, so a node has one from the moment it
exists. The check is synchronous and the caller is told immediately, which is
strictly better than a diagnostic that surfaces a pass later.

### 3.6 Negative margins are refused

CSS allows them. `BoxConstraints::deflate` clamps a shrunk minimum at zero while
placement adds the margin verbatim, so a negative margin would move a child
without giving back the space, asymmetrically. That is a half-implemented
feature rather than a supported one, so it is refused.

### 3.7 The bug the reconciliation found

Wiring the table surfaced a **real, pre-existing divergence between incremental
and full layout**, reachable through the plain struct API with no properties
involved:

```
initial middle                x=84 w=149
after set_box(grow=0,w=70)    x=84 w=149   <-- incremental
after layout_full()           x=84 w=70    <-- full
```

`mark_needs_layout()` absorbs a node's own style change when its incoming
constraints are tight, reasoning that a style cannot override a constraint the
parent already fixed. That is correct for every property the node consumes
itself, and **false for parentData**, because the tight constraint was DERIVED
from the value that just changed. A flexible, stretched child is tight on both
axes, so changing its weight was absorbed at the child and never reached the row
that hands weights out.

The table is what made this visible: it is the artefact that says out loud that
`margin`, `grow` and `left/top/right/bottom` are consumed by the parent.
`LayoutTree::set_box` now marks the parent as well when one of those six fields
changes, and `tests/unit/test_layout.cpp` gained three cases pinning
incremental against full for each parentData scope.

Neither `layout.incremental_equals_full` nor any layout unit test could have
caught it: the demo scene never changes a weight, a margin or an inset after the
first layout. This is the same lesson the dirty-root comparator taught -
enumerate the shapes the ALGORITHM branches on, rather than trusting a scene
written for a different purpose.

---

## 4. The gap report

45 properties: **32 implemented**, **10 partially implemented**, **3 not yet**.

A partially implemented property applies correctly for the values listed as
supported and returns `kUnsupported` - naming the node - for the rest. Nothing
in either column is silently ignored.

The counts have moved four times since this report was first written: the
wrapping slice built the pieces section 4.4 called 1 and 5
(`doc/wrapping.md`), the clipping slice built piece 2 (`doc/clipping.md`), the
compositing slice built piece 3 (`doc/compositing.md`), and the sizing slice
built piece 4 (`doc/sizing.md`).

### 4.1 Implemented (32)

| id | property | notes |
| --- | --- | --- |
| 1 | `width` | `BoxStyle::width`; absent still means shrink-to-fit |
| 2 | `height` | `BoxStyle::height` |
| 3 | `min_width` | clamp after the size source resolves |
| 4 | `max_width` | |
| 5 | `min_height` | |
| 6 | `max_height` | |
| 7 | `aspect_ratio` | derives the unsettled axis from the settled one inside `limits_for`, so it costs no second measurement. Both axes settled: the constraints win and the disagreement is a layout diagnostic. Neither settled: the content decides and the ratio GROWS the finished box - `doc/sizing.md` section 4 |
| 8 | `padding_l` | shrinks the constraint passed to children |
| 9 | `padding_t` | |
| 10 | `padding_r` | |
| 11 | `padding_b` | |
| 16 | `background_color` | `NodeStyle::fill`; paint only, no relayout |
| 18 | `border_width_l` | reserves layout space AND paints, per side |
| 19 | `border_width_t` | |
| 20 | `border_width_r` | |
| 21 | `border_width_b` | |
| 22 | `border_color` | paint only; inert until a border width is set |
| 23 | `border_radius_tl` | paint only; also flips the node to clip-atomic |
| 24 | `border_radius_tr` | |
| 25 | `border_radius_br` | |
| 26 | `border_radius_bl` | |
| 27 | `opacity` | GROUP opacity through a `saveLayer`, not per-object alpha; the whole of 0..1, with anything outside it refused rather than clamped. Hit testing deliberately ignores it, including at 0 - `doc/compositing.md` section 4 |
| 29 | `overflow` | both values; paint, hit testing and damage all read the one field. Clips at the BORDER box, which is a deliberate deviation from CSS - `doc/clipping.md` section 2 |
| 34 | `gap` | flex and wrap containers; stacks with margins, does not collapse |
| 35 | `main_size` | both values, on flex and wrap containers. Resolved in `limits_for`, which is also what decides relayout boundaries, so a filled container becomes one without declaring it |
| 36 | `run_gap` | wrapping containers only; `kNotApplicable` on a flex row |
| 37 | `align_content` | wrapping containers only; all six values |
| 40 | `basis` | flex parent only. A DECLARED base, never a measured one, and deliberately not clamped to the container's room - overrunning it is what produces the deficit `shrink` absorbs |
| 42 | `left` | absolute parent only; negative insets allowed |
| 43 | `top` | |
| 44 | `right` | |
| 45 | `bottom` | |

Lengths are float at the boundary and round to nearest, ties away from zero,
because layout is integer device pixels. Non-finite values, magnitudes over
2^24 and negative lengths are refused - `static_cast<int>(NaN)` is undefined
behaviour, so that test is load-bearing rather than tidy.

### 4.2 Partially implemented (10)

| id | property | works | does not, and what it needs |
| --- | --- | --- | --- |
| 12 | `margin_l` | non-negative | negative margins need `deflate` to stop clamping at zero and give the space back symmetrically |
| 13 | `margin_t` | non-negative | as above |
| 14 | `margin_r` | non-negative | as above |
| 15 | `margin_b` | non-negative | as above |
| 31 | `direction` | `row`, `column` | `row_reverse` / `column_reverse` need `size_flex_children` to walk children in reverse, which changes which children absorb the integer-division remainder - so the byte-identity gate has to be re-established, not just extended |
| 32 | `justify` | `start`, `end`, `center`, `space_between` | `space_around` / `space_evenly` also distribute space BEFORE the first child; `place_flex_children` only spreads between them. `align_content` now does exactly that on the cross axis, so the arithmetic exists - what is missing is the decision to change `MainAlign` |
| 33 | `align` | `start`, `end`, `center`, `stretch` in a flex; `start`, `end`, `center` in a wrap | `baseline` needs a child's baseline before it is placed, which is intrinsic sizing. `stretch` under a WRAPPING container is a different refusal: the extent to stretch to is the child's run, which is not known until the run closes, so it is reported as a layout diagnostic and placed as `start` |
| 38 | `grow` | whole non-negative weights, under a flex parent | fractional weights would need the free-space split to stop being exact integer division. Under a WRAP parent it is refused entirely - `kNotApplicable` at the boundary, a layout diagnostic through the struct API - matching design.md section 5.4.4 |
| 39 | `shrink` | whole non-negative weights, under a flex parent, on a child whose base main size is DECLARED - it has a `basis`, or a definite size on the container's main axis | a child whose base is its measured natural size is left at that size and reported by name. Shrinking from a measured base needs a second layout of that subtree, and nesting that makes a pass exponential rather than linear unless the base is measured under an unbounded main axis and cached - which is a slice of its own, costed in `doc/sizing.md` section 1.4. There is also no freeze-and-redistribute loop: a child that floors at its `min_*` stops absorbing and the residual overrun is reported |
| 41 | `align_self` | `auto`, `start`, `end`, `center`, `stretch` | `baseline`, for the same reason `align=baseline` is unavailable. `stretch` under a wrapping parent degrades exactly as `align=stretch` does |

### 4.3 Not yet implemented (3)

| id | property | what it needs |
| --- | --- | --- |
| 17 | `background_gradient` | an `SkShader` in the painter, plus the dedicated `dg_node_set_gradient`-shaped setter design.md section 5.9.5 specifies - it cannot travel in the scalar union |
| 28 | `shadow` | the LAYER now exists and the same `saveLayer` call takes an image filter. What is still missing: painting OUTSIDE the node's declared bounds, which `subtree_extent` and `visible_bounds` would both have to learn; and making a layer damage-atomic, which a scalar alpha turned out NOT to need - `doc/compositing.md` sections 2 and 6. Also a dedicated setter |
| 30 | `transform` | the layer exists; non-axis-aligned geometry does not. Every rectangle here is axis-aligned integer pixels, so a rotated node has no damage rectangle to declare and `contains(rect, point)` is not its hit test. A layer under a transform IS damage-atomic, unlike one under a scalar alpha. Also a dedicated setter |

### 4.4 Suggested grouping for the next slice

Reading down the "needs" column, the unimplemented properties fall into five
pieces of engine work rather than nine. **Pieces 1, 2 and 5 are done**; the
remaining two are unchanged.

1. ~~**A wrapping arrangement**~~ - done. `run_gap`, `align_content` and the
   run-scoped meaning of `align` all landed with it. `doc/wrapping.md`.
2. ~~**A real clip**~~ - done. `overflow` landed in painting, hit testing and
   damage together, and it is the clip a scroll viewport will need. The rounded
   case turned out to need no new damage rule: a rounded clipper already has
   radii, so it is already clip-atomic, and the one pixel of anti-alias slack
   this engine carries was measured to be necessary and sufficient for a
   rounded CLIP as well - nesting included. The cost is that a rounded clipping
   container becomes the minimum damage unit for its whole subtree.
   `doc/clipping.md`.
3. ~~**A compositing layer**~~ - done. `opacity` landed as GROUP opacity: the
   subtree is composited through `SkCanvas::saveLayer` and the resulting image
   is blended, so overlaps inside a faded group do not accumulate. The damage
   rule turned out to be the OPPOSITE of the rounded clip's - a scalar alpha
   composites per pixel, so a layer is **not** damage-atomic and a change
   inside one damages only that change. What would make a layer atomic is a
   term that reads neighbouring pixels, which is exactly what `shadow` and
   `transform` bring. Hit testing deliberately ignores `opacity`, contradicting
   design.md section 5.11.2. `doc/compositing.md`.
4. ~~**A second sizing stage**~~ - done, and the name turned out to be wrong.
   `aspect_ratio`, `main_size` and `basis` need no second measurement at all,
   and `shrink` needs none either given a declared base, so **every node is
   still laid out exactly once** and L3 was preserved literally rather than
   weakened. What the piece really needed was the argument, not the machinery:
   `doc/sizing.md` section 1 settles it, including the exponential a
   loose-then-tight design costs and why design.md section 5.4.6's cache is
   mandatory rather than advisory.

   Two of its predicted side effects did NOT arrive. `align=stretch` inside a
   wrap and `align=baseline` everywhere both need a MEASURED quantity before a
   child is placed - a run's extent, a child's baseline - which is the one
   thing this piece declined to build. The prediction was right that they need
   the same machinery and wrong that this piece would bring it.
5. ~~**Per-child overrides and per-side painting**~~ - done. `align_self`, and
   the paint half of `border_width_*`.

The three complex-typed properties (`background_gradient`, `shadow`,
`transform`) additionally need the dedicated-setter shape from design.md section
5.9.5, which does not exist yet because none of the three is implemented.
`doc/compositing.md` section 6 lists, per property, which of the pieces
`shadow` and `transform` need are now in place and which are not.

---

## 5. What a rejected write does

Six outcomes, all observable, none silent:

| status | meaning |
| --- | --- |
| `kApplied` | written, and the right invalidation was raised |
| `kUnknownId` | no property has this id - includes 0 and everything above 45 |
| `kTypeMismatch` | real id, wrong value type. `length` and `float` are kept distinct |
| `kValueOutOfRange` | non-finite, too large, negative where meaningless, fractional `grow`, or an enum ordinal past the list |
| `kNotApplicable` | real property, wrong node - `gap` on a leaf, `grow` under a non-flex parent |
| `kUnsupported` | drawgui does not implement it yet. Section 4 is the list |

`kNotApplicable` and `kUnsupported` are separate on purpose: the first is a
statement about the caller and will always be wrong, the second is a statement
about drawgui and becomes `kApplied` when a later slice lands the capability.

**A failed write changes nothing.** Every applier works on copies of `BoxStyle`
and `NodeStyle`; `set_prop` commits them only after the dispatch succeeds, so a
value rejected halfway cannot leave a node half-configured. The boundary tests
assert the whole node is byte-identical after every rejection.

The message carries the node path (`root(column) > #2(row) > #7(leaf)`), which
is why `LayoutTree::path_of` became public rather than being derived a second
time from `RenderTree::parent`.

---

## 6. Which invalidation a property costs

Decided by which struct the property lands in, not by a second table that could
disagree with the first:

- a **paint** property writes `NodeStyle` and calls `RenderTree::set_style`,
  which damages the node and does not mark layout dirty;
- a **layout** property writes `BoxStyle` and calls `LayoutTree::set_box`, which
  marks the node dirty up to its relayout boundary; the next pass damages both
  the box the node vacated and the one it moved to;
- `border_width_*` is **both**, which is correct rather than a hedge: a border
  occupies layout space and is painted.

`tests/unit/test_props_damage.cpp` pins all three, including a hand-built
pure-translation case. The three-child demo row could not provide one - its
middle child is flexible, so every main-axis change is absorbed by a RESIZE, and
a node that changes size damages its new box for that reason alone. Taking the
weight off first is what makes "the vacated pixels were damaged" the only reason
the assertion can pass.
