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

The engine has layout, widgets, theming and a C ABI, all running on a CPU
raster path with no GPU dependency.

- **Layout**: a Flexbox-like model (row/column, wrap, `align`/`align_self`,
  `basis`/`shrink`/`main_size`/`aspect_ratio`, clipping, group opacity via
  `saveLayer`) that lays every node out exactly once per frame - measured,
  not assumed: `LayoutStats` reports `nodes_visited == 0` for scrolling,
  slider drag, typing, list recycling, theme colour switches and focus
  changes alike.
- **Properties**: 49 CSS-like properties addressable by numeric id -
  38 fully implemented, 10 partial, 1 not yet (`transform`, blocked on this
  engine's axis-aligned damage/hit-test/clip representation).
- **Widgets**: 9 `WidgetKind` values - panel, label, button, checkbox (radio
  is a `group` field on it), slider, scroll view, text field, virtualized
  list, dropdown - plus a context menu, tooltip and modal dialog through a
  shared popup host. No new `RenderObject`/node kind has been needed across
  23 consecutive development slices, the acceptance bar `doc/design.md` set
  for its own primitive set.
- **Text**: single- and multi-line, with CJK/BiDi/complex-script shaping via
  `SkParagraph` + libgrapheme (no `icudtl.dat` needed), grapheme-cluster-
  correct editing, and IME composition.
- **Focus**: Tab order via a focus-scope model, with a visible focus ring.
- **Theming**: 12 tokens, light/dark variants, and external theme packages
  (path-traversal-safe, hot-reloadable).
- **C ABI**: 21 exported functions; `examples/19_c_client` is a pure C
  program that opens two windows and cross-updates them on click.

All 35 CTest entries pass. The golden-image sha256
(`f635028e6e1e92349d23f0575f1e39e7ca8b05e5974492641ee610fe520df12e`) has been
unchanged across the entire history, including a full Skia distribution swap.

Rendering is CPU raster only - the linked Skia includes Ganesh/GL, but
nothing in this codebase calls into it. The full development history, phase
by phase, lives in `doc/*.md` and the git log; this section states only what
is true today.

The eventual target is Linux and Windows desktop, with macOS, Android and iOS
deferred. Only Linux is wired into the build, and the window manager is SDL3
on Linux with no conditional compilation for anything else - a second
platform will be measured before it is abstracted over. There is also no
platform abstraction ahead of a second backend, on purpose: an earlier
attempt wrote twelve abstract platform headers before any backend existed,
and they were removed because nothing had tested whether they described the
machine. An interface is extracted from at least one working implementation,
never written ahead of one.

## Build prerequisites

- CMake >= 3.24
- Ninja
- clang or gcc with C++20 support
- FreeType development headers (`libfreetype-dev`) - `libskia.a` references
  `SkTypeface_FreeType` unconditionally
- fontconfig development headers (`libfontconfig1-dev`) - required to
  configure the link against the prebuilt Skia (`skia2Config.cmake` needs it
  on Linux), but `SkFontMgr_New_FontConfig` is never called anywhere in this
  codebase; font fallback is built by hand instead so glyph selection stays a
  property of the program, not of the host's `/etc/fonts`. `ldd` on every
  drawgui binary confirms no runtime dependency on `libfontconfig`.

The Skia distribution is `BBDXF/libskia2` (`cmake/FetchSkia2.cmake`), a
purpose-built prebuilt Skia for self-drawn GUI frameworks; `doc/skia-
dependency.md` records the switch from the previous rust-skia-based setup.
No OpenGL development package is needed - the prebuilt ships
`GrGLMakeNativeInterface_none`, so GL entry points resolve at runtime through
a caller-supplied proc loader and nothing links against libGL.

## Building

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

The first configure downloads libskia2's release tarball into
`third_party/skia-prebuilt/` and the doctest single header into
`third_party/doctest-<version>/`, both SHA256-verified. Once present, building
without network access works; `-DDRAWGUI_BUILD_TESTS=OFF` skips doctest.

Render a frame:

```sh
./build/examples/drawgui_render_png out.png
```

## Examples

26 examples, `examples/00_*` through `examples/25_*`. Most support
`--dump-png out.png` (headless render) and a `--verify-*` flag (headless
assertion, used in CTest); run any binary with `--help` for its full flag
list. **Start with `examples/25_showcase`** - it is the only example that
combines features in one scene rather than isolating one, and is the
project's cross-feature regression bed.

| # | Example | What it demonstrates | Verify flag |
| --- | --- | --- | --- |
| 00 | `cpu_raster_png` | Renders a PNG with no window, no layout - the raster floor everything else builds on | - |
| 01 | `sdl3_multi_window` | Three independent SDL3 windows, closing one leaves the others running | - (needs a display) |
| 02 | `skia_cpu_gallery` | 16 Skia panels (geometry, text, gradients, blur, shadow, image) with raster/present timings | `--bench` |
| 03 | `damage_repaint` | Partial repaint: only the damaged rectangle is re-rasterized | `--verify-damage` |
| 04 | `layout` | Incremental layout re-run under resize and content change | `--verify-layout` |
| 05 | `widgets` | Property-id-driven layout/paint, clipping probes, damage cost | `--verify-widgets` |
| 06 | `font_fallback` | One family name covers 12 scripts plus emoji; BCP 47 tag selects Han face | `--verify-fallback` |
| 07 | `clipping` | `overflow` clips paint, hit testing and damage together, including rounded clips | `--verify-clipping` |
| 08 | `opacity` | Per-object alpha vs. group `opacity` (saveLayer) side by side | `--verify-opacity` |
| 09 | `sizing` | `basis`/`shrink`/`main_size`/`aspect_ratio` under a resizing window | `--verify-sizing` |
| 10 | `scrolling` | Two independent scroll viewports, wheel and drag, clamped at content edges | `--verify-scrolling` |
| 11 | `form_controls` | Checkbox, two radio groups, two sliders | `--verify-form-controls` |
| 12 | `text_input` | Single-line editing, selection, ellipsis/scroll, IME composition preview | `--verify-text-input` |
| 13 | `image` | Decode + four fit modes (`fill`/`contain`/`cover`/`none`) plus a placeholder colour | `--verify-image` |
| 14 | `popup` | Native vs. overlay popup branches that later examples reuse | `--verify-popup` |
| 15 | `list` | 1000-item virtualized list on a 14-node pool; `--bench` compares against a non-virtualized baseline | `--verify-list` |
| 16 | `complex_properties` | Gradient, hard-edged shadow, id-based image attach through the dedicated-setter channel | `--verify-complex-properties` |
| 17 | `animation` | Explicit animation, implicit hover transition, chained caret blink; `--idle-probe-ms` measures idle CPU | `--verify-animation` |
| 18 | `theme` | Every visual token-bound; click to switch light/dark with zero relayout | `--verify-theme` |
| 19 | `c_client` | Pure C program, two windows, cross-window click-to-recolour through the ABI | `--verify-c-client` |
| 20 | `multiline_text` | Word wrap, unspaced CJK wrap, mixed-script/emoji, BiDi Arabic-in-Latin, ellipsis | `--verify-multiline-text` |
| 21 | `focus` | Tab/Shift-Tab order, `tab_index` override, popup focus confinement | `--verify-focus` |
| 22 | `dropdown_menu` | Keyboard/mouse dropdown selection between two real siblings | `--verify-dropdown` |
| 23 | `menu_tooltip_dialog` | Context menu, hover tooltip, modal dialog (native and overlay branches) | `--verify-menus` |
| 24 | `theme_package` | External theme package load, hot reload, path-traversal rejection | `--verify-theme-package` |
| 25 | `showcase` | **Start here.** All 9 `WidgetKind`s, menu/tooltip/dialog, live theme switch, CJK text and shadow, combined in one scene | `--verify-showcase` |

## Unit tests

`ctest` runs `drawgui_unit_test`, a doctest binary covering `dg::Expected`,
the golden-image comparator, damage, layout, clipping, compositing, hit
testing, interaction, UTF-8 decoding, font fallback, text editing, paragraph
layout, focus, dropdown, tooltip timing, and theme-package security
(path traversal, symlink escapes/loops, resource bounds, a fixed-seed fuzz
pass). Run it directly for per-case output:

```sh
./build/tests/drawgui_unit_test
```

## Golden-image tests

Rendering is compared against committed PNG baselines pixel by pixel, at
**zero tolerance** - output is byte-identical across gcc and clang, so any
difference is a real change, never driver noise.

A failing comparison writes `<scene>.actual.png` and `<scene>.diff.png` into
`build/tests/golden-output/`. To accept an intended change, inspect the diff,
then regenerate:

```sh
cmake --build build --target golden_update
```

`ctest` never regenerates baselines on its own; a suite that can rewrite its
own expectations proves nothing.

## Documentation

The full design document lives at `doc/design.md` (written in Chinese).
Every other document in `doc/` records the findings and decisions from one
development slice, in more depth than belongs here:

| Document | Subject |
| --- | --- |
| `doc/development.md` | adding a property, the ABI lock, running the sanitized suite |
| `doc/completeness.md` | the MVP-8 completeness audit and its qualified verdict |
| `doc/cpu-raster-findings.md` | what CPU rasterization costs, and where it stops being enough |
| `doc/damage-repaint.md` | partial repaint and damage granularity |
| `doc/layout.md` | incremental layout and the integer-device-pixel deviation |
| `doc/widgets.md` | why there is no separate widget tree |
| `doc/properties.md` | the property system: ids, generator, gap report |
| `doc/wrapping.md` | flex wrap, `align_self`, per-side borders |
| `doc/clipping.md` | `overflow` and rounded-clip damage |
| `doc/compositing.md` | group `opacity`, why a layer isn't damage-atomic |
| `doc/sizing.md` | `basis`/`shrink`/`main_size`/`aspect_ratio`, one measurement pass |
| `doc/scrolling.md` | scroll viewports, runtime offset, zero-relayout scrolling |
| `doc/form-controls.md` | checkbox/radio/slider, and why dropdown was deferred |
| `doc/text-input.md` | single-line `TextField`, ASCII-then-grapheme editing |
| `doc/font-fallback.md` | the hand-built fallback chain, vs. fontconfig |
| `doc/image.md` | image decode/paint, size-before-decode, golden-test image source |
| `doc/list.md` | list virtualization, the recycled pool, the 1000-item measurement |
| `doc/complex-properties.md` | the dedicated-setter channel (gradient/shadow/image/transform) |
| `doc/animation.md` | `dg::AnimationEngine`, the clock, idle-CPU measurement |
| `doc/theme.md` | the theme token system, `$token` bindings, relayout cost |
| `doc/abi.md` | the C ABI generator, handle validity, try/catch trampolines |
| `doc/skia-dependency.md` | the libskia2 dependency switch and what it changed |
| `doc/text-layout.md` | multi-line `SkParagraph` layout, CJK/BiDi, exactly-once layout |
| `doc/ime.md` | IME composition, platform findings, the testing gap |
| `doc/focus.md` | Tab order, focus scopes, popup/list/IME interactions |
| `doc/menus.md` | dropdown (9th `WidgetKind`), and why context menu/tooltip/dialog were split out |
| `doc/popup.md` | the popup host mechanism shared by dropdown, menu, tooltip, dialog |
| `doc/theme-packages.md` | external theme packages, path-traversal defence, hot reload |
| `doc/showcase.md` | the cross-feature regression bed and its one emergent finding |
