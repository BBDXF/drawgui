# PopupHost, and the Popup window kind

Slice 5-2. design.md section 5.2 calls `PopupHost` "本设计最关键的一处" (the
single most critical decision in the whole design) and requires it to exist
as of MVP; `doc/completeness.md` section 7 escalated its absence to full
severity, above the ordinary decline table. This is the record of closing
that gap.

The short version:

- `PopupHost::show(...)` branches on `PlatformCaps::native_popup`: a real
  `SDL_CreatePopupWindow` window on the native side, an appended, clipped
  subtree of the caller's own `RenderTree` on the overlay side. **Both
  branches are real code paths, both are exercised every CTest run**, and the
  switch is a plain runtime `if` on a bool field passed in by the caller -
  zero `virtual`.
- `caps` is a **value the caller supplies**, not something `PopupHost` queries
  for itself. `WindowManager::platform_caps()` answers truthfully for this
  backend (`native_popup == true`, measured); a caller that wants to prove the
  overlay branch on this same desktop constructs its own
  `PlatformCaps{.native_popup = false}` instead. That is how one Linux machine
  drives both branches without a second platform or a mock.
- The equivalence oracle compares the SAME content-building call
  (`popup_scene::build_menu_content`), rasterized through the SAME `RenderTree`
  + CPU-raster `RasterSurface` pipeline, in two shapes: a fresh tree sized to
  the popup's own content (what the native branch's tree looks like), and a
  crop of the host window's own tree at the overlay's resolved position (the
  overlay branch, exercised for real through `PopupHost::show()`). Byte-for-
  byte identical. What this proves, and what it does not, is section 5 below.
- Zero new node/RenderObject kinds, zero new `WidgetKind`. The streak
  `doc/completeness.md` section 2 measured through 4-9, and 5-1 extended
  through `Image`, holds through 5-2 too - see section 3.
- Dropdown is **not** built this slice. A textless three-row menu is the
  demonstration client instead - see section 6 for why, and what is now
  unblocked.

---

## 1. What SDL3 actually offers, versus what design.md assumed

design.md section 5.2 does not name an SDL3 API at all - it states the
requirement (a real OS window that can escape the parent's bounds) and leaves
the mechanism to the implementing slice. `doc/platform-notes.md`'s P1 spike
already measured `SDL_CreatePopupWindow` doing exactly that, on both the x11
and wayland SDL video drivers, before any of this slice's code existed. This
slice's own, independent measurement through drawgui's own
`WindowManager::open_popup()` wrapper (not a standalone spike program)
confirms it again:

```
platform_caps.native_popup=1
popup drawable_size=160x104
closed ok, open_window_count=1
```

The exact signature, taken from SDL's own docs rather than assumed:

```c
SDL_Window *SDL_CreatePopupWindow(SDL_Window *parent, int offset_x, int offset_y,
                                   int w, int h, SDL_WindowFlags flags);
```

Two things this slice measured that were **not** already written down
anywhere in this project:

- **The offset is relative to the parent's own origin**, not the screen - so
  `WindowManager::open_popup()` takes the same coordinates `PopupHost`'s
  placement math already resolves, with no separate screen-coordinate
  conversion needed. This matches design.md's `anchor_rect` being expressed
  in the parent window's own space.
- **`flags` must carry `SDL_WINDOW_POPUP_MENU` or `SDL_WINDOW_TOOLTIP`**, and
  the two are genuinely different capabilities, not two names for the same
  thing: `SDL_WINDOW_POPUP_MENU` **can gain keyboard focus** (which is the
  only way an Escape key ever reaches it), while `SDL_WINDOW_TOOLTIP`
  **receives no input at all**. design.md's own window-kind table (section
  5.2) has a single `Tooltip` row ("无边框、不接收输入") that maps cleanly
  onto the second flag - this slice implements only the first
  (`WindowKind`-shaped as a single `Popup` semantic, matching Escape/
  click-outside dismissal), and declines the tooltip variant by name in
  section 6.

**A genuine, measured limitation design.md does not anticipate**: a headless
environment cannot create a real popup window at all, even though it CAN
create an ordinary top-level window. Measured directly for this slice:

```
$ SDL_VIDEODRIVER=dummy ./probe   # ordinary SDL_CreateWindow
-> succeeds

$ SDL_VIDEODRIVER=dummy ./probe   # SDL_CreatePopupWindow
-> SDL_CreatePopupWindow failed: That operation is not supported
```

Section 4 is what this forces onto the test design.

---

## 2. Keeping `virtual` out of a platform-branching abstraction

The established technique, already used by `WindowManager`'s own SDL3 backend
(no abstract `IPlatform`, per its own header comment): **write the concrete
thing first, branch on a plain data value, do not write an interface ahead of
a second implementation that would justify one.**

`PopupHost::show()` is one function with one `if (caps.native_popup)`. There
is no `IPopupBackend`, no factory, no virtual dispatch. The two branches are
not two classes implementing one interface; they are two blocks of one
function that both know how to produce a `PopupHandle` - a plain struct with
a `bool is_native` a caller reads to know which shape it got. This is
identical in spirit to how `Widget::group` turned radio into "checkbox plus a
field" rather than a class hierarchy (`doc/form-controls.md` section 1.1):
the two paths share almost everything (`PopupHandle`, dismissal routing,
placement math) and diverge only in how content is hosted, which is exactly
the shape that argues against an interface.

---

## 3. The second-root decision, and its defense

A popup is a genuinely new situation for this engine: for the first time,
one *frame* of the running program can have more than one root that needs
painting. The decision, made explicit rather than left implicit:

- **Native branch: a second `RenderTree` INSTANCE**, owned by `PopupHost`,
  one per open native popup. This is forced by the platform, not chosen for
  convenience: a second OS window needs a second `SkSurface`/`RasterSurface`,
  and `RenderTree::root()` is always `NodeId{0}` - two independent surfaces
  cannot share one tree's node table. It is a second instance of the
  **existing** class, never a new tree *type*. `WidgetSet`'s two-tree
  invariant (layout + render, never a third) is untouched: nothing here adds
  a node kind, a `RenderObject`, or a case `WidgetSet` would need to grow.
- **Overlay branch: no second instance at all.** Content is appended as
  ordinary children of the CALLER's own `RenderTree`, through
  `RenderTree::add_child()` directly - **not** through the caller's
  `LayoutTree`. This was verified safe, not assumed: `LayoutTree::layout()`
  and `layout_full()` walk `LayoutTree::Impl`'s own node vector (`nodes`,
  sized to exactly what `LayoutTree::add_child()` created -
  `src/layout/layout_tree.cpp` line 322's `node_count()` returns
  `impl_->nodes.size()`, never `RenderTree::node_count()`), so a node added
  directly to the `RenderTree` sits entirely outside `LayoutTree`'s
  bookkeeping without corrupting it. **The real cost this imposes, named
  rather than hidden**: a host window that manages its own scene through a
  `LayoutTree` must not call `layout()`/`layout_full()` again while an
  overlay popup added this way is open. Nothing prevents that call from being
  made - the safety is structural (an untouched vector cannot desync) rather
  than a checked invariant - and a future slice building a real menu on this
  infrastructure needs to either keep that restriction or extend `LayoutTree`
  to track escape-hatch nodes explicitly.
- **Closing an overlay popup does not remove its nodes.** `RenderTree` is
  append-only (`doc/widgets.md`), so `close()` clips the popup's container to
  an empty rectangle instead - the same technique `doc/clipping.md` section 2
  already establishes ("a clipping node with no area removes its whole
  subtree and is itself hittable nowhere"). This is a real, permanent
  per-open cost: every overlay popup ever shown leaves a handful of dead
  nodes in the tree forever. Accepted and named, not hidden - the same trade
  this project already makes for every other never-removed widget.

## 3.1. design.md section 5.6 line 622, re-verified

**Zero new `RenderObject`/node kinds were needed.** `NodeStyle` gained no
field this slice; `Overflow::kClip` (existing, 4-4) is what confines an
overlay's content, and `add_child` (existing) is what a native popup's own
tree uses to build content. The streak `doc/completeness.md` measured through
4-9 and 5-1 extended through `Image` holds through 5-2: this is now
`Box`/`Text`/`Button`/`Checkbox`/`Radio`/`ScrollView`/`Slider`/`TextField`/
`Image`/`PopupHost`, ten slices deep, zero new primitives.

---

## 4. What the both-branches equivalence oracle proves, and what it cannot

**The claim, stated precisely**: the same popup content, built by the same
function call, rasterizes to the same bytes whether it is hosted by a fresh
`RenderTree` (what the native branch's own tree looks like) or by a subtree
of the host window's tree, cropped at the resolved position (the overlay
branch, exercised for real through `PopupHost::show()`).

**What it does prove**: content-level equivalence through the real graphics
pipeline. Both sides run the actual `RenderTree`/CPU-raster `RasterSurface`
code this engine ships, not a stand-in; the only difference between them is
which tree the content nodes belong to and where that tree's own coordinate
origin sits, which is exactly the difference `PopupHost`'s two branches are
supposed to make invisible to a caller building content once.

**What it does NOT prove, stated as plainly as the claim itself**: that a
genuine second OS window's own presentation path (a second SDL surface,
`SDL_UpdateWindowSurfaceRects`) reproduces this. It cannot, honestly, because
whole-screen identity between "pixels inside a truly separate window" and
"pixels inside a cropped region of a shared one" was never a coherent claim -
the task that specified this oracle says so explicitly, and this document
records the same boundary rather than blur it. That second claim - a real
popup window's surface matches what its own tree/surface pair would produce
offscreen - is exactly what every OTHER window in this codebase already
relies on (`WindowManager::present()`'s own byte-for-byte copy contract,
measured since sub-step 2), so it is not re-proven here; it would be
re-deriving something this project already established rather than testing
anything new about `PopupHost`.

**Why the CTest entry does not attempt a real second OS window at all** -
measured, not a convenience: `examples/14_popup --verify-popup` sets
`SDL_VIDEODRIVER=dummy` before touching `WindowManager`, which is enough to
open an ordinary window headlessly (proven - the overlay branch and every
dismissal check run against a real, dummy-backed `WindowManager` and
`PopupHost`) but **not** enough for `SDL_CreatePopupWindow`, which fails with
"That operation is not supported" under that driver (section 1's
measurement). The check still ATTEMPTS the native branch, once, and prints
the failure and the reason **loudly and specifically** rather than skipping
silently - matching the task's own instruction. The real second OS window is
exercised instead by `examples/14_popup`'s interactive mode against this
sandbox's actual display (WSLg, `DISPLAY=:0`), which is where section 1's
`platform_caps.native_popup=1` / `popup drawable_size=160x104` measurement
came from - a real run, not a hypothetical one, matching
`examples/01_sdl3_multi_window`'s own precedent of "needs a display, so no
CTest entry, but still built and still real."

---

## 5. Cross-window dismissal routing

This turned out to be the easy half, not the hard one the task predicted -
and the reason is that the hard-looking part was already built. design.md
section 5.2 requires "所有输入事件带 `window_id`" (every input event carries
a window id); `PointerEvent::window` and `KeyEvent::window` have carried one
since slice 4-6's multi-window work, unrelated to popups entirely. Nothing
had to be added.

`WindowManager::pump()` already aggregates events from every open window -
host and popup alike - into one `PumpResult`, because it always did (it has
managed N independent windows since 4-6). So a click on the host window while
a native popup is open, and a click on the popup itself, arrive through the
identical call, distinguished only by which `WindowId` the event names.
`PopupHost::handle_pointer()`/`handle_key()` are pure functions over
`(handle, event_window, event)` - no SDL, no special cross-window plumbing,
just a comparison of `event_window` against `handle.window` (native) or a
point-in-rectangle test against `handle.content_bounds` (overlay).

The one genuine asymmetry: **Escape only reaches a native popup while it
holds keyboard focus**, and `SDL_WINDOW_POPUP_MENU` (this slice's flag
choice, section 1) can gain focus but is not GUARANTEED to hold it the
instant it opens - a caller that opens a popup without a following click into
it may find Escape still routes to the parent's window id first. This
project's own interaction model already has an answer shape for "state that
depends on what has focus" (`dg::Focus`, 4-9's `TextField`), but nothing
wires window-level (as opposed to widget-level) focus tracking yet - named
here as a real, un-worked edge rather than silently assumed away.

---

## 6. Dropdown: deferred, by name, with the prerequisite now satisfied

**Not built this slice.** The task explicitly sanctions either outcome
("build a minimal dropdown... or land PopupHost with a non-dropdown
demonstration"); cross-window event routing and the second-root question
(section 3) consumed the budget a real `WidgetKind::kDropdown` would need on
top. The demonstration client is a textless three-row menu
(`popup_scene::build_menu_content`) instead - deliberately not a widget: it
proves `PopupHost` is usable (a caller builds content once, gets it hosted
two ways, dismisses it through one small API) without also having to prove a
new interaction model.

**What IS now satisfied, precisely**: `doc/form-controls.md` section 2.4
named the exact prerequisite 4-8 was blocked on - "a `Popup` window kind in
`WindowManager`... the SDL3 backend implementing whatever
`SDL_WINDOW_POPUP_MENU`/positioning API... a `PlatformCaps`-shaped capability
query." All three exist now: `WindowManager::open_popup()`/`close_popup()`,
`PlatformCaps::native_popup`, and `WindowManager::platform_caps()`. The next
slice to build `Dropdown` needs no further platform work - it needs a
`WidgetKind` (the 8th) whose open/closed list is exactly the
`kColumn`-of-plain-rows shape `doc/scrolling.md` section 1 and
`doc/form-controls.md` section 2.3 already proved settled, shown through
`PopupHost::show()` at the field's own anchor rect.

**Explicitly declined by name, matching the task's list, plus what this
slice specifically found while building `PopupHost`**:

- Nested/cascading submenus - no second popup ever opens from within a first
  one; `PopupHost` holds no parent-popup relationship.
- Popup animations (open/close fade or slide) - no animation clock exists
  project-wide (`doc/scrolling.md` section 1's fling, `doc/text-input.md`
  section 9's caret blink - the same absence, a third time).
- Drag-out-of-popup gestures.
- Accessibility/screen-reader hooks.
- Menu keyboard navigation beyond Escape (arrow-key item selection, Enter to
  activate).
- The full per-window `FocusManager` design.md section 5.2 also asks for -
  already assigned to P4 by slice 4-9, and section 5 above names the one
  place its absence is felt (window-level, not widget-level, focus).
- Windows/macOS platform work - `doc/platform-notes.md`'s own P1 spike
  already scoped this the same way ("Windows and macOS are not measured yet").
- `SDL_WINDOW_TOOLTIP` (the no-input popup variant) - section 1's measured
  distinction from `SDL_WINDOW_POPUP_MENU`; this slice's `Popup` kind takes
  input (needed for Escape), a tooltip kind is a different, un-built flag
  choice.
- Horizontal placement clamping/flipping - only the vertical below/above flip
  is built (`resolve_popup_placement()`); an anchor near the left or right
  edge is not re-centred. `anchor_rect` exists for exactly this reason per
  the task's own framing, and only half of what it enables is built.
- `WindowKind::kDialog` - design.md's four-row window-kind table names it;
  nothing in this slice's scope (menus/tooltips/dropdowns) needs it.

---

## 7. Verification performed

- **19/19 CTest** (18 inherited + `popup.verify_demo_scene`), including under
  `-DDG_SANITIZE=ON` (both invocations, split to fit the sanitized suite's
  slower runtime - `widgets.interaction_equals_full` alone took 141s
  instrumented).
- `./build/examples/drawgui_render_png` sha256
  `f635028e6e1e92349d23f0575f1e39e7ca8b05e5974492641ee610fe520df12e` -
  unchanged.
- One `-Werror` configure+build each, g++ 15.2.0 and clang++ 21.1.8, both
  clean.
- clang-tidy `-p build` and clang-format on every changed/new file: exit 0,
  zero `NOLINT`. `.github/workflows/ci.yml`'s hand-maintained TU list gained
  `src/window/popup_host.cpp` and all four of `examples/14_popup`'s `.cpp`
  files.
- A real interactive run against this sandbox's actual display (section 1's
  measurement), plus a direct probe through `WindowManager::open_popup()`
  confirming position/size/parent linkage - not re-derived from
  `doc/platform-notes.md`'s standalone spike, measured again through this
  slice's own production wrapper.

## 8. Property status after this slice

Unchanged: 49 properties, 35 implemented / 10 partial / 4 not-yet.
`PopupHost` added no property - its content is built through plain
`RenderTree::add_child()` calls, the same as every flat-colour demo scene
before it (`doc/damage-repaint.md`, `doc/compositing.md`'s own examples).
`doc/properties.md` needs no edit.

## 9. Cross-reference: 7-4 landed — window-level focus tracking (append-only)

Section 5 named the one real, un-worked edge this slice found: "nothing
wires window-level (as opposed to widget-level) focus tracking yet." Slice
7-4 is that follow-up, and the answer it
found is structural rather than a new mechanism: a native popup already
forces a second `RenderTree` instance (section 3 above), so 7-4 gives it
an entirely separate `dg::Focus` (and `WidgetSet`) too, rather than a
single global focus keyed by a `NodeId`-plus-`window_id` compound. Two
independent `Focus` instances never share a `NodeId` numbering space, so
there is nothing for a cross-window struct to disambiguate — confirmed
directly (`examples/21_focus`'s own structural check hands two separate
windows' first buttons the IDENTICAL numeric `NodeId` and shows each
`Focus` still names only its own). The Escape/native-popup-focus edge
this section named is unaffected by 7-4 either way — it was always an SDL/
window-manager-level question (does `SDL_WINDOW_POPUP_MENU` currently hold
keyboard focus), not one this engine's own `Focus` concept was ever going
to answer. For the OVERLAY branch, which shares the host's own `RenderTree`
and therefore its own `Focus`, 7-4 adds `enter_scope()`/`exit_scope()` so
Tab does not leak out of an open popup into the host window behind it, and
`exit_scope()` blurs a focused widget still inside the popup when it
closes — the append-only-but-invisible-content hazard section 3 above
already named ("a handful of dead nodes... accepted and named") now has a
FOCUS-side answer to match: closing does not delete the nodes, but it does
stop `Focus` from still pointing at one. `doc/focus.md` is the full
record, including a real bug 7-4's own build found while attaching an
overlay popup's buttons: they must be attached to the HOST's own
`WidgetSet` (never a second one) for `dg::focus_order()`'s tree walk ever
to see them — a finding this document's own section 3 did not anticipate
because `WidgetSet` predates 7-4's own Tab-order consumer of it.

## 10. Cross-reference: 7-5b landed — `SDL_WINDOW_TOOLTIP` threaded, and `Dialog`'s own window (append-only)

Section 6's own declined-by-name list named `SDL_WINDOW_TOOLTIP` and
`WindowKind::kDialog` explicitly. Slice 7-5b (`doc/menus.md` section 12) is that follow-up. `WindowManager::open_popup()`
gained a trailing `PopupWindowKind kind = PopupWindowKind::kMenu`
parameter — every existing call site (this document's own, and every
popup/dropdown client since) keeps requesting `SDL_WINDOW_POPUP_MENU`
unchanged; `kTooltip` requests `SDL_WINDOW_TOOLTIP` instead, threaded
through `PopupHost::show()`'s own new trailing parameter of the same type.
Measured directly, matching this document's own section 1 methodology: a
tooltip window fails to create under `SDL_VIDEODRIVER=dummy` with the
IDENTICAL "That operation is not supported" message a menu-flagged popup
already does — the dummy driver's limitation is about `SDL_CreatePopupWindow`
itself, not about which flag it is asked for.

`Dialog` is a genuinely separate window-layer addition, `WindowManager::
open_dialog()`, deliberately NOT built on `PopupHost` — a real,
ordinary top-level window (`SDL_CreateWindow`, unlike `open_popup()`'s
`SDL_CreatePopupWindow`) additionally given real OS ownership/modality
(`SDL_SetWindowParent()` then `SDL_SetWindowModal()`, in that documented
order) and an opt-in `WindowSpec::cancellable_close` for design.md section
5.2's own cancellable `on_close_request`. This is a peer window kind to
`Popup`, not a variant of it, matching design.md's own four-row window-kind
table naming `Normal`/`Dialog`/`Popup`/`Tooltip` as siblings rather than a
hierarchy. `doc/menus.md` section 12.3/12.4 has the full record, including
why the modal FOCUS trap this engine's own `dg::Focus` enforces is a
separate concern from this window's own real OS-level modality, and the
measured failure of `SDL_SetWindowParent()`/`SDL_SetWindowModal()` under
`SDL_VIDEODRIVER=dummy`.
