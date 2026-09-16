# Multi-line paragraph layout, CJK, shaping and BiDi (7-2)

7-1 proved `SkParagraph`/`SkUnicode(libgrapheme)`/`SkShaper` link and
initialize (`doc/skia-dependency.md`). This slice is the first thing that
*consumes* them: `TextStyle::wrap` routes a node's text through a real
`skia::textlayout::Paragraph` instead of the single `SkFont::measureText` +
`SkCanvas::drawSimpleText` call every node has used since slice 2, and it is
the thing `doc/text-input.md`'s ASCII-single-line scoping decision named as
its own successor.

## 1. Scope: the substrate and display path, not the editing path

The task itself asked for a scope judgement up front, and this is it, made
explicit rather than discovered by omission:

**Built in this slice (items 1-5 of the task)**: multi-line `SkParagraph`
layout and reflow, UAX#14 line breaking via libgrapheme, HarfBuzz shaping
(free - it is what `SkShaper` inside `SkParagraph` already does for every
run), BiDi display (also free at the `Paragraph::paint()` level - BiDi
reordering is internal to how `SkParagraph` lays out a line, not something
this project's code performs), and CJK end-to-end through the real
font-fallback chain 6-1 already built.

**Named as a follow-up, not started**: grapheme-cluster cursor movement
(item 6) and revisiting `TextField`'s ellipsize/horizontal-scroll for
non-ASCII (item 7). Both are `TextField`-EDITING concerns - `Widget::cursor`,
`selection_anchor`, `text_field_insert/backspace/delete_forward/move/click`
in `src/widget/widget_set.cpp` - and none of that code was touched here.
7-2's whole surface is `TextStyle`/`RenderTree`/a new `dg::Paragraph`
measurement API; `WidgetKind::kTextField` and everything under it is
untouched. `.omo/plans/drawgui-phase7.md` names the remainder **7-2b:
grapheme-cluster `TextField` editing** explicitly, as a separate checkbox,
because the reasoning that makes 4-9's ASCII filter safe to remove is a
different, larger piece of work than the reasoning that makes multi-line
*display* safe to add: display needs no strongly-typed byte/UTF-16/grapheme
offset arithmetic at all (a paragraph is measured, painted, and never
edited), while cursor movement needs exactly the three-index-space
discipline design.md section 5.13.2 mandates (`ByteOffset`/`Utf16Offset`/
`GraphemeIndex`, no implicit conversions) BEFORE a single line of it is
correct. Landing both in one slice would have meant either rushing the
type-safety argument or leaving half the editing surface still on 4-9's
ASCII filter while claiming grapheme correctness - neither is acceptable,
so the split is named rather than silently narrowed.

**Explicitly out, per the task**: IME composition (7-3, depends on this
slice existing first), rich-text WYSIWYG editing (`design.md` section
11), RTL UI mirroring (section 11 again, section 5.13.7 in full), vertical
writing modes, word-boundary text selection beyond what libgrapheme already
offers "for free" as a fact (noted, not wired to anything - no double-click
exists in this codebase for any widget, `TextField` included).

## 2. What replaced 4-9's ASCII boundary, and what remains of it

4-9's argument in full (`doc/text-input.md` section 1.2): scope `TextField`
to printable ASCII so that a byte offset, a UTF-8 codepoint offset and a
grapheme-cluster boundary are provably the same number, satisfying design.md
line ~1001-1003's grapheme-cluster mandate **by construction** rather than
by segmentation code.

**For `TextField` specifically, ALL of it remains** - `WidgetSet::
text_field_insert()`'s `filter_ascii()` (`src/widget/widget_set.cpp:37-55`)
is untouched, byte for byte. The premise the task called obsolete is real
(libgrapheme now provides real segmentation, proven since 7-1), but this
slice does not act on it for `TextField` - that is 7-2b, named above.

**For DISPLAY, the premise is superseded in full**: any node with
`TextStyle::wrap = true` is no longer limited to ASCII at all. Multi-byte
UTF-8, CJK, Arabic, emoji - everything `examples/20_multiline_text` draws -
goes through the real pipeline this slice built. The single-line, non-wrap
path (`paint_text()`'s original branch in `src/render/skia_paint.cpp`) is
ALSO unchanged and ALSO still ASCII-safe in the narrow sense that it was
always byte-oriented and never claimed to segment anything - it draws
whatever bytes a caller gives it, one `SkFont::measureText`/
`drawSimpleText` call at a time, with no editing operations attached to it.

So the honest one-sentence answer: **4-9's ASCII boundary is superseded for
read-only DISPLAY, in full; it remains completely intact for EDITING,
because editing is what 7-2b is for.**

## 3. The input-filter removal, and what replaced it at the boundary

The task's framing is precise and worth restating literally: 4-9 filtered
non-ASCII bytes **at a single entry point** (`text_field_insert()`'s
`filter_ascii()`), and that filter is a `TextField`-editing concern this
slice does not touch. There is therefore **no filter to remove** for this
slice's own new surface - `TextStyle::wrap`/`dg::Paragraph::build()` never
had an ASCII restriction to begin with; they accept arbitrary UTF-8 from
the day they exist. The question the task actually asks - "how is malformed
UTF-8 handled at the boundary now that a whole input class is possible" -
is real, but it applies to the display path's OWN new decode, not to a
regression at `text_field_insert()`.

**What is done, concretely**: `utf8_decode()` (`include/drawgui/base/
utf8.h`, unchanged - design.md section 5.13.3's own decoder, already used
by `font_fallback.cpp`) rejects a lone continuation byte, a truncated
multi-byte lead, an overlong encoding, a surrogate codepoint and anything
past U+10FFFF, reporting `valid = false` rather than silently repairing to
U+FFFD. `src/render/paragraph_runs.cpp` groups the string into per-family
runs using this decoder and marks a run `invalid` when every step in it
failed. `src/render/paragraph_build.cpp` then does the one substitution
this slice adds: an invalid run is never handed to `SkParagraph::addText()`
as raw bytes - it is replaced, byte for byte, with the well-formed 3-byte
UTF-8 encoding of U+FFFD (`\xEF\xBF\xBD`) instead.

**Why a substitution exists here when design.md section 5.13.3 forbids
exactly that at the ABI boundary**: the two are different boundaries. The
section 5.13.3 rule is about an ABI caller's input - reporting an error
code so the caller can react, never silently rewriting text the caller
believes was preserved. `paragraph_build.cpp`'s substitution has no error
channel to report through (paint has never had one - a missing font already
draws nothing, silently, by the same reasoning `font_catalog.h`'s own
comments record) and no caller waiting on a return value to notice; it
exists purely to keep a third-party shaping library from being handed
byte sequences it does not defend against. That distinction was not
academic - it was found by a real hang.

## 4. The hang this slice found and fixed, before it shipped

Feeding `"A\xE0"` (a truncated three-byte UTF-8 lead with nothing after it)
straight through `SkParagraph::addText()` **hangs the process** rather than
crashing it or drawing a tofu box - a strictly worse failure mode for
ASan/UBSan to catch, because a hang produces no signal at all until a test
harness's own timeout fires. `tests/unit/test_paragraph.cpp`'s malformed-
UTF-8 `TEST_CASE` reproduces this by feeding a lone continuation byte, a
truncated lead and an overlong encoding to `Paragraph::build()` directly;
before the U+FFFD substitution above existed, this test case reliably
timed out. The fix is section 3's substitution, and a defect-injection
re-run (regressing it back to raw bytes) reproduced the exact hang on
demand, confirming the fix is load-bearing rather than incidental.

A second, related defect surfaced from the SAME injection campaign and is
now a permanent regression test: the run-merging logic originally compared
only a run's resolved family name, not whether it was `invalid`. Because an
invalid step's family defaults to the PRIMARY font's own name - the same
name a valid ASCII run right beside it already carries - `"A" + <invalid
byte> + "B"` merged into ONE run whose `invalid` flag was whatever the
FIRST character (`"A"`, valid) had set. The merged run's raw bytes,
including the invalid one, were then handed to `addText()` unmodified - the
same hang, reached a different way. `paragraph_runs.cpp`'s merge condition
now compares `invalid` alongside `family`; the direct-unit-level regression
test (`tests/unit/test_paragraph.cpp`, "a valid byte immediately beside an
invalid one... does NOT merge") pins the fix at the function that owns it,
not only at the two-layers-up hang the malformed-UTF-8 `Paragraph::build()`
test already happened to catch.

## 5. The golden sha256, and why it held

`drawgui_render_png`'s output hash (`f635028e6e1e92349d23f0575f1e39e7ca8b05e5974492641ee610fe520df12e`)
is unchanged, and the reason is structural rather than a coincidence this
slice merely got lucky on:

- `TextStyle::wrap` defaults to `false`, and `paint_text()`'s new branch
  (`if (text.wrap) { paint_paragraph(...); return; }`) is entered ONLY when
  a caller sets it. No scene built through 7-1 sets it, so no existing
  `NodeStyle::text` takes the new path at all - byte for byte, the same
  guarantee `ImageStyle`/`background_gradient`/`shadow` each made for
  themselves when they were added (`doc/complex-properties.md`, `doc/
  image.md`).
- `examples/00_cpu_raster_png` (the scene the golden hash measures) draws
  no text at all - confirmed again by this slice, not merely inherited from
  7-1's own confirmation - so the question "does this scene's text now
  route through SkParagraph" does not even arise for it.

**This was verified by injection, not merely argued**: flipping
`TextStyle::wrap`'s default to `true` (so every existing text-bearing scene
would have rerouted) left the golden hash **completely unchanged** -
confirming, independently, that the golden scene itself carries no text.
The SAME injection was caught immediately and precisely by
`test_font_fallback.cpp`'s hand-derived pixel oracles (`Han unification
reaches the pixels` and `a string needing two faces is drawn with both of
them both failed on exact pixel counts a reader can check by hand -
`hans_ink.top == 0` failed at `403`, `mixed.top == latin_only.top` failed
`962` vs `546`). **The honest, reportable finding**: the zero-tolerance
golden PNG hash is the wrong gate to prove this slice did not silently
change existing text rendering - it happens to draw no text at all, so it
would stay green even if every text-bearing scene in the whole project
started rendering completely differently. The gate that actually protects
existing text rendering is `test_font_fallback.cpp`'s hand-derived pixel
oracles, and it is what this slice's own `TextStyle::wrap` default-off
design is built to keep untouched, VERIFIED by the injection above rather
than assumed from "the hash didn't move."

**Decision made and defended, per the task's own framing**: existing
single-line text keeps the old `SkFont::measureText`/`drawSimpleText` path
unconditionally. A re-baseline was never on the table, because nothing
about routing text through `SkParagraph` was forced onto scenes that never
asked for wrapping - `wrap` is an opt-in field, not a global switch, and the
`TextField`/`Label` machinery built through 4-9/6-1 keeps drawing exactly
what it always drew.

## 6. The exactly-once-layout verdict

`doc/sizing.md` section 1 is precise about why a *measured* base cannot be
turned into a flex allotment without a second layout pass, and the task
asked, directly, whether wrapped text - "the classic case that forces a
measure pass in other engines" - breaks the same invariant here. **It does
not, and the reason is structural rather than a coincidence of this
slice's own scene being simple**:

This project's `LayoutTree` (`include/drawgui/layout/box.h`) has **zero
knowledge of text**. `BoxStyle` carries no font, no string, nothing
`TextStyle` owns; `LayoutTree::Impl::measure_leaf()` sizes a node from its
CHILDREN's sizes, never from anything a `RenderTree::NodeStyle::text` field
holds. 7-2 keeps that separation rather than teaching `measure_leaf()` to
ask a `FontCatalog` a question mid-pass - the identical shape design.md
section 5.10.3 already forces on a decoded image (`doc/image.md`): **a box
must have a determinate size before its content is resolved, never the
other way round.**

Concretely, `dg::Paragraph::build(fonts, text, width)` (`include/drawgui/
render/paragraph.h`) is a measurement primitive a caller runs BEFORE
constructing (or resizing) a node - `examples/20_multiline_text/
multiline_scene.cpp`'s `wrapped_height()` is the one call site. The
resulting height is handed to `BoxStyle::height` as an ordinary declared
number, the same way an image's `aspect_ratio`-derived height or a
`Slider`'s explicit width already is. `LayoutTree::layout_full()` then runs
ONCE over the whole tree, and every node - including the text-bearing
leaves - is entered exactly once, because their heights were never
unknown to begin with.

**Measured, not assumed** (`examples/20_multiline_text --verify-multiline-
text`, `LayoutStats` from the actual demo scene, 12 nodes: one column, five
panels, five text leaves, one column-of-panels wrapper):

```
after building the scene (one layout_full() call):
  nodes_total=12 nodes_visited=12 nodes_relaid_out=12
a second, no-op layout() call:
  nodes_visited=0 nodes_relaid_out=0
```

`nodes_visited == nodes_total` on the FIRST pass is the ordinary "every
node laid out once" invariant every prior slice already measured; the
SECOND call's `nodes_visited == 0` is what proves nothing about wrapped
text secretly re-enters `LayoutTree` on a later frame either. **Verdict:
L3 holds literally, for the identical reason `doc/sizing.md` already
generalised it - "a child whose base is DECLARED does not have to be
measured to find it out" - extended from a flex allotment to a wrapped
paragraph's height.** The cost this buys is not zero: a caller now has to
run `dg::Paragraph::build()` once per panel before the node exists, which
is a real second call this project's LABEL path never needed before
(`TextStyle` used to be pure paint-time content with no bearing on size at
all). That cost is outside `LayoutTree::layout()`'s own accounting
entirely - it is application code building a scene, the same shape
`quadrants()`'s PNG encode/decode already is for `examples/13_image` - so
it does not appear in any `LayoutStats` number, and this document says so
rather than letting the "exactly once" framing imply the work vanished
rather than moved.

**What is declined as a consequence, named rather than glossed over**: a
SHRINK-TO-FIT wrapping label - one whose OWN width is not known up front,
so its content must decide both its width and (from that) its height in
one step - is not built here, for the identical reason `doc/sizing.md`
section 1.4 declines a fully general shrink from a measured base: it would
need `LayoutTree` to query an unbounded-axis intrinsic size, cached, which
this project's layout model still does not have. Every panel this slice's
demo draws has an EXPLICIT, declared width; the height is the only axis
`dg::Paragraph::build()` derives.

## 7. design.md section 5.6 line 622 (zero new RenderObject kinds)

**The streak holds. Zero new node/`RenderObject`/`WidgetKind` kinds were
needed, a sixteenth consecutive slice** (4-1 through 6-3, then 7-1, now
7-2). `TextStyle` gained two fields (`wrap`, `max_lines`); no new
`WidgetKind` enumerator, no new `LayoutKind`, no new `NodeStyle` variant.
`design.md`'s own primitive inventory (section 5.4) lists `RenderText` as
one of roughly 13 conceptual `RenderObject`s Flutter's model would carry;
this project has never had a `RenderText` type at all (`TextStyle` is a
field of `NodeStyle`, painted by a switch-free function, exactly as
`doc/widgets.md`'s "no widget tree, no vtable" argument already
established for every other kind of content) - multi-line text is content
too, painted a different way when `wrap` is set, and adds no new thing a
`switch` has to grow a case for. This was the plausible place the streak
could have broken (multi-line text being the engine's first genuinely new
SHAPE of content, per the task's own framing), and it did not.

## 8. Font selection: reusing the chain, not delegating to SkParagraph's own

`SkParagraph`'s ordinary font-fallback story asks its `FontCollection` for
`matchFamilyStyleCharacter()` on whichever `SkFontMgr` is registered - and
`doc/font-fallback.md` already measured that call returning null, always,
on `SkFontMgr_New_Custom_Directory` (the manager this project links to
keep "zero fontconfig" a real property, not merely an aspiration).
Registering it as `FontCollection`'s fallback manager and calling
`enableFontFallback()` would therefore find nothing `SkParagraph`'s own
search could not - the identical dead end 4-1 already walked into with the
plain `SkFont` path, one API surface over.

**The fix reuses 6-1's chain rather than re-deriving a second one**:
`src/render/paragraph_runs.cpp` walks the SAME `FontCatalog::resolve()`
call `font_fallback.cpp`'s plain-`SkFont` `TextRun` splitter already uses,
turning the string into per-family runs UP FRONT. Each run is handed to
`SkParagraph` naming EXACTLY ONE family (`skia::textlayout::TextStyle::
setFontFamilies({family})`), and `FontCollection::disableFontFallback()` is
called explicitly - `SkParagraph` never searches on its own, because it
never has to: every run already carries the chain's own answer. The
`FontCollection`'s manager IS registered (`FontCollection::
setAssetFontManager`, not `setDefaultFontManager` - the latter is
consulted only by `SkParagraph`'s OWN fallback search, never for an
explicit family-name lookup, a real API gotcha this slice found by
measurement rather than by reading a comment: the first draft used
`setDefaultFontManager` and every paragraph came back with zero lines),
so that an explicit family name can still be resolved to an `SkTypeface`
- `FontAccess::font_manager()` (a new one-line accessor) is what exposes
`FontCatalog`'s own `SkFontMgr` for exactly this.

**Net effect**: `examples/20_multiline_text`'s CJK/mixed/BiDi panels are
drawn through the identical zero-fontconfig chain `doc/font-fallback.md`
built - `FontCatalog::resolve()`, called once per codepoint, feeding
`SkParagraph` one family name per run - not a second, independently-built
answer that could disagree with it.

## 9. BiDi and shaping: consumed, not implemented

Neither is new CODE in this project; both are what `skia::textlayout::
Paragraph::layout()`/`paint()` already do internally once real, styled
text reaches them, exactly as 7-1's smoke test already proved at the
`SkUnicode` level (`getBidiRegions` reporting an RTL region for Arabic-in-
Latin text). `examples/20_multiline_text`'s `bidi` panel
(`"hello \u0645\u0631\u062D\u0628\u0627 world"`) is the DISPLAY consumer of
that same fact: the Arabic run reorders right-to-left WITHIN the panel's
text, while the panel itself, its siblings, and the whole window stay
strictly left-to-right - design.md section 5.13.7's "BiDi yes, UI
mirroring no" drawn on screen rather than only asserted at the `SkUnicode`
level. Shaping (ligatures, joined Arabic letterforms, correct advance
widths for combining marks) is `SkShaper`'s job inside `SkParagraph`'s own
run-building, invoked identically for every run this slice hands it -
nothing here calls HarfBuzz directly, matching the project's own
`skia_paint.cpp` precedent of never calling a lower-level Skia primitive
than the one that already does the job.

## 10. Grapheme clusters: available, not wired to editing

`SkUnicode::computeCodeUnitFlags()` (proven working by 7-1's smoke test:
the ZWJ family-emoji sequence reports 1 grapheme cluster, not 4 code
points) is the primitive `TextField`'s cursor/backspace would need to stop
being a plain `int` byte offset. This slice does not wire it anywhere -
that is 7-2b, section 1 above - but it is worth recording precisely WHAT
2 is available versus what remains missing, so the next slice does not
have to re-derive it: word segmentation (`SkUnicode::getUtf8Words()` /
the equivalent libgrapheme entry point) is ALSO available through the same
backend and is noted, per the task's instruction, as something a future
double-click-to-select-word feature could use without a new dependency -
nothing in this codebase wires it to anything yet.

## 11. What was declined, named

- **IME composition** (7-3) - untouched; `WindowManager::start_text_input()`/
  `stop_text_input()` and `SDL_EVENT_TEXT_EDITING` are exactly where 4-9
  left them.
- **Rich-text WYSIWYG editing** - design.md section 11 forbids it outright;
  rich-text DISPLAY (styled spans in one paragraph) is architecturally free
  through `skia::textlayout::TextStyle::pushStyle()`/`pop()` (this slice
  already uses it, one style per resolved-family run) but no PUBLIC API
  exposes multiple caller-chosen styles inside one `TextStyle::text` - that
  is a real, named gap for whoever wants rich-text display as a feature
  rather than as this slice's own internal per-run mechanism.
- **RTL UI mirroring** - not done, per design.md section 11 and 5.13.7;
  `examples/20_multiline_text`'s panels stay left-to-right regardless of
  their content's own directionality.
- **Vertical writing modes** - not touched anywhere.
- **Word-boundary selection beyond "available"** - noted in section 10,
  wired to nothing.
- **`TextField` grapheme-cluster cursor/selection/backspace, and revisiting
  ellipsize()/horizontal-scroll for non-ASCII** - named explicitly as
  7-2b in `.omo/plans/drawgui-phase7.md`.
- **Caching a built `Paragraph`** - every measurement and every paint
  rebuilds one from scratch (a `skia::textlayout::ParagraphBuilder` call
  plus a `layout()`), matching this project's stated performance stance for
  this phase ("性能不作硬门禁"). `skia::textlayout::FontCollection` carries
  its own internal `ParagraphCache`, unused by this slice's code since a
  fresh `FontCollection` is constructed per call; a future slice that finds
  paragraph rebuilding to be a measured cost has a documented place to add
  one.

## 12. Verification performed

- **Full CTest, once**: 30/30 (29 inherited + `multiline_text.
  verify_demo_scene`).
- **Golden sha256 unchanged**: `f635028e6e1e92349d23f0575f1e39e7ca8b05e5974492641ee610fe520df12e`,
  confirmed on gcc AND clang++ builds (section 5, including the injection
  that confirms WHY it held rather than merely that it did).
- **g++ and clang++, `-Werror`**: both clean, zero warnings. Pure-C
  `examples/19_c_client/main.c` unaffected, still compiled by `cc -x c
  -std=c11` per `compile_commands.json`.
- **`-DDG_SANITIZE=ON` (ASan+UBSan)**: full 30-entry suite green, including
  the malformed-UTF-8 `Paragraph::build()` cases that found and fixed the
  hang in section 4.
- **clang-tidy `-p build` and clang-format**: zero findings on every file
  this slice touched or added, zero `NOLINT`.
- **`props.*`/theme/ABI lock and drift tests**: unaffected, still green -
  this slice added no property and no ABI surface.
- **Defect injection, four**: (1) `max_lines`'s `> 0` guard removed -
  caught immediately by the line-count claims (`setMaxLines(0)` truncates
  to a single line rather than "unlimited", a real API gotcha found by
  injection); (2) the U+FFFD substitution disabled - reproduced the exact
  hang from section 4, confirming the fix is load-bearing; (3) the
  run-merge condition dropped its `invalid` comparison - survived at the
  direct `paragraph_runs()` unit level (closed with a new regression test)
  and was independently caught by the malformed-UTF-8 `Paragraph::build()`
  test via the hang it re-introduces two layers up; (4) `TextStyle::wrap`'s
  default flipped to `true` - the golden PNG hash did NOT catch it (that
  scene draws no text, confirmed rather than assumed), but `test_font_
  fallback.cpp`'s hand-derived pixel oracles caught it immediately and
  precisely (exact pixel-position mismatches, not a vague "something
  changed").

## 13. What this is not

No IME (7-3). No `TextField` grapheme cursor/selection/backspace (7-2b,
named in `.omo/plans/drawgui-phase7.md`). No rich-text editing (design.md
section 11). No RTL UI mirroring (section 11, 5.13.7). No vertical writing
modes. No caching of built paragraphs. No shrink-to-fit wrapping label
(section 6). No public multi-style-span API for rich-text display (section
11's named gap). No changes to the C ABI (`abi/drawgui.def.toml`
untouched - 7-2 added no property, so there is nothing new for the ABI
surface to expose yet).

## 14. Cross-reference: 7-2b landed (append-only)

Sections 1-13 above are 7-2's own record, unedited. 7-2b
(`.omo/plans/drawgui-phase7.md`) is the follow-up section 1 and section 10
named, and it has now landed: `WidgetSet::text_field_insert/backspace/
delete_forward/move/click` (`src/widget/widget_set.cpp`) are grapheme-
cluster aware, `WidgetKind::kTextField` is no longer ASCII-scoped, and
4-9's `filter_ascii()` no longer exists. `doc/text-input.md`'s own section
10 is the full cross-reference for what changed and why; the short version
this document's own section 10's "available, not wired to editing" finding
predicted correctly: `SkUnicode::computeCodeUnitFlags()` is exactly the
primitive 7-2b wired in, unchanged from how 7-1/7-2 already proved it out.

One correction to this document's own section 10, found by 7-2b rather
than assumed: `getGlyphClusterAt()` (`skia::textlayout::Paragraph`'s own
clustering, mentioned nowhere in section 10 because 7-2 never needed
per-cluster queries for display) turned out to be a SHAPING-level concept,
not a Unicode-grapheme one - it clusters by what the active font could
actually shape, not by UAX #29 boundaries, and reports one cluster PER
CODEPOINT for a ZWJ/skin-tone/flag sequence the font has no ligature glyph
for. 7-2b's cursor movement is therefore built on `SkUnicode::
computeCodeUnitFlags()` directly (a new, dedicated seam,
`dg::grapheme_boundaries()`), not on `dg::Paragraph`'s own clustering -
`dg::Paragraph` gained exactly one new method, `caret_x()`, for pixel
positions only. This distinction did not matter for 7-2's DISPLAY path
(which never asks "where is cluster N" at all), which is why it went
unrecorded here until an editing consumer needed the answer.
