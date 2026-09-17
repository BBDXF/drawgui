# Clipping and `overflow`

Slice 4-4. What a clip is, why painting and hit testing read one rule, how a
rounded clip composes with the anti-alias slack rule that was already here, and
where all of this contradicts `design.md` and CSS.

The short version:

- `overflow` is one field on `NodeStyle`. Painting applies it through the
  canvas, hit testing applies it through `clip_contains`, damage applies it
  through `Node::clip_bounds`. **Three readers, one field.**
- A rounded clip is **not clip-invariant**, measured. It needs exactly one
  pixel of slack, which is the constant this engine already carried - so the
  rule needed no change. The cost is that a rounded clipping container makes
  its whole area the minimum damage unit for everything inside it.
- Damage is intersected with the clip, so a change under a clip cannot ask for
  a repaint of pixels the clip removes.

---

## 1. The decision slice 3 deferred, and how it is closed

`doc/widgets.md` recorded this, and it is the specification for this slice:

> Hit testing does not clip a child to its parent, because painting doesn't
> either. Clipping must be a node property both readers see, not a private rule
> for one.

Both halves survive intact:

**Being someone's child still confines nothing.** An overflowing child of an
ordinary node is painted in the overflow and is clickable there, and the layout
tree still reports the overrun as a diagnostic rather than hiding it. That was
never a stopgap; it is what keeps a visible widget clickable.

**A node that asks to clip confines what is under it, in both readers.** The
question "is this point inside the clip" is asked of the same field, about the
same rectangle, with the same corner arithmetic, by `paint_subtree` and by
`descend`. They remain two separate implementations - the painter pushes the
shape onto the canvas, the hit test evaluates it directly - which is what gives
the exhaustive test something to disagree about. A hit test that replayed the
painter's traversal backwards would be a restatement of it and its test a
tautology.

The equivalence that makes this checkable rather than arguable:

> **A pixel that was not painted because it was clipped is not hittable.**

`tests/unit/test_hit_test.cpp` asserts it at every pixel of a clipped scene
against two oracles, and `examples/07_clipping --verify-clipping` asserts it at
every pixel of the scene actually on screen, at six window widths.

---

## 2. What is clipped, and to what

### The border box, not CSS's padding box

A clipping node confines its descendants to **its own bounds together with its
`radii`** - the border box.

CSS clips at the *padding box*, and `design.md` section 5.9.3 states the rule
that a property sharing a CSS name must share CSS behaviour. **This deviates,
deliberately, and the reason is the invariant everything else in the render
layer rests on:** the rectangle a node declares is exactly the rectangle it
paints, and `RenderTree` is the layer that does not know what padding is.

Clipping at the padding box would mean either putting a second copy of layout's
insets into `NodeStyle` - the two-vocabularies drift `doc/properties.md` exists
to prevent - or having the layout tree push a clip inset down, which is the same
copy with an extra hop. It would also make a clipping container look different
from a non-clipping one along its own border, which has nothing to do with
overflow: children already paint over their parent's border today, because a
parent paints before its children.

What would force the other choice is a border that is not opaque, or a border
style where a child showing through it is visibly wrong. Neither exists yet.

### The descendants, not the node itself

The node's own paint is inside its bounds by construction: the fill and border
are drawn to the box, and text carries its own containment clip. Clipping the
node with its own shape would change nothing except the anti-aliased coverage of
its own rounded fill - moving measured pixels for no reason.

This is also what makes the empty case behave. A clipping node with no area
removes its whole subtree and is itself hittable nowhere, because its own bounds
are empty too.

### Nested clips intersect

A clip inside a clip is the intersection of both, not the innermost. On the
canvas this is free - successive `clipRect`/`clipRRect` intersect. For damage it
is explicit, in `reposition()`, and for hit testing it falls out of the
traversal never reaching a descendant through a clip the point failed. Ancestor
clips therefore need no separate handling: a grandparent's clip is honoured by
the same line as a parent's.

---

## 3. The rounded clip and the anti-alias slack rule

This is the part the slice was warned about, and it was measured rather than
assumed.

### The measurement

Slice 3-1 established that Skia's anti-aliased rounded rectangles are not
clip-invariant: cutting one with a clip changes its coverage at pixels well
inside the clip. `kAntiAliasSlack = 1` exists for that, and setting it to zero
still fails the damage identity test.

The new question is one level up. A rounded **clip** is a clip, not a shape, so
does adding the damage `clipRect` on top of it change the pixels inside the
damage rectangle? Reference clip stack `{rrect}` against subject clip stack
`{damage rect, rrect}`, compared inside the damage rect, 600 randomized damage
rectangles per cell, only counting rectangles that actually cut the clipper:

| clip                     | slack 0 | slack 1 | slack 2 |
| ---                      | ---     | ---     | ---     |
| square `clipRect(aa=0)`  | 0       | 0       | 0       |
| rounded `clipRRect r=6`  | 18      | 0       | 0       |
| rounded `clipRRect r=18` | 204     | 0       | 0       |
| nested rounded r=6       | 86      | 0       | 0       |
| nested rounded r=18      | 476     | 0       | 0       |

**One pixel is necessary and sufficient for a rounded clip too, nesting
included.** The square row is the control and is zero everywhere, which is why a
square clip needs no atomicity at all.

### How it composes: for free, and that is not luck

**No new rule was needed.** A rounded clipper has non-zero radii, and
`clips_atomically()` has always returned true for a node with radii. So
`expand()` already grows any damage rectangle touching one until it contains the
clipper plus its one-pixel halo.

The containment argument for the subtree is one line: every descendant of a
clipping node is confined to that node's box, so a damage rectangle containing
the clipper contains every visible pixel of the subtree, and the damage clip
therefore never cuts one.

A descendant that is *partially* outside the clip is covered by the same
sentence - the part that is outside is not painted, so it has no pixels to cut.
A descendant that is *entirely* outside contributes nothing and is skipped by
`can_be_cut()`.

### The cost, stated plainly

**A rounded clipping container becomes the minimum damage unit for its whole
subtree.** Changing one small thing inside a rounded card repaints the card.

This is not a new cost - it is the cost slice 3-1 already measured for rounded
containers, where the same scene went from 16,250 damaged pixels per frame to
492,822 (0.78% of the window to 23.8%, a 30x difference) purely by rounding the
containers. What is new is that `overflow` makes rounded containers much more
attractive, because a rounded card that clips its content is the ordinary
real-world case. So the cost will be paid more often.

The guidance from 3-1 stands and now matters more: **round the small things that
move, not the large things that contain them.** A theme layer must expose this,
and `examples/07_clipping` is deliberately built with two square clipping panels
beside the rounded ones so the difference is visible.

What would remove the cost is a compositing layer that caches the clipped
subtree, which is slice 4-5's business, not this one's.

---

## 4. Damage must intersect the clip

Byte identity between an incremental and a full repaint is satisfied by damaging
too *much*, so it cannot be the whole requirement. The other half:

> A child that changes inside a clipped container must not damage pixels outside
> the clip - otherwise you repaint, and possibly present, regions the user
> cannot see.

`Node::clip_bounds` is the intersection of every ancestor clip, and
`damage_subtree()` adds each node's **visible** extent rather than its declared
one. A node an ancestor clip removes entirely adds nothing at all.

Three details that are load-bearing:

**`clip_bounds` is `std::optional`, absent meaning "no ancestor clips".** A
sentinel of the viewport would have started intersecting damage that used to be
allowed to run past the edge - a behaviour change riding along with one that was
asked for. Absent means a tree with no clip in it behaves exactly as it did.

**For a rounded ancestor it is the bounding rectangle, not the curve.** That is
conservative in the only safe direction: damage may keep a corner pixel the
curve removes, so it stays a superset, while the shape itself is applied by the
canvas and by `clip_contains`.

**A clip change damages the OLD extent first.** Turning a clip on hides pixels
that were painted outside it, and those pixels are only reachable from the state
before the change. `set_style` therefore damages the subtree while `clip_bounds`
still describes where it used to be allowed to go, then updates, then damages
again - the same damage-then-move shape `set_local_bounds` already had, and the
same class of stale-pixel bug.

---

## 5. Anti-aliasing, and where the equivalence stops being exact

`apply_clip` is anti-aliased for a rounded shape and aliased for a square one,
and the two halves are decided by different arguments.

A **square** clip lands on integer pixel boundaries, so anti-aliasing it could
only blend an edge with no fraction to blend - and the damage clip in
`paint_region` is aliased for exactly that reason, so an aliased square clip is
also the one that cannot disagree with it.

A **rounded** clip has a real curve, and the whole point of the feature is that
content follows the curve rather than its bounding box.

The consequence is that a rounded clip has a band roughly one pixel wide where
"was this pixel painted" has no yes-or-no answer. Hit testing has to be binary,
so it tests the **pixel centre** against the exact geometry. This means:

- Every **fully covered** and every **fully uncovered** pixel is required to
  agree with hit testing, exhaustively.
- The band between them is excluded, is required to **exist** (a zero-width band
  would mean the clip is not anti-aliased at all), and is required to lie inside
  the four corner squares - which is the machine-checkable form of "it follows
  the curve and nothing bleeds along the straight edges".

Measured on the test's own geometry: 4,508 fully covered pixels, 120 on the
curve, zero disagreements. In the demo scene the excluded set is 2,176 pixels of
a 600,000-pixel frame.

## 5.1 One radius arithmetic, shared

Skia scales a rounded rectangle's radii down when a pair sharing an edge exceeds
that edge, and it scales **all four** by one factor. A hit test that tested the
radii the caller wrote would answer for a shape the rasterizer never drew.

`fit_radii()` therefore exists once, in `src/render/clip_shape.h`, and the
painter hands **its output** to `SkRRect` rather than the raw radii. The two
readers cannot disagree because there is one answer.

This was the source of two of the three surviving defect injections, and the
reason is worth generalising: **a function whose every caller independently
repairs a bad answer is a function with no test.** Both consumers degraded a
negative radius to a square corner on their own, so removing the clamp was
invisible. The test that has teeth builds the shape where the two behaviours
differ - a negative radius *beside* a large positive one on the same edge, where
the negative one makes their sum smaller and so hides that the positive one does
not fit.

---

## 6. What this cost structurally

**The flat paint-order list is gone.** A clip is a canvas state that must be
pushed before a subtree and popped after it, and a flat pre-order vector has
forgotten where the subtrees are. `paint_subtree` walks the tree directly and
visits nodes in exactly the order the vector recorded, so the z-order is
unchanged; the vector became unread and was deleted rather than kept beside a
traversal that no longer looks at it.

Nothing else moved. There is no clip node kind, no `RenderClip` render object,
no virtual anything, and no compositing layer.

### Against `design.md`

`design.md` lists `RenderClip` among its thirteen built-in render objects
(section 5.4) but **never specifies it** - not its API, not its semantics, not
which Skia call it makes. It also names `overflow` in the property set (5.9.6)
without ever saying what its values are or do, and section 5.9.3 explicitly
warns against reusing a CSS name with different behaviour, which is a warning
about `overflow` specifically.

So there was nothing to contradict on the mechanism, and two things to record:

1. **There is no `RenderClip` node here, and there should not be one.** A clip
   is a property of a node, because that is the only shape in which painting,
   hit testing and damage can all read the same rule. A separate node kind would
   make the clip a thing in the tree that hit testing has to be taught about
   separately - exactly the private-rule-for-one-reader failure this slice
   exists to avoid.
2. **`overflow` deviates from CSS at the padding box** (section 2 above), which
   by 5.9.3's own rule is an argument for renaming it. It is not renamed,
   because the id and name are already in `props/prop_ids.lock` and renaming is
   a MAJOR ABI break for a difference of a few pixels along a border. Recorded
   here instead.

`design.md` is also **silent on whether hit testing clips**, on paint traversal
order, and on how damage interacts with a clip boundary. All three are answered
here for the first time.

---

## 7. Proving the tests can fail

Fifteen defects were injected one at a time, each built and run through the full
CTest suite, each reverted.

| # | injection | caught by |
| --- | --- | --- |
| A | hit testing ignores the clip entirely | `test_clip` geometry cases, `clipping.verify_demo_scene` |
| B | hit testing uses the bounding box instead of the rounded shape | `test_clip` rounded-curve sweep, oversized-radii case |
| C | painting does not apply the clip | `test_clip` geometry cases, `clipping.verify_demo_scene` |
| D | painting clips a rounded node to its bounding box | `test_clip` rounded-curve sweep |
| E | nested clips take the innermost instead of the intersection | `test_clip_damage` intersection case **(added for this)** |
| F | damage is not intersected with the clip | `test_clip_damage` containment and silent-node cases |
| G | a clip change does not damage the overflow it hid | `test_clip_damage` clip-closing case |
| H | `kAntiAliasSlack = 0` | `unit`, `widgets.interaction_equals_full`, `damage.verify_demo_scene`, `widgets.verify_demo_scene` |
| I | oversized radii are not scaled to fit | `test_clip` oversized-radii case and `fit_radii` unit case |
| J | a negative radius is not clamped | `fit_radii` mixed-sign case **(added for this)** |
| K | the clip is not popped after the subtree | `test_hit_test` surface oracle, `clipping.verify_demo_scene` |
| L | an empty clip is traversed instead of skipped | **nothing - see below** |
| M | both `overflow` ordinals mean `clip` | `test_props_boundary` ordinal case |
| N | a clipped-away node still forces damage expansion | `test_clip_damage` hidden-rounded case **(added for this)** |
| O | `clip_contains` uses the pixel corner, not its centre | `test_clip` rounded-curve sweep |

Eleven were caught immediately. Four survived the first pass, and this project
has recorded three distinct reasons for that; all three turned up here, which is
the first time that has happened in one slice.

**E and N - the injection aimed at a term two other mechanisms already cover.**
`clip_bounds` is not what makes the picture right: the canvas clip stack
intersects correctly on its own and hit testing walks the ancestors itself. A
wrong value there only makes damage too *large*, and growing damage is never
incorrect, so no pixel comparison can object and byte identity is blind to it by
construction. The assertion has to be about the term's own job, which is
tightness - a bound on the damage rectangle, not on the pixels. Two cases added.

**J - the scene lacked the shape.** The negative-radius test used four equal
negative radii, which is precisely the degenerate case where the clamp does not
matter: nothing is scaled either way and both consumers fall back to a square
corner. The shape with teeth is a negative radius beside a large positive one on
the same edge. This also drove a structural change: `src/` was added to the unit
test's include path so `fit_radii` can be tested directly rather than only
through the pixels it produces.

**L - the term has no observable consequence, and cannot be given one.** The
empty-clip guard is genuinely inert today, and the comment that claimed
otherwise has been corrected. What it looked like it was protecting is
guaranteed elsewhere: `PixelRect::from_edges` cannot produce an inverted
rectangle, so Skia never sees one, and `fit_radii` returns zero radii for an
empty box, so `apply_clip` takes `clipRect` rather than handing `SkRRect` a
degenerate one. An empty clip then removes the subtree by itself and the pixels
are identical. The guard is kept because it makes the traversal *stop*, which is
the only part that will still be true when a clip shape arrives that is not
derived from a `PixelRect`; and the guarantee is now asserted at its real source
instead - `fit_radii` of an empty rectangle is zero.

Two process notes worth carrying:

- The first negative-radius injection was reported as surviving when it had in
  fact been **reverted along with the test written to catch it**: the injection
  harness restored with `git checkout -- .`, which restores from the index, and
  the index predated the new tests. An injection harness must snapshot after
  every test addition. This is the second time in this project that a
  "surviving" injection turned out to be a measurement error rather than a
  coverage gap.
- `-Werror` and clang-tidy catch some defects before any test runs, and a defect
  they catch is not evidence about the suite. None of the fifteen above is in
  that category, but two candidate injections were rewritten because they were.

---

## 8. On screen

`examples/07_clipping`, on WSLg at `DISPLAY=:0`. Five panels, each a shape the
clip code branches on, with the unclipped control and the square-clipped panel
built from **one description with one flag different** so that what is between
them is the property and nothing else.

Verified on screen by comparing the unclipped and square-clipped panels'
screenshots pixel by pixel.

| what | result |
| --- | --- |
| clip on vs off, same window, nothing else changed | the control's children overrun into the gap; the clipped panel's stop at its border |
| the rounded panel, magnified 6x | the square child is cut to the arc, smooth, nothing bleeding into the corner |
| the same corner with clipping off | the child's square corner intact over the panel's rounded one |
| resize 1500 -> 1000 -> 720 wide | the overrun moves 324 -> 424 -> 296 px and the cut follows it |
| `overflow` toggled visible -> clip -> visible | the third frame is **byte-identical to the first** |

That last row is the on-screen form of section 4: it went through the
incremental damage path on a real window, so the overflow that was hidden and
then shown again left no stale pixel behind.

### The hit test, interactively

The pointer was walked across the right edge of the square-clipped panel through
the X server's own input path (`XTestFakeMotionEvent`), and the demo printed
what hit testing answered. `--probe X,Y` then pinned the same boundary offscreen,
reporting the pixel and the hit together:

```
  at 456,60  pixel #3c78c8  hit a chip of the square-clipped panel
  at 457,60  pixel #3c78f8  hit a chip of the unclipped control panel
```

The picture and the answer change at the same column.

And at the rounded corner, two points ten pixels apart, **both inside the
panel's bounding box**:

```
  at 490,16  pixel #14171c  hit rounded-clipped panel
  at 500,26  pixel #e8b45a  hit a chip of the rounded-clipped panel
```

The first is outside the arc: the chip is not painted there and is not hittable
there. A clip that used the bounding box would have kept both.

---

## 9. What this does not do

Scrolling. This slice supplies the clip a viewport will need and nothing else -
no scroll offset, no viewport node, no unbounded constraint. Also no compositing
layer, no `opacity`, no `shadow`, no `transform`, and no second sizing stage.

## 10. Property status after this slice

28 of the 45 properties fully implemented, 9 partial, 8 unimplemented.
`overflow` (id 29) moves from unimplemented to implemented, with both ordinals
applied and an ordinal past the list rejected as `kValueOutOfRange`.
