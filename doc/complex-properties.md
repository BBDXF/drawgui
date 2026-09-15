# Slice 5-4: the dedicated-setter channel, and what it lands

design.md section 5.9.5 names three complex-typed properties -
`background_gradient` (id 17), `shadow` (id 28), `image_source` (id 47,
appended by slice 5-1) - and, by its own admission, forgets a fourth:
`transform` (id 30) is a complex type in `props/drawgui.props.toml`'s own
`type` enumeration, but section 5.9.5's code block lists only three
`dg_node_set_*` signatures, not four. All four were `properties.md`'s "not
yet implemented" bucket going into this slice. This slice builds the channel
itself, lands three of the four properties through it, and settles the
fourth's omission by argument rather than by silently building it.

---

## 1. The channel's design

Every complex-typed property gets a dedicated C++ function with the shape
design.md's eventual C ABI already commits to:

```c
int dg_node_set_gradient(dg_node_t*, uint16_t prop_id, const dg_gradient_desc*);
int dg_node_set_shadow  (dg_node_t*, uint16_t prop_id, const dg_shadow_desc*);
int dg_node_set_image   (dg_node_t*, uint16_t prop_id, const dg_image_desc*);
```

This slice's C++ shape (`include/drawgui/props/node_props.h`):

```cpp
PropWrite set_image(LayoutTree&, NodeId, dg_prop_id, const ImageStyle&);
PropWrite set_gradient(LayoutTree&, NodeId, dg_prop_id, const LinearGradientStyle&);
PropWrite set_shadow(LayoutTree&, NodeId, dg_prop_id, const ShadowStyle&);
PropWrite set_transform(LayoutTree&, NodeId, dg_prop_id, const TransformDesc&);
```

`dg_node_t*` is `LayoutTree&, NodeId` for the identical reason `dg::set_prop()`
already uses that pair rather than a C handle: the C ABI is phase P5, out of
this slice's scope by name. Each `const dg_..._desc*` is the C++ descriptor
type the property's own field already needs - `LinearGradientStyle` and
`ShadowStyle` sit on `NodeStyle` beside `ImageStyle`/`TextStyle`, the same
shape doc/clipping.md section 6 and doc/compositing.md section 5 already
established for `overflow` and `opacity`. There is no second, wire-shaped
descriptor type converted into the first: this slice does not build the ABI
marshaling layer, so the C++ descriptor and the eventual ABI struct are the
same shape read twice rather than two shapes kept in step.

### Why four functions, not a `PropValue` variant

`dg::set_prop()`'s `PropValue` is a scalar tagged union - a float, a colour, an
enum ordinal, at most 8 bytes. A gradient's stop list has no fixed size; a
shadow's four scalars plus a colour do have a fixed size but still do not fit
the union's existing tags without inventing a fifth. Cramming either into
`PropValue` would need a heap-allocated variant the type never otherwise
carries, and design.md's own ABI text already answers the question by giving
the three real setters top-level, separate signatures beside
`dg_node_set_prop` rather than a new `dg_value` case. This slice follows that
choice rather than relitigating it.

### The shared prelude, and how the ABI lock stays unweakened

Every one of the four functions opens with `complex_prop_prelude()`
(`src/props/node_props.cpp`):

```cpp
std::optional<PropWrite> complex_prop_prelude(const LayoutTree& tree, NodeId node,
                                              dg_prop_id prop_id, PropType expected);
```

It calls `dg::prop_type(prop_id)` - the SAME function `dg::set_prop()`'s
scalar dispatch answers questions through - and compares the answer against
the type the CALLER's own C++ signature commits to. Three outcomes:

- the id is not in the table at all: `kUnknownId`, the identical status and
  message shape the scalar door reports for an out-of-range id;
- the id is real but names a DIFFERENT complex type (`dg::set_shadow()`
  called with `DG_PROP_BACKGROUND_GRADIENT`, or with a plain scalar id like
  `DG_PROP_WIDTH`): `kTypeMismatch`;
- the id matches: proceed to the function's own value validation.

**No new id, no new lock entry, no parallel numbering scheme.** The three
generated constants (`DG_PROP_BACKGROUND_GRADIENT`, `DG_PROP_SHADOW`,
`DG_PROP_IMAGE_SOURCE`) already existed - `background_gradient`/`shadow` since
`props/drawgui.props.toml`'s first commit, `image_source` since slice 5-1 -
and were already locked in `props/prop_ids.lock`. This slice adds zero rows to
that lock file and zero properties to the TOML. `props.no_drift`,
`props.abi_lock` and `props.lock_selftest` all still pass unmodified (section
6), which is the direct evidence that the channel rides the existing contract
rather than needing a second one: a caller who sends `DG_PROP_SHADOW` to
`dg::set_gradient()` is rejected by exactly the same id-to-type lookup that
already rejects a colour sent to `DG_PROP_WIDTH` through `dg::set_prop()`.

### The scalar door, once three of the four became real

`dg::set_prop()`'s own appliers for these three ids (`apply_background_gradient`,
`apply_shadow`, `apply_image_source`) used to report `kUnsupported` - accurate
while nothing behind them worked. Now that they are implemented, a scalar
write against them is rejected as `kTypeMismatch` instead: the property is
real and working, a `PropValue` is simply the wrong shape to carry it, and no
`PropValue` factory can ever construct one typed `k_gradient`/`k_shadow`/
`k_image` (only `number()`/`length()`/`color()`/`option()` exist). `transform`
keeps reporting `kUnsupported` through the scalar door, because unlike the
other three there genuinely is no capability behind it yet (section 4).
`tests/unit/test_props_boundary.cpp` pins both halves of this distinction.

---

## 2. The prototype: `image_source`, built first

The project's standing rule - "任何接口都必须在至少一个跑通的实现之后才写"
(no interface without a working implementation behind it first) - meant one
of the four had to be built end to end BEFORE `complex_prop_prelude()` was
extracted as a reusable shape. doc/image.md section 7 named `image_source` a
candidate for either role (the channel's fourth client, or its first
prototype) and left the choice to this slice.

**This slice built it as the prototype**, and the reasoning is specific:
`image_source`'s entire decode/paint/layout path was already proven by slice
5-1 - `ImageCatalog`, `carries_image()`, the mandatory size-before-decode
diagnostic, and a REAL working C++ entry point, `RenderTree::set_image()`.
Nothing about a new visual feature had to be invented at the same time as the
channel's own shape. `dg::set_image()` therefore does almost nothing beyond
the id check: validate, then call the existing `RenderTree::set_image()`,
which already damages correctly and already dedupes an unchanged value. Once
that worked (proven by `tests/unit/test_complex_props.cpp`'s "set_image lands
the style exactly as RenderTree::set_image already does"), `complex_prop_prelude()`
was lifted out of it and reused, unchanged, by `set_gradient()` and
`set_shadow()` - which each had real new painting/damage work to do
(sections 3 and 4) that the prototype's simplicity would have hidden if built
first. `set_transform()` is the fourth client, and it is the one that
reuses the prelude only to refuse (section 4).

---

## 3. `background_gradient`: the tractable one

Linear only. design.md section 5.9.6 also names radial and sweep; both are
declined by name here, matching the task's own framing: a linear axis is the
one whose damage story is trivial, because it paints strictly INSIDE the
node's own box - the same shape `background_color` already has, never
touching anything outside `NodeStyle::fill`'s own rectangle. It does not
threaten damage tracking at all, and no engine change beyond the paint
function was needed to land it.

### The shape

```cpp
struct GradientStop { float offset; Color color; };
struct LinearGradientStyle { float angle_deg; std::vector<GradientStop> stops; };
```

`angle_deg` follows the common CSS `linear-gradient()` convention (0 =
left-to-right, increasing clockwise), and `src/render/skia_paint.cpp`'s
`gradient_shader()` turns it into a gradient LINE using the standard
"gradient line length" formula: project the box's half-width and half-height
onto the axis and sum their magnitudes, so the line spans exactly the box's
own edges when the angle is a multiple of 90 degrees. `SkGradient::Colors`
takes the stop list directly with explicit positions (Skia's own contract:
positions must be finite, in 0..1, strictly increasing), so `dg::set_gradient()`'s
validation (`valid_gradient_stops()`) is exactly that contract, checked before
anything is written: at least 2 stops, at most 32 (a sanity bound against an
untrusted caller, not a measured limit), every offset finite and in 0..1,
strictly increasing between consecutive stops.

### Replaces, does not layer over, the flat fill

design.md section 5.9.5's decoration table lists `background_color` and
`background_gradient` as two SEPARATE layers (colour, then gradient, painted
bottom to top). This slice keeps them mutually exclusive instead: when a node
carries a valid gradient, the gradient shader paints the WHOLE fill step and
`fill` is not painted underneath it at all. The alternative - composing both,
so a gradient with translucent stops shows the flat colour through it - would
need the two to interact, and design.md's own section 5.9.5 already commits
to the same simplification one row down for the analogous case ("不支持多重
背景" - no multiple backgrounds - for `background_image`). Recorded here as
the same choice made for the same reason, not re-argued from scratch.

### Verification

`tests/unit/test_complex_props.cpp` pins the setter's own validation
(stop-count bounds, offset range, monotonicity) and that a valid gradient
lands unchanged on `NodeStyle::background_gradient`. The VISUAL claim -
that the painted pixels actually match the gradient formula - is
`examples/16_complex_properties`'s job (section 5): a hand-derived,
independently-recomputed linear-interpolation oracle, sampled at the exact
pixel-centre coordinates Skia itself samples at (`x + 0.5`), not read off a
previous run.

---

## 4. `shadow`: budgeted, and the one that had to answer the damage question

### The budget

doc/cpu-raster-findings.md measured blur (including drop shadow) at 52% of a
dense scene's raster time at 1080p - the single most expensive thing this
engine can be asked to paint. `render_tree.h`'s own `NodeStyle` comment
already named this as the reason blur was absent through four prior phases:
"belongs in a budgeted feature rather than in the primitive every node
carries." This slice keeps both halves of that sentence true. `shadow` is an
opt-in `std::optional<ShadowStyle>` field (absent by default, costing nothing
for every scene built before this slice), and `dg::set_shadow()` caps
`blur_radius` at 48 (`kMaxShadowBlur`, `src/props/node_props.cpp`) rather than
leaving it open - a shadow cannot become an unbounded-cost primitive through
this door. `spread` and the two offsets are bounded too (64 and 512
respectively), sanity limits against a hostile or buggy caller rather than
measurements, matching the same reasoning `to_pixels()`'s 2^24 length bound
already uses one property table over.

### The damage question - the intellectual core of this slice

A drop shadow paints OUTSIDE the node's own declared bounds by definition.
Every invariant this engine's damage system rests on, from sub-step 1 onward,
assumes the opposite: "a node paints only inside the rectangle it declared"
(`render_tree.h`'s own comment on `border_width`, restated by
`RenderTree::set_local_bounds()`'s doc comment as "the exact bug damage
tracking cannot survive"). `shadow` is the first property to genuinely break
that assumption on purpose, and the task named this the hard half - more so
than the ABI plumbing.

**The resolution, in one sentence: the node's damage rectangle GROWS to
include the shadow's reach, generalising the rounded-rect rule rather than
inventing a new one.**

doc/damage-repaint.md already established that a rounded node is
"clip-atomic": any damage rectangle touching one grows to contain it plus a
one-pixel anti-alias halo, because Skia's analytic anti-aliasing is not
clip-invariant. A shadow is atomic for a related but distinct reason - a
Gaussian blur reads NEIGHBOURING pixels, so cutting a blurred shape with a
damage rectangle changes the pixels inside the cut (doc/compositing.md
section 2 already predicted this: "a term that reads neighbouring pixels...
is exactly what shadow and transform bring"). The mechanical answer
generalises cleanly:

- `shadow_reach(style)` (`src/render/render_tree.cpp`) computes how far a
  shadow's paint can reach beyond the node's own box: `3 * blur_radius`
  (three sigma, ~99.7% of a Gaussian's energy) plus the larger of the two
  offset magnitudes plus the spread, ceiling-rounded. It is a SYMMETRIC,
  CONSERVATIVE superset of the true (offset-skewed) footprint - a damage
  bound, not a paint bound, so overshooting costs a few repainted pixels
  nobody sees and undershooting would corrupt a partial repaint, which is
  the one direction this project's invariant does not tolerate.
- `declared_paint_bounds(box, style)` (`src/render/tree_impl.h`) outsets `box`
  by `shadow_reach()` when there is a shadow, unchanged otherwise.
- `Node::visible_bounds()` now reads `declared_paint_bounds(absolute, style)`
  before intersecting with the inherited ancestor clip, so the outset reaches
  every existing reader through the ONE place they already share -
  `damage_subtree()`, `subtree_extent()` (the opacity layer's own extent
  computation, so a shadowed node inside a faded group is sized correctly
  without either mechanism knowing about the other), and the "wanted" test in
  `paint_node_and_children()` - rather than three private copies of "how far
  does this node's paint extend" that could each answer differently. This is
  the same argument doc/clipping.md section 6 and doc/compositing.md
  section 5 already made for `overflow` and `opacity`: one field, one rule,
  every reader.
- `shadow_atomic(style)` (`src/render/tree_impl.h`) is `style.shadow.has_value()`.
  `tree_paint.cpp`'s `can_be_cut()` now ORs it alongside the existing
  `clip_atomic` flag - a shadowed node forces whole-node damage growth for
  the identical mechanical reason a rounded node does, not a parallel rule
  bolted on beside it.
- `RenderTree::set_style()` gained a `shadow_changed` check beside the
  existing `clip_changed` one: shrinking or removing a shadow uncovers pixels
  that were only ever reachable through the OLD, wider outset, so the OLD
  extent is damaged before the style is overwritten - the same
  damage-then-move shape `set_local_bounds()` and the clip-toggle path
  already use, applied to a third kind of geometry change.

### The test that pins it - the same kind that pins the rounded-rect rule

`tests/unit/test_complex_props.cpp`'s `TEST_SUITE("shadow damage")` is built
on the identical technique `test_opacity_damage.cpp` and `test_clip_damage.cpp`
already use: two independent copies of one scene, one repainting only its
damage and one repainting everything, required to be byte-identical after
every mutation, for 100 frames at two damage-rectangle caps. The scene
deliberately mixes a shadowed node (blur, offset and spread all changing every
frame, so the outset itself moves) with a plain rounded node in the SAME tree,
so the two atomicity rules (`clip_atomic`, `shadow_atomic`) are proven to
coexist rather than proven only in isolation. Two more cases pin the
tightness in both directions, matching this project's own standing argument
that byte identity alone is satisfied by damaging too MUCH:

- `damage touching a shadowed node grows to its outset box` - a single damaged
  pixel at the node's own edge forces the actually-repainted region
  (`painted()`, read after `repaint()` rather than the pre-expansion `damage()`)
  to contain the node's declared box AND to be strictly wider than it -
  ruling out the case where growth never happened at all;
- `a change far from the shadow is not grown into it` - a change to an
  unrelated, distant node must not pull the shadowed node's halo into the
  repainted region, which is the rule doc/compositing.md's own "not damage-
  atomic" finding for `opacity` warned would be the wrong failure mode to
  introduce here.

A separate, hard-edged (`blur_radius = 0`) scene closes a gap the byte-identity
technique cannot: `spread` grows the shadow's shape BEFORE the offset is
applied, and no scene in the damage-identity script isolates that parameter
from blur and offset changing at the same time. `set_shadow's spread grows
the exact-colour region by exactly its own amount` builds a single shadowed
node with `spread` as its only non-zero shadow parameter and asserts an
EXACT pixel colour at a hand-computed distance past the box's own edge - the
coverage gap this closes, and why, is recorded in section 7.

### Painting: `DropShadowOnly`, not `DropShadow`

`src/render/skia_paint.cpp`'s `paint_shadow()` uses
`SkImageFilters::DropShadowOnly` rather than `SkImageFilters::DropShadow`
(the latter also draws the shape it is given, in the paint's own colour,
which would double-paint the node's own box before `fill`/`background_gradient`
get to it). The shape carrying the filter is drawn fully opaque
(`SK_ColorBLACK`) purely to supply coverage - only its ALPHA mask matters to
the filter, never its colour, since the shadow's own colour is the filter's
own parameter. `spread` grows the shape via `SkRect::makeOutset` before the
filter runs, matching CSS's own spread-before-blur order. Painted FIRST, so
everything else the node paints (`fill`/`background_gradient`/`image`/border)
sits on top of it, matching design.md section 5.9.5's decoration order.

---

## 5. `examples/16_complex_properties`: the channel proven on a real scene

Three panels, each built THROUGH the channel rather than by writing
`NodeStyle` fields by hand - the demonstration is that the id-based door
works end to end, not merely that the fields paint correctly (the unit tests
already prove that in isolation):

- **gradient**: a 220x110 panel, three stops (red/green/blue at 0/0.5/1),
  angle 0. `cprops_check.cpp`'s oracle recomputes the exact channel-wise
  linear interpolation Skia performs - including the pixel-centre sampling
  offset (`x + 0.5`) that was measured against a real render before the
  formula was written down, exactly the discipline `examples/13_image`'s own
  fit-mode oracle already established.
- **shadow**: a 120x120 panel with a HARD-EDGED shadow (`blur_radius = 0`,
  `spread = 0`, offset `(14, 10)`) - deliberately, so the sliver it casts past
  the panel's own right and bottom edges is one solid colour at BYTE
  precision rather than a blurred gradient a check could only bound. The
  oracle asserts both directions: the sliver IS the shadow colour, and
  everywhere the shifted rectangle does not reach IS exactly the background
  - the same two-sided proof section 4's damage tests already use, applied to
  visible pixels instead of damage rectangles.
- **image**: a 128x128 panel, a two-colour (top/bottom) source synthesized
  in-process and decoded through the real `SkCodec` path exactly as
  `examples/13_image`'s own `quadrants()` is, attached through
  `dg::set_image()` - the channel's prototype client, proving the id-based
  door reaches the same paint path `RenderTree::set_image()` already proved
  in slice 5-1.
- **transform**: no panel. `dg::set_transform()` is called once during scene
  construction; its `kUnsupported` result and message are printed by both the
  window and the headless check, which is the entire demonstration for the
  fourth property (section 4 below).

`examples/16_complex_properties --verify-complex-properties` is the headless
oracle (no display needed); `--dump-png FILE` and the plain window mode
follow every other example in this project's register. `complex_properties.verify_demo_scene`
is the CTest entry, matching `image.verify_demo_scene`'s own reasoning: this
target does not exist without SDL3 and CI has none today, so
`tests/unit/test_complex_props.cpp` carries the same claims into an
always-built binary, and this entry checks them against the scene a human
actually looks at.

---

## 6. `transform`: design.md's own gap, settled

4-10's audit (carried into `doc/completeness.md` section 3) found that
design.md section 5.9.5 names exactly THREE dedicated setters - gradient,
shadow, image - and never states `dg_node_set_transform`'s shape at all,
even though `transform` is a fourth complex-typed property in the same
table with the same "cannot travel in the scalar union" problem. This is a
genuine gap in design.md's own text, not an inference `properties.md`'s gap
report merely asserted by analogy (that report's own words: "a reasonable
engineering inference... but not literally what §5.9.5 states").

**The verdict this slice reaches: produce the shape, decline the capability,
and record the gap as a design.md worklist item rather than editing the
document's prose directly.**

### The shape

```cpp
struct TransformDesc {
  float translate_x = 0.0F;
  float translate_y = 0.0F;
  float scale_x = 1.0F;
  float scale_y = 1.0F;
  float rotate_deg = 0.0F;
  float origin_x = 0.0F;
  float origin_y = 0.0F;
};

PropWrite set_transform(LayoutTree&, NodeId, dg_prop_id, const TransformDesc&);
```

The decomposition (translate/scale/rotate + origin) is not this slice's
invention - it is exactly what design.md section 5.9.6 already specifies for
`transform` ("分解为 translate/scale/rotate + origin，便于动画插值" - so each
component interpolates independently once an animation clock exists, which
this project does not have, design.md section 5.16.1). This slice only gives
that decomposition a concrete, citable C++ shape and wires it into the SAME
id-validation prelude the other three functions use, so a caller reaches a
real, consistent, documented entry point rather than a missing symbol - the
channel's fourth client exists, and existing is not the same claim as
working.

### The decline

`dg::set_transform()` always returns `kUnsupported` (after the id check
passes), naming the specific blocker: every rectangle this engine tracks -
damage regions, hit-test bounds, ancestor clip bounds - is axis-aligned
integer device pixels, `PixelRect` throughout. A general 2D transform breaks
that in three places AT ONCE, not one at a time:

1. **Damage bounds stop being rectangles.** A rotated or skewed node's true
   footprint is a quad; representing it as an axis-aligned bounding box (the
   obvious fallback) is a correct but increasingly loose superset as the
   angle grows, and every damage rectangle downstream inherits that looseness
   - a much larger version of the same tradeoff `shadow_reach()`'s symmetric
   outset already accepts in miniature (section 4), but for `transform` the
   gap between the true and bounding shape is unbounded rather than a few
   pixels.
2. **Hit testing needs an inverse transform.** `descend()`
   (`src/render/hit_test.cpp`) currently compares a point against
   `node.absolute` directly; under a transform it would have to carry the
   point through the inverse matrix first, and `clip_contains` alongside it -
   a real, non-trivial addition to the one function this project's hit-test
   equivalence oracle (`tests/unit/test_hit_test.cpp`) already holds to an
   exhaustive per-pixel standard.
3. **Whether transform affects parent layout is an open design question, not
   an implementation detail.** CSS's own answer is that a transformed
   element's LAYOUT box is unaffected - only its PAINTED position moves,
   exactly the same shape `RenderTree::set_local_origin()` already gives an
   ordinary node move. That answer is almost certainly right here too (an
   integer translation is already fully expressible through
   `set_local_origin()` today, with none of the above problems - see the
   scoped-decline note below), but scale and rotation raise a genuinely
   different question: does a transformed node's declared SIZE (used by its
   parent's flex/wrap arithmetic) still describe its pre-transform box, or
   its transformed bounding box? Nothing in this codebase has had to answer
   that question yet, and answering it is a decision this slice declines to
   make casually.

**What would be a defensible scoped subset, named rather than built:**
translate-only or translate+scale (axis-aligned, so bounds stay rectangles
and integer device-pixel layout survives) sidesteps problem 1 entirely and
narrows problem 2 to an affine-but-not-rotational inverse. It was evaluated
and declined for this slice specifically because integer translation ALONE
is already fully expressible via `set_local_origin()` - a `transform` value
that only ever translated would be a second way to say a thing the tree
already says, adding an id and a validation path for zero new capability.
Scale is the first genuinely new case, and it still needs answer 3 above
before it can be built responsibly. This is therefore named as the next
slice's starting point, not silently absorbed into "declined."

### Where the verdict is recorded

Per this task's own instruction, the gap is recorded as a worklist item in
`doc/completeness.md` section 6 (the design.md-contradiction table this
project already maintains) rather than by editing `design.md`'s own prose in
this slice - `design.md` is treated as a document whose NEXT revision should
add the missing `dg_node_set_transform` line, informed by the shape and the
three blockers this section names, not as a file this slice edits casually.

---

## 7. Defect injection: 11 injections against this slice's own new logic

Every injection below targets code this slice wrote - the channel's id
dispatch, the gradient stop validation, the shadow budget cap, the shadow
damage-outset mechanism, and the scalar door's revised status - never
pre-existing logic. Each was applied to a backed-up copy of the file (`cp`,
not `git checkout -- .`, since every file this slice touches was already
uncommitted at injection time and `git checkout` would have reverted to the
PRE-SLICE version rather than removing the injection - the
"git-checkout-erases-new-test" failure mode this project's own accumulated
findings warn about, avoided by construction), rebuilt with `ninja` before
being trusted, and restored with a byte-for-byte `cp` verified by `diff`.

| # | injection | file | result |
| --- | --- | --- | --- |
| A | `complex_prop_prelude()`'s type check inverted (`!=` to `==`) | node_props.cpp | **Caught** - 7 test cases, 22 assertions across `test_complex_props.cpp` |
| B | gradient stop-count floor relaxed (`< 2` to `< 1`) | node_props.cpp | **Caught** - "fewer than two stops" case |
| C | gradient offset monotonicity relaxed (`<=` to `<`) | node_props.cpp | **Caught** - "non-increasing offsets" case |
| D | shadow blur cap removed (bound raised to 100000) | node_props.cpp | **Caught** - "past the budgeted cap" case |
| E | `shadow_reach()`'s `blur_radius * 3.0F` reduced to `blur_radius` (no 3-sigma factor) | render_tree.cpp | **Caught** - the shadow byte-identity script, frame 1 of 100 |
| F | `can_be_cut()`'s `clip_atomic \|\| shadow_atomic` changed to `&&` | tree_paint.cpp | **Caught broadly** - breaks BOTH the pre-existing rounded-rect damage tests (`test_text_damage.cpp`) and the new shadow damage tests, 7 cases / 20 assertions |
| G | `Node::visible_bounds()`'s outset removed (`declared_paint_bounds(...)` reverted to plain `absolute`) | tree_impl.h | **Caught** - the shadow byte-identity script, frame 30 of 100 |
| H | `RenderTree::set_style()`'s `shadow_changed` guard dropped from the damage-before-overwrite condition | render_tree.cpp | **Caught** - the shadow byte-identity script, frame 10 of 100 (re-verified after a first attempt produced a stale-build compile error rather than a real result - rebuilt clean before trusting the outcome, the "stale build" failure mode named by name rather than glossed past) |
| I | the PAINTER's own gradient stop-count guard relaxed (`>= 2` to `>= 1`) in `skia_paint.cpp` | skia_paint.cpp | **Survived** - full 21/21 CTest still green. Root-caused below, not merely noted. |
| J | `apply_background_gradient`'s scalar-door status changed from `kTypeMismatch` back to `kUnsupported` | node_props.cpp | **Caught** - `test_props_boundary.cpp`'s updated case |
| K | `set_transform()`'s id-prelude call removed (always proceeds to `kUnsupported` regardless of `prop_id`) | node_props.cpp | **Caught** - "validates its id like the other three" case |

**Injection I, root-caused rather than left as "survived":** `dg::set_gradient()`
is the ONLY writer of `NodeStyle::background_gradient` anywhere in this
slice's test surface, and its own `valid_gradient_stops()` already refuses
fewer than 2 stops BEFORE anything reaches `NodeStyle` (injection B proves
that gate is itself load-bearing). So no path this project's tests exercise
can ever hand the painter a 1-stop gradient to mis-render - the painter's own
`>= 2` guard is defence-in-depth against a `NodeStyle` constructed BY HAND,
bypassing the setter entirely, which is a real and legitimate future caller
shape (nothing stops a future test or a future direct-`RenderTree` caller
from doing exactly that) but is not one anything in this slice's suite does
today. This is the "provably inert code" failure mode this project has now
recorded four times (doc/compositing.md's anti-alias-slack finding on a
layer's extent, doc/image.md's `set_image()` dedupe-guard finding, and now
this one) - the guard is correct and kept, its own removal is inert against
THIS suite specifically, and that distinction is worth stating rather than
silently resolving as "nothing to see here."

No injection reproduced "wrong-copy" or "missing scene shape" as a NEW
finding this slice made - both were designed against structurally: every
scene this slice's tests build already mixes shadow with rounded-rect
atomicity and with an ancestor clip (`test_complex_props.cpp`'s shadow-damage
scene), so the injection campaign inherited coverage rather than having to
discover a missing shape from a failed injection.

---

## 8. design.md line 622's acceptance criterion, re-verified for this slice

> "如果实现 `Slider` 需要新增 RenderObject，说明第 3 层的原语集设计有缺陷。"

`doc/completeness.md` section 2 found this held with zero new node kinds
across 4-1 through 4-9; `doc/image.md` and `doc/list.md` extended the streak
through 5-1 and 5-3. This slice was explicitly named as one of the two most
likely to finally break it - design.md's own primitive inventory (section
5.4, line ~316-319) lists `RenderTransform` as a distinct built-in
`RenderObject`, and a shadow's out-of-bounds paint is exactly the kind of
thing that could have forced a new compositing-layer kind.

**The streak holds. Twelve consecutive slices, zero new node kinds.**
`background_gradient` and `shadow` are both fields on the existing
`NodeStyle` (`LinearGradientStyle`, `ShadowStyle`), read by the existing
paint function (`skia_paint.cpp::paint_node()`) and the existing damage
machinery (`tree_impl.h`, `tree_paint.cpp`) through the existing
`clips_atomically`/`shadow_atomic` pattern. No `RenderTransform`,
`RenderShadow` or a new compositing-layer kind was needed - shadow does not
even need `saveLayer` the way `opacity` does, because `SkImageFilters::DropShadowOnly`
is a single-draw-call image filter, not a subtree composite. `transform` is
declined outright (section 6), so it cannot have forced a new kind either.
Whether a FUTURE transform slice (translate+scale, the scoped subset named in
section 6) would still hold the streak is an open question this slice does
not answer, because it does not build that slice.

---

## 9. Final property counts

49 properties, unchanged in total count. Three moved from not-yet to
implemented; one (`transform`) remains not-yet, with its blocker fully named
rather than merely inherited:

- **38 implemented** (was 35): `background_gradient` (17), `shadow` (28) and
  `image_source` (47) join the 35 slice 5-1 left implemented.
- **10 partially implemented** (unchanged) - this slice touched none of them.
- **1 not yet implemented** (was 4): `transform` (30) alone, blocker named in
  section 6 - non-axis-aligned damage bounds, an inverse-transform hit test,
  and an unanswered layout question, all three needing to move together
  rather than one at a time.

8 `WidgetKind` values, unchanged. 6 `LayoutKind` values, unchanged. Zero new
node/RenderObject kinds (section 8). 21 CTest entries (was 20, +1:
`complex_properties.verify_demo_scene`). 17 examples (was 16, +1:
`examples/16_complex_properties`). `virtual` occurrences in `src/`+`include/`:
unchanged (this slice added none). SDL references in `include/`: unchanged.

---

## 10. What this does not do

- The C ABI itself (`abi/drawgui.h`, `abi_impl.cpp`) - phase P5, out of scope
  by the task's own name.
- Theme tokens (design.md section 5.7) - `ShadowStyle::color` and
  `LinearGradientStyle::stops[].color` are plain `Color` values, the same
  stand-in `ImageStyle::placeholder` already uses (doc/image.md section 1).
- Animating any of these properties - no animation clock exists project-wide
  (doc/scrolling.md, doc/text-input.md and doc/form-controls.md already name
  this as the shared blocker for fling, caret blink and a slider's own
  transitions; `TransformDesc`'s own decomposition exists FOR a future
  animation clock, per section 6, without one existing yet).
- Radial or sweep gradients (section 3) - declined by name, a linear axis is
  the only one this slice's own scope covers.
- Inner shadows - only the outer, `box-shadow`-shaped one design.md section
  5.9.5 names is built; declined by name per the task's own scope.
- Backdrop filters, additional blend modes, `background_image` as distinct
  from `image_source` - none of the three touched by this slice; the first
  two were never in scope for any prior slice either, and the third is
  doc/image.md section 6's own recorded, still-open gap.
- Translate/scale `transform` (section 6's named next step) - evaluated and
  deliberately left to whichever slice answers the layout question first.
