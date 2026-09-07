# CPU raster: what it costs, and whether it is enough

Measured findings from step 2 of the restarted plan — attaching Skia to the
window layer on the CPU, and timing it. Everything here is a recorded
measurement on this host, not an estimate. `examples/02_skia_cpu_gallery`
produces every number below and can be re-run.

Step 3 designs layout and widgets. This document exists so that it can be
designed against costs rather than assumptions, which is what design.md
section 5.15.7 asks for when it says a target without baseline hardware is
meaningless.

## How Skia reaches the screen

There is no GL context in this process. `SDL_CreateRenderer` is never called,
and `ldd` on the binary shows no `libGL` and no `libEGL` of our own.

```
dg::RasterSurface  ->  SkSurfaces::Raster (BGRA8888 premultiplied)
                       Skia draws with the CPU backend
   peek_pixels()   ->  { const uint8_t*, w, h, row_bytes, is_bgra8888 }
   present()       ->  row-wise memcpy into the window surface,
                       then a damage-rect flush to the display server
```

`WindowManager::present(id, image, dirty)` is the whole integration, and it
names no Skia type: an address, a shape, a row stride, a channel order and a
damage rectangle. The graphics layer owns the rasterizer; the window layer
owns the window; neither needs the other's vocabulary.

### Two traps that cost real time

**`kN32_SkColorType` disagrees with the library it ships with.** In the m153
headers that constant expands through `SK_R32_SHIFT`, which a consumer of the
prebuilt archive computes from build settings it does not have. It reads as
`kRGBA_8888` (4) here, while every surface the library actually returns is
`kBGRA_8888` (6):

```
header kN32_SkColorType          = 4   (kRGBA_8888)
SkImageInfo::MakeN32Premul(...)  = 6   (kBGRA_8888)   <- the truth
```

`MakeN32Premul` lives in the archive and is right; the constant is inline and
is wrong. Any code that branches on `kN32_SkColorType` swaps red and blue.
`RasterSurface::peek_pixels()` therefore asks the live surface.

**Channel order was verified by readback, not by eye.** A red/blue swap is
nearly invisible on a dark grey gallery. Six known colours were sampled out of
an `XGetImage` capture of the real window and compared byte for byte:

| where | expected | captured |
|---|---|---|
| background | `#14171C` | `#14171C` |
| blue swatch | `#2E86DE` | `#2E86DE` |
| green swatch | `#27AE60` | `#27AE60` |
| amber swatch | `#F6C445` | `#F6C445` |
| violet swatch | `#9B59B6` | `#9B59B6` |
| red arc wedge | `#E74C3C` | `#E74C3C` |

Exact, through raster → memcpy → display server → capture.

## Fonts

`SkFontMgr_New_Custom_Directory("/usr/share/fonts")` was chosen over
`SkFontMgr_New_FontConfig`. Both are compiled into the prebuilt archive, but
fontconfig would add `-lfontconfig` to a link line that is currently one
static archive plus `-lfreetype`, and the directory scanner finds everything
this host has: 16 families, including DejaVu, Ubuntu, Noto and WenQuanYi.

The cost of that choice is real and is a finding for step 3:

> `matchFamilyStyleCharacter()` returns **null** on the custom-directory font
> manager. It has no fallback chain at all, so every script must be selected
> by family name. CJK renders correctly here only because the gallery asks for
> `WenQuanYi Zen Hei` explicitly.

design.md section 5.10.4 builds emoji and CJK fallback on the same mechanism,
and section 5.13.5 needs per-language fallback chains for Han unification.
Neither is possible on this font manager. Step 3 will have to either take the
fontconfig dependency or build the fallback chain itself.

## What was measured

The gallery is authored at a fixed 1400x920 and scaled to the target, so the
**same scene** is rasterized at every resolution. A benchmark that changed
both the resolution and the content between samples would measure neither.

- Build: **Release** (`-O3 -DNDEBUG`), g++ 15.2.0. Debug numbers are not
  quoted anywhere in this document; they would misrepresent the decision.
- Host: Intel Core i5-1145G7 (Tiger Lake, 4 cores / 8 threads), 8 GB RAM,
  Linux under WSLg, `SDL_VIDEODRIVER=x11`. design.md section 5.15.7 sets the
  floor at "Intel UHD 620 class"; this is one generation above it, so these
  numbers are mildly optimistic against the stated floor.
- 200 timed frames per row after 12 warmup frames. Median and worst case, not
  mean — a mean hides exactly the long frame a user perceives as a stutter.

## Rasterization only (offscreen, no window)

Milliseconds per full-window repaint.

| size | median | p95 | worst | median fps |
|---|---|---|---|---|
| 800x600 | 1.39 | 2.21 | 2.50 | 717 |
| 1280x720 | 1.83 | 2.39 | 3.58 | 546 |
| **1920x1080** | **3.88** | **5.53** | **6.73** | **257** |
| 2560x1440 | 6.95 | 10.47 | 12.88 | 144 |

### Where the time goes

Measured by drawing the scene with one group removed, at 1920x1080:

| variant | median ms | attributable cost |
|---|---|---|
| full frame | 3.88 | — |
| without effects (blur, shadow, gradients) | 1.85 | **effects ≈ 2.03 ms (52%)** |
| without text | 3.40 | text ≈ 0.48 ms (12%) |
| geometry only | 2.14 | |
| text only | 0.70 | |
| effects only | 2.33 | |

The same shape holds at 2560x1440: effects ≈ 3.30 ms of 6.95 (47%), text
≈ 0.72 ms (10%).

**Blur and drop shadow are half the frame.** Text is not the CPU hotspot folk
wisdom expects — once glyphs are cached, drawing them is around a tenth of the
cost of the blurs. design.md section 5.3.3 already admits only one image
filter and section 5.15.7a already marks background blur as high-cost; this
measurement says that restraint is correct and, if anything, understated.

### Partial repaint

The same scene, drawn under a 260x72 clip — roughly a hover highlight or a
caret blink.

| size | full repaint | dirty rect 260x72 | ratio |
|---|---|---|---|
| 800x600 | 1.39 | 0.13 | 11x |
| 1280x720 | 1.83 | 0.11 | 17x |
| 1920x1080 | 3.88 | 0.12 | **32x** |
| 2560x1440 | 6.95 | 0.11 | **63x** |

The dirty-rect number is **flat at ~0.12 ms across every resolution**. This is
the single most important result in this document: with damage tracking, the
cost of a small change is a function of the change, not of the window. Without
it, every repaint is a function of the window and nothing else.

## On screen: rasterization versus presentation

Presentation is not rasterization, and conflating them would blame the
rasterizer for the compositor. Measured separately, in a real window.

| size | raster (ms) | present (ms) | total | median fps |
|---|---|---|---|---|
| 800x600 | 1.50 | 0.58 | 2.08 | 481 |
| 1280x720 | 2.02 | 2.13 | 4.15 | 241 |
| **1920x1080** | **4.54** | **7.71** | **12.25** | **82** |

| size | present, whole window | present, dirty rect |
|---|---|---|
| 800x600 | 0.58 | 0.07 |
| 1280x720 | 2.13 | 0.10 |
| 1920x1080 | 7.71 | 0.06 |

**At 1080p, getting the pixels onto the screen costs more than drawing them.**

That 7.71 ms is not the copy. A straight `memcpy` of one 1080p frame
(8.29 MB) measures **0.62 ms** on this host, so the row-wise blit into the
window surface is about 8% of it. The remaining ~7.1 ms is the display server
round trip through `SDL_UpdateWindowSurfaceRects` under WSLg.

This is environment-specific and should be re-measured on native X11, on
Wayland, and on Windows before it is treated as a property of CPU raster. What
is *not* environment-specific is the shape: presenting a full window is
proportional to the window, and presenting a damage rectangle is not.

## Verdict

**Is CPU raster good enough for a general-purpose GUI toolkit? Yes, up to and
including 1080p, and only with dirty-rect repaint above it.**

Concretely, against design.md section 5.15.8's 16.6 ms budget at 60 Hz:

- **Rasterization is not the bottleneck at or below 1080p.** A full-window
  repaint of a dense sixteen-panel scene is 3.88 ms median — 23% of the frame
  budget. There is comfortable headroom for a real widget tree on top.
- **Full-window repaint at 1080p is marginal, not comfortable.** Raster plus
  present is 12.25 ms median (82 fps), but the p95 pair is ≈17.3 ms, which is
  over budget. A toolkit that repaints the whole window every frame at 1080p
  will drop frames on this hardware.
- **Full-window repaint at 1440p and above is not viable.** Rasterization
  alone is 6.95 ms median and 12.88 ms worst; adding presentation exceeds the
  budget before any application code runs.
- **With dirty-rect repaint, CPU raster is comfortable everywhere measured.**
  0.12 ms raster plus 0.06 ms present is ~0.18 ms, about 1% of the budget, and
  it does not grow with resolution.

This confirms design.md section 5.15.2's two-megapixel threshold empirically —
1080p is 2.07 Mpx and is exactly where the margin disappears. It also amends
it: the section justifies the threshold by *rasterization* bandwidth, and the
measurement says **presentation** crosses the line first.

### What would force GPU

Each of these is a case where the damage region is the window, so dirty-rect
repaint cannot help:

1. **Sustained full-window animation at ≥1080p** — video, page transitions,
   parallax scrolling, anything where most pixels change every frame.
2. **Blur-heavy visual design.** Effects are ~50% of raster time already. A
   theme with frosted-glass surfaces over large areas at 1440p+ has no path to
   60 fps on the CPU.
3. **High-DPI.** 4K at 200% scaling is 8.3 Mpx, four times 1080p; linear
   extrapolation of the ladder above puts full-window rasterization near
   15 ms, i.e. the entire budget.

None of these is reached by an ordinary desktop application at 1080p, which is
why CPU raster is a legitimate primary path here and not merely the fallback
that design.md section 5.3.2 already refuses to call it.

## What step 3 should take from this

1. **Damage tracking is architecture, not optimization.** It is worth 32x at
   1080p and 63x at 1440p. Invalidation has to be designed before layout, not
   retrofitted after widgets exist.
2. **Damage tracking outranks `SkPicture` caching.** design.md section 5.15.2
   argues picture caching at repaint boundaries matters more than dirty
   rectangles. That argument is about traversal and recording cost, which this
   measurement does not include — but it also means picture caching does
   nothing for the presentation half, and presentation is the larger half at
   1080p. Both are needed; damage tracking is the one that helps everywhere.
3. **Budget blur explicitly.** Cap blurred area, or make blur a theme-level
   switch as section 5.15.7a already suggests.
4. **Text raster is cheap; text *shaping* is untested.** The 12% figure is
   `SkFont::measureText` and `drawString` with cached glyphs. It says nothing
   about HarfBuzz shaping or `SkParagraph` line breaking, which are not in
   this build. Do not read this as "text is solved".
5. **Font fallback is an open hole.** See the fonts section above.

## Honest limits of these numbers

- **This is a rasterizer benchmark, not a toolkit benchmark.** The gallery
  draws from a fixed list of function pointers. There is no render tree to
  traverse, no layout to run and no draw-command recording — and design.md
  section 5.15.2 says that traversal and recording, not rasterization, is
  usually where a retained-mode GUI spends its time. These figures are the
  floor a real frame builds on, not a prediction of one.
- **WSLg is not a native desktop.** The presentation number in particular
  should be re-measured on native X11, Wayland and Windows.
- **One host, one architecture.** x86_64 with AVX2. Skia's CPU backend is
  heavily SIMD-dependent and ARM has not been measured.
- **No GPU comparison exists yet.** "CPU is enough at 1080p" is a statement
  about the budget, not a claim that GPU would not be faster.

## Reproducing

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# offscreen raster ladder, size breakdown, category isolation, dirty rect
./build/examples/drawgui_skia_cpu_gallery --bench

# raster time and presentation time, separated, in a real window
./build/examples/drawgui_skia_cpu_gallery --bench-present --size 1920x1080

# the gallery itself - resize it, it re-renders
./build/examples/drawgui_skia_cpu_gallery

# one deterministic frame, no window needed
./build/examples/drawgui_skia_cpu_gallery --dump-png /tmp/gallery.png
```

The gallery is deliberately **not** a golden-image baseline. It draws system
fonts, so its output is a property of the host's font packages rather than of
drawgui, and adding it to `tests/golden/` would make the zero-tolerance suite
fail on any machine with a different DejaVu build.
