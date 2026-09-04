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

P0 (foundation) is in place: CMake build, verified prebuilt Skia, a CPU
raster path that produces a PNG, and a golden-image test pipeline. No
windowing, layout, widgets, theming, C ABI or JS yet - those are P1 onward.

## Platform scope

The current phase targets Linux and Windows desktop. macOS, Android and iOS
are deferred, with the platform abstraction shaped so they remain addable.
Only Linux is wired into the build so far; Windows needs its prebuilt Skia
asset hash registered in `cmake/FetchSkia.cmake`.

## Build prerequisites

- CMake >= 3.24
- Ninja
- clang or gcc with C++20 support
- FreeType development headers (`libfreetype-dev`)

`libskia.a` references `SkTypeface_FreeType` unconditionally, so FreeType is
required even though this phase draws no text.

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

## Unit tests

`ctest` runs `drawgui_unit_test`, a doctest binary covering `dg::Expected`
and the golden-image comparator. It can also be run directly for per-case
output:

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
