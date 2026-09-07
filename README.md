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
path that produces a PNG, a golden-image test pipeline, and a concrete SDL3
multi-window manager with a demo that puts several windows on screen at once.

Skia and the window manager do not meet yet - how a surface attaches to a
window, and whether it does so on CPU or GPU, is the next question and is
deliberately still open. There is no layout, no widget, no theming and no C
ABI.

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

FreeType is the only external library. The prebuilt includes the Ganesh GL
backend, but no OpenGL development package is needed: the build ships
`GrGLMakeNativeInterface_none`, so every GL entry point is resolved at runtime
through a proc loader the caller supplies, and nothing links against libGL.

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
