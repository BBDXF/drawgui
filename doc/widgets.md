#Sub - step 3 : basic widgets and pointer interaction

Third slice of step 3. Sub-step 1 built damage-driven retained repaint, sub-step 2
built incremental layout, and both landed with a byte-identity acceptance test.
This slice adds hit testing, a hover/press/click state machine, four concrete
widgets and `examples/05_widgets`.

Everything below is measured on this machine (i5-1145G7, WSLg/x11, Release)
unless it says otherwise. **Performance is not an acceptance condition for this
slice** — the owner relaxed that requirement on the grounds that most GUI work is
event-driven and low-frequency. Numbers are reported because they inform design,
not because anything is gated on them.

---

## 1. The architectural fork: two trees, not three

`doc/design.md` says the project is "架构参照 Flutter 分层渲染模型" (architected
after Flutter's layered rendering model), and Flutter has three trees — Widget
(configuration), Element (identity and state), RenderObject (layout and paint).
This project already has a layout tree and a render tree. Does a widget tree
join them?

**No. Widgets are state attached to the nodes that already exist.**
`WidgetSet` is `std::vector<std::optional<Widget>>`, indexed by the same node
index the layout and render vectors use. It adds no node, no identity and no
traversal.

### Why Flutter has three, and why that reason is absent here

Flutter's middle layer exists to make *rebuilding* cheap. A declarative
`build()` returns throwaway `Widget` configuration objects every frame; the
`Element` tree persists identity across those rebuilds so that state is not lost
and so that the expensive `RenderObject` tree can be mutated in place rather than
rebuilt. **The Element tree is a diffing artifact.** Take away the declarative
rebuild and it has nothing left to do.

drawgui has no rebuild. Nodes are created once with `add_child` and mutated in
place with `set_box`, `set_style`, `set_fill`, `set_text`. Nothing produces a
throwaway description of the whole UI, and nothing diffs one against another.
Adding an Element layer would mean writing a diffing algorithm first, in order
to then need the layer that makes diffing efficient.

The C ABI goal points the same way. The eventual host is another language
driving nodes through `dg_node_set_prop(node, prop_id, ...)` — imperative
mutation of retained nodes, which is what an Element tree *produces*, not what
it consumes.

### design.md agrees, and the roadmap's framing was the thing to check

Worth stating plainly, because the question was posed as "design.md says
Flutter, and Flutter has three trees": **design.md never asks for three trees.**
Section 4 lays out seven layers and puts widgets at layer 5 over a render layer
at layer 3, and says of it:

> 控件不各自造轮子，全部踩在约十来个 RenderObject 之上（Box / Flex / Stack /
> Text / Image / Path / Scroll / Clip / Transform / Opacity / CustomPaint）。
> 这是 Flutter 模型最值得抄的部分。
>
> ("Widgets do not each reinvent the wheel; they all stand on about a dozen
> RenderObjects. This is the part of the Flutter model most worth copying.")

"Stand on RenderObjects" is composition, not a parallel tree. Section 5.4's
`RenderObject` protocol carries `layout()`, `paint()` and `hit_test()` on one
tree, and there is no `Element` anywhere in the document. The phrase "layered
rendering model" refers to the *rendering* layering, not to the three-tree
widget machinery. So this slice does not contradict design.md here — it resolves
an ambiguity in the roadmap's framing of it.

### What widget identity rests on, and what would break it

Widget identity **is** `NodeId`. That is sound today for one specific reason:
the node vectors are **append-only**. `RenderTree::add_child` pushes and nothing
is ever removed, so an index never shifts under a widget holding it, and no
layout operation — `set_box`, `resize`, a full relayout — can change which node
an index names. `LayoutTree` already relies on the same property to share
indices with `RenderTree`.

**Removal is what breaks this**, and it is worth writing down now because the
failure is silent: delete node 7, compact the vector, and every widget from 8
upward is now attached to its neighbour. When removal arrives it needs either
tombstones (never compact; indices stay valid, memory is not reclaimed) or a
generation counter in `NodeId` (`{
  index, generation}`, with a stale id rejected
on lookup). The second is the one that also catches use-after-remove in the C
ABI, where a host can hold an id indefinitely.

### What would force a third layer later

Only one thing, and it is specific: **a declarative rebuild API.** If a host
language ever says "here is my whole UI as a function of state, work out the
difference", then throwaway configuration objects need somewhere to deposit
identity and persistent state, and that somewhere is an Element tree. Nothing
else on the roadmap forces it — theming, animation, scrolling and focus are all
properties of retained nodes.

Two things that look like they might force it and do not:

- **A widget owning several nodes.** A button is a box with a label inside it,
  which is a subtree, not a third tree. Hit testing lands on the label and
  `owner_of()` climbs to the nearest ancestor that accepts pointer input — the
  same targeting rule a DOM uses. That climb is exercised on every button in the
  demo, because every caption is a child node.
- **Widget state that outlives an event.** That is what the side table is for.
  The checkbox exists in this slice specifically to carry it.

---

## 2. Hit testing

### The rule

**Hit testing is painting read backwards.** Painting walks depth-first
pre-order, so the last node to cover a pixel is the visible one; hit testing
visits each child in reverse order and then the node itself, first match wins.
That is the whole specification, and both defects that have no other symptom —
a widget you can see but cannot click, and a click landing on something hidden —
are exactly the two ways of getting the order wrong.

`src/render/hit_test.cpp` deliberately does **not** read the render tree's
`paint_order` vector backwards. That vector is built by a different function
walking the other way, and the exhaustive test compares this traversal against a
linear scan of it. Two separate implementations is what gives that test
something to disagree about; reading `paint_order` would make the test a
tautology.

### There is no spatial index

Considered and rejected. The obvious optimisation is to cache each subtree's
union extent and skip a subtree that cannot contain the point. Performance is
not a gate for this slice, and a cached extent is a second copy of the geometry
with an invalidation obligation on every move, resize and insertion. This
project has already deleted one speculative structure. The index is bought by a
measurement, when there is one.

It also happens to be the shape that removes a whole class of bug: **there are
no stored hit rectangles, so none can go stale after a reflow.** Hit testing
reads `absolute_bounds` from the live tree. What *can* go stale is the
*resolved* hover — see §4.

### The clipping decision: a child is hittable outside its parent

**Decided: hit testing does not clip a child to its parent.**

design.md does not settle this. It has an `overflow` property in the visual
property list (§5.9.4) and it rejects a global stacking context (§5.9.1), but
nothing in §5.4's `hit_test` protocol or §5.5's input layer says whether an
overflowing child is hittable where it overflows. So it was decided here.

The argument is that **hit testing must agree with painting, and painting does
not clip.** `paint_node()` draws each node at its absolute bounds with no
per-node clip; a child whose box runs past its parent's is drawn in the overflow
region, and `LayoutTree` reports the overrun as a *diagnostic* rather than
hiding it. A hit test that clipped would therefore refuse clicks in precisely
the region the screen is telling the user is interactive — manufacturing the
"visible but unclickable" defect that this slice's acceptance criteria forbid.

This is not a claim that clipping is wrong in general. It is a claim that
clipping must be a **property of the node that both painting and hit testing
read**. When a scrolling container needs a real clip, it gets a clip field,
`paint_node()` honours it, and `hit_test()` honours the same field. One rule,
two readers. What is rejected is hit testing having a private clipping rule that
painting knows nothing about.

The demo contains the shape (`--list-widgets` shows `spills` running past its
host), and `tests/unit/test_widget_pipeline.cpp` clicks it in the overflow.

### The exhaustive check

`tests/unit/test_hit_test.cpp` checks **every pixel** of a scene containing
overlap, nesting, occlusion, adjacency and overflow, against two oracles:

| oracle | what it catches |
|---|---|
| flat reverse-paint-order scan over a table this file declares | a traversal that visits siblings or subtrees in the wrong order |
| the colour read back after painting every node a unique flat colour | hit testing and paint order agreeing with each other and both being wrong |

The second is the literal statement of "what you see is what you click". It is
only exact because those scenes use square corners, opaque fills and no borders
or text — sub-step 1 measured that integer-aligned square rectangles rasterize
bit-identically, while anti-aliased rounded ones do not, so a rounded scene
would blend two nodes' colours at every corner and the oracle would be reading a
colour belonging to neither.

`tests/widget/widget_identity_main.cpp` runs the same two oracles over the real
demo scene at three viewport sizes, after a resize.

---

## 3. Text became a render-tree node, and it is NOT clip-atomic

A label needs text, so `NodeStyle` grew a `TextStyle` and `RenderTree` grew a
`FontCatalog`. Three findings came out of it.

### 3.1 Skia's anti-aliased text IS clip-invariant. Rounded rectangles are not.

The assumption going in was that glyphs behave like rounded rectangles — both
are anti-aliased, so both should be clip-dependent, and `clips_atomically()` was
written to return true for any node carrying a string.

**That was wrong, and measuring it is the only reason it was caught.**
`examples/05_widgets --clip-probe` draws the same shape twice — once unclipped,
once under a random clip that cuts it — and compares the pixels inside the clip,
over 600 random clips. The square row is a control: sub-step 1 measured it at
zero, so a non-zero square row would mean the probe is broken.

| shape | slack 0 | slack 1 | slack 2 |
|---|---|---|---|
| square rect | 0 | 0 | 0 |
| rounded rect | **582** | 0 | 0 |
| **text** | **0** | 0 | 0 |

The rounded row reproduces sub-step 1's finding exactly, including that one
pixel of slack is necessary and sufficient. Text differs by **zero** even at
zero slack. The mechanism is different: glyphs are rasterized into masks and
blitted, so a clip masks the blit, whereas a path fill computes analytic
coverage that the clip participates in.

So the rule was removed. `clips_atomically()` returns `!style.radii.is_zero()`
and nothing else. **This is worth 1.57x of the demo's damage** — the same
scripted pointer path repaints 2,912,220 px with text clip-atomic and
1,860,708 px without.

The probe cannot be written against the render tree, and the first version of it
was thrown away for exactly that reason: a text node was *already* clip-atomic,
so the tree grew every damage rectangle to swallow it whole before any clip
reached it, and the probe dutifully reported zero differing pixels — measuring
the rule rather than the fact the rule was supposed to encode. It now asks Skia
directly through `RasterSurface::sk_canvas()`.

### 3.2 Text is clipped to its node, and that is load-bearing

A string is the only thing in `NodeStyle` not naturally contained by the box it
was given. `paint_text()` clips to the node's bounds, because a glyph escaping
that rectangle leaves pixels nothing will ever invalidate — the one failure the
damage model cannot survive. `setSubpixel(false)` for the same family of
reasons.

### 3.3 There is still no font fallback chain

Unchanged from sub-step 2 and still true: `SkFontMgr_New_Custom_Directory`'s
`matchFamilyStyleCharacter()` returns null, so a family that lacks a glyph draws
nothing rather than borrowing one. `FontCatalog` is shaped by that: families are
named **up front** and resolved to a `FontId`, so a missing family is one
failure at startup naming what the machine does offer, instead of a per-node
mystery at paint time. `FontId{0}` is not a font, so "I forgot to set the font"
and "I asked for a family that is not there" have the same visible outcome
rather than a silent fallback onto whichever family happened to be first.
Sub-step 4 is what fixes this, and this table is what it will fix.

### 3.4 A label does not size itself to its text

Sub-step 2 deliberately excluded intrinsic sizing and told sub-step 3 to assume
it does not exist. So a label is given a box and the text is placed inside it.
**This has a sharp edge that cost real time — see §6.**

---

## 4. The state machine

`dg::Interaction` takes **the widget the pointer is over**, already resolved by
hit testing, rather than a coordinate. Three things fall out of that split:

- It is a pure function of its inputs, testable with no tree, no surface and no
  window. `tests/unit/test_interaction.cpp` writes each awkward case in five
  lines instead of staging a scene and aiming at it.
- **"Rapid motion that skips pixels" stops being a case.** A pointer that jumps
  across the window delivers one event naming one widget, and nothing believes
  it moved continuously to get there. Only a machine that interpolated would
  have to care.
- Hit testing is tested exhaustively on its own, with no interaction state in
  the way.

**Every transition is atomic.** One event returns one `InteractionChange`
carrying *both* the widget that lost hover and the one that gained it. There is
no instant at which neither or both are hovered, so no repaint can land in
between. That is the structural answer to "no flicker of neither-or-both" — it
is not something the caller has to be careful about.

### Behaviour at the edges, and why

| case | behaviour | reasoning |
|---|---|---|
| press, drag off the widget, release | no click; widget not stuck pressed | releasing away is how a user cancels. Firing on whatever is under the pointer at release time is the naive bug |
| press, drag off, drag back, release | click fires | the press is held throughout; only the *drawn* state follows the pointer |
| a press is in flight, pointer crosses another widget | the other widget does **not** hover | the press holds an implicit grab. Lighting up a neighbour that releasing cannot activate is a promise the UI will not keep |
| pointer leaves the window while hovering | hover clears | |
| pointer leaves the window while pressed | hover clears, press is **kept**, widget stops looking pressed | what every desktop toolkit does; dragging off the window edge and back still activates |
| two presses with no release between | the second takes the grab | |
| release with no press outstanding | nothing | |

**The window leave keeps the press deliberately**, and the risk that comes with
it is a release that never arrives. It does not arise here: X implicitly grabs
the pointer for the duration of a button press, so a release outside the window
*is* delivered and the machine unwinds normally. This was checked rather than
assumed — the scripted run warps the pointer out of the window mid-gesture and
the enter/leave counters stay balanced. Were a platform found that drops the
release, the fix is to carry the button mask on move events (every platform
already has it) and cancel a press whose button is no longer down; that is not
written now because nothing needs it.

### The one thing that does go stale after a reflow

There are no stored hit rectangles, but the **resolved hover** is a cached
answer. A resize moves widgets under a stationary pointer and no platform sends
a motion event for it, so the hover would keep naming whatever used to be under
the pointer. `widget_scene::resync()` re-runs the hit test at the last known
position after a relayout. This is the surviving form of the classic
stale-hit-rectangle bug, and it is a *caller* obligation, not a library one —
which is why `PumpResult` documents that `needs_repaint` must be drained before
`pointer`.

---

## 5. The widgets, and what a corner radius costs

Four kinds: `kPanel`, `kLabel`, `kButton`, `kCheckbox`. No `virtual`, no
`IWidget`, no visitor. A fifth kind is a fifth enumerator and a fifth case in
one switch, which the compiler will demand.

The checkbox earns its place over a third button by carrying **state that
outlives the click**. Button, label and panel are pure functions of the current
pointer state; a checkbox's appearance depends on the pointer *and* on
everything that happened before, which is the case a design that kept
interaction state only in the `Interaction` machine would get wrong.

### The damage consequence of a radius, measured again in the widget layer

Sub-step 1 measured 30x from one corner radius and told sub-step 3 that a theme
layer must expose the cost. `--damage-cost` at 1120x800, over a hover of every
interactive widget:

| controls | containers | repainted per hover | asked for | share of window | vs square |
|---|---|---|---|---|---|
| square | square | 9,305 px | 9,305 px | 1.04% | 1.0x |
| **rounded** | square | 16,151 px | 9,305 px | 1.80% | **1.7x** |
| **rounded** | **rounded** | **269,920 px** | 9,305 px | 30.12% | **29.0x** |

This reproduces sub-step 1's finding and **sharpens it**. The 30x is not "a
radius"; it is a radius **on a container**. A rounded node is clip-atomic, so
its whole area becomes the smallest unit of damage anything inside it can
produce — rounding a 104x30 button costs 1.7x, rounding the panels that contain
everything costs 29x.

That is the useful form of the rule, because rounded buttons are the normal
aesthetic choice and this says they are close to free. The demo defaults to
square containers and reports the number on screen, so the trade is deliberate.

---

## 6. Proving the tests can fail

Eleven defects injected, each built, run and reverted.

| # | injection | caught by |
|---|---|---|
| A | text put back into `clips_atomically()` | `text damage` (damage no longer minimal) |
| B | drop the inner clip in `paint_text()` so glyphs escape their node | `text damage` only |
| C | paint each node clipped to the damage rect instead of its own bounds | `text damage` + `widgets.interaction_equals_full` |
| D | hit test visits children in forward order | both hit-test oracles + identity gate |
| E | hit test checks the node before its children | both hit-test oracles + identity gate |
| F | `owner_of()` returns the hit node instead of climbing | `widget pipeline` only |
| G | `released_on()` clicks whatever is under the pointer | `interaction` only |
| H | hover tracks freely during a press (no implicit grab) | `interaction` only |
| I | leaving the window cancels the press | `interaction` only |
| J | a hover transition reports the enter but not the leave | `interaction` only |
| K | `set_fill()` damages only the top half of the node | `damage repaint` + identity gate |
| M | `set_text()` writes the string but never invalidates | **nothing, at first** |

Two of these are worth more than the fact that they were caught.

### What the byte-identity gate does NOT prove

F, G, H, I and J are caught only by the semantic suites, and that is structural
rather than a gap. `widgets.interaction_equals_full` drives **two copies of the
same code** and compares damage-driven repaint against full repaint. A defect in
what the widget layer *decides* corrupts both copies equally, so the comparison
still holds. **It verifies the damage system, not the widget semantics.**

This is worth stating because it is easy to over-read a byte-identity pass. Its
job is "the pixels you skipped were the pixels that did not change", and it does
that job exactly. "The right widget was activated" is a different question and
`test_interaction.cpp` / `test_widget_pipeline.cpp` are what answer it.

### Injection M, and the label with no height

**M was caught by nothing.** Sub-step 2 recorded that this has two very different
causes — the scene lacks the shape, or the term has a single reader that cannot
tell stale from fresh — and that answering the second as if it were the first
burns hours. So it was diagnosed before anything was changed: a probe printed
the counter label's box.

```
counter label node=45 box 210,547 730x0
text before: 'clicks: 0'
text after : 'clicks: 1   last: button 'Overview''
damage rects after the click: 0,0 960x720
```

**730x0.** The label had zero height and was painting nothing, so invalidating
it damaged an empty rectangle and no pixel test could possibly see the defect.

The cause is §3.4 with a sharp edge on it. The label was given `grow = 1` and no
height. `grow` is a **main-axis** share; in a row aligned `kCenter` a child is
loosely constrained on the *cross* axis and a leaf shrinks to fit — and because
there is no intrinsic sizing, a string contributes **nothing**, so the box
collapsed to zero. Two labels in the demo were built that way and were invisible
on screen the whole time.

This was cause 1: **a missing shape**, specifically a *visible* label whose text
changes. Fixed by giving those labels a height, after which M is caught by the
identity gate at pixel 276,541 — inside the status bar, exactly where the label
lives.

The general guard added is not "test that label": `test_widget_pipeline.cpp` now
asserts that **every** text-bearing node in the scene has a non-empty box. A text
node with no area is always a mistake, so it is checked directly rather than
hoped for. That is the check that would have caught this on the day it was
written, and it is cheap because the scene can enumerate itself.

**Generalisable form:** when a layout system has no intrinsic sizing, "content
determines size" is false for *every* content-bearing node, and a zero-extent box
is the silent result. Any node that paints something a box did not ask for wants
an assertion that its box exists.

---

## 7. What changed in the shared code, and why

The rule is that no interface is written before an implementation justifies it.
Everything below grew from a demonstrated need in this slice.

| addition | the need |
|---|---|
| `PixelPoint`, `contains(PixelRect, PixelPoint)` | a pointer position is an integer pixel, and hit testing must agree with the rasterizer about which pixel that is |
| `RenderTree::hit_test()`, `RenderTree::parent()` | z-order lives in the render tree, so the inverse of paint order belongs there; `parent()` is the climb from a hit node to its widget |
| `NodeStyle::text`, `TextStyle`, `FontCatalog` | a label is text, and text is the first thing a node paints that is not a rectangle |
| `RenderTree::set_text()` | a label re-asserting the string it already shows must not damage; that is the common case in an interaction loop |
| `PointerEvent`, `PumpResult::pointer` | a demo you can click needs pointer input, and `pump()` had none |
| `WindowManager::warp_pointer()` / `post_pointer_button()` | a scripted pass has to go **through** the event queue; one that bypassed hit testing would prove nothing |

`grep -rn "SDL" include/` is still empty, and no Skia type appears in
`include/`. `PointerEvent` positions are **physical** pixels: SDL reports
logical ones, the two differ by the window's pixel density, and a pointer off by
that factor lands *near* widgets rather than on them — a defect invisible at
density 1, which is every untested developer machine. The conversion happens
once, in the backend.

Only the **primary** button produces an event; the backend drops the others.
A right-click therefore cannot activate a widget — not because the state machine
checks, but because the event does not exist. A second button arrives with the
code that consumes one.

---

## 8. Verification performed

- **9 CTest entries** green (7 inherited, 2 new), g++ and clang++, Debug and
  Release, `-Werror`. Golden baselines untouched, tolerance still 0,
  `drawgui_render_png` output sha256 still `f635028e…`.
- **`-DDG_SANITIZE=ON`** clean on the whole suite, both compilers, plus a
  multi-minute interactive run under ASan+UBSan+LSan driven by real X input:
  zero diagnostics.
- **Real display, real input.** Screenshots under
  `.omo/evidence/drawgui-kernel/widgets-*.png` were captured with a scratch
  Xlib/XTest tool that injects motion and button events **at the X server**, so
  the demo receives ordinary hardware events and nothing inside the process is
  synthesized. (`XGetImage` on the root window still fails with `BadMatch` under
  WSLg's rootless server; per-window capture works — unchanged from sub-step 1.)

  Sampled byte-exactly on the `Apply` button, clear of its caption glyphs:

  | state | fill | expected |
  |---|---|---|
  | idle | `#2E86DE` | `kAccentNormal` |
  | hovered | `#4FA3F7` | `kAccentHover` |
  | pressed | `#1B5F9E` | `kAccentPressed` |

  A sidebar button sampled in all four screenshots stays `#2C3644` throughout —
  no spurious hover. In the overlap region, only the topmost widget changes:
  `#2E9E6B` → `#44C489` while the widget underneath stays `#7D5BA6`.

- **Resize then hit test.** The `pinned` button is anchored to the lab's far
  corner specifically so that it *moves* on reflow — everything else in the
  scene is a fixed size at a fixed offset and sits still, which would have made
  a resize check pass without testing anything. It moves from `(1026,565)` at
  1120x800 to `(726,385)` at 820x620, and under a real `XResizeWindow` it is
  hovered correctly at both.

---

## 9. Left undone, deliberately

Focus, keyboard navigation and activation, text input and editing, scrolling,
animation, a theme system, font fallback, the C ABI, GPU. Also: double-click,
drag thresholds, pointer capture across widgets, and design.md §5.16's gesture
arena — an arena with one competitor is a data structure with no purpose, and it
arrives with the second recognizer.

Two specific notes for whoever picks this up:

- **`parent_uses_size`.** `layout_tree.h` says sub-step 3 should re-add it the
  moment an arrangement genuinely stops reading a child's size. None does; it is
  still absent.
- **Intrinsic sizing.** A label that shrink-wraps its text needs it, and §6 shows
  the cost of not having it is a class of silent zero-extent box. That is the
  strongest argument for it so far, and it should be weighed against §5.4.6's
  O(n²) warning and the caching obligation that comes with it.
