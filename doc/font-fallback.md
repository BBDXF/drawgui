# Font fallback: the chain drawgui builds, and why it is not fontconfig

Measured findings from slice 4-1 — closing the hole slice 2 opened and slice 3
was shaped around:

> `SkFontMgr_New_Custom_Directory` has no fallback chain.
> `matchFamilyStyleCharacter()` returns null on it, so a family that lacks a
> glyph does not quietly borrow one from another family — it draws nothing.

Everything below is a measurement on this host (Ubuntu questing under WSLg,
23 font files under `/usr/share/fonts`, Skia m153-0.101.1 prebuilt).
`examples/06_font_fallback` reproduces the visible half;
`tests/unit/test_font_fallback.cpp` and `tests/unit/test_font_coverage.cpp`
reproduce the checkable half.

## The headline

One named family — `DejaVu Sans` — now draws Latin, Greek, Cyrillic, Hebrew,
Arabic, Han, Kana, Hangul, Georgian, Armenian, symbols, **colour emoji**, and a
visible box where nothing on the machine has a glyph. The caller names no
family per script and no family per language.

The same four Han codepoints under `language = "zh-Hans"` and `language = "ja"`
select two different faces and produce **5,940 differing pixels** in the demo's
two panels. Evidence:
`.omo/evidence/drawgui-kernel/font-fallback-han-unification.png`.

The dependency list is still exactly one: `-lfreetype`.

---

## The decision: build the chain, do not take fontconfig

Both managers are in the archive:

```
$ nm -C --defined-only libskia.a | grep -o 'SkFontMgr_New_[A-Za-z_]*' | sort -u
SkFontMgr_New_Android
SkFontMgr_New_Custom_Data
SkFontMgr_New_Custom_Directory
SkFontMgr_New_Custom_Embedded
SkFontMgr_New_Custom_Empty
SkFontMgr_New_FCI
SkFontMgr_New_FontConfig
```

`SkFontMgr_New_FontConfig` is real, and the archive carries **47 undefined
`Fc*` symbols** to go with it (`FcFontMatch`, `FcFontSort`, `FcLangSetHasLang`,
`FcCharSetHasChar`, …). Route A is not a fiction. It was rejected for three
reasons, in increasing order of weight.

### 1. It does not link here, and installing is not on the table

```
$ gcc x.c -o x -lfontconfig
/usr/bin/ld: cannot find -lfontconfig: No such file or directory
$ ls /usr/include/fontconfig
ls: cannot access '/usr/include/fontconfig': No such file or directory
$ pkg-config --exists fontconfig; echo $?
1
```

`libfontconfig.so.1` exists; the `.so` link name, the headers and the
pkg-config file do not, because `libfontconfig-dev` is not installed. This is a
practical blocker for this slice, not an argument — `apt-get install` would
dissolve it. It is listed first because it is the reason no measurement of
route A's *runtime* behaviour appears below.

### 2. An extra system dependency is a real embedding cost

This project's stated value is "a self-drawn GUI kernel that any language can
embed through a C ABI". Every external `.so` is something the host's packaging
has to satisfy — a Python wheel, a Node prebuild, a Rust crate's `build.rs`, a
static musl binary. FreeType is unavoidable (`libskia.a` references
`SkTypeface_FreeType` unconditionally; 43 undefined `FT_*` symbols even in a
build that draws no text). fontconfig is avoidable, and "one archive plus
`-lfreetype`" has been a deliberate, documented property since slice 3.

### 3. The decisive one: fontconfig's per-language answers come from the host

This is the reason that would still hold if `libfontconfig-dev` were installed.

The project's golden tests run at **zero tolerance** because CPU raster output
is byte-identical across gcc/clang and Debug/Release. No text-bearing golden
exists yet, but the moment one does, its baseline has to be a function of the
program, not of the machine. fontconfig's answer to "which font for Japanese"
is read from `/etc/fonts/conf.d`, which differs between distributions, between
releases of one distribution, and between two machines running the same release
with different packages installed.

And the alternative is not worse, because — see below — **the font metadata
cannot answer the question either.** Whichever route is taken, per-language
preference is a table. The only choice is whose.

### What would force route A later

Written down now so the next reader does not have to re-derive it:

- **A platform whose fonts are not files in a directory.** Android's font set
  is described by `/system/etc/fonts.xml`, and macOS/iOS fonts are behind
  CoreText. `SkFontMgr_New_Custom_Directory` is a Linux/Windows answer;
  `SkFontMgr_New_Android` and `SkFontMgr_New_CoreText` exist for the others and
  the chain built here would sit on top of whichever manager the platform has.
  That is a *manager* swap, not a fallback-chain rewrite.
- **A machine with thousands of fonts.** The pool here is one representative
  face per family and the walk is linear. 16 families is nothing; 2,000 would
  want fontconfig's charset index. Performance is explicitly not a gate for
  this slice, so this is a note, not a finding.
- **Fonts installed at runtime, or a user font directory that changes.**
  fontconfig watches and re-scans. The catalog here scans once, at `scan()`.
- **A host that WANTS the system's own font policy.** A desktop application
  embedding drawgui may reasonably want to honour the user's fontconfig
  preferences. That is a good reason, and it is why `set_fallback_rules()`
  exists: the host can supply the answers fontconfig would have given without
  drawgui linking it.

---

## The measurements

### `matchFamilyStyleCharacter()` really is null, re-measured

Slice 2 recorded it. Slice 4-1 re-ran it with more cases, because a whole
design was about to be built on the claim:

```
cp U+41    family=DejaVu Sans  -> null
cp U+41    family=(null)       -> null
cp U+4E2D  family=DejaVu Sans  -> null
cp U+4E2D  family=(null)       -> null
cp U+1F600 family=DejaVu Sans  -> null
cp U+1F600 family=(null)       -> null
with bcp47 = {"zh-Hans"}       -> null
```

Seven for seven. There is no fallback to borrow.

### The font's own metadata CANNOT tell Japanese from Chinese

This is the finding that decided the shape of the per-language chain, and it
contradicts the obvious plan (read OS/2 `ulCodePageRange` and derive the
language chains automatically).

Every CJK-capable face on this machine declares **all five** CJK codepages:

| family | `ulCodePageRange1` | declared languages |
|---|---|---|
| Droid Sans Fallback | `0x203F01FF` | ja(932) zh-Hans(936) ko(949) zh-Hant(950) ko-johab(1361) |
| WenQuanYi Zen Hei | `0x603E000D` | ja zh-Hans ko zh-Hant ko-johab |
| WenQuanYi Zen Hei Mono | `0x603E000D` | ja zh-Hans ko zh-Hant ko-johab |
| WenQuanYi Zen Hei Sharp | `0x603E000D` | ja zh-Hans ko zh-Hant ko-johab |
| WenQuanYi Micro Hei | `0x603E019F` | ja zh-Hans ko zh-Hant ko-johab |
| WenQuanYi Micro Hei Mono | `0x603E019F` | ja zh-Hans ko zh-Hant ko-johab |

Six faces, six identical language claims, and every one of them is a Chinese
font. `ulUnicodeRange` is no better — it names scripts (CJK Unified
Ideographs), and Han unification is precisely the case where one script needs
different glyphs.

**Therefore the language chain is a TABLE, in this repository, and fontconfig
would not have avoided that — it would only have moved the table to
`/etc/fonts`.** `FontCatalog::default_fallback_rules()` is that table; it names
fonts from Linux, Windows and macOS, and families a machine does not have are
skipped, so one table is correct everywhere without conditional compilation.
`set_fallback_rules()` lets the host replace it, which is the right shape for a
kernel that is embedded rather than installed.

The OS/2 codepage bits are therefore **not read at all** by the shipped code.
They were measured, they answered "no", and the probe was deleted rather than
kept as decoration.

### Skia enumerates families in readdir order

The directory manager returned the 16 families as `Noto Color Emoji, Noto Mono,
Noto Sans Mono, DejaVu Serif, DejaVu Sans Mono, DejaVu Sans, Ubuntu Sans Mono,
Ubuntu Sans, …` — neither alphabetical nor by path. That is filesystem order,
which is a property of the machine and not of the fonts.

**The pool is therefore sorted by family name**, and that sort is a correctness
property rather than tidiness: it is what makes the last-resort step answer the
same way on two machines carrying identical font packages.
`tests/unit/test_font_fallback.cpp` pins the sorted order of the four generated
fonts, and the generated set is deliberately one whose readdir order differs
from its sorted order (measured: `Colour, Rare, Han Ja, Han Hans, Latin`).

### 250 faces, 16 families

Enumerating every style gives 250 faces, 155 of them Ubuntu variable-font
instances with identical coverage. The pool takes **one representative face per
family** (`matchStyle(Normal)`), which is 16.

That is also a correctness decision, not only a size one. Coverage is read from
the representative and the representative is what draws, so the index cannot
disagree with the result. Selecting a weight-matched face at draw time would
reopen that gap — DejaVu Sans regular has 6,253 glyphs and DejaVu Sans Bold has
6,196, so a family chosen because the regular covered a codepoint could hand
back a bold face that does not.

**The cost, stated rather than hidden:** a bold run that falls through to the
chain is drawn in the fallback family's representative weight, not in bold.

### Colour emoji works, and needed one rule to not be broken by the primary

`Noto Color Emoji` (CBDT/CBLC, 4,124 glyphs, upem 2048) loads through
`SkFontMgr_New_Custom_Directory` and rasterizes in colour on CPU raster:
drawing U+1F600 at 64 px into a black 96x96 bitmap produced 3,702 non-black
pixels of which **3,566 were chromatic**. `SkFont::getPath()` returns nothing
for the glyph, confirming it is a bitmap strike rather than an outline.
`measureText` works. No extra code was needed.

What *was* needed is a rule about ordering. **DejaVu Sans carries a monochrome
outline for U+1F600.** With a plain "the family the caller named wins" chain,
every emoji on this machine would have rendered as DejaVu line art and the
colour font would never have been consulted. So:

> Codepoints in **U+1F000–U+1FAFF** consult colour-capable faces BEFORE the
> family the caller named.

A face is colour-capable when it carries `CBDT`, `sbix` or `COLR` — read off
the font's own tables, never matched by family name.

**The deliberate gap:** the BMP symbols that Unicode also gives an emoji
presentation — U+2714, U+2B50, U+26A1 and their neighbours — are left to the
ordinary chain. Routing them to a colour font would change how existing symbol
text renders, and doing it correctly needs the real `Emoji_Presentation`
property table, which this project does not ship. The consequence is that
`✔` and `★` render as monochrome glyphs from whatever face covers them. Variation
selectors (U+FE0F / U+FE0E) are also ignored: honouring them requires grapheme
clustering, which design.md section 5.10.4 puts behind ICU.

---

## The walk

```
1. colour-first   codepoint in U+1F000..U+1FAFF, and some face carries
                  CBDT / sbix / COLR and covers it
2. primary        the family the caller named, if it covers the codepoint
3. language chain rules matching the BCP 47 tag, most specific first,
                  families in the order the rule lists them
4. pool           every family, in sorted family-name order
5. nothing        reported as glyph 0
```

Rules are ordered most-specific-first by `set_fallback_rules()` using a
**stable** sort, so precedence is a property of the table rather than of the
walk, and two rules of equal specificity keep the caller's order.

Language matching is a **prefix match on subtag boundaries**, case-insensitive:
`zh-Hans` matches `zh-Hans-CN` but not `zh-Hant`, and `ja` does not match
`jav` (Javanese). There is deliberately **no BCP 47 canonicalization engine** —
`zh-CN` does not become `zh-Hans` by algorithm, it becomes it by being listed,
which is a table a reader can check rather than an inference they have to
trust.

`from_primary` is decided at the end of the walk by comparing the chosen face
against the primary, not by whichever step produced the hit. That was a bug the
system-coverage test found: step 1 returns a pool entry, so a caller naming the
colour emoji font itself and asking about its own coverage was told "no".

### Missing glyphs: the decision

> A codepoint no face covers is drawn as **the primary font's `.notdef` box**,
> one per codepoint.

Not nothing. "Nothing drawn" is indistinguishable from "the label is empty" and
from "I forgot to set the font", and those three need different fixes. A box is
the universally understood "this machine has no glyph for this", it is visible
on a screenshot, and it costs one branch in the paint path.

The demo carries U+10330 GOTHIC LETTER AHSA on purpose so a reader can see what
it looks like. **The first codepoint chosen for that row was U+10000 LINEAR B
SYLLABLE B008 A, and it was wrong: WenQuanYi Zen Hei covers it.** The row
rendered perfectly and `--verify-fallback` reported a clean pass over a demo
that had silently stopped demonstrating its own point. The check now REQUIRES
that row to be missing, which is what turned a passing lie into a failure.

Ill-formed UTF-8 takes the same path. design.md section 5.13.3 forbids silently
rewriting it to U+FFFD; `utf8_decode` reports `valid = false` and leaves the
codepoint at zero rather than at U+FFFD, so code that forgets to check asks
about NUL instead of about a character the input never contained. The paint
path draws one box per invalid byte.

---

## Han unification: what works, and what does not

### What works, and is proven

The **routing**. `TextStyle::language` is a BCP 47 tag, it reaches the chain,
and the chain selects a different face for the same codepoint:

- `tests/unit/test_font_fallback.cpp` resolves U+4E2D under `zh-Hans` and `ja`
  against two generated fonts that cover **exactly the same four codepoints
  with different outlines**, and asserts both the family names and the rendered
  pixels — the Hans font is a bar below the baseline centre, the Ja font a bar
  above it, so a routing failure fails on ink distribution, not only on a
  string.
- `examples/06_font_fallback --verify-fallback` compares the two on-screen
  panels pixel by pixel and fails if they are identical while the two families
  differ.

### What does NOT work, stated plainly

**This machine cannot render correct Japanese Han, and neither can drawgui on
it.** There is no Japanese font installed — no Noto Sans CJK JP, no IPAGothic,
no Source Han Sans. Every Han-capable face here is Chinese. The demo therefore
routes `ja` to a second *Chinese* face so that the routing is visible, and says
so on screen in red, twice.

design.md section 5.13.5 asks for four chains (zh-Hans / zh-Hant / ja / ko) and
for the right glyphs at the end of them. **The chains exist and are populated;
the glyphs are only as correct as the machine's fonts allow.** Under the shipped
default rules, on this machine, `zh-Hans` and `ja` both resolve to
`WenQuanYi Zen Hei` — the same face — because that is the only honest answer
available. The demo overrides the rules to show the mechanism.

### What it would take to close it

1. **A Japanese and a Korean font on the machine.** `fonts-noto-cjk` provides
   `Noto Sans CJK JP` and `Noto Sans CJK KR`, which the shipped table already
   names first. Nothing in the code needs to change; the entries stop being
   skipped. This is the whole of it for the *glyph selection* half.
2. **Language inheritance.** design.md 5.13.5 wants three levels of source
   — node property → `TextStyleScope` → app default locale. Only the node
   property exists. The other two are a theme/scope concern and belong with the
   slice that introduces scopes.
3. **Locale-aware `ja` versus `zh` when one font serves both.** Noto Sans CJK
   is a single family with per-language *variable* instances and OpenType
   `locl` features; selecting between them needs shaping, which needs HarfBuzz,
   which design.md 5.10.5 puts behind the ICU/textlayout decision. Until then a
   language chain can only choose between *files*, never within one.

Point 3 is the real ceiling. **Font-file selection covers the common case
(separate JP/SC/TC/KR font files, which is how every Linux and Windows desktop
ships them) and cannot cover the pan-CJK single-file case at all.** That is a
limit of this slice, not a defect in it, and it is the sentence to re-read
before anyone claims 5.13.5 is done.

---

## What is NOT in this slice, and shows on the screenshot

The demo is honest about its own gaps because they are visible in it:

- **No shaping.** Arabic renders as isolated letterforms rather than joined
  ones, and Devanagari would render as unreordered codepoints. Joining, ligature
  substitution and reordering are HarfBuzz's job (design.md 5.10.5).
- **No BiDi.** The Hebrew and Arabic rows read left-to-right. design.md 5.13.7
  supports text BiDi via SkParagraph + ICU, which is not linked here.
- **No line breaking.** A string wider than its node is clipped, not wrapped —
  which is why the demo's caveat is two hand-split lines. design.md 5.13.6 puts
  breaking behind ICU's UAX #14.
- **No grapheme clustering.** ZWJ sequences, skin-tone modifiers and flag pairs
  resolve per codepoint, so `👨‍👩‍👧‍👦` draws as its component emoji rather than as
  one family. design.md 5.10.4 makes clustering the unit for *editing*, which
  this slice deliberately does not touch.
- **No intrinsic sizing.** A label is given a box; it does not shrink-wrap its
  text. Unchanged from slice 3.

All five are the same missing dependency wearing different hats: HarfBuzz + ICU
via Skia's `textlayout` feature, and the `icudtl.dat` distribution question
design.md 5.10.5 defers. **This slice deliberately did not open it**, because
"which glyph from which face" is answerable without shaping and is what every
widget from here on needs.

---

## Determinism: what survives, and what does not

The project's zero-tolerance golden guarantee is intact, and the reasoning is
worth being precise about, because it is easy to overstate in either direction.

**What is deterministic:**

- Given the **same set of font files**, the resolver's answer is a pure
  function of the program. The pool is sorted by family name (not readdir
  order), the rules are stably sorted by specificity, and the walk has no
  other input. `tests/unit/test_font_fallback.cpp` builds two catalogs from one
  directory and asserts identical answers for six codepoints across three
  language tags.
- Rendering is unchanged for text that has only ever used one face:
  `examples/05_widgets`'s scene hashes **byte-identically** before and after
  this slice (`fbcca51cbdf3ef3a` over the full 1280x800 frame, both builds).
  A single-run string still goes through one `drawSimpleText` call at the same
  origin.
- `./build/examples/drawgui_render_png` is still
  `f635028e6e1e92349d23f0575f1e39e7ca8b05e5974492641ee610fe520df12e`, and the
  four golden baselines are untouched at tolerance 0.

**What is NOT deterministic, and this is the part to read before adding a
text-bearing golden:**

> **Text output depends on the host's installed fonts.** It always did — slice 2
> already noted that `examples/02_skia_cpu_gallery` "draws system fonts, so its
> output is a property of the host". Fallback does not create that dependence,
> but it *widens* it: previously a label either drew with the one family it
> named or drew nothing, so the only host variable was "is DejaVu Sans here".
> Now the answer can be any family in the pool, and adding a font package to a
> machine can change which one wins.

Consequences, decided now rather than discovered later:

1. **A text-bearing golden baseline MUST NOT scan a system directory.** It has
   to scan a directory whose contents the repository controls — exactly what
   `tests/fonts/gen_test_fonts.py` produces for the fallback tests. Those fonts
   are byte-identical on every run (fixed timestamps, sorted input), so a
   golden built on them is as reproducible as the four existing shape goldens.
2. **The zero tolerance stays.** It is a statement about the *rasterizer* being
   bit-exact across compilers and build types, and that is still true. Nothing
   here makes the same inputs produce different pixels.
3. **`FontCatalog::fallback_families()` is public** so a diagnostic can print
   what the machine offered. A future golden failure caused by a font package
   change should be answerable in one line rather than by bisecting.

fontconfig would have been strictly worse on this axis: its answer depends on
`/etc/fonts` **as well as** on the installed files.

---

## How this is tested, and how the tests were proven able to fail

Three layers, because they answer different questions and only the first two
can run on a machine whose fonts nobody controls.

### Hermetic — `tests/unit/test_font_fallback.cpp`

Resolves against **five fonts this build generates**
(`tests/fonts/gen_test_fonts.py`, run by CMake into
`build/tests/test-fonts/`). Because the coverage of each is written down in one
place, the test pins exact family names and **exact glyph ids** — `'A'` is
glyph 34 of `DgTest Latin`, U+03B1 is glyph 96 — rather than checking that a
pointer was non-null, which a tofu box also satisfies.

The set is built to make each rule falsifiable:

| font | covers | why it exists |
|---|---|---|
| `DgTest Latin` | ASCII, U+03B1, **U+1F600** | the primary; the emoji is the DejaVu trap in miniature |
| `DgTest Han Hans` | U+4E2D U+6D77 U+76F4 U+9AA8, low bar | the zh-Hans target |
| `DgTest Han Ja` | **the same four**, high bar | the ja target — identical coverage, different ink |
| `DgTest Rare` | U+16A0, U+1F900 | BMP + astral fall-through, cmap format 12 |
| `DgTest Colour` | U+1F600, has `COLR`/`CPAL` | must beat the primary's monochrome outline |

### System — `tests/unit/test_font_coverage.cpp`

The half only the real font set can answer, written so it cannot become a
tautology:

- **Oracle.** "Some family covers this codepoint" is computed by naming each
  family as a *primary* and reading `from_primary` — a different code path from
  the chain walk, so the oracle cannot be wrong the same way.
- **Implication.** Whenever the oracle says a codepoint is coverable, the
  resolver must return a **non-zero glyph**. When the oracle says it is not, the
  resolver must agree — returning a family it never checked is a real bug class.
- **No silent skip.** Fewer than four of the thirteen sample scripts covered is
  a FAILURE, not a pass. Four is reachable with DejaVu Sans alone, which is in
  the base package set of every distribution this project targets. On this
  machine all thirteen are covered.

### Scene — `examples/06_font_fallback --verify-fallback`

Runs the scene a human is looking at, not a second scene written to be easy to
check. Registered as CTest `fonts.verify_demo_scene`. It is **not** the primary
gate — the target does not exist without SDL3 and CI has none today — which is
why the two unit files above are compiled into the always-built unit binary.

### Defect injection

Nine defects, each built and run against the whole suite, each reverted. Two
were **not caught on the first attempt**, and those two are the useful part.

| # | injection | caught by |
|---|---|---|
| A | pool left in readdir order (the sort deleted) | `enumerate deterministically` + `"jav" routes to the generic chain` |
| B | the language tag never reaches the chain | both Han-unification cases, `subtag boundaries`, **and** `fonts.verify_demo_scene` |
| C | prefix match without the subtag-boundary check | `language tags match on subtag boundaries and ignore case` |
| D | colour-first step removed | hermetic `a colour face beats the named family's own monochrome emoji` **and** system `emoji prefer a colour face` |
| E | chain returns the first family a rule names without checking coverage | 6 cases across both files, **and** the demo check |
| F | an uncovered codepoint draws nothing instead of a `.notdef` box | `a codepoint nothing covers still puts ink on the surface` |
| G | `utf8_decode` repairs to U+FFFD instead of reporting | all three ill-formed-sequence cases |
| H | **run splitting collapsed** — every codepoint uses the first run's face | *nothing, at first* — see below |
| I | **specificity sort deleted** | *nothing, at first* — see below |

Two side notes from doing this. First, `-Werror` catches some injections before
any test runs — deleting the `language_chain` call made the parameter unused,
and stubbing out `draw_missing_run` made the function unused. Those had to be
rewritten into compiler-invisible forms (an extra clause in a condition, a
zero-length glyph array) to be a real test of the suite rather than of the
compiler. Second, the first attempt at injection E silently failed to apply
because the anchor text also appeared in a neighbouring function; it "passed"
and would have been recorded as an uncaught defect. **An injection that reports
no failure must be verified to have actually been applied** before it is
diagnosed.

#### H and I: two missing shapes, and how they were told apart

Slice 2 recorded that an injection nothing catches has two very different
causes — the scene lacks the shape, or the term has a single reader that cannot
distinguish stale from fresh — and that answering the first as if it were the
second burns hours. Both of these turned out to be the first kind, and the
diagnosis was quick because the question was asked before any code was written.

**H — run splitting.** Collapsing the splitter so every codepoint uses the
first run's face left all ten CTest entries green. The reason is structural:
every rendering assertion in the suite drew a string that needs exactly ONE
face, and `resolve_text()` — which the demo check and most unit cases use —
does not go through the splitter at all. So the run splitter, the *only new
machinery in the paint path*, had zero rendering coverage. The missing shape is
"a rendered string that needs two faces".

The new case exploits the generated fonts' disjoint vertical extents:
`DgTest Han Hans` draws U+4E2D entirely BELOW the midline, so appending it to a
Latin string must not add a single pixel above the midline — whereas the Latin
face's `.notdef` box, which is what the broken splitter draws there, spans the
full ascender. Re-injected: caught, on `mixed.top == latin_only.top`.

**I — the specificity sort.** Deleting it left everything green, and the reason
is embarrassing in a useful way: **both** rule tables in the codebase — the
shipped default and the one the tests use — are already written
most-specific-first, so the sort had nothing to reorder. The header's contract
says precedence is a property of the table's *content*, not of the order a
caller happened to list it in, and nothing had ever supplied a table that
tested the difference. The new case lists the generic rule FIRST and asserts
the `ja` rule still wins. Re-injected: caught.

The generalisable form, which is a sharper version of slice 2's rule: **a
sorting or normalising step is invisible to any input that is already sorted,
and hand-written tables are almost always already sorted.** Testing such a step
requires deliberately hostile input, and "the demo's table works" is evidence
about the demo's table only.

---

## Cost

Not a gate for this slice — the owner relaxed performance requirements, and
event-driven GUI text is low-frequency — but recorded so nobody has to guess.

- **No cache.** A codepoint is resolved by walking at most 16 families and
  asking each one `unicharToGlyph`. A 30-character label costs at most ~480
  cmap lookups per repaint. Deliberately not cached: a cache is a second copy
  of an answer, with an invalidation question attached, bought against a budget
  nobody is holding. The CTest suite went from 7.50 s to 7.68 s, and that
  includes three new entries' worth of work.
- **One draw call per run, not per character.** Consecutive codepoints
  resolving to the same face are grouped, so an all-Latin label is one
  `drawSimpleText` exactly as before.
- **The baseline comes from the primary font**, not from each run's own
  metrics. A fallback face has its own ascent and descent, and letting each run
  place itself would step a mixed label up and down mid-sentence.
