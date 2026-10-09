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
raster path with no GPU dependency. Linux only; SDL3 is the sole window
backend, with no platform-abstraction layer ahead of a second one.

- **Layout**: a Flexbox-like model laid out exactly once per frame -
  `LayoutStats` reports `nodes_visited == 0` for scrolling, slider drag,
  typing, list recycling, theme colour switches and focus changes alike.
  `RenderTree` still has no node-kind enum, and the six-value `LayoutKind`
  set has not grown since the first layout slice.
- **Properties**: 51 CSS-like properties addressable by numeric id.
- **Widgets**: 11 `WidgetKind` values - panel, label, button, checkbox
  (radio is a `group` field on it), slider, scroll view, text field,
  multi-line text area, virtualized list, dropdown, and the scrollbar part. Context menu, tooltip and modal dialog are
  **not** separate widget kinds - they are application-layer recipes built
  on a shared, overlay-only popup host (`kDropdown` itself is real: it
  owns an anchor, an option list and a selected index; the popup *list*
  under it is assembled by the caller out of ordinary `kButton` rows).
  `src/widget/tooltip.cpp` is a 20-line hover timer, not a control.
  The data controls `ProgressBar`, `Tabs`, `TreeView` and `Table`
  (`include/drawgui/ui/`, `examples/27`) are component-layer builders too,
  not new kinds: tree and table rows recycle through `kList`'s pool.
  `Dialog` (`examples/23`) is the one exception: it is a real, separate
  top-level OS window opened via `open_dialog()`, never built on the popup
  host.
- **Popups**: overlay-only. An earlier native-OS-window branch existed and
  was removed (`48c9cd3`) after three native-only defects were found; there
  is no code path left that opens a second OS window for a dropdown, menu
  or tooltip.
- **Text**: single- and multi-line, with CJK/BiDi/complex-script shaping via
  `SkParagraph` + libgrapheme (no `icudtl.dat` needed), grapheme-cluster-
  correct editing, and IME composition.
- **Focus**: Tab order via a focus-scope model, with a visible focus ring.
- **Shortcuts / clipboard**: a generated `action_id` family (8 intent
  actions - copy/cut/paste/select_all, keyboard scrolling) resolved through
  a four-level router, and a real platform clipboard seam on
  `WindowManager`.
- **Theming**: 20 tokens, light/dark variants, and external theme packages
  (path-traversal-safe, hot-reloadable).
- **C ABI** (version 0.2): 85 exported functions (12 of them `dg_debug_*`
  test-only hooks) - still a smaller surface than the C++ engine. It can
  open and operate windows (title, size, position, min/max size,
  minimize/maximize/restore, fullscreen, resizable, icon, state query);
  create fourteen node types (`BOX`, which `DG_PROP_DIRECTION` turns into a
  row/column container, `BUTTON`, `LABEL`, `CHECKBOX`, `TEXT_FIELD`,
  `TEXT_AREA`, `SCROLL_VIEW`, a virtualized text `LIST` and `IMAGE` with
  PNG/JPEG/WebP/SVG decoding, plus the components `DROPDOWN`, `PROGRESS`,
  `TABS`, `TREE` and `TABLE`); open menus, modal dialogs and tooltips; set
  61 properties including font size, text
  alignment, percentage sizes, tab order and an editor's max length; move
  keyboard focus (`dg_node_focus()`, Tab/Shift+Tab); read back checkbox
  state, editor text, list selection and scroll offsets; receive click
  (with click count), context-click, focus, list-select, text-changed,
  action, timer, cross-thread user and window-state events; and load a
  theme. Nodes can be inserted before a sibling, moved within a window and
  removed as whole subtrees, so a keyed reconciler can drive the tree. The
  components are poll-based like everything else: a menu choice, a dropdown
  change, a dialog answer, a tab/tree/table selection or a sort request each
  arrive as one `dg_event`. Not exported: animation and any IME composition
  surface. `examples/19_c_client` and `examples/28_c_components` are pure C
  programs built entirely on the ABI.

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
| `doc/contracts.md` | See the 8 rules the caller must follow that the code does not enforce |
| `doc/limitations.md` | Know what will bite you before it does - costly corners, ABI capability limits, what the tests actually prove |
| `doc/guide/layout.md` | Work with the Flexbox-like layout model |
| `doc/guide/text.md` | Work with text display and editing |
| `doc/guide/widgets.md` | Use the 11 widget kinds and the ProgressBar/Tabs/TreeView/Table components |
| `doc/guide/theming.md` | Use or write a theme package |
| `doc/guide/input.md` | Work with pointer, keyboard, focus and shortcuts |
| `doc/guide/popups.md` | Build a dropdown, menu, tooltip or dialog on the popup host |
| `doc/guide/animation.md` | Drive animation and scrolling |
| `doc/reference/properties.md` | Look up a property by name or id |
| `doc/reference/c-abi.md` | Look up a C ABI function |
| `doc/contributing.md` | Add a property, extend the ABI, run the sanitized suite |
| `doc/design.md` | Read the full design document and its decision/risk log (Chinese) |

## Examples

29 examples, `examples/00_*` through `examples/28_*`. Most support
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
| 14 | `popup` | Overlay popup placement/dismissal (`PopupHost` is overlay-only, `doc/guide/popups.md`) | `--verify-popup` |
| 15 | `list` | 1000-item virtualized list on a 14-node pool; `--bench` compares against a non-virtualized baseline | `--verify-list` |
| 16 | `complex_properties` | Gradient, hard-edged shadow, id-based image attach through the dedicated-setter channel | `--verify-complex-properties` |
| 17 | `animation` | Explicit animation, implicit hover transition, chained caret blink; `--idle-probe-ms` measures idle CPU | `--verify-animation` |
| 18 | `theme` | Every visual token-bound; click to switch light/dark with zero relayout | `--verify-theme` |
| 19 | `c_client` | Pure C program, two windows, cross-window click-to-recolour through the ABI (BOX/BUTTON, plus LABEL/CHECKBOX/TEXT_FIELD checks in the verify run) | `--verify-c-client` |
| 20 | `multiline_text` | Word wrap, unspaced CJK wrap, mixed-script/emoji, BiDi Arabic-in-Latin, ellipsis | `--verify-multiline-text` |
| 21 | `focus` | Tab/Shift-Tab order, `tab_index` override, popup focus confinement | `--verify-focus` |
| 22 | `dropdown_menu` | Keyboard/mouse dropdown selection between two real siblings | `--verify-dropdown` |
| 23 | `menu_tooltip_dialog` | Context menu and tooltip built on the overlay popup host, plus a modal `Dialog` that is a real, separate OS window (`--branch native\|overlay` compares two ways of opening it) | `--verify-menus` |
| 24 | `theme_package` | External theme package load, hot reload, path-traversal rejection | `--verify-theme-package` |
| 25 | `showcase` | **Start here.** All 9 `WidgetKind`s, menu/tooltip/dialog, live theme switch, CJK text and shadow, combined in one scene | `--verify-showcase` |
| 26 | `text_area` | A heading row (a `BOX` made a row) over a multi-line `TEXT_AREA`, all through the C ABI: soft wrap, Enter, Up/Down column, Home/End per visual line, PageUp/PageDown, drag selection, scrollbar | `--verify-text-area` |
| 27 | `data_widgets` | `ProgressBar` (determinate + indeterminate), `Tabs` with a disabled tab, a 10,000-item `TreeView` and a 10,000-row sortable `Table` on fixed node pools | `--verify` |
| 28 | `c_components` | Pure C: anchored and context menus, a dropdown, tooltips, a modal dialog, a progress bar, tabs holding a tree, and a table the app sorts on request - all through the poll-based C ABI | `--verify` |

## Unit tests

`ctest` runs `drawgui_unit_test`, a doctest binary covering `dg::Expected`,
the golden-image comparator, damage, layout, clipping, compositing, hit
testing, interaction, UTF-8 decoding, font fallback, text editing, paragraph
layout, focus, dropdown, tooltip timing, theme-package security (path
traversal, symlink escapes/loops, resource bounds, a fixed-seed fuzz pass),
shortcut routing, clipboard, and node removal - **572 test cases**
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

Separately, a past measurement (recorded in a since-removed development-slice
doc, see git history) found that `examples/00_cpu_raster_
png`'s output sha256
(`f635028e6e1e92349d23f0575f1e39e7ca8b05e5974492641ee610fe520df12e`) was
measured unchanged across the Skia-provider switch, on both gcc and clang.
That is a manually-recorded comparison from one slice, not a golden-image
baseline and not a CI gate - there is no test target that checks this hash.

## Building

The Skia distribution is `BBDXF/libskia2` (`cmake/FetchSkia2.cmake`), a
purpose-built prebuilt Skia for self-drawn GUI frameworks; `doc/skia-
dependency.md` records the switch from an earlier rust-skia-based setup
that was never actually adopted past evaluation. No OpenGL development
package is needed - the prebuilt ships `GrGLMakeNativeInterface_none`, so GL
entry points resolve at runtime through a caller-supplied proc loader and
nothing links against libGL.

The first configure downloads libskia2's release tarball into
`third_party/skia-prebuilt/` and the doctest single header into
`third_party/doctest-<version>/`, both SHA256-verified. Once present,
building without network access works; `-DDRAWGUI_BUILD_TESTS=OFF` skips
doctest. Prerequisites and the full build/prerequisite list live in
`doc/getting-started.md`.

All 35 CTest entries pass on Linux (`ctest --test-dir build -N`).
