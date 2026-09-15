# Slice 4-10: completeness audit — the evidence for (and against) "basic GUI components are complete"

This slice builds nothing new by default. Its only deliverable is a verdict, and
the verdict has to survive the question a reader is entitled to ask of every
other doc in this directory: *how do you know, and what does the code actually
say, not what does a previous slice's prose say it says.* Every claim below is
backed by a command run against this repository or a file read in it, dated to
this slice's own audit pass, not inherited from another doc's word.

## 1. Widget inventory against design.md's MVP-8

design.md section 5.6, line 615: `Box` `Text` `Button` `TextField` `ScrollView`
`List` `Image` `Row/Column`. Line 617: `List`/`Table` virtualization is **part of
the widget's definition**, not an optional optimization — quoted in full because
this table's `List` row rests on it. Line 622: the acceptance bar the whole set
exists to test (`Slider`-as-litmus for the layer-3 primitive set) — audited
separately in section 2.

| MVP-8 item | Present? | What it actually is | Evidence |
| --- | --- | --- | --- |
| `Box` | Yes, as **composition**, not a `WidgetKind` | `LayoutKind::kLeaf` + `BoxStyle` (padding/border/margin/background/radii) is the general decorated box every node already has; `WidgetKind::kPanel` is a `Box` that also "holds children and takes part in layout... not interactive" | `include/drawgui/layout/box.h` (`BoxStyle`), `include/drawgui/widget/widget_set.h:50-53` (`kPanel`), `examples/04_layout`, `examples/05_widgets` |
| `Text` | Yes, as `WidgetKind::kLabel` | "Static text. Not interactive" | `widget_set.h:55-57`, `render_tree.h`'s `TextStyle`, `examples/05_widgets` |
| `Button` | Yes, as `WidgetKind::kButton` | third widget kind, pure function of pointer state | `widget_set.h:59`, `examples/05_widgets` |
| `TextField` | Yes, as `WidgetKind::kTextField` (7th kind) | single-line, ASCII-scoped; clip leaf + 3 positioned children | `widget_set.h:86-96`, `examples/12_text_input`, `doc/text-input.md` |
| `ScrollView` | Yes, as `WidgetKind::kScrollView` (5th kind) | clipping node + unbounded-axis child + runtime scroll offset, three existing mechanisms composed | `widget_set.h:68-73`, `examples/10_scrolling`, `doc/scrolling.md` |
| `Row`/`Column` | Yes, as **`LayoutKind`**, not a `WidgetKind` | `kRow`/`kColumn` (plus `kWrapRow`/`kWrapColumn`), direction folded into the enumerator | `include/drawgui/layout/box.h:180-195`, every example from `04_layout` onward |
| `List` | **Qualified partial** | Functionally present as *composition* — a `kColumn`/`kRow` of ordinary children inside a `kScrollView` leaf is exactly what `examples/10_scrolling`'s 24-chip vertical strip is. **Not present** as design.md's own `List` control: line 617 defines virtualization as part of the control's DEFINITION, and no object pool / recycling / visible-range-only node creation exists anywhere in this codebase — every chip in the scrolling demo is a real, permanently-allocated node whether on-screen or not. **Closed by slice 5-3** — `WidgetKind::kList`, an 8th kind, is a fixed pool of recycled nodes (never one node per item), covering 1000 items with 14 real nodes. Zero new node/RenderObject kinds were needed, extending this document's own streak through the second of the two MVP-8 items it had found short. See `doc/list.md` for the full record; this row is a historical snapshot of what section 1 measured at the time of this audit and is left unchanged below. | `doc/scrolling.md` section 1 ("List virtualization: checked against the phase table, and declined by that check"), `examples/10_scrolling` (24 real nodes, none recycled) |
| `Image` | **Absent** | Zero image-drawing capability anywhere in the engine. `NodeStyle` (render_tree.h:144-215) has fields for fill, radii, border, overflow, opacity and one text run — no image/bitmap/`SkImage` field, no fit-mode, nothing. `grep -rn "SkImage\|kImage\|ImageStyle" include/ src/` finds exactly one match, in `src/graphics/raster_surface.cpp`, and that is the PNG-encode path for the *offscreen framebuffer itself* (`SkImageInfo::MakeN32Premul`, `SkImage::encodeToData`) — nothing to do with drawing a decoded image as node content. `examples/02_skia_cpu_gallery` does decode and draw an image, but directly against Skia as a demo of what Skia can do; it does not go through `RenderTree`/`NodeStyle` at all, so it is not evidence for the engine. **Closed by slice 5-1** — `ImageStyle` is now a field on `NodeStyle` (the same shape `TextStyle` already occupies, argued against this document's own section 6/7 precedent for why `overflow`/`opacity` are fields rather than node kinds), decoded through the real `SkCodec` path, sized before decode per design.md section 5.10.3's mandatory rule. Zero new node/RenderObject kinds were needed, extending this document's section 2 finding through the one MVP-8 item it had found absent. See `doc/image.md` for the full record; this row is a historical snapshot of what section 1 measured at the time of this audit and is left unchanged below. | `include/drawgui/render/render_tree.h:144-215` (full `NodeStyle` field list, no image field), `grep` above, `examples/02_skia_cpu_gallery/*.cpp` (Skia-direct, not engine) |


**Headline finding**: of the MVP-8, six are unambiguously done (`Box`, `Text`,
`Button`, `TextField`, `ScrollView`, `Row`/`Column`), one is done as mechanism
but not as the control design.md defines (`List` — the ScrollView+Column
substrate exists, virtualization does not), and one is **completely absent**
(`Image` — not a stub, not a partial path, not a property that reports
`kUnsupported`; there is no representation for an image anywhere in the node
model).

## 2. design.md's own acceptance criterion (line 622), re-verified across every widget this phase built

> "如果实现 `Slider` 需要新增 RenderObject，说明第 3 层的原语集设计有缺陷。MVP
> 阶段的八个控件就是用来验证这条的。" — if implementing `Slider` needs a new
> RenderObject, the layer-3 primitive set is flawed; the eight MVP widgets exist
> to test exactly this.

4-8 already checked this for `Slider` by name and reported it passed. This audit
re-derives the same fact independently and extends it to every widget landed in
4-1..4-9, because the acceptance bar is about the *primitive set*, not about one
control:

| Widget | New `WidgetKind`? | New node/RenderObject kind? | What it reused |
| --- | --- | --- | --- |
| Checkbox | 4th (`kCheckbox`, pre-existing since 3-3) | No | `Widget::fill_normal/hover/pressed`, `NodeStyle::fill` |
| Radio | **No new kind** — `Widget::group : std::optional<int>` on `kCheckbox` | No | the checkbox's own toggle path, branched |
| Slider | 6th (`kSlider`) | No | `RenderTree::set_local_origin` (already existed for ordinary node moves) to position the thumb |
| ScrollView | 5th (`kScrollView`) | No | `NodeStyle::overflow` (clip, from 4-4), `BoxStyle::scroll_axis` (unbounded constraint, new to 4-7 but a *field*, not a kind), `RenderTree::set_scroll_offset` (a method, not a new node concept) |
| TextField | 7th (`kTextField`) | No | the same clip-leaf shape `ScrollView` uses, plus plain positioned children (`content`/`caret`/`selection_highlight`) shaped exactly like `kCheckbox`'s `indicator` and `kSlider`'s `thumb` |

Independent verification performed for this audit, not inherited: `RenderTree`
(`include/drawgui/render/render_tree.h`) has **no node-kind enum at all** — a
node is one homogeneous struct (`NodeStyle`) with a fixed field set; there is
no tagged union, no `NodeKind`, nothing a new widget could have needed to
extend. Every one of the five widgets built in 4-6..4-9 (checkbox reused,
radio, slider, scrollview, textfield) is implemented purely as (a) a
`WidgetKind` enumerator in the side-table `WidgetSet`, and (b) composition of
fields and methods `RenderTree`/`LayoutTree` already exposed before this phase
began (`overflow`, `scroll_axis`, `set_local_origin`, `set_scroll_offset`,
`set_fill`, `set_text`). Zero new node kinds were added across the entire
phase (4-1 through 4-9).

**Verdict on the acceptance criterion: TRUE, and more strongly than 4-8's own
check claimed.** It was not merely satisfied for `Slider`; the primitive set
proved sufficient for every widget this phase built, checkbox through
TextField. This is real evidence for §5.6's premise: the ~13-primitive
`RenderObject` set design.md's own inventory names (§5.4, line ~316:
`RenderBox`/`RenderFlex`/`RenderStack`/`RenderWrap`/`RenderText`/`RenderImage`/
`RenderPath`/`RenderViewport`/`RenderClip`/`RenderTransform`/`RenderOpacity`/
`RenderCustomPaint`/`RenderView`) is oversized relative to what this codebase
actually built: this engine has no `RenderClip`, no `RenderOpacity` and no
`RenderImage` as distinct *kinds* at all — `overflow` and `opacity` are fields
on the one node struct (doc/clipping.md section 6, doc/compositing.md section
5, both argued this by name at the time), and `RenderImage` has no
representation whatsoever (section 1 above). The acceptance criterion passed
by never needing a new *kind* — but one whole line item of the original
~13-primitive inventory (`RenderImage`) also never arrived in any form, which
is a different and more serious gap than "we didn't need a new primitive
category"; see section 7.

## 3. Property table (46 properties): re-verified, not re-derived

`grep -c '^\[\[property\]\]' props/drawgui.props.toml` → **46**. `doc/properties.md`
section 4 (read in full for this audit) states 33 implemented / 10 partial / 3
not-yet, with the definitive per-id table already current as of the 4-7
scrolling slice (verified via `git log -1 -- doc/properties.md` → last touched
2026-09-09, the scrolling slice, after which no further property changed).
This audit re-read that table against the property ids actually referenced in
`include/drawgui/layout/box.h` and `include/drawgui/render/render_tree.h` and
found no discrepancy: the 3 not-yet (`background_gradient` id 17, `shadow` id
28, `transform` id 30) have no corresponding field or partial code path
anywhere in either header, matching the claim that nothing in either column is
silently more or less implemented than stated.

**The dedicated-setter gap, independently checked against design.md section
5.9.5** (quoted in full for this audit):

```c
int dg_node_set_gradient(dg_node_t*, uint16_t prop_id, const dg_gradient_desc*);
int dg_node_set_shadow  (dg_node_t*, uint16_t prop_id, const dg_shadow_desc*);
int dg_node_set_image   (dg_node_t*, uint16_t prop_id, const dg_image_desc*);
```

Section 5.9.5 names three dedicated setters — gradient, shadow, **image** — not
four. `transform` is not among them in this section; `properties.md`'s gap
report asserts `transform` also needs "a dedicated setter" by analogy, which is
a reasonable engineering inference (a 2D matrix is no more a scalar than a
gradient descriptor is) but is not literally what §5.9.5 states. Recorded here
as a small textual gap in design.md itself, not in this codebase: the section
that is supposed to enumerate every complex-typed property's ABI shape is
missing `dg_node_set_transform`. Note also that `dg_node_set_image` already
exists in this same list — design.md's own ABI plan already assumes `Image` as
a real, settable node property, which sharpens section 1's finding: `Image`'s
absence is not a case where design.md never asked for it, but one where
design.md already specified its C ABI shape and nothing downstream of that
plan was ever built.

Property counts, unchanged by this slice: **46 total, 33 implemented, 10
partial, 3 not-yet.** `doc/properties.md` needs no edit; it is already current.

**Update (slice 5-4, phase 5): the dedicated-setter gap this section found is
closed for three of its four properties.** `dg::set_gradient()`,
`dg::set_shadow()` and `dg::set_image()` now exist, sharing one id-validation
prelude; `background_gradient` (17), `shadow` (28) and `image_source` (47,
appended by slice 5-1 after this section's audit pass) moved from not-yet to
implemented. `transform` (30) remains not-yet - its setter (`dg::set_transform()`)
exists and validates its id through the same channel, but always reports
`kUnsupported`, with the specific blocker named. Current counts (49 properties,
the three appended by 5-1 already counted in this section's own 46):
**38 implemented / 10 partial / 1 not-yet.** See `doc/complex-properties.md`
for the full record, including the damage-atomicity argument for `shadow`
painting outside its own declared bounds and the design.md section 5.9.5
gap analysis section 6 below cross-references; this paragraph is left
appended rather than rewriting the audit above it.

## 4. Invariants, re-checked once each

| Invariant | Check run | Result |
| --- | --- | --- |
| Exactly-once layout | Read `layout()`/`size_flex_children()` call sites; re-read 4-6's own claim (`nodes_visited == nodes_relaid_out == nodes_total`, asserted by the sizing demo on itself) and 4-7/4-8/4-9's independent no-relayout claims (scroll offset, slider drag, text edit under a fixed-width field) | **Holds**, and holds as a *conjunction*: nothing in 4-7/4-8/4-9 reintroduces a second layout pass for anything the earlier slices already made a repaint-only path. The one honestly-scoped exception (`TextField` is fixed-width; a future shrink-to-fit variant would need real relayout) is named by 4-9 and unchanged by this audit. |
| Damage correctness | Re-read `RenderTree::repaint()`/`repaint_full()` and the clip/opacity interaction rules in `render_tree.h` (`clip_bounds`, `hit_test`) | **Holds.** Clip and opacity compose without contradicting each other's damage rule (clip shrinks the atomic repaint unit for rounded clips; opacity's scalar-alpha layer is proven *not* atomic) — both are pinned by tests already in CTest, re-run in section "verification" below rather than re-derived. |
| Zero `virtual` | `grep -rn '\bvirtual\b' src/ include/` | **3 matches, all three inside comments** explaining why there is no `virtual` (`widget_set.h:47`, `render_tree.h:11`, `box.h:162`). Zero actual uses of the keyword in code. Invariant holds literally. |
| Zero SDL leakage into `include/` | `grep -rn 'SDL' include/` | Matches only in `include/drawgui/window/window_manager.h`, and every one of them is inside a `//` comment (verified: no `#include <SDL...>`, no `SDL_*` type used as a declared type or value in that header). Invariant holds literally. |
| Golden byte-stability | `./build/examples/drawgui_render_png` then `sha256sum` | `f635028e6e1e92349d23f0575f1e39e7ca8b05e5974492641ee610fe520df12e` — **matches** the value this task was given as ground truth, and matches the value plan.md has quoted (abbreviated `f635028e...`) since 4-6. |
| ABI lock | `ctest --test-dir build` | `props.abi_lock` and `props.lock_selftest` both **Passed** in the full 17/17 run (section "verification"). |

No invariant regressed. This is a re-check, not a re-derivation: each of these
was already claimed by an earlier slice; this section exists so the phase's
closing document states, once, that all six still hold with all nine slices'
work coexisting, rather than trusting nine separate earlier claims to still be
true in combination.

## 5. Every explicit decline, consolidated

One table,每条给出：declined item、declining slice/doc、stated reason、the
design.md phase it belongs to (if named). Rows are grouped by area only for
readability; nothing here is re-ordered by importance.

### Layout

| Declined | Slice/doc | Reason | Phase |
| --- | --- | --- | --- |
| Intrinsic sizing (`intrinsic_width/height`) | layout.md | speculative layout, breaks exactly-once, O(n²) worst case | whichever slice needs a child's size before allocating |
| Stack sizing from non-positioned children | layout.md | every child here is positioned; two-step sizing arrives with the first non-positioned child | first non-positioned-child feature |
| `align: stretch` inside a wrap | layout.md / wrapping.md §3.1 | §5.4.3 (tight stretch) and §5.4.4 (exactly-once wrap) cannot both hold | unresolved even after piece 4 (sizing.md confirms) |
| Two of three constraint-conflict diagnostics | layout.md | no unbounded constraint constructible pre-scrolling | closed by 4-7 (scrolling) |
| `grow` under a wrap | wrapping.md / properties.md §4.2 id 38 | design.md 5.4.4 itself calls the CSS combination confusing | none named (matches design.md's own decline) |
| `baseline` alignment (`align`, `align_self`) | wrapping.md / properties.md §4.2 ids 33, 41 | needs a measured baseline before placement | predicted for piece 4; sizing.md confirms it did NOT arrive |
| `space_around`/`space_evenly` justify | properties.md §4.2 id 32 | arithmetic exists via align_content; missing decision to extend `MainAlign` | unnamed |
| `row_reverse`/`column_reverse` | properties.md §4.2 id 31 | reversing remainder-absorption breaks byte-identity gate | unnamed |
| Negative margins | properties.md §3.6 | `deflate` clamps at zero | unnamed |
| Percentage lengths | properties.md §3.2 | no representation added before a consumer exists | unnamed |
| Fractional `grow` weights | properties.md §3.4 / §4.2 id 38 | exact integer prefix-sum needed for byte-identity | unnamed |
| Shrinking from a measured (not declared) base | sizing.md §1.3-1.6 / properties.md §4.2 id 39 | costs `2^(n/2)` without an unbounded-axis intrinsic pass | needs its own slice (costed in sizing.md §1.4) |
| CSS freeze-and-redistribute shrink loop | sizing.md §1.8 item 2 | needs its own semantics tests | unnamed |

### Clipping / compositing / properties

| Declined | Slice/doc | Reason | Phase |
| --- | --- | --- | --- |
| `background_gradient` (id 17) | properties.md §4.3 | needs `SkShader` + dedicated setter (5.9.5) | unnamed |
| `shadow` (id 28) | properties.md §4.3 / compositing.md §6 | layer must become damage-atomic + paint outside declared bounds + dedicated setter | ordered: setter → outset → atomicity → filter |
| `transform` (id 30) | properties.md §4.3 / compositing.md §6 | non-axis-aligned geometry, damage-atomicity, inverse-transform hit test, dedicated setter | unnamed |
| Single-leaf alpha-into-`SkPaint` optimization (§5.11.2 tier 3) | compositing.md §5 | precondition narrower than it looks; risks silently degrading group opacity to per-object alpha | when a measurement justifies it |
| `hit_test_behavior` property | compositing.md §4 | not yet in the props table | unnamed future append |
| Repaint-boundary promotion during opacity animation (§5.4.8) | compositing.md §5 | scalar-alpha layer already isn't damage-atomic, argued unnecessary rather than merely deferred | n/a |
| A compositing layer caching a clipped subtree | clipping.md §3 | "slice 4-5's business" — and 4-5 didn't build it either | unassigned, still open |

### Scrolling

| Declined | Slice/doc | Reason | Phase |
| --- | --- | --- | --- |
| Fling/inertia | scrolling.md §1 | needs an animation clock (design.md 5.16.1) that does not exist | P4.5 |
| Overscroll rebound animation | scrolling.md §1 | same animation-clock dependency; the hard-stop clamp half IS built | P4.5 |
| Nested scroll delegation | scrolling.md §1 | no real nested-scroll scene exists to drive the policy | whenever such a scene exists |
| Scroll anchoring | scrolling.md §1 | node vectors are append-only; nothing can insert above a sibling to anchor against | whichever slice adds insertion |
| Keyboard scrolling | scrolling.md §1 | needs the intent-binding mechanism (§5.5.1) which does not exist | the intent-binding slice |
| Scrollbar as a drawn/hit-tested control | scrolling.md §1 | none exists at all | unnamed |
| **List virtualization** | scrolling.md §1 | design.md's own phase table (line 1714) puts it at **P3** as part of `List`'s definition, not this viewport's | **P3**, a `List`-shaped slice (see section 1 and 7) |

### Form controls

| Declined | Slice/doc | Reason | Phase |
| --- | --- | --- | --- |
| **Dropdown / `PopupHost`** | form-controls.md §2 | window layer has zero popup-window concept; design.md calls `PopupHost` "the single most critical decision in the whole design" | P4 (see section 7 for the severity escalation this audit adds) |
| Vertical slider orientation | form-controls.md §8 | nothing in the scene needs it | unnamed |
| Hover/press feedback on the slider thumb | form-controls.md §8 | needs a second appearance-holder that doesn't receive clicks | unnamed |

### Text input / fonts

| Declined | Slice/doc | Reason | Phase |
| --- | --- | --- | --- |
| Non-ASCII input / grapheme clustering | text-input.md §1.2 | needs SkUnicode/ICU grapheme API, "a real slice of its own" | unnamed |
| IME composition preview | text-input.md §1.3 | design.md names this P7 explicitly; the interface hook itself is real, only the preview is deferred | P7 |
| Multi-line reflow / `SkParagraph` | text-input.md §1.4 | full slice's worth of line-breaking/run management; MVP scopes TextField to single-line | a text-layer slice building `Paragraph` over `SkParagraph` |
| Shrink-to-fit TextField | text-input.md §4, §9 | breaks the no-relayout guarantee named as this slice's precondition | unnamed |
| Caret blink | text-input.md §9 | no animation clock, identical reasoning to declined fling | same prerequisite as fling |
| Tab order / focus tree | text-input.md §3 | design.md's per-window `FocusManager` with tab order is P4 scope | P4 |
| HarfBuzz shaping, ICU BiDi, UAX#14 line-breaking | font-fallback.md | needs `textlayout` (HarfBuzz+ICU) and the `icudtl.dat` distribution decision design.md 5.10.5 defers | design.md 5.10.5's own deferred decision, due "before P3's text widgets land" per design.md line ~1770 |
| fontconfig (`SkFontMgr_New_FontConfig`) | font-fallback.md | per-language answers come from `/etc/fonts`, breaking determinism/golden tests | forced only by non-file-based platform font systems, thousands-of-fonts scale, or runtime font install |
| Locale inheritance (node→scope→app-default) | font-fallback.md | design.md 5.13.5 wants 3 levels; only node-property level exists | the slice that introduces scopes |

### Widgets / interaction (pre-existing, re-affirmed unchanged this phase)

| Declined | Slice/doc | Reason | Phase |
| --- | --- | --- | --- |
| Spatial index for hit testing | widgets.md §2 | no measurement demands it yet | when one does |
| `parent_uses_size` | widgets.md §9 | no arrangement genuinely stops reading a child's size yet | when one does |
| Gesture arena (§5.16.3) | widgets.md §9, scrolling.md §6, form-controls.md §1.4 | "an arena with one competitor is a data structure with no purpose" | whenever a second pointer recognizer competes |
| Double-click, drag thresholds, cross-widget pointer capture | widgets.md §9 | out of scope, undedicated | unnamed |
| `prop_lock.py` enum-ordering coverage | properties.md §2 | small known gap | the slice that first needs to reorder an enum |

**46 distinct declines recorded across the phase.** Nothing here is new to
this audit — the value this table adds is that they are now in one place,
against one column naming the design.md phase (where one is named), so the
next phase's owner does not have to re-read nine documents to build a
backlog.

## 6. Every place the phase found the implementation contradicts design.md

| # | Contradiction | Doc | design.md section | Nature |
| --- | --- | --- | --- | --- |
| 1 | Layout is integer device pixels, not float logical pixels | layout.md | §5.4.9 | Deliberate deviation: a float layout has to round somewhere, breaking damage-rect/paint-rect byte-identity for one frame per rounding crossing. Cost: a DPI change re-lays-out instead of only repainting (argued near-free, rides the same frame a DPI change already forces a full repaint on). |
| 2 | `overflow` clips at the border box, not CSS's padding box | clipping.md §2 | §5.9.3 ("a property sharing a CSS name must share CSS behaviour") | Clipping at the padding box would need a second copy of layout's insets inside `NodeStyle`, the exact two-vocabulary drift properties.md's own boundary rule exists to prevent. Not renamed — the id is ABI-locked. |
| 3 | No `RenderClip` node kind exists | clipping.md §6 | §5.4 line ~316-319 (lists `RenderClip` among ~13 built-in RenderObjects) | design.md names it but never specifies its API/semantics/Skia call. This codebase implements clip as a field (`NodeStyle::overflow`) read by three call sites (paint, hit test, damage), not as a node kind. |
| 4 | Hit testing ignores `opacity` entirely, including at 0 | compositing.md §4 | §5.11.2 (only specifies that `opacity==0` skips paint **and hit test**, while still participating in layout) | Deliberate, and design.md's own justification (§5.9.7: "`opacity:0` covers what `visibility:hidden` would do") is separately argued **false** by compositing.md: a continuous fade has no natural cliff to put a hit-test cutoff at, so this codebase makes opacity never affect hit testing, at any value — broader than what §5.11.2's table literally specifies (which only addresses the `==0` case) and directly contrary to it there. |
| 5 | No `RenderOpacity` node kind, no repaint-boundary promotion during animation | compositing.md §5 | §5.4 (lists `RenderOpacity`), §5.4.8 (auto-promotes to repaint boundary during animation) | Same reasoning as `RenderClip`: opacity is a node field, not a kind. Boundary promotion is argued unnecessary rather than merely unbuilt: a scalar-alpha layer already isn't damage-atomic, so nothing needs promoting. |
| 6 | §5.4.3 step 1 (measure every `grow==0 && shrink==0` child under an unbounded main axis) is not the general policy this codebase runs | sizing.md §1.8 item 1, scrolling.md §3 | §5.4.3 step 1 | Rejected generally (would cost `2^(n/2)` without full caching); the only unbounded axis that exists is `scroll_axis`-scoped (4-7), which is a narrow exception design.md's own §5.4.7 diagnostic text presupposes exists more broadly — a contradiction inside design.md's own roadmap that this codebase resolved by scoping narrowly. |
| 7 | No freeze-and-redistribute loop for shrink (§5.4.3 step 4) | sizing.md §1.8 item 2 | §5.4.3 step 4 | CSS re-runs distribution when an item hits its `min_*`; this codebase stops absorbing and reports the residual as a diagnostic instead. |
| 8 | `grow` child's base is never its own measured width (§5.4.3 step 3) | sizing.md §1.8 item 3 | §5.4.3 step 3 | CSS resolves `flex-basis:auto` to the item's own width; changing this would move every flexible child in every existing scene, so it is declined outright. |
| 9 | §5.4.6/§5.4.1's intrinsic-size cache is asserted **mandatory**, stronger than §5.4.6's own hedge-free wording states explicitly | sizing.md §1.4 | §5.4.6, and §5.4.1 line 334 ("L3 的唯一例外是内在尺寸查询…因此它必须缓存") | Not a contradiction of behavior (no intrinsic query exists in this codebase to cache or not) but of *emphasis*: §5.4.1 does use "必须" (must) at line 334, which this audit independently confirms — so sizing.md's "mandatory, not advisory" reading is textually supported once §5.4.1 is read alongside §5.4.6, not an overreach. |
| 10 | **`PopupHost` does not exist at all, contradicting design.md's explicit MVP mandate** | form-controls.md §2 (decline), escalated by this audit | §5.2 lines 196-208, specifically line 207: "**这个抽象必须在 MVP 就存在**" ("this abstraction must exist as of MVP") | **Newly escalated by this audit, not previously framed as a contradiction.** Every other item in this table is a considered deviation the implementing slice weighed and accepted. This one is different in kind: design.md does not merely prefer `PopupHost` exist by MVP, it calls the abstraction "本设计最关键的一处" (the single most critical decision in the whole design) and names the exact failure mode of building without it first (rewriting the entire menu/dropdown/tooltip subsystem later, the stonegui-postmortem trap). `Dropdown` is not one of the MVP-8 (§5.6 line 615), so its absence does not weaken the MVP-8 verdict directly — but design.md's own words make the missing abstraction underneath it a bigger gap than "one control not built," because every future popup-shaped control (`Dropdown`, `Menu`, `Tooltip`, `Dialog`) inherits the same missing prerequisite. Recorded here rather than only in the decline table because design.md's language ("must", "most critical") reads as a stronger claim than an ordinary roadmap item, and a reader of this audit deserves to see that severity distinction made explicit. |
| 11 | Text rendering uses none of `textlayout`/SkParagraph/HarfBuzz/ICU that design.md commits to across §3.2, §5.3, §5.10.4, §5.10.5, §5.13.6, §5.13.7 | font-fallback.md, text-input.md §1.4 | multiple (listed) | Confirmed independently for this audit: `grep -rn "SkParagraph\|hb_\|icu" include/ src/` finds nothing. design.md's own roadmap does not force this before P3's text-widget landing (icudtl.dat decision due then, design.md line ~1770-1771), so this is a scoped, named gap rather than a broken promise — but it is real and load-bearing for anything beyond ASCII single-line text. |
| 12 | §5.9.5's dedicated-setter code block lists only three complex-typed properties (gradient, shadow, image) — `transform` is a fourth complex type in the same table with no `dg_node_set_transform` signature anywhere in the section | complex-properties.md §6 (added by slice 5-4) | §5.9.5's code block (three `dg_node_set_*` signatures) | Textual omission in design.md itself, not an implementation deviation: `transform`'s `type = "transform"` in `props/drawgui.props.toml` needs the identical "cannot travel in the scalar union" treatment the other three get, and §5.9.5 simply never states its ABI shape. Settled rather than left as 4-10 found it: `TransformDesc` (translate/scale/rotate + origin, matching §5.9.6's own decomposition) is the shape a future design.md revision should add; `dg::set_transform()` implements the validation half of that shape today and always reports `kUnsupported` for the capability half, naming three blockers that must move together (non-axis-aligned damage bounds, an inverse-transform hit test, whether transform affects parent layout). |

12 distinct contradictions, one of them (#10) escalated by this audit beyond
how the originating slice framed it, because design.md's own wording for that
one item is categorically stronger ("must", "the single most critical
decision") than the "declined, named, deferred" register every other item in
this table shares.

**Update (slice 5-4, phase 5): row #12 added.** This is a worklist item for
design.md's own next revision, not a fix applied to design.md's prose by this
slice — `doc/complex-properties.md` section 6 is the full argument, including
why an axis-aligned translate+scale subset was evaluated and still declined
(integer translation is already fully expressible via
`RenderTree::set_local_origin()`, and scale raises a layout question this
slice does not answer). This paragraph is appended rather than rewriting the
"11 distinct contradictions" count above it, which is left as this audit
originally computed it.

**Update (slice 6-1, phase 6), cross-reference rather than a rewrite: row #5's
contradiction still holds.** Section 5's declines "Fling/inertia",
"Overscroll rebound animation" and "Caret blink" (rows citing "needs an
animation clock (design.md 5.16.1) that does not exist") named a prerequisite
that no longer holds - `dg::AnimationEngine` exists as of slice 6-1
(`doc/animation.md`). That does NOT retroactively un-decline any of the
three: fling still needs a gesture-derived velocity source this slice did not
build, overscroll rebound's PLATFORM-SPECIFIC behaviour is still absent (only
its motion primitive exists now), and caret blink is unblocked only as a
standalone demo, not wired into the real `TextField` widget -
`doc/animation.md` section 8 is the row-by-row accounting. Row #5 in THIS
section (no repaint-boundary promotion during animation) is a separate,
STILL-open contradiction even with the clock built: `doc/animation.md`
section 7 measured, rather than assumed, that this slice's animated writes
go through the same `dg::set_prop()` path an ordinary write already did, with
no automatic promotion to a repaint boundary and no demotion afterward -
§5.11.2's own promotion/demotion clause remains unimplemented, now with a
running clock behind it rather than without one. Neither original finding is
edited; both are still accurate as written.

## 7. Verdict

**"基础 GUI 组件完备" (basic GUI components are complete) is TRUE, under a
named and non-trivial scope boundary — not an unqualified TRUE.**

What is true without qualification:

- The layer-3 primitive set (`RenderObject`-equivalent: `NodeStyle` fields +
  `LayoutKind` + `RenderTree`/`LayoutTree` methods) proved sufficient for every
  widget this phase built — checkbox, radio, slider, scrollview, textfield —
  with **zero new node kinds added**. design.md's own acceptance bar (line
  622) is satisfied more broadly than the `Slider`-only check 4-8 originally
  ran (section 2).
- Six of the MVP-8 are unambiguously present, each with an evidence trail
  (`WidgetKind` or `LayoutKind`, an example, a CTest entry): `Box`, `Text`,
  `Button`, `TextField`, `ScrollView`, `Row`/`Column`.
- 46 properties are accounted for exactly as claimed: 33 implemented, 10
  partial, 3 not-yet, every one of the 13 gaps named with what it needs.
- Every invariant this phase established — exactly-once layout, damage
  correctness, zero `virtual`, zero SDL in `include/`, golden byte-stability,
  the ABI lock — still holds with all nine slices' work coexisting, re-checked
  independently for this audit rather than assumed from prior claims.

What is NOT true without qualification, named exactly:

- **`List` is present as mechanism (ScrollView + Column composition, proven in
  `examples/10_scrolling`) but not as the control design.md defines**, because
  design.md's own line 617 makes virtualization part of that definition, and
  none exists. Building it now would mean building object-pool recycling and
  a visible-range node lifecycle — explicitly out of this slice's budget per
  the task's own "no virtualization" boundary, and design.md's own roadmap
  (line 1714) already assigns it to **P3** as a control distinct from
  `ScrollView`. **First task named for the next phase.**

  **Update (slice 5-3, phase 5): closed.** `WidgetKind::kList` is a fixed,
  permanently-allocated pool of item nodes (never one node per logical item),
  recycled via `RenderTree::set_local_bounds()`/`set_style()` as the visible
  range moves - node-removal was evaluated and correctly NOT added (the fixed
  pool sidesteps the question rather than needing an answer to it), zero new
  node/RenderObject kinds were needed (extending this document's own streak
  through an 11th consecutive slice), and recycling costs a repaint and never
  a relayout, measured the same way 4-7's plain-offset claim was. See
  `doc/list.md` for the full record, including the 1000-item measurement
  against a real pre-virtualization baseline; this paragraph is left as the
  audit originally wrote it, a snapshot of what was true before slice 5-3,
  not rewritten.
- **`Image` is completely absent** — not a stub, not a `kUnsupported` property,
  not a partial path. `NodeStyle` carries no image-bearing field of any kind,
  and design.md's own §5.9.5 already specifies a `dg_node_set_image` ABI
  shape nothing downstream was ever built for. This is the one MVP-8 item this
  audit found with genuinely zero engine-side trace. It was evaluated for a
  same-slice fix and declined: building it correctly needs at minimum a
  decoded-image ownership/lifecycle story (what owns the `SkImage`, when it is
  released), a fit-mode decision (`fill`/`contain`/`cover`/`tile`, per
  §5.10.1's `ImageSource` abstraction design.md already specifies), a decision
  on whether it is a `NodeStyle` field or a new leaf concept, and golden-test
  infrastructure for a non-solid-fill node kind this project has never needed
  before (every existing golden scene is vector fills/borders/text; an image
  golden test needs a checked-in source image and a decode step the current
  test harness has no precedent for). That is comparable in scope to the other
  items this phase's own instructions correctly kept out (virtualization,
  PopupHost, IME, HarfBuzz) — not "one function," a small subsystem with its
  own decisions doc worth of argument. **Declined by name, first task named
  for the next phase**, alongside `List` virtualization.

  **Update (slice 5-1, phase 5): closed.** Every sub-problem this paragraph
  named was resolved — ownership/lifecycle (`ImageCatalog`, an append-only
  table of `sk_sp<SkImage>` behind a pimpl, the same shape `FontCatalog`
  already has), fit-mode (`fill`/`contain`/`cover`/`none`; `tile` and
  nine-patch declined by name, both belonging to `background_image` rather
  than to `RenderImage`), the field-vs-leaf-vs-`WidgetKind` question (a field,
  defended against this exact document's section 6/7 precedent for
  `overflow`/`opacity`), and the golden-test problem (a synthesized,
  in-process PNG source decoded through the real `SkCodec` path, leaving the
  existing byte-exact golden suite this section's own table cites — sha256
  `f635028e6e1e92349d23f0575f1e39e7ca8b05e5974492641ee610fe520df12e` —
  untouched). `doc/image.md` is the full record; this paragraph is left as
  the audit originally wrote it, a snapshot of what was true before slice
  5-1, not rewritten.
- **`PopupHost` does not exist**, and design.md's own words (§5.2 line 207)
  call this "the single most critical decision in the whole design," to be in
  place "as of MVP." `Dropdown` itself is not one of the MVP-8, so this does
  not by itself break the MVP-8 verdict, but it is the sharpest gap between
  this project's current state and design.md's own stated priorities, and is
  named here at full severity rather than folded quietly into the decline
  table.

  **Update (slice 5-2, phase 5): closed.** `PopupHost::show()` now exists,
  branching on `PlatformCaps::native_popup` between a real
  `SDL_CreatePopupWindow` window and an appended, clipped overlay subtree of
  the caller's own `RenderTree` - both branches real and exercised every
  CTest run, zero new node/RenderObject kinds (extending this document's own
  section 2 finding through a tenth slice). `Dropdown` itself is still not
  built - the prerequisite this section named is satisfied, and building the
  widget is the next slice's task, named by `doc/form-controls.md` §2.4 and
  now by `doc/popup.md` §6. See `doc/popup.md` for the full record; this
  paragraph is left as the audit originally wrote it, a snapshot of what was
  true before slice 5-2, not rewritten.
- Text rendering has no HarfBuzz/ICU/BiDi/line-breaking/`SkParagraph` — real,
  named, and scoped by design.md's own roadmap to no later than P3's text
  widgets landing, which already happened in 4-9 for the ASCII case. Anything
  beyond ASCII single-line text is out of this phase's proven scope.
- No theme system (§5.7) exists; design.md's own P3 acceptance criterion
  (line 1714) additionally names "light/dark 运行时切换" and "1000 项列表
  60fps" as part of what P3 requires — neither was in the user's original
  directive for this phase ("layout实现，类css属性，widget控件的实现等"), so
  their absence is not scored against this phase's own goal, but a reader
  comparing this phase's output against design.md's P3 row in full (as
  opposed to the narrower MVP-8 widget-existence question this audit was
  asked to settle) should not conclude P3 itself is closed. It is not; **this
  phase is a proper subset of P3**, and that subset is what is complete.

  **Cross-reference, not a rewrite (added by slice 6-2, phase 6):** this
  bullet's finding stood as written above through phase 5. Slice 6-2 closes
  it - `themes/schema.toml`, a JSON loader, `$token` live references, and
  the light/dark runtime switch this bullet names by name all now exist.
  See `doc/theme.md` for the full record; this paragraph is left exactly as
  the phase-5 audit wrote it, a snapshot of what was true then, not
  retroactively edited to look prescient.

**Stated scope of the TRUE verdict**: basic GUI components are complete for
*layout + CSS-like properties + the widget primitives that compose from
existing RenderObjects without needing a new one* — six of the MVP-8 fully,
`List` as a proven-sufficient mechanism without its mandatory virtualization,
and `Image` named as the one genuine hole — on Linux, CPU raster, ASCII text,
no theme system, no popups. That is a real and defensible "complete," not the
literal, unqualified P3 row design.md's roadmap table describes. The two
gaps that keep it from being unqualified (`Image`, `List` virtualization) are
both explicitly named as the first tasks for whichever phase follows this one,
rather than being silently absorbed into "basically done."

**Update (slice 6-1, phase 6), cross-reference:** this verdict's scope
boundary was scoped to P3 (widgets); the animation clock is P4.5, a separate
row on design.md's own roadmap, and was out of scope for the phase this
verdict describes - it is not retroactively added to the "true without
qualification" list above, which is left exactly as this audit wrote it.
`doc/animation.md` records phase 6's own opening slice, including the honest
finding that §5.11.2's animation-triggered repaint-boundary promotion (named
absent in section 6, row 5, above) is still absent with a working clock
behind it as it was without one.

## 8. Verification run for this slice

Run once each, per the task's own reduced-verification instruction:

- `ctest --test-dir build --output-on-failure`: **17/17 passed** (unit,
  layout.incremental_equals_full, widgets.interaction_equals_full,
  golden.scenes, props.no_drift, props.abi_lock, props.lock_selftest,
  damage/layout/widgets/fonts/clipping/opacity/sizing/scrolling/
  form_controls/text_input.verify_demo_scene).
- `./build/examples/drawgui_render_png` → sha256
  `f635028e6e1e92349d23f0575f1e39e7ca8b05e5974492641ee610fe520df12e` —
  matches the golden value unchanged since 4-6.
- One `-Werror` configure+build, clang++ 21.1.8, Debug: clean, zero warnings,
  130/130 targets built.
- No code was changed by this slice (it is a pure audit + docs deliverable),
  so clang-tidy/clang-format were not re-run against a diff; there is no diff
  to lint or format.

## 9. Final counts, precisely

- Properties: 46 total — 33 implemented / 10 partial / 3 not-yet (unchanged).
- `WidgetKind` values: 7 (`kPanel`, `kLabel`, `kButton`, `kCheckbox`,
  `kScrollView`, `kSlider`, `kTextField`).
- `LayoutKind` values: 6 (`kLeaf`, `kRow`, `kColumn`, `kWrapRow`, `kWrapColumn`,
  `kAbsolute`).
- CTest entries: 17.
- Examples: 13 (`00_cpu_raster_png` through `12_text_input`).
- `virtual` occurrences in `src/`+`include/`: 3, all in comments, zero real
  uses.
- SDL references in `include/`: all in comments, in one file
  (`window_manager.h`), zero real leakage.
- MVP-8 widgets fully present: 6 of 8. Qualified partial: 1 (`List`, mechanism
  without virtualization). Absent: 1 (`Image`).
- Declines consolidated: 46 rows across 6 topic groups (section 5).
- design.md contradictions consolidated: 11 rows, one (`PopupHost`) escalated
  in severity by this audit (section 6).
