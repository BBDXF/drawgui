# Slice 5-1: `Image`, the one MVP-8 item doc/completeness.md found genuinely absent

design.md section 5.10 gives `ImageSource` three rows - bitmap, vector (SVG),
nine-patch - and a mandatory sizing rule (section 5.10.3) whose stated reason
is layout jitter: if a node's size could depend on decoded content, every
finished image load would trigger a reflow. `doc/completeness.md` section 1
found `NodeStyle` carried no image-bearing field of any kind and section 7
declined to build one in that slice, naming four sub-problems: decoded-image
ownership/lifecycle, a fit-mode decision, the `NodeStyle`-field-vs-new-leaf
question, and golden-test infrastructure for a non-solid-fill node. This slice
answers all four.

---

## 1. The scoping decisions, made in writing

- **Bitmap only.** design.md section 5.10.1's other two `ImageSource` rows -
  SVG and nine-patch - are declined by name, matching the task's own scope.
  SVG needs `SkSVGDOM`, an entirely different renderer with its own picture
  cache; nine-patch needs slice metadata on top of a bitmap. Neither is built.
- **Synchronous decode.** design.md section 5.10.3's thread pool is out.
  `ImageCatalog::decode()` calls `SkCodec::MakeFromData` and `getImage()`
  directly on the caller's thread. Section 9 below states precisely what an
  async version would and would not have to change.
- **PNG is the only format this slice's own tests and examples exercise**,
  though nothing in `ImageCatalog::decode()` refuses the rest of the default
  codec set design.md section 5.10.2 names (BMP/GIF/ICO/JPEG/WBMP) - it hands
  every byte straight to `SkCodec::MakeFromData`, which picks a codec by
  content sniffing, not by a format argument this engine's own API restricts.
  WebP is explicitly NOT enabled (design.md section 5.10.2: it needs a
  feature this project's Skia fetch does not turn on); AVIF/HEIF are out of
  design.md's own target range.
- **Four fit modes**: `fill`, `contain`, `cover`, `none` - `ImageFit` in
  `render_tree.h`. design.md section 5.10.1 never enumerates fit modes for
  `RenderImage` at all (only design.md section 5.9.5's *background_image*
  property table names `fill`/`contain`/`cover`/`tile`/`nine-patch`, and that
  is a **different property this slice does not build** - see section 6). The
  four chosen are exactly CSS's `object-fit` values minus `scale-down`
  (`scale-down` is `none` or `contain`, whichever is smaller - a second-order
  refinement no example here motivates). `tile` is declined by name: repeating
  a bitmap across a box is a real feature, but nothing in `examples/13_image`
  exercises it and it is a `background_image` concern per design.md's own
  table, not a `RenderImage` one.
- **A plain configurable placeholder colour**, not a theme token. design.md
  section 5.10.3 says "绘制主题 token 指定的占位色" (paint the theme-token's
  placeholder colour); no theme system exists in this codebase (design.md
  section 5.7, out of every phase through this one), so `ImageStyle::placeholder`
  is an ordinary `Color` a caller sets directly. The future link is exact:
  when a theme system arrives, its default value for whichever token owns
  this colour becomes the caller's *default*, not a different code path.

### Explicitly declined, by name (do not start these)

| Declined | Why |
| --- | --- |
| Async thread-pool decode | design.md section 5.10.3's own scope; section 9 states what it would touch |
| GPU texture cache with budget + LRU | no GPU backend is wired into this engine at all (CPU raster only, per every slice before this one) |
| DPI variants (`icon@2x`/`@3x`) | no `IWindow::dpi_scale()`-driven asset selection exists; would need a resource-resolution layer this slice does not build |
| SVG via `SkSVGDOM` | a different renderer, design.md section 5.10.1's own "SVG is a source, not a first-class node" argument |
| Nine-patch | needs slice metadata beyond a plain bitmap |
| Animated GIF/WebP via `SkAnimatedImage` | needs a frame clock, the same animation-clock dependency `doc/scrolling.md` and `doc/text-input.md` already declined fling/caret-blink for |
| WebP feature enablement | a Skia build-time feature this project's `FetchSkia.cmake` does not turn on (design.md section 5.10.2) |
| Lottie/skottie | design.md section 5.10.2 itself calls this "未来可选" (future optional), not P0-P8 |
| `background_image` (the Box decoration-layer property, design.md section 5.9.5's table) | a different property from the one this slice builds - see section 6 |

---

## 2. The node-model decision: a field on `NodeStyle`, not a `WidgetKind`, not a new leaf concept

Three shapes were on the table:

1. A field on `NodeStyle`, the way `TextStyle` already sits beside `fill` and
   `border_color`.
2. A new leaf concept distinct from an ordinary `kLeaf` node.
3. A new `WidgetKind` (design.md's MVP-8 lists `Image` as a widget, so this
   was worth taking seriously rather than dismissing).

**Decision: (1) - `ImageStyle image` on `NodeStyle`, read by `carries_image()`.**

The argument is the identical one `doc/clipping.md` section 6 and
`doc/compositing.md` section 5 already made for `overflow` and `opacity`, and
it generalizes cleanly: painting, hit testing and (for image, uniquely)
*layout* all need to agree on ONE rule about whether a node carries image
content. A clip is a property, not a node kind, because a separate kind would
teach each of those readers a private rule about it. The same sentence holds
for an image: `skia_paint.cpp::paint_image()` reads `NodeStyle::image`,
`box_layout.cpp`'s sizing check reads the same field through the same
`carries_image()` predicate, and there is exactly one place either could
disagree with the other - there is only one field to read.

**Why not a new leaf concept?** Nothing about painting an image needs a
different LAYOUT behaviour than an ordinary `kLeaf` already has. A leaf sizes
itself from its own style and constraints and positions no children; an image
node does exactly that, with one more thing to paint inside the box once
sized. Inventing a second leaf concept identical in every respect except one
extra paint field would be the two-vocabulary drift `doc/properties.md`
section 1 already named as the failure mode the property table's own
reconciliation exists to prevent, one layer up.

**Why not a `WidgetKind`?** This is the one design.md's own MVP-8 wording
makes tempting - `Image` is right there in the list next to `Button` and
`TextField`, which ARE `WidgetKind`s. But `doc/completeness.md` section 1
already established that `Box` and `Row`/`Column` are correctly NOT
`WidgetKind`s despite being MVP-8 members, because a `WidgetKind` in this
codebase names *interactive or stateful* behaviour a plain node does not
have on its own - `WidgetSet::hover`/`press`/`group`/`focus`, the runtime
state scroll offset and slider value already needed. An image has none of
that: it is not interactive (no MVP-8 wording asks it to be), it carries no
runtime state beyond what `RenderTree::set_image()` already offers as an
ordinary style setter, and its only added behaviour - the sizing
diagnostic - is a LAYOUT-time check, not a widget-time one. `Image` being
named in the MVP-8 is evidence that it needs to be BUILDABLE, not evidence
that it needs its own enumerator; `Box` already proved the same distinction
the other MVP-8 members did not need to.

**The one respect image differs from `overflow`/`opacity`, and why it does not
change the answer:** image is the only one of the three that touches LAYOUT,
not just paint and hit testing. That is exactly what the sizing rule in
section 3 below exists to contain: the field's presence is read once, at
layout time, to ask "does this node need a decoded size before its own size
is settled" - never "what is the decoded size", which stays paint-time-only
(`ImageCatalog::pixel_size()` is documented as PAINT-TIME ONLY and its only
caller is `skia_paint.cpp`, never `box_layout.cpp`). A field carries that
question exactly as well as a new node kind would, without teaching layout,
paint and hit testing three different vocabularies for the same fact.

### design.md section 5.6 line 622, re-verified for this slice specifically

> "如果实现 `Slider` 需要新增 RenderObject，说明第 3 层的原语集设计有缺陷。"
> - if implementing `Slider` needed a new RenderObject, the layer-3 primitive
> set would be flawed.

`doc/completeness.md` section 2 found this held with **zero new node kinds**
across every widget 4-1 through 4-9 built. This slice is the first one since
that audit to touch the one MVP-8 member the audit itself called out as a
genuine hole, so the question is worth asking pointedly rather than assumed
to still hold by inertia: **did `Image` need a new node/RenderObject kind?**

**No.** `RenderTree` still has no node-kind enum of any sort:
`render_tree.h` declares exactly four `enum class`es -
`TextAlign`, `ImageFit`, `Overflow`, `PaintMode` - and every one of them is a
per-FIELD vocabulary (how a run of text aligns, how a bitmap fits its box,
whether a node clips, how a repaint records itself), never a per-NODE tag the
way `LayoutKind` (`box.h`) or `WidgetKind` (`widget_set.h`) are. `NodeStyle`
remains one homogeneous struct with a fixed field set, now one field larger.
The streak `doc/completeness.md` reported (4-1 through 4-9, zero new node
kinds) extends through 5-1 without exception. This is worth stating
prominently rather than in passing, because `Image` was the one MVP-8 item
with a real chance of breaking it - a decoded bitmap is a qualitatively
different kind of content from a fill, a border or a run of text, and it
still did not need a new kind of node to carry it.

---

## 3. The mandatory sizing constraint (design.md section 5.10.3), and where it lives

> `RenderImage` 必须在内容尚未解码完成时就能确定自身尺寸 —— 通过显式
> `width`/`height`、`aspect_ratio`、或完全由父约束决定（如 `grow`）。三者皆无时
> 输出诊断错误，而非等解码完再布局。

Implemented as one check inside `LayoutTree::Impl::measure()`
(`src/layout/box_layout.cpp`), placed at the exact point that already knows
whether BOTH axes are settled independent of content - after `limits_for()`
has folded `width`/`height`/`aspect_ratio` and the incoming parent constraint
into `limits`, before a single child is measured:

```cpp
if (carries_image(render.style(NodeId{index}).image) &&
    !(limits.tight_width() && limits.tight_height())) {
  report(index, "image has no determinate size before decode; ...");
}
```

**The three legal routes, each with its own test in `tests/unit/test_image.cpp`:**

| Route | How it settles both axes | Test |
| --- | --- | --- |
| 1. Explicit `width`/`height` | Both folded into `limits` directly | `legal route 1: explicit width and height settle the image leaf` |
| 2. `aspect_ratio` | One axis explicit, the other derived inside `limits_for()` before this check runs - no second measurement | `legal route 2: aspect_ratio derives the second axis from a settled one` |
| 3. A parent constraint that arrives tight | `grow` under a row with `cross_align: stretch` makes both axes tight without either being declared locally | `legal route 3: a parent constraint (grow + stretch) settles both axes` |

**The illegal fourth case**: an image-bearing leaf with none of the above,
under an ordinary loose parent. It is reported through `LayoutTree::report()`
- the same `dg::Expected`-adjacent diagnostic channel design.md section 5.4.7
already established for every other constraint conflict in this file (an
unbounded `grow`, a `height: 50%` under an unbounded axis) - collected into
`LayoutTree::diagnostics()`, never an `assert`, never a silent fallback to an
intrinsic size. `has_sizing_diagnostic()` in the test file greps the
collected diagnostic text for the constraint's own explanation string, so the
test checks that a HUMAN-READABLE, LOCATABLE message fired, not merely that
some unspecified thing went wrong. This is the acceptance criterion the task
itself named, and it holds: no `assert`, no silent intrinsic-size fallback,
a real diagnostic through the project's existing mechanism.

Nothing about this check depends on whether a source has actually been
decoded yet - `carries_image()` is true for a placeholder-only style with no
valid `ImageId` at all (`image_bearing_style()` in the test file sets a
source id that "need not resolve in a catalog for a layout-only test",
exactly because the check fires from the FIELD's presence, not from
`ImageCatalog::holds()`). That is deliberate: the rule design.md states is
about content NOT YET DECODED, and the whole point is to never look at
decoded content to answer a layout question in the first place.

---

## 4. THE central claim, measured rather than asserted

The rule in section 3 exists for one stated reason: if size could depend on
decoded content, every finished load would reflow. The property that PROVES
the rule bought something is not "the diagnostic fired" (section 3) but "a
real image swap, of a wildly different decoded size, moves nothing" - and
this project's own convention, from 4-6 (sizing), 4-7 (scrolling) and 4-9
(text input), is to measure that with `LayoutStats`, never to assert it from
the code's shape.

**`tests/unit/test_image.cpp`'s `measure_swap()`**: an 8x8 decoded source at a
50x50 node, then swapped for an independently-decoded 400x400 source through
`RenderTree::set_image()` - the exact call an eventual async decode would use
once its result arrives - followed by `LayoutTree::layout()` (incremental, not
`layout_full()`). Measured:

```
nodes_visited    == 0
nodes_relaid_out == 0
dirty_roots      == 0
bounds unchanged (50x50 before and after)
```

**`examples/13_image --verify-image`, on the actual five-panel demo scene**,
independently repeats the same claim against a DIFFERENT pair of decoded
sizes (64x64 swapped for 512x512, an 8x scale factor rather than the unit
test's 50x) and prints the measured numbers rather than only asserting them:

```
after swapping the source from 64x64 to 512x512: nodes_visited=0 nodes_relaid_out=0 dirty_roots=0
swap-does-not-relayout: PASS
```

Both numbers were captured from an actual run of the built binaries, not
transcribed from the test's own expectations. `LayoutStats::nodes_visited`
being exactly zero is the strongest form of the claim available in this
codebase: it is not merely that the swapped node's OWN bounds are unchanged
(a weaker claim a coincidence could satisfy), it is that `LayoutTree::layout()`
did not even VISIT any node on the frame the swap happened - `set_image()`
never calls `mark_needs_layout()`, only `RenderTree::invalidate()` for
repaint, so there is nothing in `LayoutTree`'s dirty set for `layout()` to
find.

---

## 5. The golden-source-image problem, solved

Every golden scene before this slice is vector fills/borders/text, compared
byte-for-byte against a checked-in PNG baseline at zero tolerance
(`f635028e6e1e92349d23f0575f1e39e7ca8b05e5974492641ee610fe520df12e`, unchanged
since 4-6 and unchanged by this slice - see section 10). An image test needs a
DECODE step with no such precedent, and two options were considered:

1. **A checked-in binary PNG**, sha256-pinned, decoded through the real
   `SkCodec` path.
2. **A synthesized source, generated in-process from a documented pixel
   formula**, encoded through this project's OWN `RasterSurface::encode_png()`
   and then decoded back through the real `SkCodec` path.

**Decision: (2).** A checked-in binary ties the test to whichever libpng
shipped inside this build's prebuilt Skia - the exact "version-specific
decoder output" risk the task named. A synthesized source has no such
dependency: `quadrants()`/`quadrant_png()` (duplicated once in
`examples/13_image/image_scene.cpp` and once in `tests/unit/test_image.cpp`,
by the same formula rather than a shared header, matching the "two
independently-invented formulas would risk drifting" argument the doc for
these files states explicitly) draw four flat-coloured squares through the
ordinary, already-public `dg::Canvas::fill_rect` API, encode them with
`RasterSurface::encode_png()` (the SAME encode path `examples/00_cpu_raster_png`
already established as this project's baseline PNG writer), and
`ImageCatalog::decode()` runs the REAL `SkCodec::MakeFromData` decode over
those bytes - not a shortcut, not a stub, the identical call path a real
PNG file loaded from disk would take.

This is why the technique proves what it needs to: the SOURCE is
reproducible from source code alone (four `fill_rect` calls, no binary
blob, no pinned hash for the source itself), while the DECODE is the real
Skia codec, genuinely exercised. The existing byte-exact golden suite - the
one the task requires to stay untouched - is not modified in any way; this
is an entirely separate oracle, using the same hand-derived-pixel technique
`examples/09_sizing` through `examples/12_text_input`'s `--verify-*` modes
already established, not a second byte-exact baseline. `examples/13_image`'s
own `--verify-image` and `tests/unit/test_image.cpp`'s fit-mode tests both
independently re-derive their own expected pixel coordinates from the fit
arithmetic (documented in `image_scene.h`'s panel-by-panel comment and each
`TEST_CASE`'s own comment) rather than reading them off a previous run - the
same "computed independently of the code under test" discipline
`test_opacity.cpp` and `test_clip.cpp` already use.

---

## 6. Fit modes: what was built, what was declined, and the separate `background_image` question

`ImageFit` implements exactly the four CSS `object-fit` values the task named
as candidates: `fill` (stretch, non-uniform), `contain` (uniform scale-to-fit,
letterboxed), `cover` (uniform scale-to-fill, cropped), `none` (unscaled,
centred). Each has a dedicated hand-derived test in
`tests/unit/test_image.cpp` AND an independent panel in `examples/13_image`'s
five-panel scene, checked by `image_check.cpp` against the SAME formula
computed a second time rather than read back from a first run.

**`tile` is declined by name.** It is a real CSS `object-fit`-adjacent
concept (actually CSS has no `tile` value for `object-fit` at all - `tile` is
this project's own name for design.md section 5.9.5's `background_image`
row), and design.md's own table names it for `background_image`, not for
`RenderImage`. Nothing in this slice's examples exercises repeating a bitmap
across a box, and building it without a test would be exactly the "declining
by not doing rather than by naming" the task explicitly asked this slice to
avoid.

**`background_image` itself is a separate, still-absent property.** design.md
section 5.9.6 lists it in the same visual property group as
`background_gradient`, and section 5.9.5 states "只有 `RenderBox` 有背景 ...
`Text`/`Image` 等叶子节点无背景属性" (only `RenderBox` has a background; leaf
nodes like `Text`/`Image` do not - if one is needed, wrap it in a `Box`). This
slice's `image_source`/`image_fit`/`image_placeholder_color` are `Image` AS
CONTENT - the same shape `TextStyle` already occupies - not `Box`'s
background layer. `background_image` was never added to `props/drawgui.props.toml`
by any prior slice either (`grep -c background_image props/drawgui.props.toml`
finds zero, though design.md names it in prose); it remains absent after this
slice too, and is not one of this slice's three not-yet properties (section 7)
- it is a different property this slice does not touch at all, recorded here
so a future reader does not conflate the two.

---

## 7. The property table: `image_source`, `image_fit`, `image_placeholder_color`

Three properties appended, ids 47-49 (`props/drawgui.props.toml`,
`props/prop_ids.lock`):

| id | property | type | status |
| --- | --- | --- | --- |
| 47 | `image_source` | `image` (complex) | **not yet** - see below |
| 48 | `image_fit` | `enum` (`fill`/`contain`/`cover`/`none`) | implemented |
| 49 | `image_placeholder_color` | `color` | implemented |

`image_fit` and `image_placeholder_color` are plain scalar values and ride
the ordinary `dg::set_prop()` path exactly like any enum or colour property
already does - `apply_image_fit`/`apply_image_placeholder_color` in
`src/props/node_props.cpp`.

**`image_source` cannot ride the scalar tagged union**, for the identical
reason `background_gradient`/`shadow`/`transform` cannot: it names a decoded
`ImageCatalog` entry, not a number/colour/enum ordinal. design.md section
5.9.5 already specifies its dedicated-setter shape:

```c
int dg_node_set_image(dg_node_t*, uint16_t prop_id, const dg_image_desc*);
```

`props/drawgui.props.toml`'s `type = "image"` marks it as the fourth complex
type alongside `gradient`/`shadow`/`transform` (`tools/gen_props.py`'s
`COMPLEX_TYPES` tuple grew one entry), so `apply_image_source()` correctly
reports `kUnsupported` through the same boundary path the other three complex
properties already use - not a `kUnknownId` (the id is real and recognised)
and not a silent no-op.

**Decision: the dedicated-setter CHANNEL (the actual `dg_node_set_image` C
ABI entry point) is NOT built in this slice, and is left to 5-4.** The
reasoning: unlike `background_gradient`/`shadow`/`transform`, `image` already
has a REAL, WORKING C++ entry point a caller can use TODAY -
`RenderTree::set_image(NodeId, const ImageStyle&)` - because this slice needed
one anyway to prove the swap-does-not-relayout property in section 4. What is
missing is only the id-based channel a HOST LANGUAGE across the C ABI would
need, which is exactly the gap 5-4 is scheduled to close for the other three
complex types. This makes `image` a candidate for either role in 5-4: it
could be that channel's FOURTH CLIENT (built after gradient/shadow/transform
establish the shape, following their pattern), or its FIRST PROTOTYPE (built
first, since it is the only one of the four with a working internal
implementation already in place to wire the channel onto - gradient and
shadow both still need painting work `doc/compositing.md` section 6 names
before there is anything for a setter to configure). That choice belongs to
5-4, not to this slice; it is named here so 5-4's own scoping does not have
to rediscover it.

**Counts, exactly**: 49 properties (was 46) - **35 implemented** (was 33, +2:
`image_fit`, `image_placeholder_color`), **10 partially implemented**
(unchanged), **4 not yet** (was 3, +1: `image_source`).
`doc/properties.md` section 4 is updated to match; `props.no_drift`,
`props.abi_lock` and `props.lock_selftest` all still pass (section 10).

---

## 8. The placeholder path

`ImageStyle::placeholder` paints "whenever `source` is invalid AND this
colour is not fully transparent" (`carries_image()`), matching design.md
section 5.10.3's "未就绪时绘制主题 token 指定的占位色，不留空洞" (paint the
placeholder colour while not ready, never a hole) with a plain `Color`
standing in for the theme-token system this project does not have. Painted
in `paint_image()` (`skia_paint.cpp`) through the SAME `apply_clip()` call the
real decoded path uses, so a placeholder respects rounded corners identically
to a real image - one clip routine, not two. A fully transparent placeholder
(the default `Color{}`) is how an ordinary node with no image at all stays
inert: `carries_image()` is false for it, so every scene built before this
slice - every one of the previous twelve examples - is byte-for-byte
unaffected by `NodeStyle` growing this field.

Two tests pin this directly: `placeholder paints while source is invalid,
never a hole` and `a real image replaces the placeholder entirely, once
decoded` - the second checks that a REAL decoded pixel (not the placeholder
colour) is what a real source paints, so the two branches in `paint_image()`
cannot both silently draw the same thing.

---

## 9. What a future async decode would and would not have to change

design.md section 5.10.3's thread pool is explicitly out of this slice, but
every call site here is already shaped for it, and that is the concrete
payoff of the mandatory sizing rule in section 3:

**Would NOT have to change:**
- `NodeStyle::image` / `ImageStyle` - the field a node carries is already
  "which `ImageId` to paint", independent of when that id became valid.
- `RenderTree::set_image()` - already the exact call a worker thread's result
  would arrive through; it already damages-but-does-not-relayout (section 4),
  which is the property async decode needs most, since a background thread
  finishing at an arbitrary frame must not perturb layout geometry.
- The sizing rule itself - it is what BUYS the "no relayout on load" property
  an async path needs; nothing about making decode asynchronous would loosen
  it, since the rule's premise (decode may not have happened yet) is already
  the async case's normal condition, made synchronous only in when it happens.
- `ImageCatalog`'s public shape - `decode()` already returns a value the
  caller could equally receive from a worker thread's completion callback;
  `ImageId` is already a plain, copyable handle a cross-thread handoff can
  carry.

**Would have to change:**
- `ImageCatalog::decode()` itself would move its body onto a worker thread,
  synchronized with the main thread's next `RenderTree::set_image()` call -
  today it runs synchronously on the caller's own thread.
- A pending-vs-decoded state distinct from "no image at all" would need to
  exist somewhere so a caller can tell "still decoding" from "no source was
  ever requested" - today `ImageId{}` (invalid) means both, which is fine
  synchronously (there is no in-between moment) but would need to be split
  for a request that is in flight.
- The placeholder's lifetime would extend from "until this frame's
  synchronous decode call returns" (effectively instantaneous today) to
  "until the worker thread's result arrives, frames or seconds later" - the
  same code path, exercised for much longer.
- A thread-safety story for `ImageCatalog::Impl::images` (currently a plain
  `std::vector` under `shared_ptr`, mutated only synchronously) - append-only
  from multiple threads would need at minimum a mutex around the push_back,
  which this slice's synchronous, single-threaded caller does not need.

---

## 10. Verification performed

Run once each, per the task's own reduced-verification instruction:

- `ctest --test-dir build --output-on-failure`: **18/18 passed** (the
  existing 17 plus the new `image.verify_demo_scene`).
- `./build/examples/drawgui_render_png` → sha256
  `f635028e6e1e92349d23f0575f1e39e7ca8b05e5974492641ee610fe520df12e` -
  **unchanged** since 4-6, confirmed by an actual run of the rebuilt binary,
  not assumed from an earlier slice's record.
- `g++` 15.2.0 and `clang++` 21.1.8, Debug, `-DDRAWGUI_WERROR=ON`, fresh build
  directories each: both clean, both 18/18 CTest.
- `-DDG_SANITIZE=ON` (clang++, Debug, `-DDRAWGUI_WERROR=ON`): clean build,
  18/18 CTest, including `widgets.interaction_equals_full` at 80.70s and
  `clipping.verify_demo_scene` at 39.22s under instrumentation - decoding
  attacker-shaped bytes (`image catalog decode fails on bytes no codec
  accepts`, garbage-byte input) is exactly ASan/UBSan's domain and it is
  clean.
- `clang-tidy -p build` against every `.cpp` this slice touched or added -
  `src/layout/box_layout.cpp`, `src/props/node_props.cpp`,
  `src/render/image_catalog.cpp`, `src/render/render_tree.cpp`,
  `src/render/skia_paint.cpp`, `src/render/tree_paint.cpp`,
  `tests/unit/test_image.cpp`, and all four `examples/13_image/*.cpp` files -
  matching `.github/workflows/ci.yml`'s own hand-maintained list convention of
  naming translation units rather than headers: **zero diagnostics**, zero
  `NOLINT`. The CI list itself gained `src/render/image_catalog.cpp`,
  `tests/unit/test_image.cpp` and the four `examples/13_image` files.
- `clang-format --style=file --dry-run --Werror` against every non-generated
  `.cpp`/`.h` this slice touched or added (all fourteen files under
  `src/`/`include/`/`tests/unit/`, plus the seven `examples/13_image/*`
  `.cpp`/`.h` files): clean.

---

## 11. Defect injection: 6 injections, 4 caught, 2 provably inert (both explained, not merely noted)

Six defects were injected one at a time into this slice's OWN new engine
logic only (decode path, sizing rule, fit-mode math, placeholder path, plus
two more the campaign's own scope naturally reached), each built and run
through the full `ctest --test-dir build`, each restored from a pre-injection
backup copy of the file (not `git checkout`, since every file this slice
touches is still uncommitted at injection time and `git checkout` would have
reverted to the PRE-SLICE version, destroying the feature rather than the
defect - itself an instance of the "git-checkout-erases-new-test" failure
mode this project's own accumulated findings warn about, avoided here by
construction rather than discovered by falling into it).

| # | Injection | File | Result |
| --- | --- | --- | --- |
| 1 | Removed the `SkCodecs::Register(SkPngDecoder::Decoder())` call's effect (commented out inside the `std::call_once` lambda) | `image_catalog.cpp` | **NOT caught** - all 18/18 still passed. Root-caused below. |
| 2 | Disabled the section-3 sizing diagnostic (`if (false && carries_image(...) ...)`) | `box_layout.cpp` | **Caught** - `unit` failed (`illegal fourth case` test) |
| 3 | Swapped `min`/`max` between `contain` and `cover` in the fit-mode scale arithmetic | `skia_paint.cpp` | **Caught** - both `unit` (four fit-mode tests) and `image.verify_demo_scene` failed |
| 4 | Inverted the placeholder-alpha guard (`== 0` to `!= 0`) | `skia_paint.cpp` | **Caught** - both `unit` and `image.verify_demo_scene` failed |
| 5 | Flipped `carries_image()`'s `\|\|` to `&&` | `render_tree.h` | **Caught**, broadly - ten distinct pixel mismatches across every fit-mode panel plus the placeholder, in both `unit` and `image.verify_demo_scene` |
| 6 | Removed `RenderTree::set_image()`'s no-op dedupe (`if (style.image == image) return;`) | `render_tree.cpp` | **NOT caught** - all 18/18 still passed. Root-caused below. |

**Injection 1, root-caused rather than left as "no failure":** removing the
explicit registration call did not break decode in ANY binary, including
`examples/drawgui_image` run standalone (which links no other translation
unit that calls `SkCodecs::Register` at all - `resources.cpp` and
`golden_image.cpp`, the only other two call sites in this codebase, are not
linked into it). Reading `SkCodec.h`'s own comment on `SkCodecs::Register` -
"Add the decoder to the end of a linked list of decoders" - and design.md
section 5.10.2's own phrase, "预编译 Skia 的默认 codec 集合" (the prebuilt
Skia's DEFAULT codec set), together explain it: this prebuilt archive already
self-registers PNG (and presumably the rest of the default set) at static-init
time inside `libskia.a` itself, independent of any application-level
`SkCodecs::Register` call. The explicit call this slice added - mirroring the
identical pattern already established by `resources.cpp` (step 2) and
`golden_image.cpp` (an earlier slice) - is measured to be defensive rather
than load-bearing against THIS prebuilt. It is kept anyway, for parity with
those two existing call sites and as a guard against a future minimal/custom
Skia build that does not pre-register defaults; the finding is recorded here
so a future reader does not have to re-discover it by their own injection.

**Injection 6, root-caused rather than left as "no failure":** the dedupe
guard is a pure repaint-damage optimisation - it skips `invalidate()` when an
identical `ImageStyle` is reassigned. No test in this slice's suite asserts
`RepaintStats`/`DamageRegion` contents for a no-op image reassignment
specifically (section 4's swap test asserts `LayoutStats`, which this guard
does not touch either way - `set_image()` never calls `mark_needs_layout()`
regardless of the dedupe). This is the "provably inert code" failure mode
this project's own accumulated findings name, confirmed here for a genuinely
different reason than injection 1: not because the underlying capability is
redundant against this build, but because the specific optimisation this
guard performs has no assertion in this slice's test surface that would
notice its absence. The guard is correct and kept - `set_text()` immediately
above it in the same file carries the identical shape for the identical
reason (`doc/text-input.md`'s own note on why re-asserting unchanged text
should not repaint) - but its own removal, in isolation, is inert against
THIS suite specifically, which is worth stating rather than silently
resolving as "nothing to see here."

No injection reproduced a "stale build/mtime" or "wrong-copy" failure mode -
both were avoided structurally: each injected file was rebuilt with `ninja`
via `cmake --build build` immediately before testing (so a stale `.o` was
never possible), and each restore used a byte-for-byte `cp` from a
pre-injection backup (verified with `diff` after every restore) rather than a
hand-edited reversal that could silently differ from the original.

---

## 12. What this does not do

Everything section 1 already named, plus:

- No GPU texture upload of any kind - decoded bitmaps stay CPU-side
  `sk_sp<SkImage>`, matching every other Skia object this engine already
  keeps CPU-resident.
- No per-image memory budget or eviction. `ImageCatalog` is append-only for
  the lifetime of the `RenderTree` that holds it, the same shape
  `FontCatalog` already has.
- No `dg_node_set_image` C ABI entry point - `RenderTree::set_image()` is the
  real C++ call; the id-based channel is 5-4's to build (section 7).
- No `background_image` property (section 6) - a Box's decoration-layer
  image is a different, still-entirely-absent property from the one this
  slice builds.
- No multi-frame/animated bitmap of any kind.

---

## 13. Property status after this slice

49 properties: **35 implemented**, **10 partially implemented**, **4 not
yet**. 7 `WidgetKind` values, unchanged. 6 `LayoutKind` values, unchanged.
Zero new node/RenderObject kinds - the streak `doc/completeness.md` reported
across 4-1 through 4-9 extends through this slice (section 2). 18 CTest
entries (was 17, +1: `image.verify_demo_scene`). 14 examples (was 13, +1:
`examples/13_image`). `virtual` occurrences in `src/`+`include/`: still 3, all
in comments (this slice added none). SDL references in `include/`: still all
in comments, in the one file that already had them.
