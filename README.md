# drawgui

[中文](README.zh-CN.md)

drawgui is a lightweight, self-drawn GUI framework written in C++20. It draws
every pixel itself with Skia on the CPU, opens windows through SDL3, and also
exports a pure C ABI, so any language that can call C functions can use it.

There are two ways to use it:

- **C++**: the `dg::App` / `dg::Window` runtime. You create nodes, set
  properties, and read events. The runtime handles layout, painting, input,
  focus, popups and animation.
- **C**: a thin C ABI (`include/drawgui/abi/drawgui.h`) over the same runtime.

## What makes it different

- **Embeddable from any language.** The whole framework is reachable through a
  pure C ABI. A host language needs no Dart VM, no QML engine and no embedded
  browser, only the ability to call C functions. The ABI is generated from one
  definition file (`abi/drawgui.def.toml`) and locked, so accidental breaking
  changes fail the build.
- **One runtime, two front doors.** The C ABI is a thin shell over the same
  C++ `dg::App` runtime, not a second implementation. C and C++ programs get
  the same behaviour.
- **Does only the work that changed.** Layout runs at most once per frame and
  only for dirty nodes; scrolling, typing, slider drags, list recycling, focus
  changes and theme switches cause no relayout at all. Repaint is limited to
  the damaged rectangle.
- **Sleeps when idle.** With nothing animating, the event loop blocks and uses
  no CPU. Animations wake it only while they run.
- **Large data on a fixed budget.** Lists, trees and tables recycle a small
  pool of row nodes, so 10,000 rows cost about the same as 20.
- **Small dependency footprint.** CPU rendering with Skia, so no GPU and no
  libGL. Text shaping for CJK, BiDi and complex scripts works without ICU data
  files.
- **Built-in, not bolted on.** Keyboard navigation, focus traps in dialogs,
  shortcuts, tooltips, disabled states, theming and close confirmation are part
  of the components; the app does not write glue code for them.
- **Skia rendering quality.** Everything is drawn by Skia, the 2D engine
  behind Chrome and Flutter: anti-aliased shapes and rounded corners, smooth
  gradients, soft shadows, group opacity, vector SVG and high-quality text, the
  same on every machine. Lower-level C++ code can still reach the Skia canvas
  directly (see `examples/02_skia_cpu_gallery`); a custom-paint node inside the
  runtime is not available yet.
- **Flexbox layout.** A layout model close to CSS Flexbox: row and column
  direction, wrapping into several lines, `justify` (start/end/center,
  space-between/around/evenly), `align` and `align_self`, `align_content` for
  wrapped lines, `gap` and `run_gap`, `grow`/`shrink`/`basis`, min/max sizes,
  `aspect_ratio`, percent and `auto` sizes, and absolute positioning with
  `left`/`top`/`right`/`bottom`. Window resizes re-run only the parts that
  depend on the new size.
- **CSS-like styling.** 71 properties with familiar CSS names and meanings:
  margin and padding, borders and rounded corners, linear and
  radial gradients, box shadows, group opacity, and `overflow` clipping that
  also clips hit testing. Each property has a numeric id, and simple values are
  set from any language through one function, `dg_node_set_prop()`
  (gradients and shadows are C++ only for now).
- **Chinese and emoji out of the box.** One font family name covers Chinese,
  Japanese, Korean and 9 other scripts plus colour emoji through automatic
  font fallback. A language tag picks the right Han glyphs (Simplified,
  Traditional, Japanese). Chinese text wraps correctly without spaces, and
  editing moves by whole characters, so an emoji or a combined character is
  never split.
- **SVG icons as vectors.** SVG files are drawn as vectors, so icons stay
  sharp at any size, and they can be tinted with a theme colour so they follow
  light and dark themes. PNG, JPEG and WebP are supported too, with
  fill/contain/cover/none fit modes.
- **Checked by tests.** Every example has a self-check mode, rendering is
  compared against golden images pixel by pixel, and the suite runs under
  AddressSanitizer.

## Components

| Component | Description |
| --- | --- |
| Box | Container that lays out children as a row, column, wrap or absolute stack |
| Label | Text, single-line or wrapped, with ellipsis |
| Image | PNG/JPEG/WebP/SVG with fit modes and theme-coloured tint |
| Button | Push button; with `checked` it becomes a toggle button; icon and text centred |
| Checkbox | Check box with label |
| Radio | Radio buttons grouped by a shared group id |
| Slider | Horizontal value slider, mouse and keyboard |
| TextField | Single-line editor: selection, clipboard, undo/redo, placeholder, max length, IME |
| TextArea | Multi-line editor with soft wrap and per-line navigation |
| ScrollView | Scrollable area with scrollbars, wheel and drag |
| List | Virtualized list; rows can have icons, secondary text, check boxes and action buttons |
| Tree | Virtualized expandable tree with the same rich rows |
| Table | Virtualized table with sortable columns and rich rows |
| Dropdown | Selection popup, also usable inside dialogs |
| ProgressBar | Determinate or indeterminate progress |
| Tabs | Tab strip with pages and disabled tabs |
| MenuBar | Application menu bar with shortcut labels and F10/Alt keyboard mode |
| Menu | Context or popup menu, opened by mouse, Menu key or Shift+F10 |
| Tooltip | Hover tooltip on any node |
| Dialog | Modal dialog with buttons, optional form content and a focus trap |

Every node also supports the common properties: size, margin, padding,
colours, borders, rounded corners, gradients, shadows, opacity, `visible`,
`disabled`, mouse cursor and per-node user data (71 properties in total).

## Other features

- **Text**: CJK, emoji, BiDi and complex scripts, word wrap, selection,
  clipboard, undo/redo and IME input.
- **Input**: focus and Tab order, app-defined shortcuts (app-wide or per area),
  keyboard context menus, double and triple click.
- **Animation**: explicit animations and implicit transitions with easing
  curves, a finished event, and a reduced-motion switch.
- **Themes**: 20 design tokens, light and dark, and external theme packages
  that reload while the app runs.
- **Windows**: several windows per app, with title, size, position,
  minimize/maximize, fullscreen, icon and close confirmation.
- **C ABI** (version 0.2): 116 functions, 17 node types, 71 properties and 32
  event kinds. Events are polled; there are no callbacks.

## Examples

All examples are in `examples/` and are built with the project. Most have a
`--verify` style flag that runs a self-check without a window.

| # | Example | What it shows |
| --- | --- | --- |
| 25 | `showcase` | **Start here.** Every feature in one window, split into tabs |
| 29 | `enterprise` | A full asset-management app: menu bar, toolbar, tree, table, detail form, dialogs, shortcuts, light/dark theme |
| 11, 12, 15 | `form_controls`, `text_input`, `list` | Basic controls, text editing, a 1000-item list |
| 20, 26 | `multiline_text`, `text_area` | Wrapping, mixed scripts, multi-line editing |
| 22, 23 | `dropdown_menu`, `menu_tooltip_dialog` | Dropdowns, context menus, tooltips, dialogs |
| 27 | `data_widgets` | Progress bars, tabs, a 10,000-item tree and table |
| 13, 16, 17, 18, 24 | `image`, `complex_properties`, `animation`, `theme`, `theme_package` | Images, gradients and shadows, animation, themes |
| 19, 28 | `c_client`, `c_components` | Pure C programs using only the C ABI |
| 00–10, 14, 21 | low-level examples | Rendering, damage, layout, clipping, opacity, scrolling, popups and focus built directly on the engine layers |

## Progress

- Works on **Linux** only, with SDL3 as the window backend.
- Rendering is CPU-only; no GPU is needed.
- The runtime, the widgets, and the desktop-app features listed above are
  done and covered by tests: 43 CTest entries, 865 unit test cases and 8
  golden images, all passing.
- The C ABI is at version 0.2 and is not released yet, so it may still change.
- Not done yet: Windows, macOS and mobile, accessibility, submenus, multiple
  selection, cell editing, a custom-paint node, and language bindings (JavaScript is planned next).

See `doc/limitations.md` for the full list of known gaps.

## Build

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
./build/examples/drawgui_showcase
```

The first configure downloads a prebuilt Skia and doctest. See
`doc/getting-started.md` for prerequisites and first programs, and `doc/` for
the guides and references.
