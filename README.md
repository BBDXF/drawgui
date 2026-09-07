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
repaint of pixels the clip removes. Rounded clips follow the curve. 28
properties are now fully implemented, 9 partially and 8 report `kUnsupported`.
`doc/clipping.md` records why a rounded clip needed no new damage rule, what it
costs, and where `overflow` deviates from CSS.

Text now falls back across scripts: one named family draws any string, and a
BCP 47 language tag selects between Han faces. `doc/font-fallback.md` records
why that chain is built here rather than delegated to fontconfig.

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

## Unit tests

`ctest` runs `drawgui_unit_test`, a doctest binary covering `dg::Expected`,
the golden-image comparator, damage, layout, clipping, hit testing,
interaction, UTF-8 decoding and font fallback. It can also be run directly for per-case output:

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
| `doc/development.md` | adding a property, the ABI lock, and running the sanitized suite |
