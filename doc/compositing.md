# The compositing layer, and `opacity`

Slice 4-5. What a layer is, why `opacity` needs one, the damage rule it turned
out NOT to need, the decision about hit testing, and what `shadow` and
`transform` still require.

The short version:

- `opacity` is **group opacity**: the subtree is composited offscreen at full
  strength, its overlaps resolved there, and the resulting image is drawn
  translucently. It is not per-object alpha, and a scene with overlapping
  children shows the difference immediately.
- **A layer is NOT damage-atomic.** Compositing at a scalar alpha is a
  per-pixel operation, so a change inside a layer damages that change and
  nothing more. That is the opposite of the rounded clip's rule, it was
  measured rather than assumed, and it is what a `shadow` or a `transform`
  will break.
- **Hit testing ignores `opacity` entirely, including at zero.** A pixel not
  painted because it was clipped is not hittable; a pixel not painted because
  it was faded away still is. This contradicts `design.md` section 5.11.2 and
  the contradiction is deliberate.
- A layer is opened only when one is needed: never at `opacity == 1`, and
  never at `opacity == 0` either, where the subtree is skipped instead.

---

## 1. Two meanings of "half transparent", and which one this is

`design.md` section 5.11.1 already states the distinction and this slice
agrees with it word for word, so it is repeated here only because everything
below depends on it:

| | what it means | how it is asked for |
| --- | --- | --- |
| **per-object alpha** | one drawing operation blends with what is under it | the alpha byte of `fill`, `border_color`, `text.color` |
| **group opacity** | the whole subtree is composited first, then blended as one image | `NodeStyle::opacity` |

The two are indistinguishable wherever content does not overlap and
unmistakable wherever it does. `examples/08_opacity` puts them side by side
with the **same three overlapping chips** and the same amount of
translucency:

- the per-object panel shows **five** bands from three chips: each chip's solo
  region plus the two overlaps, which come out lighter because two
  half-transparent chips blend with each other;
- the grouped panel shows **three** flat bands: inside the layer the chips are
  opaque, so the topmost simply wins, and only then is the whole picture
  faded.

`.omo/evidence/drawgui-kernel/opacity-group-vs-per-object-2x.png` is that
comparison magnified.

### The values, derived rather than recorded

`tests/unit/test_opacity.cpp` pins the difference with arithmetic done by
hand, because byte identity against a second render of the same code would be
satisfied by an implementation that gets the meaning wrong in both copies -
a lesson this project has written down twice already. Over an opaque black
backdrop, with white content and a group alpha of exactly `128/255`:

| what | expected | why |
| --- | --- | --- |
| per-object, one layer of content | 128 | `0 * (1 - a) + 255 * a` |
| per-object, two layers of content | 192 | `128 + 128 * (1 - a)` |
| group, one **or two** layers | 128 | the layer resolves the overlap at full strength first |
| two nested groups | 64 | `255 * a * a` |

All four are what Skia produces. `128/255` rather than `0.5` so that every
expected value is an integer and the test is about compositing rather than
about which way a tie rounds.

### The one place they are not bit-identical

Where content does **not** overlap, group opacity and per-object alpha agree
to within **one level per channel**, not exactly. Measured across the demo
scene at six window widths: never zero difference everywhere, never more than
one level anywhere.

The reason is an extra quantization. Per-object alpha premultiplies the
chip's colour once, on the way to the framebuffer. Group opacity rasterizes
the chip at full strength into an **eight-bit** layer and multiplies that, so
the group's answer carries one more rounding step. `examples/08_opacity
--verify-opacity` asserts the bound rather than equality, and says so.

---

## 2. The damage rule: there isn't one, and that was the surprise

The expectation going in was that a layer would be a damage-atomic region
much like a rounded clip: composited as a unit, therefore repainted as a
unit. **It is not**, and the argument is short enough to check.

Compositing a layer at alpha `a` computes, for each pixel independently,

```
out(p) = layer(p) * a + backdrop(p) * (1 - a * layerAlpha(p))
```

with no term from any other pixel. So the picture decomposes by pixel exactly
as an ordinary draw does. A damage rectangle that cuts a layer in half
produces, inside itself, the same bytes a full repaint produces at the same
coordinates - **provided the content inside the layer is itself
clip-invariant**, which is the rule this engine already carries
(`NodeStyle::radii`, `kAntiAliasSlack`) and already enforces through
`clip_atomic`.

`tests/unit/test_opacity_damage.cpp` is the measurement: 120 frames, three
damage-rectangle policies including the always-union one and the recorded
`SkPicture` path, with overlapping children inside a layer, a rounded
clip-atomic child inside a layer, a fade through both 0 and 1, a layer inside
a layer, and a layer that also clips. Byte-identical to a full repaint on
every frame.

### What WOULD make a layer atomic

A term that reads neighbouring pixels. Concretely:

- a **blur** or a **drop shadow** - the whole point of both is that one output
  pixel is a function of many input pixels, so cutting the layer changes the
  answer inside the cut;
- a **transform** that is not an integer translation - the sampling grid moves,
  so a damage rectangle in device space is not a rectangle in layer space;
- a **backdrop filter** - it reads what is underneath, which a partial repaint
  has only partly redrawn.

None of the three exists yet. When the first one arrives, the rule it needs is
the one a rounded clip already has: `clips_atomically()` returns true, and
`expand()` grows any damage rectangle touching the node until it contains the
node's whole **subtree extent** plus the filter's own reach. That is a
one-line change to `clips_atomically` plus a wider halo, and
`test_opacity_damage.cpp`'s tightness case is the test that will have to be
inverted when it happens - deliberately written as the opposite shape of the
clip's rule so the two cannot be confused.

### The two tightness claims

Byte identity is satisfied by damaging too *much*, so it cannot be the whole
requirement. Both halves are asserted:

- **a change inside a layer is not grown to the layer.** Changing one square
  child of a faded group damages exactly that child's box - not the group, not
  the sibling it overlaps.
- **a change to the opacity itself damages the whole subtree.** That one change
  really does alter every pixel the subtree covers, including the ones nothing
  inside it moved, and `RenderTree::set_style` already damages a node's whole
  subtree so no new code was needed - only an assertion that says why.

### The looseness that is left, stated plainly

A change to a node inside a group whose `opacity` is **0** still raises
damage, even though nothing in that subtree can put a pixel anywhere. The
repaint is correct - it repaints the backdrop and draws nothing - but it is a
repaint nobody needed.

Closing it would mean propagating a "hidden" flag down the tree the way
`clip_bounds` is propagated, which is a second cached term with an
invalidation obligation on every opacity write. It is not worth it for a value
an animation passes through for one frame, and it is recorded here rather than
fixed silently.

---

## 3. The layer's extent, and why it is computed rather than cached

`SkCanvas::saveLayer`'s bounds argument is documented as a hint. It is also a
**clip**: content outside it is cut away. So the bounds have to cover the
whole subtree, not the node carrying the opacity - `overflow` deliberately
does not confine a child to its parent (doc/clipping.md), so a child
overflowing its faded group is an ordinary scene here and a layer sized to the
group would silently eat the overflow.

`Impl::subtree_extent()` is the union of every descendant's **visible** bounds,
so an ancestor clip shrinks it and an overflowing child grows it. It is
computed on demand, for the nodes that actually open a layer, rather than
cached on the node - the same decision `hit_test.cpp` records against a cached
subtree extent, and for the same reason: a stored extent is a second copy of
the geometry carrying an invalidation obligation on every move, resize and
insertion.

### It is NOT inflated by the anti-alias slack, and that is a proof

The obvious thing to do - and the first thing this slice did - was to inflate
the extent by `kAntiAliasSlack`, the one pixel every damage rectangle carries,
on the theory that a rounded node flush with the layer's edge would have its
coverage decided by that edge.

**Removing the inflation was one of fourteen injected defects, and it is the
one no test could see.** The diagnosis is not a coverage gap; the term has no
observable consequence and cannot be given one, which is the second of the
four diagnoses this project has recorded. The argument:

The halo on a damage rectangle exists because a damage rectangle **differs**
between an incremental repaint and a full one. A rounded node cut by one is
therefore rasterized under two different clips, and slice 3-1 measured that
Skia's analytic anti-aliasing answers differently.

A layer's extent is derived from the tree alone - the union of the subtree's
visible bounds - so it is **the same rectangle in both passes**. A rounded node
flush with its edge is cut identically in both and produces identical pixels.
The effective layer rect is `extent ∩ clip`, and near such a node the extent
edge is the binding one in both passes: the damage rectangle is already grown
to contain any rounded node plus a pixel of halo, because a rounded node is
clip-atomic, so the damage edge is never the nearer of the two.

Nor is any ink lost. This engine's standing invariant is that a node paints
only inside the rectangle it declared - it is why a uniform border is inset by
half its width rather than centred - so there is nothing outside the extent to
protect.

So the constant is not carried here. It is carried next door, for damage,
where it was measured and where it is necessary; copying it to a place where it
provably does nothing would be exactly the cargo cult this project's notepad
warns about in a different costume.

### Where the layer sits relative to a clip

The layer is opened **outside** the clip: `saveLayer`, then the node's own
paint, then `apply_clip`, then the children, then `restore`. For a scalar
alpha both orders produce the same pixels, and this is the one that will still
be right when the layer carries a filter - a shadow belongs to the node, not
to the region its descendants are confined to.

Both compositions are pinned: a faded group that also clips, and a faded group
**inside** a clip. The ORDER itself is not, and cannot honestly be: for a
scalar alpha the two commute, because a clip only ever removes pixels and an
alpha only ever scales them, and both are per-pixel. There is no scene today
that tells `clip(fade(x))` from `fade(clip(x))`. They stop commuting the
moment the layer carries a blur, which reads pixels the clip has already
removed - so the order is chosen for the case that does not exist yet, and
said so here rather than defended by a test that would pass either way.

---

## 4. Hit testing ignores `opacity`. This contradicts design.md

`design.md` section 5.11.2 says:

> | `opacity == 0` | 跳过绘制与命中测试，**但仍参与布局** |

**Painting obeys it. Hit testing deliberately does not**, and section 5.9.7's
justification for the rule is what makes the disagreement worth stating:

> | `visibility: hidden` | 与 `opacity: 0` 语义重叠，只保留后者 |

Those two entries together say that `opacity: 0` IS `visibility: hidden`. They
are not the same thing, and collapsing them removes a capability without
replacing it.

The reasons, in the order they decided it:

**There is no threshold to put the boundary at.** `opacity` is the property
animations drive. A fade runs continuously through every value between 1 and
0. A group at 0.5 is obviously still clickable; so is one at 0.02, which is
already invisible to a human. Under design.md's rule, interactivity changes
discontinuously at exact float equality with zero - a fade that lands on
0.004 stays clickable and one that lands on 0.0 does not. That is not a
semantic, it is a cliff.

**A clip and a fade remove a pixel for different reasons.** A clip says the
pixels belong to somebody else, and something else really is visible there to
be clicked - so a hit there would land on something the user cannot see, which
is the defect doc/clipping.md exists to prevent. A fade leaves the node
exactly where it was and puts nothing in its place. The equivalence this layer
checks is therefore two sentences, not one:

> A pixel that was not painted because it was **clipped** is not hittable.
> A pixel that was not painted because it was **faded** still is.

**CSS agrees, and for the same reason.** `opacity: 0` elements remain
hit-testable; `visibility: hidden` ones do not. They are two properties
because they answer two questions.

Both sentences are checked exhaustively, at every pixel, by the oracle in
`tests/unit/test_hit_test.cpp`: the faded scene is built twice, once with its
fades and once without, hit testing is required to give the identical answer
at every pixel of both, and the two renders are required to differ at 7,550 of
them so that "identical" is not holding for the trivial reason. Inside the
box of a group faded to zero, the surface is required to show the node
underneath and hit testing is required to keep naming the invisible node - at
every pixel.

On screen, `examples/08_opacity --freeze-at 0` shows a panel that is
completely blank while the pointer walking across it through the X server's
own input path reports `fading chip 0`, `fading chip 1`, `fading chip 2`.

### What this leaves unfilled, and where it belongs

The job `visibility: hidden` does - out of hit testing, still in layout - is
now genuinely unfilled, and section 5.9.7's claim that `opacity: 0` covers it
is false. The property that should do it already has a name in design.md's own
list: **`hit_test_behavior`**, in the interaction group of section 5.9.6. It is
not in `props/drawgui.props.toml`, which stops at the box-model, visual and
container groups, so it is a future append rather than a rename.

---

## 5. `saveLayer` allocates, so a layer nobody needs is a defect

Two values open no layer, for two different reasons:

- **`opacity == 1`**: compositing at alpha 1 is the identity, so a layer here
  would allocate an offscreen buffer to produce the picture that was already
  going to be produced.
- **`opacity == 0`**: nothing in the subtree can change a pixel, so the subtree
  is skipped entirely rather than composited into a buffer that is then
  multiplied away. The two are pixel-identical - a source of alpha zero leaves
  its destination untouched - and `test_opacity.cpp` requires a faded-to-zero
  group to render byte-identically to the same scene with the subtree absent.

**The first of those is invisible to every pixel comparison in this project**,
which is the whole reason `RepaintStats::layers` exists. A painter that opened
a layer for every node would produce byte-identical output while allocating a
buffer per node per damage rectangle; no golden image, no byte-identity script
and no hit-test oracle could object. The counter is incremented at the
`saveLayerAlphaf` call itself rather than derived from the styles a second
time, so it cannot report a layer that was not opened or miss one that was.

`design.md` section 5.11.2 asks for exactly this counter, in debug builds,
with a warning above a threshold. It is here in **every** build and reported
rather than warned, because a debug-only counter cannot be asserted by a test
that also has to pass in Release - and this project's acceptance technique is
tests, not warnings.

### The tier that was NOT implemented

Section 5.11.2's third row asks for one more optimisation:

> | 子树只有单个叶子且自身无重叠绘制 | 将 alpha 直接乘入该叶子的 `SkPaint`，**不开 layer** |

**Deliberately not implemented**, and the reason is that its precondition is
narrower than it looks. `paint_node` draws up to three things - a fill, a
border and a run of text - and they overlap each other by construction: a
translucent border sits over the fill, and text sits over both. The node
carrying the opacity is itself inside the layer, so the tier applies only when
that node paints nothing at all, has exactly one child, and that child paints
exactly one shape.

Getting the condition wrong turns group opacity back into per-object alpha,
silently, in exactly the case that has no other symptom - which is the defect
this whole slice exists to make impossible. Performance is not an acceptance
condition here, so the trade is a real correctness risk against an unmeasured
saving. It becomes worth doing when there is a measurement that says a layer
is costing something, and the condition it needs is written above.

### There is no `RenderOpacity` node

`design.md` section 5.4 lists `RenderOpacity` among its built-in render
objects and section 5.4.8 has it auto-promote to a repaint boundary during an
animation. Neither is here, and the reasoning is the one slice 4-4 already
recorded for `RenderClip`: opacity is a **property of a node**, because that is
the only shape in which painting, damage and hit testing can read one rule. A
separate node kind would make it a thing in the tree that each reader has to
be taught about separately.

The repaint-boundary half is answered by section 2 above: a scalar-alpha layer
is not damage-atomic, so a fade needs no boundary promotion at all. Its damage
is already exactly its subtree, which is the smallest correct answer.

---

## 6. What `shadow` and `transform` still need

This slice built the place they will live and deliberately built neither.
Both are complex-typed properties, so both additionally need the dedicated
setter shape of `design.md` section 5.9.5, which still does not exist.

### `shadow` (id 28)

| what it needs | status |
| --- | --- |
| a layer to composite through | **done** - `needs_layer` / `saveLayerAlphaf`, and the same call takes an `SkPaint` carrying an image filter |
| the layer to become **damage-atomic** | not done. A blur reads neighbouring pixels, so the per-pixel decomposition in section 2 fails. `clips_atomically()` must return true for a shadowed node and `expand()` must grow to the subtree extent plus the blur radius |
| painting **outside** the node's declared bounds | not done, and this is the hard half. Every rectangle in this engine is the rectangle the node paints; a shadow is the first thing that is not. `subtree_extent()` is where the outset belongs - it is already the union of what a subtree may touch - but `damage_subtree()` and `Node::visible_bounds()` still speak the declared box |
| a dedicated setter | not done. A shadow is offset, blur, spread and colour; `PropValue` is a scalar tagged union and cannot carry it |

The order that works: dedicated setter, then the outset in `subtree_extent`
and `visible_bounds`, then atomicity, then the filter itself. The middle step
is the one that needs its own damage measurement.

### `transform` (id 30)

| what it needs | status |
| --- | --- |
| a layer to composite through | **done** |
| the layer to become **damage-atomic** | not done, for a different reason than the shadow's: the layer's sampling grid stops aligning with the framebuffer's, so a device-space damage rectangle is not a rectangle in layer space |
| non-axis-aligned geometry | not done. `PixelRect` is axis-aligned integer pixels throughout - damage, hit testing and clipping all speak it - so a rotated node has no damage rectangle to declare and `contains(rect, point)` is not its hit test |
| hit testing under an inverse transform | not done. `descend()` compares a point against `node.absolute`; under a transform it must carry the point through the inverse first, and `clip_contains` with it |
| a dedicated setter | not done. design.md asks for translate / scale / rotate / origin decomposed for interpolation, which is five scalars |

An **integer translation** is the special case that needs none of the above -
it is already expressible as `set_local_origin`, and adding it as a
`transform` value would be a second way to say a thing the tree already says.
The property is worth implementing only for the cases that are not that, which
is to say for the ones that break `PixelRect`.

**Update (slice 5-4, phase 5): `shadow` built, `transform` declined.**
`shadow`'s dedicated setter (`dg::set_shadow()`) and its damage-atomicity
turned out NOT to need this section's own predicted route through
`needs_layer`/`saveLayerAlphaf` at all: `SkImageFilters::DropShadowOnly` is a
single-draw-call image filter, not a subtree composite, so no layer opens for
a shadow the way one does for `opacity`. The damage-atomicity half was built
as this section predicted in shape (a `shadow_atomic()` flag alongside
`clip_atomic`, and an outset folded into `Node::visible_bounds()` via
`declared_paint_bounds()`/`shadow_reach()`) though not literally through
`subtree_extent()` - the outset is a NODE-level fact, not a subtree one,
since `shadow` (unlike `opacity`) does not composite its descendants. `transform`
was evaluated and declined outright, with the three blockers this section
already named (damage-atomicity, non-axis-aligned geometry, inverse-transform
hit testing) all still open, plus a fourth this section did not anticipate -
whether transform affects parent layout at all, unresolved even for the
axis-aligned subset. `doc/complex-properties.md` is the full record for both;
this paragraph and the two tables above it are left as this slice originally
wrote them, a snapshot of what was predicted before 5-4, not rewritten.

---

## 7. Proving the tests can fail

Fourteen defects injected one at a time, each built, each run through the full
CTest suite, each reverted. Eleven were caught on the first pass.

| # | injection | caught by |
| --- | --- | --- |
| A | `needs_layer` always false - group opacity silently becomes per-object alpha | `test_opacity` overlap and nested cases, `opacity.verify_demo_scene` |
| B | `needs_layer` always true - a layer for every node that is not fully transparent | `test_opacity` and `test_opacity_damage` layer counts |
| C | `paints_nothing` always false - a fully faded subtree is composited at alpha 0 instead of skipped | `test_opacity` zero case, `test_opacity_damage` layer counts |
| D | the layer's bounds are the node's own box, not the subtree extent | `test_opacity` overflowing-child case |
| E | the extent joins `absolute` instead of `visible_bounds` | **nothing at first - see below** |
| F | the extent omits the node carrying the opacity | `test_opacity` own-fill case |
| G | the extent is not inflated by `kAntiAliasSlack` | **nothing, and nothing can - see below** |
| H | `restore()` is skipped after a layered subtree | `test_opacity`, `test_hit_test` surface oracle, `opacity.verify_demo_scene` |
| I | the node's own paint is drawn outside its own layer | `test_opacity` own-fill case |
| J | `apply_opacity` clamps out of range instead of refusing | `test_props_boundary` opacity range case |
| K | `apply_opacity` forgets `style_changed`, so the write never reaches the node | `test_props_boundary` opacity case |
| L | hit testing skips a subtree at `opacity == 0` - design.md's rule | `test_hit_test` ghost case, `opacity.verify_demo_scene` |
| M | the children are painted outside the layer, only the node inside it | `test_opacity`, `test_opacity_damage`, `opacity.verify_demo_scene` |
| N | the extent's early-out is dropped, so a layer opens for a region it cannot reach | `test_opacity_damage` clipped-away-group case **(added for E)** |

**E - the third diagnosis, plus the first.** Joining the declared bounds makes
the extent a **superset**, and a superset is never wrong for pixels: Skia
intersects a layer with the clip stack anyway, so the allocation and every
pixel are identical. Byte identity is blind to it by construction, exactly as
slice 4-4 found for `clip_bounds`. The assertion has to be about the term's own
job, which is answering "may this subtree put a pixel in the region being
repainted" - and a wrong answer opens an offscreen buffer for a subtree that
cannot paint anything, which `RepaintStats::layers` observes.

Building the shape took two attempts, and the first failure is the more useful
half. Hanging the fade one level below a separate clipping node left the
injection alive, because the clipper's own culling returned before the
traversal ever reached the layer - **the scene contained the term but could not
exercise it**. Making the fading node and the clipping node the SAME node fixed
it. Injection N was then added to pin the early-out itself.

**G - the second diagnosis: no observable consequence, and none is possible.**
Section 3 above is the proof rather than a shrug. The response was not to write
a test but to **delete the inflation** and record why: the constant belongs to
damage rectangles, which differ between two passes, and a layer's extent does
not.

---

## 8. On screen

`examples/08_opacity`, on WSLg at `DISPLAY=:0`. Four panels; the first two
carry the same three overlapping chips and the same amount of translucency
asked for in the two different ways.

Evidence under `.omo/evidence/drawgui-kernel/opacity-*`.

| what | result |
| --- | --- |
| per-object beside group, same content | **five** bands against **three**; the solo bands match, the overlaps do not |
| nested groups | the same three bands, visibly closer to the card - `a * a`, not `a` |
| resize 1560 -> 1180 | the layer extents move with the panels and the picture is unchanged in kind |
| a real fade, sampled eight times while it ran | the faded panel's region holds **exactly six colours in every frame, with identical pixel counts** (36,432 card / 11,328 / 7,080 / 7,080 chips / 2,160 background / 180 border). Only the three chip *values* change |
| the ring of pixels just outside the layer's extent | the card colour, in every frame |
| a group frozen at `opacity 0`, pointer walked across it with `XTestFakeMotionEvent` | the panel is blank; the demo reports `fading chip 0`, `fading chip 1`, `fading chip 2` |

The fade row is the on-screen form of "no accumulated artefacts and no stale
pixels at the layer boundary". A rim of the previous frame surviving at the
extent's edge, or a band composited twice, would introduce a seventh colour
and change the counts. Across the whole animation - opaque, mid-fade and
nearly gone - it never does.

---

## 9. What this does not do

No `shadow`, no `transform`, no `background_gradient`, no dedicated setters,
no second sizing stage, no scrolling. No repaint-boundary promotion, no
`SkPicture` caching per layer, and no GPU anything.

## 10. Property status after this slice

29 of the 45 properties fully implemented, 9 partial, 7 unimplemented.
`opacity` (id 27) moves from unimplemented to implemented, accepting the whole
of 0..1 and refusing anything outside it rather than clamping.
