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

There is also no platform abstraction, on purpose. An earlier attempt wrote
twelve abstract platform headers before any backend existed; they were removed
because nothing had ever tested whether they described the machine. The rule
now is that an interface is extracted from at least one working
implementation, never written ahead of one.

## Platform scope

The eventual target is Linux and Windows desktop, with macOS, Android and iOS
deferred. Only Linux is wired into the build, and the window manager is SDL3
on Linux with no conditional compilation for anything else - a second platform
will be measured before it is abstracted over.

## Build prerequisites

- CMake >= 3.24
- Ninja
- clang or gcc with C++20 support
- FreeType development headers (`libfreetype-dev`)

`libskia.a` references `SkTypeface_FreeType` unconditionally, so FreeType is
required even though this phase draws no text.

FreeType is the only external library, and it stays that way now that text
falls back across scripts. `SkFontMgr_New_FontConfig` is present in the
prebuilt archive - with 47 undefined `Fc*` symbols to go with it - and was
deliberately not used: fontconfig's per-language answers come from
`/etc/fonts` on the host, so glyph selection would have become a property of
the machine rather than of the program. drawgui builds the chain itself
instead. `doc/font-fallback.md` records the measurements, the cost, and what
would force the other choice.

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

The first configure downloads a prebuilt Skia static library and the matching
headers into `third_party/skia-prebuilt/`. The library is verified by SHA256
and the headers by commit SHA; a mismatch is a hard error, because headers
that disagree with the binary produce a link that succeeds and then
misbehaves at runtime.

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
blurs the current one.

```sh
./build/examples/drawgui_text_input                          # click/type/select it
./build/examples/drawgui_text_input --preset-field-b TEXT     # open field b pre-filled
./build/examples/drawgui_text_input --preset-focus-a          # open with field a focused
./build/examples/drawgui_text_input --preset-select-a         # open with a selection in field a
./build/examples/drawgui_text_input --verify-text-input       # headless check
./build/examples/drawgui_text_input --dump-png out.png
./build/examples/drawgui_text_input --script                  # real click/type/key through SDL's queue
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
testing, interaction, UTF-8 decoding, font fallback and text-field editing. It
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
| `doc/text-input.md` | a single-line `TextField`, why ASCII scoping satisfies design.md's grapheme-cluster requirement by construction, the IME hook as real plumbing rather than a placeholder, and the exact condition under which typing would force a relayout |
| `doc/development.md` | adding a property, the ABI lock, and running the sanitized suite |
| `doc/completeness.md` | the phase-closing audit against design.md's MVP-8 widget list, the acceptance-criterion re-check, the consolidated decline and contradiction tables, and the qualified completeness verdict |
| `doc/image.md` | the `Image` node-model decision (a `NodeStyle` field, not a new kind), the mandatory size-before-decode rule and its measured no-relayout property, the synthesized-source solution to the golden-test problem, and what a future async decode would and would not change |
| `doc/list.md` | the `List` virtualization decision (`kList`, an 8th `WidgetKind`, a fixed recycled pool rather than a new node kind), why node removal was evaluated and not added, the data-source seam that needed no interface, the measured no-relayout property extended to recycling, and the 1000-item measurement against a real pre-virtualization baseline |
| `doc/complex-properties.md` | the dedicated-setter channel (`dg::set_gradient`/`set_shadow`/`set_image`/`set_transform`), why `image_source` was built first as the prototype rather than last, the damage-atomicity argument that lets `shadow` paint outside a node's declared bounds without breaking partial repaint, and the `transform` decline with its three named blockers |
