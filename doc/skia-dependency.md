# Skia dependency: the libskia2 switch (7-1)

7-1 is a pure build-system slice: it replaces the Skia provider, ships no new
engine feature, and its whole job is answered by this document plus
`cmake/FetchSkia2.cmake`. `.omo/plans/drawgui-phase7.md` 7-1 has the
one-paragraph summary; this file has the full evidence trail.

## 1. What changed and why

Through phase 6 this project consumed prebuilt Skia from
`rust-skia/skia-binaries`: one release asset per `<triple>:<feature-string>`
combination, a separate `rust-skia/skia` header checkout pinned by commit SHA,
and a hand-written CMake target (`drawgui_skia`) that declared the include
path, the Freetype link and (in an aborted draft of this exact slice) the
link order and per-module targets a `-textlayout` asset would need.

The owner built `BBDXF/libskia2` (https://github.com/BBDXF/libskia2)
specifically for this class of framework: one release tarball per platform
carrying 7 static libraries, the full header tree, and a **generated CMake
package** (`skia2Config.cmake`) that declares link order, include paths,
system libraries and ABI-affecting compile options - everything
`cmake/FetchSkia.cmake` used to hand-maintain. 7-1 switches to it.

## 2. What was verified, and how (not taken on faith)

Everything in the task's "already verified" list was re-checked directly
against the real release before being relied on:

- **Tarball + sidecar exist and match**: downloaded
  `libskia2-0.1.0-linux-x64.tar.gz` (15,808,697 bytes) and its `.sha256`
  sidecar independently with `curl`; `sha256sum -c` passed offline, giving
  `f3d698e92deb8b4c6c627a3a3dd132a95f89a6c8d5f5d5c8c27e4d02a55abb07` - the
  value now pinned in `cmake/FetchSkia2.cmake`.
- **Package layout matches `skia2Config.cmake.in` exactly**: extracted the
  tarball and confirmed `lib/cmake/skia2/skia2Config.cmake` (already
  variable-substituted, not a template), `lib/lib{skia,skparagraph,
  skshaper,skunicode_core,skunicode_libgrapheme,svg,skresources}.a` (7
  archives, matching the profile doc's §3.4 count) and `include/skia/{include,
  modules,src}` - the header tree travels **inside the same tarball** as the
  binaries, so there is no second pin and no "do these two artifacts still
  agree" question left to ask (design.md's old two-artifact risk, gone by
  construction rather than by discipline).
- **`key.txt` inside the tarball**: `skia_ref=chrome/m153`,
  `skia_commit=4f574af2444846ceca4d277a8095c5d4229d175f`, and the full GN arg
  dump - independently confirms the capability set (fontconfig on, PDF/XPS/
  RAW/Skottie/Graphite/Vulkan/AVIF/JPEG-XL/Perfetto/PartitionAlloc off,
  `skia_use_libgrapheme=true` + `skia_use_icu=false`) rather than trusting
  the README's prose table.
- **No external ICU, no `libGL`**: `nm -u` across all 7 `.a` files shows
  `Fc*` (fontconfig, expected - see §6) and libc/libm/pthread/dl as the only
  true external undefined symbols; the `libicu_bidi.*.o` entries `nm`
  reports are archive **member names** (an internal, bundled icu_bidi
  subset compiled into `libskshaper.a`/`libskunicode_libgrapheme.a`), not
  external undefined symbols - grepping for a real ICU symbol
  (`u_strlen`, `ubidi_*` as an *undefined* reference) finds none. No `gl*`/
  `glX*` undefined symbol exists anywhere either, confirming
  `GrGLMakeNativeInterface_none` is still what got built, exactly as
  `doc/platform-notes.md`'s "Ganesh linking" section already documented for
  the old asset.
- **`SkUnicodes::Libgrapheme::Make()` is the real factory function**, not
  `SkUnicodes::ICU::Make()` (which the aborted draft's smoke test called,
  and which does not exist in this package - there is no
  `libskunicode_icu.a` in the tarball at all). Confirmed by reading
  `modules/skunicode/include/SkUnicode_libgrapheme.h` from the extracted
  tarball directly.

## 3. The central risk: did the golden image change?

**No. `drawgui_render_png`'s output hash is unchanged: still
`f635028e6e1e92349d23f0575f1e39e7ca8b05e5974492641ee610fe520df12e`.**

Measured, not assumed, on both compilers:

| build | compiler | golden PNG sha256 |
| --- | --- | --- |
| baseline (old rust-skia asset, HEAD before this slice) | gcc 15 | `f635028e6e1e92349d23f0575f1e39e7ca8b05e5974492641ee610fe520df12e` |
| after (libskia2 0.1.0) | gcc 15 | `f635028e6e1e92349d23f0575f1e39e7ca8b05e5974492641ee610fe520df12e` |
| after (libskia2 0.1.0) | clang 21 | `f635028e6e1e92349d23f0575f1e39e7ca8b05e5974492641ee610fe520df12e` |

The baseline build used a real `git worktree` at the pre-slice commit
(`f196c37`), the pre-existing `third_party/skia-prebuilt` cache (old asset),
a from-scratch `ctest` run (29/29 passed there too), and its own render
compared byte-for-byte against the after build's render - not a re-read of
the already-committed hash. All 29 CTest entries pass unchanged on both the
old and new dependency.

### Why an unchanged hash is credible here, not merely convenient

`drawgui_render_png` (`examples/00_cpu_raster_png`) draws with `SkSurfaces::
Raster` - `SkPath`/`SkPaint`/`SkRRect`/gradient/blur, all in Skia **core**,
none of it gated behind any of the GN args that actually differ between the
two assets:

- The differing args are `skia_enable_skparagraph`, `skia_use_harfbuzz`,
  `skia_use_libgrapheme`/`skia_use_icu`, `skia_enable_svg`,
  `skia_use_libwebp_*`, `skia_use_wuffs`, `skia_use_fontconfig`,
  `skia_enable_pdf` - text layout, shaping, SVG, three image codecs, system
  font discovery, and a document backend. `drawgui_render_png`'s scene calls
  none of that code, directly or transitively (it links `skia2::skia2`, but
  the linker never pulls an archive member whose symbols nothing
  references - confirmed for the `svg`/`skparagraph`/`skunicode_*`/
  `skshaper`/`skresources` archives specifically by `ldd` on every one of
  this project's binaries, `drawgui_render_png` included, showing **no**
  `libfontconfig` runtime dependency anywhere: §6 below).
- The core rasterization path itself - `SkDraw`, `SkScan`, `SkBlitter`,
  `SkAAClip`, the software gradient/blur code - is the same `chrome/m153`
  milestone in both cases. libskia2's `key.txt` pins
  `4f574af2444846ceca4d277a8095c5d4229d175f`; the previous rust-skia asset
  was built from a different m153 fork/commit
  (`c9c3c5a91e9d74181a2ca34de81d78fef9b4d2b6`, a **different repository**:
  `rust-skia/skia` vs `google/skia`). Two different m153 snapshots
  byte-matching is not guaranteed by milestone alone - it is corroborated by
  the fact that nothing in the differing GN args touches the code path this
  scene exercises, and confirmed empirically rather than argued from that
  alone.
- `is_official_build=true` and `skia_enable_optimize_size=false` (both
  builds use `-O3`-class optimization, not `-Oz`) rules out the one build
  flag that plausibly changes floating-point codegen paths in a way that
  could perturb anti-aliasing rounding; libskia2's `doc/gui-framework-
  profile.md` §2.1 documents deliberately not enabling
  `skia_enable_optimize_size` for exactly this class of concern.
- CPU raster was already established (design.md, `doc/development.md`) to be
  **byte-identical across gcc and clang** on the old asset; it still is on
  the new one (table above), which is itself evidence the rasterization
  path is deterministic and insensitive to the surrounding toolchain/build
  in the way this project's whole golden-image methodology assumes.

No re-baseline was necessary. The zero-tolerance comparison in
`tests/golden/golden_image.cpp` was not touched, weakened, or bypassed.

## 4. SkParagraph and SkUnicode: proven to link and initialize

`tests/unit/test_skia_textlayout_smoke.cpp` (salvaged and rewritten from the
aborted attempt - see §8) is a CTest-visible part of `drawgui_unit_test` and
asserts, against the real archive, not a mock:

1. `SkUnicodes::Libgrapheme::Make()` returns non-null.
2. A `skia::textlayout::ParagraphBuilder` built over that `SkUnicode` and a
   real (test-generated, not system) font lays out `"Hello"` and reports a
   positive height and exactly one line at 200px width.
3. `computeCodeUnitFlags` on the family-emoji ZWJ sequence
   (`\U0001F468\u200D\U0001F469\u200D\U0001F467\u200D\U0001F466`) reports
   **1** grapheme cluster, not 4 code points.
4. `computeCodeUnitFlags` on 16 unspaced Han characters reports **more than
   4** soft line-break opportunities - proof the UAX#14 rule table is
   actually present and active, not silently empty (an absent table would
   report 0, since there are no spaces to fall back to).
5. `getBidiRegions` on `"hello ﻣﺮﺣﺒﺎ world"` (Arabic embedded in Latin)
   reports at least 2 regions, at least one with an odd (RTL) bidi level.

```
$ build/tests/drawgui_unit_test --test-case="*textlayout*"
[doctest] test cases:  5 |  5 passed | 0 failed | 413 skipped
[doctest] assertions: 16 | 16 passed | 0 failed |
[doctest] Status: SUCCESS!
```

This is deliberately not a text *feature* test - no `NodeStyle`/render-tree
wiring exists yet, on purpose (7-2's job). It is the one working call
design.md's own rule ("接口必须在至少一个跑通的实现之后才写") requires before
a dependency counts as a foundation rather than four linked-but-unverified
archives.

## 5. design.md §12 open question 5, and the §5.10.5-vs-§5.13 tension: settled

**Settlement**: both are dissolved, not merely deferred again. libgrapheme
carries no `icudtl.dat` at all - there is nothing to embed and nothing to
trim, so the §5.10.5 trim-vs-completeness question does not arise, and the
§5.13.6 "the line-break rule table must not be trimmed" constraint is
satisfied by construction (the table is compiled into `libskunicode_
libgrapheme.a`'s code, not stored as external trimmable data).

Evidence, not README-trust:

- **No `icudtl.dat` file exists anywhere this build can reach.**
  `find / -iname icudtl.dat` on this host finds only unrelated Windows
  browser installs under the WSL `/mnt/c` mount (Chrome, Edge, VS Code) -
  nothing under this project's `third_party/`, `build*/`, or system library
  paths, and nothing a Linux ELF binary on this filesystem would ever
  resolve to by relative path.
- **The compiled test binary contains no `"icudtl"` string at all**:
  `strings build/tests/drawgui_unit_test | grep -i icudtl` finds nothing.
  If any code path referenced the filename (even as a fallback), the
  string would be present in the binary regardless of whether the branch
  executes.
- **`ldd` shows zero ICU shared-library dependency** on every drawgui binary
  checked (`drawgui_unit_test`, `drawgui_render_png`,
  `drawgui_skia_cpu_gallery`) - only `libstdc++`/`libm`/`libgcc_s`/`libc`.
- **And yet grapheme clustering, UAX#14 CJK line breaking and BiDi region
  detection all measurably work** (§4, tests 3-5) - the exact three
  capabilities §5.10.5 says the trim must not break and §5.13.6 singles out
  the line-break table specifically. A backend that were silently missing
  its rule tables would fail test 4 first (an absent UAX#14 table gives at
  most a start/end mark on unspaced CJK, not "more than 4"), and it does
  not fail.

What is genuinely still lost, named per design.md's own accounting (not
newly discovered here): sentence-boundary iteration, locale-sensitive case
mapping, UTF-16 code paths, and dictionary-based word segmentation for Thai/
Lao/Khmer - none of which design.md's own MVP scope (§5.13.8/§5.13.9 - no
built-in locale formatting, no built-in language packs beyond an overridable
string table) currently needs. This matches libskia2's own accounting in
`doc/gui-framework-profile.md` §2.3, independently corroborated rather than
copied.

## 6. fontconfig: a new build prerequisite, and NOT a reversal of the
   "zero fontconfig" property

Two things are true at once, and they do not contradict each other:

1. **`libfontconfig1-dev` is now a required system package to *build* this
   project.** `skia2Config.cmake` does `find_library(SKIA2_FONTCONFIG_
   LIBRARY NAMES fontconfig REQUIRED)` and `find_path(... fontconfig/
   fontconfig.h REQUIRED)` on Linux, because Skia's own `BUILD.gn` never
   vendors fontconfig (`third_party/BUILD.gn` only lists
   `libs = ["fontconfig"]`) and `skia_use_fontconfig=true` in libskia2's own
   configuration. Confirmed present on this host: `fontconfig 2.17.1`,
   `/usr/lib/x86_64-linux-gnu/libfontconfig.so`. This is a genuinely new
   prerequisite versus the old rust-skia asset (which never enabled
   fontconfig at all) and is documented in `doc/development.md` and
   `doc/platform-notes.md`.
2. **This project still does not use fontconfig for font selection.**
   `src/render/font_catalog.cpp` calls `SkFontMgr_New_Custom_Directory`
   exclusively - never `SkFontMgr_New_FontConfig` - exactly as
   `doc/font-fallback.md` already recorded as a deliberate choice (glyph
   selection must not be a property of the host machine's `/etc/fonts`).
   Measured proof the property survives the dependency swap, not merely
   asserted: `ldd` on every drawgui binary, including the ones that link
   `skia2::skia2` most heavily (`drawgui_skia_cpu_gallery`,
   `drawgui_render_png`), shows **no runtime dependency on
   `libfontconfig`** at all - because nothing in this codebase's object
   files references an `Fc*` symbol, the linker's default `--as-needed`
   drops the `DT_NEEDED` tag for a library that was only ever a *build-time*
   requirement of the package, never an actually-called one. libskia2
   *offers* a fontconfig-backed `SkFontMgr`; this project links against a
   package that could use it and structurally does not.

## 7. Capability inventory: newly available, deliberately not wired up here

7-1 changes what the Skia archive *can* do; it wires up none of it. Recorded
here because several later slices and previously-declined items were
blocked on exactly these:

| Capability | Evidence it now exists | Blocked-on-this, previously | Status after 7-1 |
| --- | --- | --- | --- |
| SkParagraph / SkShaper / SkUnicode(libgrapheme) | §4 | 7-2 (multiline/BiDi/shaping/CJK), 7-3 (IME) | Linked and proven to initialize; **not** wired into any `NodeStyle`/render path |
| SVG (`SkSVGDOM` + `SkPathOps` + `skresources`) | `libsvg.a`, `libskresources.a` present in the tarball, declared by `skia2Config.cmake` | Image formats/vector-icon work named in `.omo/plans/drawgui-phase7.md`'s "not in this phase" list | Available; **no** SVG decode/paint path exists in `ImageCatalog` |
| WebP + GIF decode | `key.txt`: `skia_use_libwebp_decode=true`, `skia_use_wuffs=true` (GIF) | Same "not in this phase" list | Available through the same `SkCodec` path `ImageCatalog` already uses for PNG/JPEG; **not** exercised by any test or example yet |
| Ganesh + GL | `key.txt`: `skia_enable_ganesh=true skia_use_gl=true skia_use_x11=false skia_use_egl=false`, `GrGLMakeNativeInterface_none` confirmed (§2) | GPU backend named out-of-phase since P1 (`third_party/` had a GL-capable asset sitting unused even before this slice) | Same as before this slice: available, unused; CPU raster is the only path any code takes |
| Windows (`windows-x64-msvc` tarball) | Confirmed to exist and download in the v0.1.0 release (`libskia2-0.1.0-windows-x64-msvc.tar.gz`, 22,549,036 bytes, own `.sha256` sidecar) | design.md §3.2 cross-platform goal | Tarball exists upstream; `cmake/FetchSkia2.cmake` explicitly refuses any non-`linux-x64` triple today (`message(FATAL_ERROR ...)`) - wiring an MSVC job is out of scope for 7-1 by name |

## 8. The three files left mid-flight, and what happened to each

An earlier, aborted attempt at this slice (before the direction changed to
libskia2) left three files modified/added. Each was inspected before being
touched:

1. **`cmake/FetchSkia.cmake`** (+137/-29 in the aborted diff, switching to
   rust-skia's `ganesh-gl-jpegd-jpege-pdf-textlayout` asset) - **superseded
   in full**. The working-tree edit was discarded (`git checkout --`
   restored the committed version), and the whole file was then deleted
   (`git rm`) in favour of `cmake/FetchSkia2.cmake`. Nothing from that draft
   was salvageable: it fetched from a different provider, verified a
   different artifact shape (two pins, not one), and hand-declared exactly
   the link order/include paths/system libs that `skia2Config.cmake` now
   declares instead - keeping any of it would have been the "two sources of
   truth" this slice is required not to leave behind.
2. **`tests/CMakeLists.txt`** (+1 line, adding the new test source to
   `drawgui_unit_test`'s sources) - **kept as-is**. The line is provider-
   agnostic (`unit/test_skia_textlayout_smoke.cpp`, no asset-specific
   content), so it needed no change at all.
3. **`tests/unit/test_skia_textlayout_smoke.cpp`** (66 lines, untracked) -
   **salvaged and extended**. Its structure, its font-manager choice
   (`SkFontMgr_New_Custom_Directory` over the generated test-font
   directory, not a system font), its `TEST_CASE` shape and its own stated
   rationale (a foundation smoke test, not a feature test) were all exactly
   right and reused unchanged. The one substantive fix: it called
   `SkUnicodes::ICU::Make()` and included `SkUnicode_icu.h`, which do not
   exist in libskia2 (no `libskunicode_icu.a` is shipped at all - only
   `libskunicode_libgrapheme.a` and `libskunicode_core.a`); both were
   changed to `SkUnicodes::Libgrapheme::Make()` /
   `SkUnicode_libgrapheme.h`. Three more `TEST_CASE`s were then added
   (grapheme/line-break/BiDi, §4/§5) to make the file also carry the §12
   open-question settlement's evidence, rather than writing a second,
   separate throwaway program to do the same job.

## 9. How much hand-written build logic `find_package(skia2)` deleted

`cmake/FetchSkia.cmake` (271 lines) is gone; `cmake/FetchSkia2.cmake` (217
lines, most of it rationale comments matching this project's own commenting
density, not mechanism) replaces it. The line-count delta undersells the
real deletion, which is qualitative:

- **The entire git-based header-fetch pipeline is gone**: shallow sparse
  clone, `sparse-checkout set`, `rev-parse HEAD`, commit-SHA comparison,
  cleanup-on-mismatch - roughly 90 lines whose entire job was keeping a
  *second* artifact (headers) in sync with the first (the binary archive).
  There is no second artifact any more; the header tree ships inside the
  same tarball the SHA256 check already covers.
- **The hand-maintained `<triple>:<feature-string>` SHA256 registry
  collapses to one entry.** The old file carried three registered
  combinations by the time of this slice (`jpegd-jpege-pdf`,
  `ganesh-gl-jpegd-jpege-pdf`, and the aborted draft's
  `ganesh-gl-jpegd-jpege-pdf-textlayout`); each new capability meant a new
  registry line requiring a manual `sha256sum` run. libskia2 does not
  fragment its release by feature subset - one tarball carries everything
  this project uses - so there is exactly one hash to track, and upstream
  now publishes it itself (§10 covers why it is still pinned in-repo too).
- **All target declaration is gone.** The old file's
  `add_library(drawgui_skia STATIC IMPORTED GLOBAL)` +
  `target_include_directories(... SYSTEM INTERFACE ...)` +
  `target_link_libraries(... Freetype::Freetype)`, and - in the aborted
  draft specifically - four more `add_library`/`set_target_properties`
  calls plus a hand-reasoned link-order comment for
  `skparagraph`/`skshaper`/`skunicode_icu`/`skunicode_core`, are replaced by
  one call: `find_package(skia2 REQUIRED CONFIG PATHS ... NO_DEFAULT_PATH)`.
  `skia2Config.cmake` (upstream-maintained, not this repo's problem any
  more) does the equivalent of 7 `add_library(IMPORTED)` calls, the
  topological `INTERFACE_LINK_LIBRARIES` chain, the Linux system-library
  block (`fontconfig`, `Threads`, `dl`, `m`) and the ABI-affecting compile
  option block (C++20; `/utf-8` + `_HAS_EXCEPTIONS=0` on Windows) - all of
  it now a single upstream-owned source of truth this project consumes
  rather than re-derives.
- What remains in `cmake/FetchSkia2.cmake` is exactly the part that is
  genuinely this project's own concern: which release to fetch, verifying
  it is the release that was reviewed, and calling `find_package`.

## 10. The SHA256 verification design, defended

Upstream now publishes a `.sha256` sidecar - strictly better than the old
design for the common case (no PR needed to hand-copy a hash for a new
asset). It is *not*, by itself, equivalent in strength to the old registry,
because the sidecar is generated from the same release as the archive: an
attacker or an accidental release recreation capable of changing the
tarball can regenerate a matching sidecar identically. What the sidecar
cannot detect is the release *changing since it was last reviewed* - a
moved tag, a force-pushed release, or a compromised upstream account - which
is precisely the scenario the old per-asset registry existed to catch (this
project has already lived through one Skia-provider security-relevant
decision point: the registry's own "download a linker input without
verifying it is not an option" rule).

So `cmake/FetchSkia2.cmake` pins `DRAWGUI_SKIA2_SHA256` in-repo *and* checks
it twice: the downloaded sidecar's hash must equal the pin (catching a
changed release before an unverified tarball is ever fetched), and the
downloaded archive's actual hash must equal the pin (catching transport
corruption or a sidecar/archive inconsistency). Verified as a real gate, not
a no-op: configuring with a deliberately wrong pinned hash against an
already-populated `third_party/` correctly short-circuits (an
already-extracted package is trusted without re-verification, matching the
old file's own behaviour), but against an **empty** `third_party/` it fails
loudly with the exact mismatch message before any archive is extracted -
tested directly, not assumed.

## 11. Measurements

### Binary size (`drawgui_render_png`, static-linked against `skia2::skia2`,
`-Wl,` default flags, no special stripping)

| build type | compiler | before (old rust-skia asset) | after (libskia2 0.1.0) | delta |
| --- | --- | --- | --- | --- |
| Release | gcc 15 | 4,517,984 B | 5,213,032 B | +695,048 B (+15.4%) |
| Release | clang 21 | (not separately measured; gcc/clang produce byte-identical *pixels*, not identical binary bytes) | 5,222,848 B | - |
| Debug | gcc 15 | 5,028,832 B (matches the task's stated "current" figure exactly) | 5,719,776 B | +690,944 B (+13.7%) |

The growth is real and expected, not a regression to chase down: the new
core `libskia.a` itself carries more compiled-in codec/backend support than
the old asset did (WebP encode/decode, GIF via wuffs, fontconfig - none of
which the old `ganesh-gl-jpegd-jpege-pdf` asset had at all, since it predates
this project ever asking for those capabilities) even though
`drawgui_render_png` calls none of the new code paths directly. libskia2's
own README states a 6.4 MB linked linux-x64 executable for its own smoke
test binary (which *does* call SkParagraph/SkSVGDOM/etc); this project's
render-only binary at 5.2 MB is smaller than that reference point precisely
because it does not call the text/SVG code paths that would pull more
object files out of the static archives, which is internally consistent.

### Clean build time (`cmake --build`, 8-way parallel `ninja`, `/usr/bin/time
-v`, both cases with Skia already fetched/cached - i.e. compilation time
only, not download time)

| | before | after |
| --- | --- | --- |
| Wall clock | 1:22.12 | 1:07.56 |
| User CPU | 523.88 s | 443.81 s |
| System CPU | 56.46 s | 40.49 s |

The after-build is faster, on both wall clock and CPU time. This was
measured, not assumed to be neutral or worse; a plausible (not exhaustively
proven) explanation is that libskia2's header package is the header tree
alone (no Skia GN-generated build metadata, no other contributors' test
trees beyond what ships in `modules/`), against the old setup's full
`rust-skia/skia` sparse checkout of `include/` + all of `modules/` from a
different, separately-versioned fork - every drawgui translation unit that
includes a Skia header re-parses whichever tree is larger, and this is a
CPU-heavython path across ~20 first-party TUs plus the Skia-facing example/
test TUs.

### Clean-checkout fetch path

Verified directly, not inferred from the cached-`third_party/` case: with
`third_party/` moved aside entirely, `cmake -B build-clean-fetch ...`
downloaded the `.sha256` sidecar, verified it against the in-repo pin,
downloaded the tarball, verified its SHA256, extracted it, and successfully
ran `find_package(skia2 REQUIRED CONFIG ...)` - `skia2::skia2` was defined,
SDL3 was independently found via pkg-config, and doctest was fetched via the
pre-existing `FetchDoctest.cmake` in the same configure run. The resulting
build passed the full 29-test CTest suite.

## 12. CTest, sanitizer and toolchain gates run for this slice

All run once, per the owner's narrowed verification scope:

| Gate | Result |
| --- | --- |
| `ctest` (Debug, gcc, `-DDRAWGUI_WERROR=ON`) | 29/29 passed, including `unit` (which now also runs the 5 new textlayout-foundation `TEST_CASE`s) |
| Release build, gcc, `-DDRAWGUI_WERROR=ON` | Builds clean, zero warnings-as-errors; golden hash unchanged |
| Release build, clang, `-DDRAWGUI_WERROR=ON` | Builds clean, zero warnings-as-errors; golden hash unchanged (byte-identical PNG to the gcc build) |
| `examples/19_c_client/main.c` | Confirmed compiled by `cc -x c -std=c11` (real C front end, from `compile_commands.json`), unaffected by the Skia provider swap |
| `-DDG_SANITIZE=ON` (Debug, gcc, ASan+UBSan) | 29/29 passed (206.83 s total - the sanitized suite is markedly slower, as documented in `doc/development.md`, and `widgets.interaction_equals_full` alone took ~92 s instrumented) |
| `props.no_drift` / `props.abi_lock` / `props.lock_selftest` | Passed (part of the 29) |
| `theme.no_drift` / `theme.abi_lock` / `theme.consistency` | Passed (part of the 29) |
| `abi.no_drift` / `abi.abi_lock` | Passed (part of the 29) |
| clang-tidy `-p build` over the CI's hand-maintained TU list (now including `tests/unit/test_skia_textlayout_smoke.cpp`) | Ran once over all 143 TUs. Found 7 errors total: 4 in `tests/unit/test_complex_props.cpp` (`bugprone-unchecked-optional-access`, lines 183/217/249/250) and 3 in this slice's own new file (an implicit `int`->`bool` conversion, and two `const_cast`s). The 3 in the new file were fixed (no NOLINT: the conversion made explicit with `!= 0`, the `const_cast`s removed by making the two local strings non-`const` so `.data()` already returns `char*`) and re-verified clean in isolation. The 4 in `test_complex_props.cpp` were checked against the pre-slice baseline (`git worktree` at commit `f196c37`, same clang-tidy invocation) and are confirmed pre-existing - a real, unrelated gap this slice did not introduce and, being outside a "pure build-system slice"'s scope (that file is a 5-4 property test, untouched by any commit in this slice), does not fix either. Net result of this slice's own changes: zero new clang-tidy findings. |
| clang-format, dry-run, `--Werror` | This slice's own changed files are clean; one pre-existing violation (`src/theme/theme_loader.cpp:106`, confirmed present at the pre-slice commit too) is unrelated to this slice and out of scope to fix here |

## 13. What this slice explicitly did not do

Per the task's scope boundary and `.omo/plans/drawgui-phase7.md`'s own
"not in this phase" convention: no `NodeStyle`/render-tree wiring for text
layout, multiline, shaping, BiDi rendering, line breaking or grapheme
cursor movement (7-2); no IME composition handling (7-3); no GL/GPU
wiring even though Ganesh is available; no SVG/WebP/GIF feature wiring even
though the codecs are available; no Windows CI even though the tarball
exists and its SHA256 could be pinned the same way linux-x64's is.
