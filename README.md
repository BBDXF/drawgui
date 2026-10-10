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

The engine has layout, widgets, components, theming, a C++ application
runtime and a C ABI, all running on a CPU raster path with no GPU
dependency. Linux only; SDL3 is the sole window backend, with no
platform-abstraction layer ahead of a second one.

- **Runtime**: `dg::App` / `dg::Window` (`include/drawgui/app/`) is the
  public C++ entry point. Create nodes of 16 `NodeKind`s, set properties,
  call `App::run()` (or `pump()` yourself); the runtime does layout, paint,
  input routing, focus, popups, scrolling, list recycling and theming, and
  reports user actions as typed `dg::Event`s. Lists, dropdowns, tabs, trees,
  tables, sliders and radios are reached through typed refs
  (`window->list(node)` and friends). The lower-level objects (`LayoutTree`,
  `WidgetSet`, `Focus`, `PopupHost`, ...) stay public for callers that need
  them - see `doc/guide/app.md`.
- **Widgets**: 11 `WidgetKind` values - panel, label, button, checkbox
  (a radio is a grouped checkbox), slider, scroll view, text field,
  multi-line text area, virtualized list, dropdown, and the scrollbar part.
  Widgets can be disabled (the whole subtree greys out and follows theme
  switches) and choose a mouse cursor.
- **Components** (`include/drawgui/ui/`): `Menu`, `Dropdown`, `Tooltip`,
  `Dialog`, `ProgressBar`, `Tabs`, `TreeView` and `Table`, built on the
  widget layer rather than as new kinds; tree and table rows recycle through
  fixed node pools. Popups are overlay-only (`PopupHost`); the modal dialog
  is a full-window overlay with a focus trap.
- **Layout**: a Flexbox-like model (row/column, wrap, absolute stack;
  `justify` including `space_around`/`space_evenly`; `grow`/`shrink`/`basis`;
  percentage and `auto` sizes) laid out exactly once per frame -
  `LayoutStats` reports `nodes_visited == 0` for scrolling, slider drag,
  typing, list recycling, theme colour switches and focus changes alike.
  `RenderTree` still has no node-kind enum, and the six-value `LayoutKind`
  set has not grown since the first layout slice.
- **Properties**: 68 CSS-like properties addressable by numeric id
  (`doc/reference/properties.md`).
- **Text**: single- and multi-line, with CJK/BiDi/complex-script shaping via
  `SkParagraph` + libgrapheme (no `icudtl.dat` needed), wrapping labels,
  grapheme-cluster-correct editing, undo/redo, word moves, double/triple-click
  selection, max length, placeholders, and IME composition.
- **Images**: PNG, JPEG and WebP decoding, SVG icons drawn as vectors, four
  fit modes, and a draw-time tint that can follow a theme colour.
- **Input**: click counts, context clicks, pointer capture, Tab order via a
  focus-scope model with a visible focus ring, per-node mouse cursors, and a
  generated `action_id` family (11 intent actions - copy/cut/paste/select_all,
  keyboard scrolling, undo/redo) resolved through a four-level router, with a
  real platform clipboard.
- **Timers and wake**: one-shot and repeating timers, and a thread-safe
  wake / user event that unblocks a waiting event loop.
- **Windows**: several windows per app; title, size, position, min/max size,
  minimize/maximize/restore, fullscreen, resizable, icon, and state events.
- **Theming**: 20 tokens, light/dark variants, and external theme packages
  (path-traversal-safe, hot-reloadable).
- **C ABI** (version 0.2): a thin shell over `dg::App` - 87 exported
  functions (12 of them `dg_debug_*` test-only hooks), 16 node types, 68
  properties, 29 event kinds. It covers windows and window operations, every
  node kind above (`BOX` becomes a row/column container through
  `DG_PROP_DIRECTION`), menus, dialogs and tooltips, focus, timers, wake,
  images, insert/move/remove of whole subtrees (enough for a keyed
  reconciler) and themes. Everything is poll-based: a click, a menu choice,
  a dropdown change, a dialog answer or a tab/tree/table selection each
  arrive as one `dg_event`. Not exported: animation, custom paint, the
  gradient/shadow/transform setters and any IME composition surface.
  `examples/19_c_client` and `examples/28_c_components` are pure C programs
  built entirely on the ABI.

See `doc/limitations.md` for the full list of known gaps, costly corners
and things that are deliberately out of scope.

## Quick start

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

That builds the engine, the examples and the test suite. For build
prerequisites, a minimal C++ program, a minimal C-ABI program, and the
per-frame call order, see **`doc/getting-started.md`**.

## Documentation map

| Read this | If you want to |
| --- | --- |
| `doc/getting-started.md` | Build the project and write your first program (C++ or C ABI) |
| `doc/concepts.md` | Understand the engine's non-obvious design choices before writing code against it (two trees, side tables, damage, exactly-once layout, text does not participate in layout, integer pixels, zero virtual dispatch) |
| `doc/contracts.md` | See the 9 rules the caller must follow that the code does not enforce |
| `doc/limitations.md` | Know what will bite you before it does - costly corners, ABI capability limits, what the tests actually prove |
| `doc/guide/app.md` | Build an application on the `dg::App` / `dg::Window` runtime |
| `doc/guide/layout.md` | Work with the Flexbox-like layout model |
| `doc/guide/text.md` | Work with text display and editing |
| `doc/guide/widgets.md` | Use the 11 widget kinds and the ProgressBar/Tabs/TreeView/Table components |
| `doc/guide/theming.md` | Use or write a theme package |
| `doc/guide/input.md` | Work with pointer, keyboard, focus and shortcuts |
| `doc/guide/popups.md` | Build a dropdown, menu, tooltip or dialog on the popup host |
| `doc/guide/animation.md` | Drive animation and scrolling |
| `doc/reference/properties.md` | Look up a property by name or id |
| `doc/reference/c-abi.md` | Look up a C ABI function |
| `doc/reference/theme-tokens.generated.md` / `shortcuts.generated.md` | Look up a theme token or a shortcut action (generated) |
| `doc/contributing.md` | Add a property, extend the ABI, run the sanitized suite |
| `doc/design.md` | Read the full design document and its decision/risk log (Chinese) |

## Examples

30 examples, `examples/00_*` through `examples/29_*`. Each builds to
`build/examples/drawgui_*`. Most support `--dump-png out.png` (headless
render) and a `--verify-*` flag (headless assertion, used in CTest); there
is no `--help` - each example's flags are listed in the comment at the top
of its `main` source file. **Start with `examples/25_showcase`** - one window, built on the public
`dg::App` / `dg::Window` runtime, with a tab for every area (controls,
text, layout, data, popups, images, window) and a live light/dark switch;
`--verify-showcase` drives every tab with injected input.

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
| 10 | `scrolling` | Two independent scroll viewports, wheel and drag, clamped at content edges (low-level teaching example) | `--verify-scrolling` |
| 11 | `form_controls` | Checkbox, two radio groups, two sliders, a disable switch (on `dg::App`) | `--verify-form-controls` |
| 12 | `text_input` | Single-line editing, selection, clipboard, ellipsis, IME composition preview (on `dg::App`) | `--verify-text-input` |
| 13 | `image` | Decode + four fit modes (`fill`/`contain`/`cover`/`none`) plus a placeholder colour | `--verify-image` |
| 14 | `popup` | Overlay popup placement/dismissal straight on `PopupHost` (low-level teaching example) | `--verify-popup` |
| 15 | `list` | 1000-item virtualized list recycling a fixed row-node pool (on `dg::App`) | `--verify-list` |
| 16 | `complex_properties` | Gradient, hard-edged shadow, id-based image attach through the dedicated-setter channel | `--verify-complex-properties` |
| 17 | `animation` | Explicit animation, implicit hover transition, chained caret blink; `--idle-probe-ms` measures idle CPU | `--verify-animation` |
| 18 | `theme` | Every visual token-bound; click to switch light/dark with zero relayout | `--verify-theme` |
| 19 | `c_client` | Pure C program, two windows, cross-window click-to-recolour through the ABI (BOX/BUTTON, plus LABEL/CHECKBOX/TEXT_FIELD/LIST checks in the verify run) | `--verify-c-client` |
| 20 | `multiline_text` | Word wrap, unspaced CJK wrap, mixed-script/emoji, BiDi Arabic-in-Latin, ellipsis | `--verify-multiline-text` |
| 21 | `focus` | Tab/Shift-Tab order, `tab_index` override, popup focus confinement (low-level teaching example) | `--verify-focus` |
| 22 | `dropdown_menu` | Keyboard/mouse dropdown selection between two buttons (on `dg::App`) | `--verify-dropdown` |
| 23 | `menu_tooltip_dialog` | Context menu, hover tooltip and modal dialog through the runtime's `open_menu()`/`set_tooltip()`/`open_dialog()` (on `dg::App`) | `--verify-menus` |
| 24 | `theme_package` | External theme package load, hot reload, path-traversal rejection | `--verify-theme-package` |
| 25 | `showcase` | **Start here.** Every feature in one `dg::App` window: seven tabs (buttons/checkbox/radio/slider/disabled group/cursors/tooltips; text field, CJK+emoji text area, font sizes, wrapping; row/column, all `justify` modes, % widths, wrap; 1000-item list, tree, sortable table, timer-driven progress; dropdown, context menu, dialogs; PNG/JPEG/WebP + tinted SVG icons; window title/maximize/fullscreen, timer, `Waker`), live light/dark switch, last-event status line. `--dump-png FILE --tab N --theme light\|dark` | `--verify-showcase` |
| 26 | `text_area` | A heading row (a `BOX` made a row) over a multi-line `TEXT_AREA`, all through the C ABI: soft wrap, Enter, Up/Down column, Home/End per visual line, PageUp/PageDown, drag selection, scrollbar | `--verify-text-area` |
| 27 | `data_widgets` | Progress bars (determinate + indeterminate), tabs with a disabled tab, a 10,000-item tree and a 10,000-row sortable table on fixed node pools (on `dg::App`) | `--verify` |
| 28 | `c_components` | Pure C: a context menu, a dropdown, tooltips, a modal dialog, a slider, a radio group, a progress bar, tabs holding a tree, and a table the app sorts on request - all through the poll-based C ABI | `--verify` |
| 29 | `enterprise` | A realistic "资产管理" (asset inventory) app on `dg::App`: menu bar + shortcuts + context menus sharing one command switch, SVG icon toolbar with a toggle filter and search, a location tree filtering a sortable table with icon/check cells and row keys, detail tabs (form, undoable history list, tag rows via `set_user_data`), form dialogs (with a dropdown) and a disabled OK, compact rows (`row_height`), a hideable detail panel (`visible`), an unsaved-changes prompt on Ctrl+Q and the title-bar close (`CloseRequestedEvent`), fade-ins that respect reduced motion, status bar. `--dump-png FILE --theme light\|dark --state default\|menu\|context\|dialog` | `--verify` |

## Unit tests

`ctest` runs `drawgui_unit_test`, a doctest binary covering `dg::Expected`,
the golden-image comparator, damage, layout, clipping, compositing, hit
testing, interaction, UTF-8 decoding, font fallback, text editing, paragraph
layout, focus, scrollbars, cursors, images, the UI components, theme-package
security (path traversal, symlink escapes/loops, resource bounds, a fixed-seed
fuzz pass), shortcut routing, clipboard, node removal, the `dg::App` runtime
and the C ABI - **804 test cases**
(`./build/tests/drawgui_unit_test`'s own summary line is the source of
truth). Run it directly for per-case output:

```sh
./build/tests/drawgui_unit_test
```

## Golden-image tests

`tests/golden/baseline/` holds 8 committed PNGs, compared pixel-by-pixel at
**zero tolerance**. Four exercise bare `Canvas` primitives only - a clear,
a rounded rect, two strokes, and alpha blending - not text, not a node
tree, not layout, not a theme, not a widget; they are the raster floor.
The other four are built entirely through the C ABI and captured with
`dg_debug_capture_png()`: a label (`abi_label_text`), a checkbox off/on
(`abi_checkbox_off`/`abi_checkbox_on`), and a typed-into text field
(`abi_text_field`). `ctest` never regenerates them on its own:

```sh
cmake --build build --target golden_update
```

## Building

The Skia distribution is `BBDXF/libskia2` (`cmake/FetchSkia2.cmake`), a
purpose-built prebuilt Skia for self-drawn GUI frameworks. No OpenGL development
package is needed - the prebuilt ships `GrGLMakeNativeInterface_none`, so GL
entry points resolve at runtime through a caller-supplied proc loader and
nothing links against libGL.

The first configure downloads libskia2's release tarball into
`third_party/skia-prebuilt/` and the doctest single header into
`third_party/doctest-<version>/`, both SHA256-verified. Once present,
building without network access works; `-DDRAWGUI_BUILD_TESTS=OFF` skips
doctest. Prerequisites and the full build/prerequisite list live in
`doc/getting-started.md`.

All 42 CTest entries pass on Linux (`ctest --test-dir build -N`).
