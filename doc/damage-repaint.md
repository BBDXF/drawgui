# Damage-driven retained repaint: what it buys, and what it costs

Measured findings from sub-step 1 of step 3 — building the smallest retained
render tree that can mark one node dirty and repaint only that region.
Everything here is a recorded measurement on this host, and
`examples/03_damage_repaint` reproduces every number.

Step 2 (`doc/cpu-raster-findings.md`) ended with one instruction for step 3:

> **Damage tracking is architecture, not optimization.** It is worth 32x at
> 1080p and 63x at 1440p. Invalidation has to be designed before layout, not
> retrofitted after widgets exist.

This document is what happened when that was built.

## The headline

At 1920x1080, in a real window, paced to a 60 Hz cadence:

| | raster | present | total |
|---|---|---|---|
| damage-driven repaint | **0.19 ms** | **0.38 ms** | **0.56 ms** |
| forced full repaint | 9.76 ms | 5.14 ms | 14.89 ms |
| **speedup** | **51.9x** | **13.7x** | **26.4x** |

A frame that changes a small node repaints 16,250 pixels — **0.78% of the
window** — and draws 9 of the scene's 35 nodes.

26x total against the 32x step 2 predicted. The rasterization half beat that
prediction comfortably (51.9x); presentation is what holds the total down, and
the reason is measured below. Across several runs the total lands between 20x
and 28x — this host is a laptop under WSLg and the full-repaint lane in
particular varies by a factor of two between runs, so treat the ratio as "tens
of times", not as three significant figures.

## What was built

Three concrete types, no virtual function anywhere, no node interface, no
visitor.

```
dg::PixelRect        integer device-pixel rectangle, half-open
dg::DamageRegion     a bounded, pairwise-disjoint set of dirty rectangles
dg::RenderTree       retained nodes; mark one dirty, repaint what that implies
```

A node is a rectangle plus a fixed set of appearance fields. Painting is a
function over those fields. When a node needs to draw something the struct
cannot express, the struct grows a field — that does not require a vtable, and
design.md section 5.15.3 asks for exactly this storage shape.

`RenderTree::repaint()` clips the canvas to each damage rectangle, draws every
node whose bounds intersect it in z-order, and hands the same rectangles to
`WindowManager::present()`. Nodes above the changed one are redrawn because
they intersect the region, not because anything tracks them — which is what
makes the classic z-order artifact impossible rather than merely unlikely.

## Correctness: byte-identity, and the Skia property that nearly killed it

The check that makes partial repaint trustworthy is not watching the window.
It is this:

> After N frames of damage-driven repaint, the framebuffer must be
> **byte-identical** to the same scene rendered by one full repaint.

It runs offscreen, needs no display, and is a CTest entry. It found a real
problem on its second frame.

### Skia's anti-aliased rounded rectangles are not clip-invariant

Clipping a shape and drawing it produces *different pixels inside the clip*
than drawing it unclipped. Not at the clip boundary — everywhere in the shape.

Isolated with a standalone probe against `libskia.a` directly:

| geometry | randomized clips | differing pixels |
|---|---|---|
| axis-aligned rectangles at integer bounds, fills and 0.5-inset strokes | 1800 | **0** |
| rounded rectangles, same clips | 1800 | **6530** |

The differences are one or two levels of coverage, invisible to the eye and
fatal to a byte comparison. Skia's analytic anti-aliasing accumulates coverage
along a scanline, so where the blitter starts changes the rounding.

Then the useful part — how much slack the shape needs:

| clip | differing pixels |
|---|---|
| exactly the shape's bounds | 45 |
| bounds inflated by 1 pixel | **0** |
| bounds inflated by 2..6 pixels | 0 |

Confirmed over 3200 randomized clips: **one pixel of slack is necessary and
sufficient**, zero is not.

### The rule that follows

A node with rounded corners is **clip-atomic**: it is repainted whole or not
at all. Any damage rectangle touching one grows to contain it plus a
one-pixel halo, repeatedly, until nothing it touches is cut. Square-cornered
nodes have no such constraint and cost nothing, which matters because the root
node covers the whole viewport.

This is not a workaround. It is the render-tree equivalent of a repaint
boundary: **a rasterizer's anti-aliasing is not a per-pixel function, so the
unit of damage cannot be smaller than the unit of rasterization.**

### The consequence: round the leaves, not the containers

A rounded *container* makes its entire area the smallest damage any of its
children can produce. Measured on the same scene at 1080p, changing only
whether the cards and the sidebar items have rounded corners:

| containers | damage per frame | share of window | raster |
|---|---|---|---|
| rounded (radius 6) | 492,822 px | 23.8% | 0.27 ms |
| square | **16,250 px** | **0.78%** | **0.018 ms** |

Same scene, same animation, same damage algorithm. **Corner radius on a
container is a performance decision, not a visual one.** The widget layer has
to know this before it grows a theme.

The demo pays a 29% expansion tax as it stands — its dirty nodes ask for
12,584 px and 16,250 px get repainted — entirely because the animated accents
themselves are rounded. That is a fair price for the shape.

### What the verification covers

`tests/unit/test_damage_repaint.cpp`, in the always-built unit test:

- a node changing colour in place
- a node moving, so both its old and its new bounds are stale
- a node changing underneath an overlapping node that must survive on top
- two far-apart nodes dirty in the same frame
- damage caps of 1, 2 and 8, and both paint modes
- an odd viewport (481x331) so row padding differs from `width * 4`

Plus a test that the comparison **can** fail: skipping the last frame's
repaint must produce a difference. Writing that revealed something worth
keeping — skipping a *middle* frame is invisible, because damage accumulates
until something paints it. A dropped repaint is deferred, not lost.

Also verified from the demo binary itself: 2000 frames at 1280x800, 900 frames
at 1080p under a cap of 1, and 900 frames at 1080p replaying a recorded
picture — all byte-identical.

## How damage from several nodes combines

The question was whether to union everything into one rectangle or keep a
short list. Two dirty nodes at opposite corners have a bounding box of the
whole window, so a region that always unions has switched itself off in
exactly the case that matters. Measured rather than argued — the cap is a
parameter and `max_rects == 1` *is* the always-union policy.

Offscreen rasterization, Release, 300 frames per row:

| size | full repaint | damage, 8 rects | damage, 1 rect |
|---|---|---|---|
| 800x600 | 0.128 ms | **0.015 ms** | 0.033 ms |
| 1280x720 | 0.253 ms | **0.017 ms** | 0.052 ms |
| 1920x1080 | 1.238 ms | **0.018 ms** | 0.082 ms |
| 2560x1440 | 4.056 ms | **0.018 ms** | 0.200 ms |

**A list of rectangles wins by 4.7x at 1080p and 11x at 1440p.** In the
windowed demo the gap is wider still: at 1080p, cap 1 costs 1.45 ms of raster
and 1.71 ms of presentation, against 0.15 and 0.35 for cap 8 — the always-union
policy is only 4.5x better than repainting the entire window, while the
rectangle list is 27x better.

The list is kept **pairwise disjoint**, which is a correctness property rather
than tidiness: overlapping rectangles would repaint shared pixels twice and
blend a translucent node onto itself, so a frame assembled from an overlapping
region would not match a full repaint. When the cap is exceeded, the pair whose
union wastes the least area is merged — comparing union area alone would
happily fuse the two largest rectangles even when they sit at opposite corners.

Eight is the default. It is not a measured optimum; it is comfortably more than
the number of independently animating things a frame usually has.

Note the damage cost is **flat at ~0.018 ms from 800x600 to 2560x1440**. That
is step 2's central result reproduced through a real render tree: with damage
tracking, the cost of a change is a function of the change.

## Did SkPicture pay off? No.

design.md section 5.15.2 argues that a retained-mode GUI spends its time
traversing the tree and recording draw commands rather than rasterizing, and
that `SkPicture` caching therefore matters more than dirty rectangles. That is
a testable claim, so `RenderTree` implements both: `kDirect` walks the tree and
issues draw calls under the clip, `kPicture` records the whole tree once and
replays it, letting Skia cull.

Median milliseconds, four independent runs of 300-400 frames per row:

| size | damage direct | damage picture |
|---|---|---|
| 800x600 | **0.015 / 0.016 / 0.019** | 0.018 / 0.020 / 0.022 |
| 1280x720 | **0.019 / 0.034 / 0.025** | 0.022 / 0.025 / 0.025 |
| 1920x1080 | **0.020 / 0.020 / 0.020** | 0.023 / 0.023 / 0.023 |
| 2560x1440 | **0.021 / 0.036 / 0.018** | 0.023 / 0.044 / 0.021 |

**On the damage path, direct traversal beat picture replay in 10 of 12 paired
runs, tied once and lost once — a consistent 15-20% edge.** On the full-repaint
path the two are within run-to-run noise (direct won 8 of 12, by margins
smaller than the spread between repeats of the same configuration), so the
honest verdict there is "no measurable difference", not "direct wins".

Replaying a picture through the same clip costs slightly more than the
traversal it replaces.

That is not a refutation of section 5.15.2 — it is a statement about *this*
tree. 35 nodes of flat rectangles is a traversal cheap enough that recording
it buys nothing, and every structural or style change re-records the whole
scene. Picture caching earns its keep when the subtree is expensive to walk
and stable across frames, which is scrolling, not this. The mode stays in the
code because it is a handful of lines and re-measuring it when the tree grows
text and images is worth more than the argument.

Worth noting separately: picture replay is **pixel-identical** to direct
traversal. The byte-for-byte verification passes in both modes, including 900
frames at 1080p. So the choice is purely a cost question.

**Damage tracking is the mechanism that helps everywhere. Picture caching is
not.**

## Presentation is now the expensive half

At 1080p, damage-driven presentation (0.35 ms) costs more than twice
damage-driven rasterization (0.15 ms), and the difference is not pixels.

### Batching rectangles into one present() call

Each `present()` call is its own round trip to the display server, and the
round trip dominates. `WindowManager::present()` gained an overload taking a
span of rectangles — the first extension to that header since step 2, and it
was made against a measurement, not in anticipation. Six runs each at 1080p:

| | median present |
|---|---|
| one `present()` per damage rectangle | 0.572 ms |
| one `present()` carrying all of them | **0.424 ms** |

26% cheaper for two to four rectangles. It would have been reverted if it had
not measured.

### A tight benchmark loop understates a paced frame

Step 2 measured a dirty-rect present at 0.06 ms. This demo, at a 60 Hz
cadence, measures 0.35 ms for the same kind of work. Both numbers are right,
and the difference is the point. Pacing the loop and holding everything else
fixed:

| frame budget | damage raster | damage present | total |
|---|---|---|---|
| 1 ms (effectively a tight loop) | 0.077 ms | 0.204 ms | **0.28 ms** |
| 4 ms | 0.123 ms | 0.339 ms | 0.46 ms |
| 16 ms (60 Hz) | 0.212 ms | 0.552 ms | 0.76 ms |

Back to back, the display server pipelines the update and the framebuffer
never leaves cache. At 60 Hz, every frame pays a real round trip and a cold
cache. **A benchmark that measures a loop is measuring a loop.** Step 2's
partial-repaint numbers are tight-loop numbers and should be read as a floor.

The same effect makes the *full* repaint look far worse on screen than
offscreen — 8.78 ms in the window against 1.157 ms offscreen at 1080p. A scene
of flat fills is almost pure memory bandwidth, and after `present()` has walked
8.3 MB the next repaint starts with nothing cached. The on-screen figure is the
one a user experiences.

## Honest limits

- **The scene is flat rectangles.** No text, no blur, no images, no layout.
  Step 2 measured effects at 52% of rasterization; nothing here exercises that.
  A full repaint of a *real* scene is more expensive than the 8.78 ms above,
  which makes the damage ratio better, not worse — but that is an inference,
  not a measurement.
- **35 nodes.** Traversal is O(nodes) per damage rectangle, with no spatial
  index. That is fine at 35 and will not be at 5000; a real widget tree needs
  culling before it needs anything else here. This is also why the `SkPicture`
  result should be re-measured rather than treated as settled.
- **WSLg is not a native desktop.** The presentation numbers in particular
  must be re-measured on native X11, Wayland and Windows.
- **One host.** Intel i5-1145G7, x86_64 with AVX2, Release (`-O3 -DNDEBUG`).
  Debug numbers appear nowhere in this document.
- **The clip-invariance result is a property of Skia m153's CPU backend.** It
  should be re-checked against a GPU backend, where the rasterizer is entirely
  different and the guarantee may be weaker still.

## What sub-step 2 should take from this

1. **Corner radius is a damage-granularity decision.** Rounded containers cost
   30x the damage area of square ones on the same scene. The theme layer must
   expose that, and layout must not hand out radii casually.
2. **Keep a rectangle list, not a union.** Always-union is 4.7x worse at 1080p
   and barely better than repainting the whole window.
3. **Presentation, not rasterization, is the remaining cost.** Anything that
   reduces round trips to the display server is worth more than anything that
   speeds up drawing.
4. **Node bounds must be honest.** The entire model rests on a node touching no
   pixel outside the rectangle it declares. That is why borders are stroked
   inset rather than centred, and it is a rule every future node kind inherits.
5. **Do not add picture caching on faith.** It measured slower here. Re-measure
   when there is a scrolling viewport to cache.

## Reproducing

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# the demo: alternates between damage-driven and full repaint, prints both
./build/examples/drawgui_damage_repaint --size 1920x1080

# damage only, for watching whether anything rots over a long run
./build/examples/drawgui_damage_repaint --size 1920x1080 --mode damage

# correctness: N frames, compared byte for byte against a full repaint
./build/examples/drawgui_damage_repaint --verify-damage 2000 --size 1280x800

# offscreen raster ladder: full vs damage, direct vs picture, cap 8 vs cap 1
./build/examples/drawgui_damage_repaint --bench --frames 300

# the same equivalence check as a CTest entry, no display needed
ctest --test-dir build -R 'unit|damage'
```
