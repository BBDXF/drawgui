# drawgui

drawgui is a lightweight self-drawn GUI kernel written in C++20, architected
after Flutter's layered rendering model, exporting a pure C ABI so any
language can embed it.

## Unique value proposition

Relative to Qt QML or Flutter, the value of drawgui is **"a self-drawn GUI
kernel that any language can embed through a C ABI"** - not "another complete
desktop application framework". A host language needs no Dart VM, no QML
engine, and no embedded Chromium - only the ability to call C functions.

## Accepted trade-off

This project does not compete with Qt or Flutter on feature completeness. It
wins on embedding cost and language neutrality, and will lag long-term on
accessibility, complex text editing, and depth of native integration.

## Current status

The foundation is in place: CMake build, verified prebuilt Skia, a CPU raster
path that produces a PNG, a golden-image test pipeline, a concrete SDL3
multi-window manager, and Skia rendering on the CPU into a real window.

Skia and the window manager now meet, on the CPU. A frame is rasterized into
an ordinary buffer and copied onto the window surface; no GL context is
created anywhere. `doc/cpu-raster-findings.md` records what that costs and
where it stops being enough. There is no layout, no widget, no theming and no
C ABI.

Layout and paint are now configurable by **property id**. `props/` held a
45-property CSS-like table, a generator and an ABI lock from the very first
phase, frozen because nothing included either generated file; its real consumer
- the eventual C ABI, where a host language sets a property by number rather
than by calling a C++ setter - finally exists in outline, so `dg::set_prop()`
connects the table to the layout and render trees. 21 properties are fully
implemented, 12 partially, and 12 report `kUnsupported` naming what they need.
`doc/properties.md` records the reconciliation, the per-property gap report, and
why the generated ids are plain constants rather than an enumeration.

Layout now **wraps**. A container whose children overrun the main axis breaks
them into runs; `run_gap` spaces the runs, `align_content` positions the run
stack, and `align` applies within a run rather than across the container. A
child can override its container with `align_self`, and a border can be a
different thickness on each of its four sides. 27 properties are now fully
implemented, 9 partially and 9 report `kUnsupported`. `doc/wrapping.md` records
why wrapping still lays every node out exactly once, and where design.md asks
for two things that cannot both be true.

A container can now **clip** what overflows it. `overflow` is one field on the
node, and painting, hit testing and damage all read it: content is cut at the
boundary, a point in the cut-away region does not hit the widget that would
have been there, and a change inside a clipped container does not ask for a
repaint of pixels the clip removes. Rounded clips follow the curve.
`doc/clipping.md` records why a rounded clip needed no new damage rule, what it
costs, and where `overflow` deviates from CSS.

A subtree can now be **faded as a group**. `opacity` composites the whole
subtree through `SkCanvas::saveLayer` and blends the result as one image, so
overlapping children inside a faded group do not show through each other - the
CSS meaning, not per-object alpha, and the two are drawn side by side out of
the same node table in `examples/08_opacity`. A layer is opened only when one
is needed: never at `opacity == 1`, and never at `0` either, where the subtree
is skipped. Hit testing deliberately ignores `opacity` entirely, so a group
faded to nothing is invisible and still clickable. 29 properties are now fully
implemented, 9 partially and 7 report `kUnsupported`. `doc/compositing.md`
records why a layer is **not** a damage-atomic region, why the anti-alias slack
does not belong on a layer's extent, where this contradicts `design.md`, and
what `shadow` and `transform` still need.

Sizing now has a **second stage**, and the finding is that it needed no second
measurement. `basis` gives a child a declared base main size, `shrink` takes
space back from it when the container overruns - weighted by `shrink x base`,
CSS's scaled shrink factor, split by prefix sums so no pixel is invented or
lost - `main_size` makes a container fill its main axis instead of hugging its
content, and `aspect_ratio` derives one axis from the other, including the
sharp direction where a stretched child's WIDTH follows the height its
container handed down. Every node is still laid out **exactly once** per pass,
which the demo asserts on itself. The one thing that would have cost a second
measurement is shrinking from a base the engine had to measure, and that is
declined by name with a layout diagnostic; `doc/sizing.md` section 1 records
the argument, including the exponential the obvious two-pass design costs and
why design.md section 5.4.6's cache is mandatory rather than advisory. 32
properties are now fully implemented, 10 partially and 3 report `kUnsupported`.

A leaf can now be a **scrolling viewport**, and a "list" turned out to be
nothing new: it is a `kColumn`/`kRow` of ordinary children composed inside
one. `scroll_axis` hands that single child an unbounded constraint on one
axis instead of squeezing it to fit, `overflow` (unchanged) clips the
overflow at the viewport's own bounds, and `RenderTree::set_scroll_offset`
shifts the child without moving what it declared - the same shape a clip
confines a descendant without confining its own paint. The offset itself is
runtime state, not a property, for the reason hover and press already are:
it accumulates across an unbounded stream of wheel notches and drag deltas
rather than being declared once. Scrolling costs a repaint and never a
relayout - `LayoutTree::layout()` visits zero nodes on a frame where only the
offset changed, measured on the demo scene rather than assumed from the code
that makes it true. Hit testing needed no new code at all: it already read
the position the offset shifts, so a scrolled-out child stops answering and a
scrolled-in one starts, for free. 33 properties are now fully implemented, 10
partially and 3 report `kUnsupported`. `doc/scrolling.md` records why nested
scrolling composes without new code, what design.md's own roadmap asks for
that needs an animation clock this project does not have yet (fling,
overscroll rebound - both declined and named), and why list virtualization
belongs to a later phase's `List` control rather than to this viewport.

Checkbox, radio and slider are three form controls out of two mechanisms.
Radio is not a new control at all: a checkbox gained one field, `group`, and
selecting one option clears every other checkbox sharing its group id instead
of toggling - "checkbox plus a group id", the same composition doc/scrolling.md
found for scrolling rather than a fourth widget kind. Slider is a sixth
`WidgetKind` - a track and a thumb, the thumb's position a paint-time function
of its value, moved through the same `RenderTree::set_local_origin` a plain
node move already used - satisfying design.md's own acceptance bar for this
control head-on: no new RenderObject was needed. The value itself is runtime
state, not a property, for the identical reason the scroll offset is: it
accumulates across an unbounded stream of drag deltas rather than being
declared once. Dropdown is declined outright rather than half-built: this
engine's window layer has no popup-window concept whatsoever - no window
kind, no `PopupHost`, nothing - and design.md calls that abstraction the
single most critical decision in the whole design, naming the exact trap a
naive in-window dropdown falls into. `doc/form-controls.md` records the
scoping arguments in full, a real `LayoutTree` constraint found while sizing
the slider's thumb (a leaf's child cannot exceed the leaf's own resolved
size, even "loosened"), and a defect-injection campaign that found and fixed
a genuine bug in its own first regression test.

Text can now be **typed and edited**, in a single-line `TextField` scoped to
design.md's own MVP concession: ASCII direct input and correct display of
committed text, nothing more. `TextField` is a seventh `WidgetKind` built from
the same primitives every widget here already stands on - a clipping leaf plus
three plain children the widget positions - so the field needed no new
RenderObject, echoing design.md's own acceptance bar for Slider. ASCII scoping
is not a shortcut around design.md's grapheme-cluster requirement for cursor
movement and selection, it satisfies that requirement by construction: for
ASCII, a byte offset, a codepoint offset and a grapheme-cluster boundary are
the same number, so no ICU or HarfBuzz is wired in, and no non-ASCII byte is
ever mis-segmented - it is filtered at the model boundary instead. IME's
interface hook, `start_text_input`/`stop_text_input`, is real SDL3 plumbing
rather than a placeholder: SDL3 emits no committed-text event at all until it
is called, so this slice needs it for plain ASCII typing to work, while the
actual IME feature - reading the in-progress composition preview - stays
future work exactly where design.md puts it. Unfocused, an overflowing field
shows an ellipsis-truncated prefix; focused, it shows the full string scrolled
to keep the caret visible - both built on the same `SkFont::measureText`
primitive this project's text rendering already uses, not on `SkParagraph`,
which appears nowhere in this codebase and stays out of scope for a
single-line field. Text content, cursor and selection are runtime widget
state, not properties, for the same reason the scroll offset and the slider's
value already are: keystrokes and drag deltas are unbounded streams, not
declared values. A `dg::Focus` concept - one optional node id, exclusive, no
tab order - had to be introduced from nothing, this engine's first notion of
which widget receives keyboard input. Typing costs a repaint and never a
relayout in this slice specifically because every `TextField` here is
fixed-width, exactly like a slider's track; a future shrink-to-fit field would
need a real relayout on every edit, and that condition is named rather than
glossed over. `doc/text-input.md` records every scoping decision in full, the
relayout finding measured rather than assumed, and a defect-injection campaign
that found a sixth project failure mode: a weak assertion that was satisfied
by an entire family of wrong answers, not only the right one.

Text now falls back across scripts: one named family draws any string, and a
BCP 47 language tag selects between Han faces. `doc/font-fallback.md` records
why that chain is built here rather than delegated to fontconfig.

This phase closes on an audit, not a feature: `doc/completeness.md` checks the
whole phase against design.md's own MVP-8 widget list (`Box` `Text` `Button`
`TextField` `ScrollView` `List` `Image` `Row`/`Column`) rather than against
this project's own prior claims about itself. Six of the eight are unambiguous
- `Box` and `Row`/`Column` are `BoxStyle`/`LayoutKind`, not `WidgetKind`s, and
that is by design, not a shortfall. `List` turns out to be present as
mechanism (a `kColumn`/`kRow` composed inside a `kScrollView`, exactly what
`examples/10_scrolling` already draws) but not as the control design.md
itself defines: line 617 makes virtualization part of `List`'s definition,
and none exists, so an honest reading calls this a proven substrate rather
than a finished widget. `Image` is the one true gap: nothing in `NodeStyle`
carries an image of any kind, and no decode/draw path exists anywhere the
engine's own render tree can reach - the Skia gallery's image panel is a demo
of Skia, not of this engine. Both gaps are named as the first tasks for
whichever phase follows this one rather than papered over. Everything else
checks out further than expected: the layer-3 primitive set that design.md's
own acceptance bar asks Slider to validate (line 622) turns out to have
needed zero new RenderObject kinds across every widget this phase built -
checkbox, radio, slider, scrollview, textfield alike - and every invariant
this phase established (exactly-once layout, damage correctness, zero
`virtual`, zero SDL in `include/`, golden byte-stability, the ABI lock) still
holds with all nine slices' work coexisting, each re-checked once rather than
assumed. The verdict is a qualified TRUE: basic GUI components are complete
for layout, CSS-like properties and the widget primitives that compose from
what already exists, on Linux, CPU raster, ASCII text, with no theme system
and no popups - a real boundary, stated exactly, not an unqualified claim
papering over the two named holes. `doc/completeness.md` also finds and
escalates the sharpest single gap between this project and design.md's own
priorities: `PopupHost`, which design.md calls "the single most critical
decision in the whole design" and requires to exist as of MVP, does not exist
at all.

Phase 5 opened by closing the sharper of that audit's two named gaps:
`Image` can now be **decoded and painted**, as one more field on the same
`NodeStyle` every node already carries - `ImageStyle`, sitting beside
`TextStyle` rather than becoming a new node kind, for the identical reason
`overflow` and `opacity` are fields rather than kinds. A decoded bitmap comes
through the real `SkCodec` path (`ImageCatalog`), scaled into its node's box
by one of four fit modes (`fill`/`contain`/`cover`/`none`), with a plain
configurable colour standing in for the theme-token placeholder design.md
asks for. The one rule that made this safe to build at all is a layout
constraint stated as a hard requirement, not a suggestion: an image node
must know its own size **before** decoding finishes - through an explicit
size, an `aspect_ratio`, or a parent constraint that settles both axes -
because sizing from decoded content would turn every finished image load
into a visible reflow. The fourth, illegal case (none of the three) is a real
diagnostic through this project's existing layout-error channel, never a
silent fallback and never an assert. The property the rule exists to buy was
measured, not assumed: swapping a node's decoded 64x64 source for a 512x512
one moves nothing, `LayoutStats` reporting zero nodes visited and zero
relaid out on that frame. An image golden test had no precedent in this
project - every prior golden scene is vector fills, borders and text - so
`examples/13_image` synthesizes its own source in-process from a documented
pixel formula, encodes it with this project's own PNG writer, and decodes it
back through the real codec, leaving the existing byte-exact golden suite
(`f635028e...`, unchanged since 4-6) untouched. `doc/image.md` records the
decision in full, including why zero new node/RenderObject kinds were needed
- extending `doc/completeness.md`'s own streak through the one MVP-8 item it
had found absent - and what a future asynchronous decode would and would not
have to change about any of this.

`List` can now be **virtualized**, which closes the other of the two gaps
4-10's audit named. A `kList` is an 8th `WidgetKind`: a fixed, permanently-
allocated pool of item nodes - never one node per logical item - recycled as
the visible range moves, so a 1000-item list costs 14 real nodes rather than
1000. Node removal was evaluated and correctly not added: the pool sidesteps
the question rather than needing an answer to it, so a recycled `NodeId`
never dangles anywhere - it is restyled and repositioned, never destroyed.
Recycling routes entirely through `RenderTree::set_local_bounds()`/
`set_style()`, the same primitives a slider's thumb or a checkbox's indicator
already move through, so it costs a repaint and never a relayout - measured,
not assumed, extending 4-7's `nodes_visited == 0` finding to cover recycling
as well as a plain offset. The data-source seam needed no interface at all:
`WidgetSet` reports which pool node now represents which logical item (a
plain `NodeId, int` pair), and the caller writes that item's content through
the exact same `RenderTree::set_style()` an unrecycled node already uses.
Only fixed-extent rows are built; variable-height rows are declined by name,
because the two usual techniques (measuring every off-screen item, or
estimate-then-correct) either defeat virtualization outright or introduce
visible scrollbar jitter. Measured against design.md's own "1000 项列表
60fps" bar with a real pre-virtualization baseline (1000 permanently-
allocated real nodes): both clear 60fps comfortably at 1000 items on this
CPU-raster engine, and the decisive difference - the virtualized pool's cost
staying flat as item count grows, against the baseline's cost growing with
node count - shows up further out, where `doc/list.md` measures it crossing
the 60fps line on this host somewhere past 100,000 items. Zero new node/
RenderObject kinds were needed, extending `doc/completeness.md`'s streak
through an 11th consecutive slice. `doc/list.md` records the decision in
full, including the residue-correctness design that makes a recycled node's
stale content structurally impossible rather than merely checked for, and a
defect-injection campaign that found two real coverage gaps and closed both
with new regression tests.

Phase 5 closes on the property table's last three gaps. `background_gradient`,
`shadow` and `image_source` all needed a shape design.md itself specifies but
this project had never built: a dedicated setter, separate from the ordinary
scalar `dg::set_prop()` door, for a value too large or too variable to fit a
tagged union. `image_source` was built first and proven working - its
decode/paint/layout path already existed from the prior slice - and the
shared id-validation shape was extracted from that working code afterward,
never written ahead of it. `background_gradient` and `shadow` then reused the
same shape for real new work: a linear gradient shader, and a drop shadow
that is this project's first property to paint OUTSIDE the pixels a node
declares as its own. That is a fact damage tracking's whole design assumes
never happens, so the node's own damage rule was generalised rather than
bypassed - a shadowed node forces whole-node repaint growth for the same
mechanical reason a rounded node already does (both are unsafe to cut with a
damage rectangle), just outset by the shadow's own reach - budgeted, per a
prior slice's measurement that blur is over half a dense frame's raster
time. `transform`, the table's fourth complex-typed property, is declined:
its dedicated setter exists and answers consistently, but every rectangle
this engine tracks - damage, hit testing, clipping - is axis-aligned integer
pixels, and a general 2D transform breaks that in three places at once
rather than one at a time. 49 properties: 38 fully implemented, 10 partially,
1 not yet - down from 4. Zero new node kinds were needed, extending the
streak through a twelfth consecutive slice. `doc/complex-properties.md`
records the decision in full, including a defect-injection campaign that
found one genuinely inert guard and root-caused why, rather than merely
noting it survived.

Phase 6 opens with the piece design.md's own roadmap named as the shared
blocker for five separate features: an **animation clock**. `dg::AnimationEngine`
owns it, C++-side, exactly as design.md section 5.16.1 requires - a host
declares a `from`, a `to`, a duration and a curve, and interpolation never
leaves C++. The clock's own seam is a value, not a `virtual` interface:
`AnimTime` is a plain integer-millisecond struct passed into `tick()` as an
ordinary argument, the same technique every prior seam in this project used
for a varying input (`WindowManager::warp_pointer`'s `x, y`,
`RenderTree::set_scroll_offset`'s offset) rather than a new one invented for
testability - so every assertion in `tests/unit/test_animation.cpp` advances
time by an exact, hand-chosen amount and never sleeps. Two APIs, both
over the property table this project already had: `animate()` returns a
handle that can be paused, reversed or cancelled, and `set_transition()` +
`set_value()` give any subsequent write to a declared property the CSS
transition model - including the subtle case where a property changes again
before its first transition finished, which retargets from the CURRENT
interpolated value rather than restarting from the original (a defect
injection confirmed this is load-bearing, not merely asserted). A handle's
lifetime problem is new, not 5-3's list-pool answer reapplied: an animation's
count is unbounded over a session the way a render node's is not, so a
finished slot's storage IS reused, and a generation counter (the mechanism
`doc/widgets.md` already named as what a future removal path would need) is
what keeps a stale handle from ever controlling the wrong animation. The
on-demand frame loop design.md section 5.15.1 asks for - block indefinitely
while idle, run a short frame-pacer interval while animating, return to
blocking once nothing is - is built on `WindowManager::pump()`'s existing
timeout shape and measured, not assumed: blocking for a genuine two-second
idle span consumes 0.009% of a CPU core. Two real clients prove the system:
an implicit `background_color` transition on hover, and a text-cursor blink
built from four chained explicit animations - unblocking 4-9's caret-blink
decline and 4-7/5-2's animation-shaped declines at the mechanism level, while
naming plainly what each still needs (a gesture-arena velocity source for
fling, in particular, stays out of scope). Zero new node kinds were needed -
a thirteenth consecutive slice - and `doc/animation.md` records an honest gap
this slice does NOT close: §5.15.2's three-level invalidation model is not
implemented as a cost model, so an animated `opacity` write costs exactly
what a hand-written one already did, not a cheaper recomposite-only path.

Phase 6 closes its second slice on the half of P3 the completeness audit
found at zero: a **theme token system**. `themes/schema.toml` is a
compile-time contract - which tokens exist, `color.surface`/`radius.md`/
11 total - generated exactly like `props/drawgui.props.toml` already is,
by a sibling tool (`tools/gen_theme.py` + `tools/theme_lock.py`) rather than
an extension of the property generator, because the two source-of-truth
shapes genuinely differ. `themes/builtin/theme.json` is the one shipped
instance of what each token EQUALS under light/dark, loaded by a
purpose-built ~300-line JSON parser rather than a third-party library - this
project's first new dependency decision since Skia/SDL3/FreeType, argued
and declined in `doc/theme.md` section 3 on the grounds that `theme.json`'s
own grammar is closed and small enough that a general-purpose parser buys
nothing this slice needs. `$token` live references - a theme switch that
updates a bound node without touching the widget tree - are a side table,
`ThemeBindings`, keyed by `NodeId` exactly the way `WidgetSet` already is,
and deliberately NOT a generation-counter handle the way `AnimationEngine`'s
slots are: a token binding's lifetime is tied one-to-one to a node's, and
nodes here never get removed, so there is no reuse and no ABA problem to
guard against - the opposite precedent applies for the opposite reason.
Resolving a binding reuses `dg::set_prop()` unchanged, which is what lets
"which invalidation a theme switch costs" fall out of a rule this project
already had rather than needing a new one: measured on a real `LayoutTree`,
a colour-only variant switch costs zero relayout (`nodes_visited == 0`),
while an int-token (spacing/radius) value change through the identical path
costs a real one. Unknown token names and type mismatches are both
`dg::Expected` failures naming the exact JSON key path, and
`tools/check_consistency.py` verifies in CI that the shipped theme covers
every schema token in both directions. Zero new node/`RenderObject`/
`WidgetKind` kinds were needed - a fourteenth consecutive slice - and a
defect-injection campaign (three caught immediately, including a
demo-oracle-only bug a unit-test-only campaign could not have found, and
two real coverage gaps closed) is recorded in full, along with everything
this slice explicitly declines (external theme packages, hot reload,
theme-package security limits, an expression evaluator for token alpha, and
the `.d.ts`/ABI-constant-table generation deferred to 6-3), in
`doc/theme.md`.

Phase 6 closes on the piece its own opening paragraph named as the point of
the whole exercise: a **C ABI**, closing this section's own opening claim
("no C ABI") four phases later. `abi/drawgui.def.toml` is a third instance of
the same TOML-to-generator-to-lock family `props/drawgui.props.toml` and
`themes/schema.toml` already are, importing the props generator's own
validated table directly rather than re-parsing it, and producing a real,
C89-compilable `drawgui.h` alongside the try/catch trampolines design.md's
own risk register names as mandatory (a C++ exception crossing a C boundary
is undefined behaviour) - generated uniformly, with no per-function opt-out,
so a hand-written export is structurally impossible rather than merely
discouraged. `examples/19_c_client` is a genuinely pure C program, compiled
by a C front end and linking nothing else, that opens two windows and
responds to clicks - design.md's own acceptance bar for this piece, met
literally rather than approximated: a click on either window's button is
delivered as an event naming both the window and the node, and the host
program uses it to set the OTHER window's colour through the same ABI,
proving the round trip rather than merely a callback firing. Handle
validity is generation-free by design, not by omission: an app/window/node
arena here only ever appends, unlike an animation slot's pool, so there is
no ABA problem index reuse would reopen, and a removed handle's own tiny
wrapper is never freed (only marked dead) - a real defect the first draft
had (freeing it, which would have turned a stale handle into an actual
use-after-free) was found and fixed before this slice landed, and the
opposite mistake (allocating it and never accounting for the memory at all)
was caught immediately by LeakSanitizer. Two real engine gaps the abstract
ABI sketch does not admit to are named rather than quietly worked around:
`LayoutTree`/`RenderTree` have never grown an insertion-order primitive
(only append) or a removal primitive at all, so `dg_node_insert_before`'s
`ref` argument only accepts null and `dg_node_remove` only invalidates a
handle without detaching the node's tree structure - both are honestly
reported as future engine-layer work, not invented here ungrounded in a
working caller. `dg_dump_layout_tree`, P2's own missed acceptance item,
landed alongside the ABI it was always meant to expose. The theme ABI, the
animation ABI, a callback event mode and QuickJS binding stubs are all
declined by name for the identical reason: no working caller in this
project's own build exercises any of them yet. `doc/abi.md` records every
decision in full, including a defect-injection experiment against the
generated try/catch wrapping itself (removing it crashes the process
instead of silently doing nothing, closing the "provably inert code" defect
mode by construction rather than by argument).

There is also no platform abstraction, on purpose. An earlier attempt wrote
twelve abstract platform headers before any backend existed; they were removed
because nothing had ever tested whether they described the machine. The rule
now is that an interface is extracted from at least one working
implementation, never written ahead of one.

Phase 7 opens on the piece 7-1's Skia switch was building toward: text can
now **wrap, reflow across multiple lines, and draw CJK, mixed-script and
bidirectional text through the real font-fallback chain** - `TextStyle::wrap`
routes a node through a real `skia::textlayout::Paragraph` (HarfBuzz shaping,
libgrapheme UAX#14 line breaking and BiDi, all consumed rather than
reimplemented) instead of the plain `SkFont` call every node has used since
slice 2, opt-in and off by default so every scene built before this slice is
byte-for-byte unaffected. `LayoutTree` gained zero knowledge of text to make
this work: a wrapping paragraph's height is measured by a new
`dg::Paragraph::build()` call BEFORE the node exists, the same "know your
size before your content is resolved" discipline design.md already forces on
a decoded image - so multi-line text, the classic case that forces a second
measurement pass in other layout engines, costs this one nothing extra:
`LayoutStats` on the real demo scene shows every node laid out exactly once,
measured rather than assumed. Font selection for wrapped text reuses 6-1's
own zero-fontconfig fallback chain rather than delegating to SkParagraph's
own (broken, on this project's font manager) search - each run is handed
exactly one resolved family name up front. A real hang (not a crash) was
found and fixed along the way: handing ill-formed UTF-8 straight to
`SkParagraph::addText()` hangs the process, so every byte range this
project's own decoder already rejects is substituted with well-formed U+FFFD
before it ever reaches the shaping library. Zero new node/`RenderObject`/
`WidgetKind` kinds were needed - a sixteenth consecutive slice - and the
golden PNG hash is unchanged, confirmed by injection to be a structural fact
(that scene draws no text at all) rather than evidence that text rendering
was untouched, which `test_font_fallback.cpp`'s own pixel oracles are what
actually still guard. `TextField`'s ASCII-only editing surface (4-9) is
completely untouched by this slice - grapheme-cluster cursor movement is
named as a separate follow-up (7-2b) rather than rushed alongside the display
substrate. `doc/text-layout.md` records the full decision set, the four
defect injections, and the exactly-once-layout verdict in detail.

`TextField` can now **edit any well-formed UTF-8, by whole grapheme
cluster** - 4-9's printable-ASCII-only `filter_ascii()` is gone.
Left/Right/Home/End, Backspace/Delete, selection and click-to-position all
move and delete by user-perceived character rather than by byte: a ZWJ
family emoji (`👨‍👩‍👧‍👦`, seven codepoints), a skin-tone-modified emoji and a
regional-indicator flag pair each vanish in exactly one Backspace, matching
design.md's own named acceptance example. The seam this needed was NOT
`dg::Paragraph`'s own clustering - measuring it directly found
`skia::textlayout::Paragraph::getGlyphClusterAt()` clusters by SHAPING
outcome, splitting a ZWJ sequence into one cluster per codepoint whenever
the active font has no ligature glyph for it, which a `TextField`'s content
font cannot be guaranteed to have. Cursor movement is instead built on a
new, font-independent primitive, `dg::grapheme_boundaries()`
(`SkUnicode::computeCodeUnitFlags()`, the same libgrapheme backend 7-1
proved and 7-2 already links), while `dg::Paragraph` gained exactly one new
method, `caret_x()`, for the pixel-position half. Malformed UTF-8 at the
editing boundary - a lone continuation byte, a truncated lead, an overlong
encoding, a surrogate - is repaired to U+FFFD via `dg::sanitize_utf8()`,
7-2's own `paragraph_build.cpp` substitution promoted into a header so both
consumers share one policy rather than two. `ellipsize()` was rewritten to
truncate at a grapheme boundary rather than a byte offset, with hand-derived
exact-string assertions (not a looser "ends in an ellipsis" check) - 4-9's
own recorded "assertion-too-weak" failure mode, re-verified not to recur by
re-injecting the identical off-by-one bug and confirming the new assertions
catch it. `measure_ascii_width()`/`ascii_offset_at_x()` are deleted outright,
their shaping-aware replacements measured to agree with them exactly on
every ASCII case 4-9 already hand-derived. Typing still costs a repaint and
never a relayout - re-measured, not assumed, under CJK input specifically,
confirming 4-9's fixed-width condition still holds unchanged for non-ASCII
content. `dg::ByteOffset`/`Utf16Offset`/`GraphemeIndex` (design.md's
strong-typed index spaces) were deliberately NOT built: every Skia call
this slice's editing surface needs turned out, measured against the linked
archive, to already be UTF-8-byte-offset-native, so there was no second
index space for the type system to guard against confusing with the
first - the day a caller needs Skia's UTF-16-native API, that is what would
justify it. Zero new node/`RenderObject`/`WidgetKind` kinds were needed - a
seventeenth consecutive slice. `doc/text-input.md` and `doc/text-layout.md`
both carry cross-reference sections recording the decision without editing
either document's original text.

`TextField` can now **compose Chinese (and any other IME's) input, not
just receive it already committed** - reading `SDL_EVENT_TEXT_EDITING`,
the composition preview 4-9 named as P7's own job and left deliberately
unread. A not-yet-committed preedit string is spliced inline at the point
composition began and shown underlined - `composition_underline`, a
fourth plain positioned child the same shape `caret`/`selection_highlight`
already are, not a new paint primitive - and never touches the committed
model until a real commit reaches the exact same `text_field_insert()`
every other keystroke already goes through. A genuine platform finding,
not an assumption: on this project's own development machine, with a
real, correctly-configured IME (fcitx5 + rime) actually installed and
running, composing real pinyin through it never sends this engine a
composition event at all - the IME draws its own real, separate X11
window instead, positioned using exactly the caret rectangle
`start_text_input()` already reports (confirmed causally: moving that
rectangle moves the IME's own window one-for-one). Building a second,
redundant candidate-window UI was therefore declined by name rather than
half-built past a platform behaviour this project does not control. The
composition-preview code path itself is tested by driving a real,
synthesized `SDL_EVENT_TEXT_EDITING` through the actual SDL event queue
(`WindowManager::post_text_editing()`, the same synthetic-injection shape
`post_text_input()`/`post_pointer_button()` already are) - proven to be
read correctly, but honestly NOT proven end-to-end against a live
composing IME, which no run performed for this slice ever observed. SDL's
own documented unit for the event's cursor/length ("UTF-8 characters") is
a third offset convention this project had not measured before, distinct
from both halves 7-2b already found on Skia's own editing surface - one
small, narrow conversion function was enough, not the general strong-typed
index-space system design.md sketches, because one real caller needed
exactly one seam. Composing costs zero relayout across several different
preedit lengths in a row - the harder case than committed text, whose
length changes only on insert/backspace - re-measured rather than assumed.
Zero new node/`RenderObject`/`WidgetKind` kinds were needed - an
eighteenth consecutive slice. `doc/ime.md` records the full investigation,
including the real X11 window measurement, the honest testing-gap
statement, and a defect-injection campaign that found one injection causes
an actual crash rather than merely a wrong answer.

`TextField` and every other interactive control can now be **reached and
driven by keyboard alone** - Tab/Shift-Tab, wrapping, and a visible focus
ring. `dg::focus_order()` needed no third tree to compute a DOM-shaped Tab
sequence: `RenderTree`'s own `children()`/`parent()` (6-3's own addition)
already are the tree Tab order and a popup's own focus boundary both walk,
so the one new piece of state is a single optional scope-root `NodeId`
inside `dg::Focus` itself - the identical "smallest structure that earns
its place" argument this project's own `WidgetSet` and `ThemeBindings`
already made against a fourth or fifth table. An explicit
`Widget::tab_index` override (HTML's own `tabindex` semantics: positive
values lead, ascending; unset/zero follow in tree order; negative values
are focusable by a click but never a Tab stop) was built deliberately
rather than left to tree order by accident, and a scene mixing
`kButton`/`kCheckbox`/`kSlider`/`kTextField` with one non-focusable widget
sandwiched between two focusable ones and one sibling whose Tab position
reverses its tree position is what `examples/21_focus` demonstrates and
verifies. Crossing into a popup turned out to need no new cross-window
machinery: a native popup already forces its own separate `RenderTree`
(5-2), so it gets its own separate `dg::Focus` too, and two independent
`Focus` instances never share a `NodeId` numbering space to confuse; an
overlay popup shares the host's own `RenderTree` and `Focus`, scoped by
`enter_scope()`/`exit_scope()` so Tab cannot leak out of it, with
`exit_scope()` blurring a still-focused widget when the popup closes - the
exact popup-close hazard this project's own append-only `RenderTree`
(nothing is ever removed, only clipped to empty) made real. Two further
hazards this slice owns rather than assumes fixed: a focused widget in a
virtualized `kList` whose pool slot gets recycled to a different logical
item is blurred rather than left pointing at the wrong item's content, and
Tab-ing away from a `TextField` mid-IME-composition cancels the
composition end to end (re-verified through a real posted `SDLK_TAB`
against a synthesized composition event, not assumed still correct from
7-3). A focus change costs a repaint and never a relayout, measured after
13 Tab/click-driven transitions in a row. Zero new node/`RenderObject`/
`WidgetKind` kinds were needed - a nineteenth consecutive slice. `doc/
focus.md` records the full decision set, including a real bug this
slice's own build caught (an overlay popup's buttons must be attached to
the HOST's `WidgetSet`, not a second one, for Tab order to ever see them)
and a second one caught wiring Tab's own side effects (calling
`dg::Focus::set()` a second time to derive a `FocusChange` after
`focus_next()` had already performed the transition silently reported a
no-op and skipped every side effect).



The eventual target is Linux and Windows desktop, with macOS, Android and iOS
deferred. Only Linux is wired into the build, and the window manager is SDL3
on Linux with no conditional compilation for anything else - a second platform
will be measured before it is abstracted over.

## Build prerequisites

- CMake >= 3.24
- Ninja
- clang or gcc with C++20 support
- FreeType development headers (`libfreetype-dev`)
- fontconfig development headers (`libfontconfig1-dev`) - new as of the
  libskia2 dependency switch (`doc/skia-dependency.md`); see below for why
  this is a *build*-time requirement without being a reversal of the
  "no fontconfig for font selection" decision this section already recorded

`libskia.a` references `SkTypeface_FreeType` unconditionally, so FreeType is
required even though this phase draws no text.

The Skia distribution is `BBDXF/libskia2` (`cmake/FetchSkia2.cmake`), a
purpose-built prebuilt Skia for self-drawn GUI frameworks; `doc/skia-
dependency.md` records the full switch from the previous rust-skia-based
setup, including why the golden-image hash did not change.

FreeType and fontconfig are external libraries, and fontconfig is a build-
time-only one: `skia2Config.cmake` requires `libfontconfig` and its headers
to configure the link on Linux (Skia's own `BUILD.gn` never vendors it), but
`SkFontMgr_New_FontConfig` is still not called anywhere in this codebase -
`src/render/font_catalog.cpp` uses `SkFontMgr_New_Custom_Directory`
exclusively - deliberately, for the same reason recorded below: fontconfig's
per-language answers come from `/etc/fonts` on the host, so glyph selection
would have become a property of the machine rather than of the program.
drawgui builds the chain itself instead. Measured proof the property
survived the dependency switch: `ldd` on every drawgui binary shows **no**
runtime dependency on `libfontconfig` at all, because nothing in this
codebase's object files references an `Fc*` symbol and the linker's
`--as-needed` default drops the unused `DT_NEEDED` entry. `doc/font-
fallback.md` records the measurements, the cost, and what would force the
other choice; `doc/skia-dependency.md` section 6 records the fontconfig
build-vs-runtime distinction in full.

The prebuilt includes the Ganesh GL backend, but no OpenGL development package
is needed: the build ships `GrGLMakeNativeInterface_none`, so every GL entry
point is resolved at runtime through a proc loader the caller supplies, and
nothing links against libGL.

## Building

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

The first configure downloads libskia2's release tarball (one archive per
platform, carrying all 7 static libraries, the full header tree and a
generated CMake package) into `third_party/skia-prebuilt/`, verified by
SHA256 against both the hash libskia2 itself publishes and a copy pinned in
`cmake/FetchSkia2.cmake` - see `doc/skia-dependency.md` section 10 for why
both checks run rather than trusting the published sidecar alone.

It also downloads the doctest single header into `third_party/doctest-<version>/`,
verified by SHA256 on every configure. Building without network access is
possible once both are present; `-DDRAWGUI_BUILD_TESTS=OFF` skips doctest
entirely.

Render a frame:

```sh
./build/examples/drawgui_render_png out.png
```

## The multi-window demo

`examples/01_sdl3_multi_window/main.cpp` opens three windows at once, each a different size
and flat colour, and exits when the last one is closed. Closing any one of
them leaves the others running.

```sh
./build/examples/drawgui_multi_window
```

It needs a display, so there is no CTest entry for it - a GUI test would fail
on every headless machine, and the only way to keep it green would be to stop
asserting anything. The target is still built wherever SDL3 is present, so it
cannot rot uncompiled.

For a run that needs no human, `--auto-close-ms N` asks for one window to
close every N milliseconds. It goes through the same close-request path the
window manager's close button does, so the scripted sequence exercises the
real code rather than a shortcut around it:

```sh
./build/examples/drawgui_multi_window --auto-close-ms 700
```

SDL3 is found through `pkg-config sdl3`. Without it the window manager and
this demo are skipped and everything else still builds; the configure output
says which way it went.

## The Skia CPU gallery

`examples/02_skia_cpu_gallery` draws sixteen labelled panels covering
geometry, stroking, dashes, anti-aliasing, transforms, clipping, text
(including measurement and CJK), compositing, gradients, blur, drop shadow and
image decoding - all rasterized on the CPU and blitted to the window.

```sh
./build/examples/drawgui_skia_cpu_gallery              # resize it; it re-renders
./build/examples/drawgui_skia_cpu_gallery --bench          # offscreen timings
./build/examples/drawgui_skia_cpu_gallery --bench-present  # raster vs presentation
./build/examples/drawgui_skia_cpu_gallery --dump-png out.png
```

It is the one example that links Skia directly, because its subject is Skia:
it exists to show what the rasterizer does and to measure it, so that layout
and widgets can be designed against real costs. It is not a template for
application code, and it is deliberately not a golden-image baseline - it
draws system fonts, so its output is a property of the host.

The headline result, in Release on an i5-1145G7: a full-window repaint at
1080p is 3.88 ms of rasterization plus 7.71 ms of presentation, while the
same scene under a 260x72 damage rectangle is 0.12 ms and does not grow with
resolution. See `doc/cpu-raster-findings.md`.

## The font fallback demo

`examples/06_font_fallback` draws Latin, Greek, Cyrillic, Hebrew, Arabic, Han,
Kana, Hangul, Georgian, Armenian, symbols and colour emoji - all from **one
named family**, `DejaVu Sans`, with no family named per script. A last row
carries a codepoint nothing on the machine has, so what a missing glyph looks
like is visible rather than theoretical.

Below it, the same four Han characters are drawn twice, differing only in their
BCP 47 language tag, and select two different faces.

```sh
./build/examples/drawgui_font_fallback
./build/examples/drawgui_font_fallback --verify-fallback         # headless check
./build/examples/drawgui_font_fallback --dump-png fallback.png
```

The honest caveat, which the demo prints on itself: this machine has no
Japanese font, so the `ja` panel draws Japanese text in a Chinese face. What is
proven is the routing, not the typography. There is also no shaping, no BiDi
and no line breaking - Arabic renders in isolated forms - because those need
HarfBuzz and ICU, which `doc/font-fallback.md` explains are deliberately still
out.

## The sizing demo

`examples/09_sizing` puts each half of the second sizing stage on a row whose
behaviour the window's own size drives: a toolbar of three buttons with the
same base and different `shrink` weights, two thumbnails that derive their
width from the height their row hands them, four declared bases that stop
fitting, and a footer whose items reach the right edge only because its row
fills the main axis.

```sh
./build/examples/drawgui_sizing                  # resize it, in both directions
./build/examples/drawgui_sizing --size 620x700   # open already in deficit
./build/examples/drawgui_sizing --verify-sizing  # headless check
./build/examples/drawgui_sizing --dump-png out.png
```

## The scrolling demo

`examples/10_scrolling` draws two independent viewports out of the same
mechanism: a vertical list of 24 chips inside a `scroll_axis: vertical` leaf,
and a horizontal strip of 14 wider chips inside a `scroll_axis: horizontal`
one. Both overflow their viewport - that is the point - and both clip through
the same `overflow` this project already had. Wheel over either scrolls it;
click-drag inside one grabs its content; both clamp at their content's edges
rather than overscrolling past them.

```sh
./build/examples/drawgui_scrolling                       # wheel or drag either strip
./build/examples/drawgui_scrolling --scroll-vertical 300  # open pre-scrolled (offscreen modes)
./build/examples/drawgui_scrolling --verify-scrolling     # headless check
./build/examples/drawgui_scrolling --dump-png out.png
```

## The form controls demo

`examples/11_form_controls` draws a checkbox, two independent radio groups
(three options and two options, sharing group ids 1 and 2) and two sliders -
one that widens with the window, one fixed-width and stepped. Clicking a
radio option selects it and clears every other option in its own group,
never the other group; dragging either slider's track moves its thumb
continuously and clamps at both ends.

```sh
./build/examples/drawgui_form_controls                        # click/drag it
./build/examples/drawgui_form_controls --preset-radio-a 1      # open with an option selected
./build/examples/drawgui_form_controls --preset-volume 72      # open with a slider dragged
./build/examples/drawgui_form_controls --verify-form-controls  # headless check
./build/examples/drawgui_form_controls --dump-png out.png
```

## The text input demo

`examples/12_text_input` draws two single-line `TextField`s: `field_a`
pre-filled with a string wider than the field, showing an ellipsis-truncated
prefix while unfocused and the full string scrolled to keep the caret visible
while focused; `field_b` empty, for typing from scratch. Click a field to
focus it and place the cursor there; type to insert; Left/Right/Home/End move
the cursor and, held with Shift, extend a selection; drag to select with the
pointer; Backspace/Delete edit; clicking the other field (or empty space)
blurs the current one. Both fields accept arbitrary well-formed UTF-8, by
whole grapheme cluster (7-2b) - `--verify-text-input`'s own headless check
types CJK text and a ZWJ family emoji into `field_b` and confirms a single
Backspace removes exactly one character each time. Field b can also show an
in-progress IME composition (7-3) - a not-yet-committed preedit string
spliced inline and underlined, never touching the committed model until a
real commit arrives; Escape cancels it without committing anything.
`--preset-compose-b` shows this deterministically; `doc/ime.md` records
that a real IME on this project's own development machine draws its own
composition window rather than sending this engine a preview at all, so
the composition CODE PATH is exercised through a synthesized event instead
(see `--script` below).

```sh
./build/examples/drawgui_text_input                          # click/type/select it
./build/examples/drawgui_text_input --preset-field-b TEXT     # open field b pre-filled
./build/examples/drawgui_text_input --preset-focus-a          # open with field a focused
./build/examples/drawgui_text_input --preset-select-a         # open with a selection in field a
./build/examples/drawgui_text_input --preset-compose-b        # open field b mid-IME-composition
./build/examples/drawgui_text_input --verify-text-input       # headless check
./build/examples/drawgui_text_input --dump-png out.png
./build/examples/drawgui_text_input --script                  # real click/type/key + a synthesized
                                                               # SDL_EVENT_TEXT_EDITING through SDL's queue
```

## The image demo

`examples/13_image` draws five panels, static: `fill`/`contain`/`cover`/`none`
each paint the same synthesized 64x64 source (four flat quadrants, generated
in-process and PNG-encoded/decoded through the real codec path - no checked-in
image asset) at a box shaped so that fit mode's own arithmetic is visibly
distinct from the others; the fifth carries no source at all and paints the
configured placeholder colour instead - never a hole.

```sh
./build/examples/drawgui_image                     # resize it
./build/examples/drawgui_image --verify-image      # headless check
./build/examples/drawgui_image --dump-png out.png
```

## The list demo

`examples/15_list` draws a virtualized, 1000-item vertical list behind a
fixed pool of 14 real nodes. Every item's fill, label and image cycle on
three independent periods (12/none/3), so a recycling bug that leaves a
node's previous content behind is visible rather than invisible; scrolling,
jumping and scrolling back all recycle the same pool.

```sh
./build/examples/drawgui_list                      # wheel or click-drag the panel
./build/examples/drawgui_list --jump 500            # open pre-scrolled to item 500
./build/examples/drawgui_list --verify-list        # headless check
./build/examples/drawgui_list --bench [N]          # measured against the pre-virtualization
                                                    # baseline at N items (default 1000)
./build/examples/drawgui_list --dump-png out.png
```

## The complex properties demo

`examples/16_complex_properties` draws three panels, each built through the
dedicated-setter channel rather than by writing node style fields directly:
a linear gradient (three stops, red/green/blue); a hard-edged drop shadow
(no blur, so the sliver it casts past the panel's own right and bottom edges
is one solid colour rather than a soft one, byte-exact and hand-derivable);
and a decoded two-colour image attached through the channel's id-based
`dg::set_image()` rather than the plain C++ call a prior slice already
proved. A fourth call, `dg::set_transform()`, is made once and its declined
result printed - there is no fourth panel, because there is nothing yet to
paint.

```sh
./build/examples/drawgui_complex_properties                          # resize it
./build/examples/drawgui_complex_properties --verify-complex-properties  # headless check
./build/examples/drawgui_complex_properties --dump-png out.png
```

## The animation demo

`examples/17_animation` draws three panels, each a real client of
`dg::AnimationEngine`: `slide` explicitly animates a chip's `left` back and
forth, pausable/reversible/cancellable through its returned handle; `hover`
declares an implicit transition on `background_color` and retargets smoothly
if the pointer leaves mid-fade; `caret` blinks a text cursor through four
chained one-shot animations, launched off each other's completion events.
`--idle-probe-ms N` blocks in the window loop for `N` milliseconds with
nothing animating and reports the process CPU time actually consumed - the
measured answer to design.md's "wait_events() blocks, CPU 0%" claim.

```sh
./build/examples/drawgui_animation                       # resize it; hover the middle panel
./build/examples/drawgui_animation --reduced-motion       # the slide finishes in one frame; the caret freezes solid
./build/examples/drawgui_animation --idle-probe-ms 2000   # measure idle CPU over a real block
./build/examples/drawgui_animation --verify-animation     # headless check
./build/examples/drawgui_animation --dump-png out.png
```

## The theme demo

`examples/18_theme` draws four panels, none carrying a literal colour or
radius: every fill, border and corner radius is a `dg::bind_token()` binding
against the shipped `themes/builtin/theme.json`. Click anywhere to switch
light/dark at runtime - `dg::ThemeBindings::apply()` re-resolves every
binding and repaints, with no widget tree rebuild and (measured, printed on
every switch) zero relayout for this scene's colour-only bindings.

```sh
./build/examples/drawgui_theme                      # click to switch light/dark
./build/examples/drawgui_theme --verify-theme        # headless check
./build/examples/drawgui_theme --dump-png out.png
```

## The C client demo

`examples/19_c_client` is a genuinely pure C program (compiled by a C front
end, not a C++ one told to accept `.c` files) against `drawgui.h` alone -
design.md's own acceptance bar for the C ABI, met literally: it opens two
windows, each with a clickable button, and clicking either sets the OTHER
window's background colour through `dg_node_set_prop`, driven by the
`DG_EVENT_CLICK` event the click itself produced.

```sh
./build/examples/drawgui_c_client                     # click either button; close both to exit
./build/examples/drawgui_c_client --verify-c-client   # headless check (SDL_VIDEODRIVER=dummy)
```

## The multiline text demo

`examples/20_multiline_text` draws five panels against real system fonts
(`/usr/share/fonts`, the same default `examples/06_font_fallback` scans):
a long English sentence wrapped at ordinary word breaks; sixteen-plus
UNSPACED Chinese characters wrapped purely by UAX#14's rule table (there is
no space to fall back on); Latin, Chinese and a colour emoji mixed in one
run of text through the same zero-fontconfig font-fallback chain 6-1 built;
Arabic embedded in Latin, its run reordered right-to-left by BiDi while the
panel itself (and the whole window) stays strictly left-to-right; and the
same English sentence again, truncated to two lines with an ellipsis. Every
panel's height is computed by `dg::Paragraph::build()` before its node
exists - `LayoutTree` never measures a byte of text.

```sh
./build/examples/drawgui_multiline_text                              # resize it
./build/examples/drawgui_multiline_text --font-dir DIR                # scan a different font directory
./build/examples/drawgui_multiline_text --verify-multiline-text       # headless check
./build/examples/drawgui_multiline_text --dump-png out.png
```

## The focus demo

`examples/21_focus` draws a mixed-kind scene deliberately shaped so a
wrong Tab order is visibly wrong: `btn_open` (kButton) - a non-focusable
label - `checkbox` (kCheckbox) - `slider` (kSlider) on one row;
`textfield` (kTextField) - `reversed` (kButton, `tab_index=1`) - `inert`
(kButton, `tab_index=-1`) on the second. Tab/Shift-Tab cycles focus
through `[reversed, btn_open, checkbox, slider, textfield]` and wraps at
both ends; `inert` is reachable by a click but never by Tab. Clicking
`btn_open` opens a popup through `PopupHost` - `--branch native|overlay`
forces which branch, matching `examples/14_popup`'s own precedent - and
Tab is confined inside it until it closes.

```sh
./build/examples/drawgui_focus                        # Tab/Shift-Tab it; click "open popup"
./build/examples/drawgui_focus --branch overlay        # force the overlay popup branch
./build/examples/drawgui_focus --verify-focus          # headless check
./build/examples/drawgui_focus --dump-png out.png
```

## The opacity demo

`examples/08_opacity` draws four panels. The first two carry **the same three
overlapping chips** and the same amount of translucency, asked for in the two
different ways: the left one gives each chip an alpha of 128/255, the right one
leaves the chips opaque and fades the group. The left panel shows five bands,
because the overlaps blend; the right shows three, because the group resolves
its overlaps before it fades. The third panel nests two fades, and the fourth
animates one.

```sh
./build/examples/drawgui_opacity                    # resize it; hover the panels
./build/examples/drawgui_opacity --fade-ms 2500     # watch one panel fade
./build/examples/drawgui_opacity --freeze-at 0      # invisible, still clickable
./build/examples/drawgui_opacity --verify-opacity   # headless check
./build/examples/drawgui_opacity --dump-png out.png
```

## Unit tests

`ctest` runs `drawgui_unit_test`, a doctest binary covering `dg::Expected`,
the golden-image comparator, damage, layout, clipping, compositing, hit
testing, interaction, UTF-8 decoding, font fallback, text-field editing,
multi-line paragraph layout and focus (Tab order, scopes, the ring). It
can also be run directly for per-case output:

```sh
./build/tests/drawgui_unit_test
```

## Golden-image tests

Rendering is compared against committed PNG baselines pixel by pixel, at zero
tolerance. CPU raster output is deterministic - it is byte-identical across
gcc and clang - so any difference is a real change rather than driver noise.

A failing comparison writes `<scene>.actual.png` and `<scene>.diff.png` into
`build/tests/golden-output/`. To accept an intended rendering change, inspect
the diff, then regenerate:

```sh
cmake --build build --target golden_update
```

`ctest` never regenerates baselines. A suite that can rewrite its own
expectations proves nothing.

## Documentation

The full design document lives at `doc/design.md` (written in Chinese).

Findings and decisions from each slice live beside it:

| Document | Subject |
| --- | --- |
| `doc/cpu-raster-findings.md` | what CPU rasterization costs, and where it stops being enough |
| `doc/damage-repaint.md` | partial repaint, and why rounded corners are a damage-granularity decision |
| `doc/layout.md` | incremental layout, and the integer-device-pixel deviation |
| `doc/widgets.md` | why there is no widget tree |
| `doc/font-fallback.md` | why the fallback chain is built here rather than delegated to fontconfig |
| `doc/properties.md` | the property system: reconciliation, boundary shape, and the gap report |
| `doc/wrapping.md` | the wrapping arrangement, `align_self`, and per-side borders |
| `doc/clipping.md` | `overflow`, and how a rounded clip composes with the anti-alias slack rule |
| `doc/compositing.md` | `opacity` as group opacity, why a layer is not damage-atomic, and what `shadow` and `transform` still need |
| `doc/sizing.md` | `basis`, `shrink`, `main_size`, `aspect_ratio`, and why a second sizing stage needed no second measurement |
| `doc/scrolling.md` | `scroll_axis`, the runtime scroll offset, why scrolling costs a repaint and never a relayout, and what design.md's roadmap asks for that needs an animation clock this project does not have yet |
| `doc/form-controls.md` | radio as a checkbox field, a slider needing no new RenderObject, why dropdown is declined and what its prerequisite is, and a real `LayoutTree` sizing constraint found while building it |
| `doc/text-input.md` | a single-line `TextField`, why ASCII scoping satisfies design.md's grapheme-cluster requirement by construction, the IME hook as real plumbing rather than a placeholder, and the exact condition under which typing would force a relayout; section 10 (7-2b, append-only) records the ASCII scoping being superseded once libgrapheme made grapheme-cluster segmentation real |
| `doc/development.md` | adding a property, the ABI lock, and running the sanitized suite |
| `doc/completeness.md` | the phase-closing audit against design.md's MVP-8 widget list, the acceptance-criterion re-check, the consolidated decline and contradiction tables, and the qualified completeness verdict |
| `doc/image.md` | the `Image` node-model decision (a `NodeStyle` field, not a new kind), the mandatory size-before-decode rule and its measured no-relayout property, the synthesized-source solution to the golden-test problem, and what a future async decode would and would not change |
| `doc/list.md` | the `List` virtualization decision (`kList`, an 8th `WidgetKind`, a fixed recycled pool rather than a new node kind), why node removal was evaluated and not added, the data-source seam that needed no interface, the measured no-relayout property extended to recycling, and the 1000-item measurement against a real pre-virtualization baseline |
| `doc/complex-properties.md` | the dedicated-setter channel (`dg::set_gradient`/`set_shadow`/`set_image`/`set_transform`), why `image_source` was built first as the prototype rather than last, the damage-atomicity argument that lets `shadow` paint outside a node's declared bounds without breaking partial repaint, and the `transform` decline with its three named blockers |
| `doc/animation.md` | `dg::AnimationEngine` - the clock's value-based seam and why it needed no `virtual`, why `curve_id` is a plain constant set rather than generator-backed, the generation-counter handle lifetime and why 5-3's "never free" precedent does not transfer, the retarget-mid-transition proof, the measured idle-CPU number, and the honest §5.15.2 three-level-invalidation gap report |
| `doc/theme.md` | The theme token system - `themes/schema.toml`'s generator family reused from `props/`, the JSON-parser decision (hand-rolled vs. nlohmann/json), `$token` live references as a `WidgetSet`-shaped side table rather than a generation-counter handle, the measured colour-only-vs-int-token relayout cost, `dg::Expected`-based load errors naming the exact JSON key path, and what CI's `tools/check_consistency.py` verifies |
| `doc/abi.md` | The C ABI - `abi/drawgui.def.toml`'s generator family (reusing the props generator directly), how the generated try/catch wrapping is made provably uniform and how its removal was shown to crash rather than silently do nothing, why handle validation is append-only rather than AnimHandle's generation-counter shape, the two real engine gaps (no insertion-order or removal primitive) the ABI sketch does not admit to, and everything declined by name (theme ABI, animation ABI, callback events, QuickJS stubs) |
| `doc/skia-dependency.md` | The libskia2 dependency switch (P7 7-1) - why the golden-image hash is unchanged and why that is credible rather than merely convenient, the empirical proof SkParagraph/SkUnicode link and initialize, the design.md §12 open-question-5 and §5.10.5-vs-§5.13.6 settlement (libgrapheme carries no `icudtl.dat` at all, verified rather than taken from the README), the fontconfig build-time-vs-runtime distinction, the newly-available-but-unwired capability inventory (SVG/WebP/GIF/Ganesh-GL/Windows), and the measured binary-size and clean-build-time deltas |
| `doc/text-layout.md` | Multi-line `SkParagraph` layout, CJK/BiDi/mixed-script display (P7 7-2) - why `TextField`'s ASCII editing surface stays untouched (7-2b named as the follow-up), why `LayoutTree` gained zero text knowledge and the exactly-once-layout verdict this bought, why the golden PNG hash held for a structural reason confirmed by injection rather than an accident, the real hang found feeding ill-formed UTF-8 to `SkParagraph` and its fix, and reusing 6-1's font-fallback chain instead of `SkParagraph`'s own (broken, on this project's font manager) search; section 14 (7-2b, append-only) records that the follow-up landed and corrects section 10's prediction about `getGlyphClusterAt()`; section 15 (7-3, append-only) records that IME composition landed reusing 7-2b's grapheme seam unchanged, and the one narrow offset-unit question it raised |
| `doc/ime.md` | IME composition (P7 7-3) - what SDL3 3.x actually delivers (`SDL_TextEditingEvent`/`SDL_TextEditingCandidatesEvent`) versus what design.md/4-9 assumed; the empirical finding, on this project's own development machine with a real running IME (fcitx5+rime), that the candidate/composition window is drawn by the platform itself and positioned using the caret rectangle 4-9 already reports (measured causally, twice); why a candidate-list UI is declined by name rather than duplicated; the synthetic-event testing answer (`WindowManager::post_text_editing()`) and the honest, explicit gap between it and a genuine composing IME; the one narrow byte-offset-unit conversion SDL's own "UTF-8 characters" convention forced, and why it did not reopen design.md §5.13.2's general type-index-space question; the re-measured relayout verdict under a preedit that changes length on every keystroke; and a defect-injection campaign that found one injection causes an actual crash rather than merely a wrong answer; section 14 (7-4, append-only) records that a window-level focus change now ends an in-progress composition, satisfying section 11's own named dependency |
| `doc/focus.md` | Tab order and the focus tree (P7 7-4) - why no third tree was needed (`RenderTree`'s own `children()`/`parent()` already are the tree Tab order and a popup's own focus boundary walk, so the only new state is one optional scope-root `NodeId`); `dg::focus_order()`'s DOM-shaped default and the deliberate `Widget::tab_index` override (HTML's own tabindex semantics); which `WidgetKind`s are focusable and why `kScrollView`/`kList` are declined by name rather than overlooked; why Tab reaching a zero-opacity widget is the CONSISTENT reading of 4-5's own hit-test divergence, not a second one; `enter_scope()`/`exit_scope()` as the smallest mechanism that serves a popup's Tab boundary without being `Dialog`'s modal trap (7-5's own job); why crossing into a popup needed no `NodeId`-plus-`window_id` struct (a native popup gets its own separate `Focus` for its own separate `RenderTree`; an overlay popup shares the host's, scoped); the list-recycling and mid-composition-Tab-away hazards, both handled and tested rather than assumed already safe; the ring's four-strips-outside-the-bounds geometry and why a single rectangle would have silently made a focused widget unclickable; the measured zero-relayout finding; and two real bugs this slice's own build caught (an overlay popup's buttons needing the HOST's `WidgetSet`, and a double-`set()` call that silently skipped every focus-change side effect) |
