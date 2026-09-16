# Slice 4-9: text input and editing — an ASCII single-line `TextField`, a `Focus` concept, and why typing never relays out (yet)

What was built (a `TextField`, keyboard + text-input plumbing, a focus
concept), every scope decision made and why, the relayout finding the task
demanded be worked out rather than assumed, the defect-injection campaign
(a sixth failure mode found), and verification performed.

The short version:

- **`TextField` is a seventh `WidgetKind`** — a `kLeaf` with `overflow:
  kClip` and three plain children (`selection_highlight`, `content`,
  `caret`) the widget positions at paint time, the identical composition
  `kCheckbox`'s indicator and `kSlider`'s track/thumb already are. No new
  `RenderObject`, matching design.md section 5.6 line 622's acceptance bar
  the same way `doc/form-controls.md` already proved it for `Slider`.
- **Scoped to design.md's own MVP escape hatch, in full**: ASCII-only
  direct input (bytes `0x20`-`0x7E`), single-line, no IME, matching
  design.md line ~547-548's explicit concession verbatim rather than
  building past it.
- **Grapheme clusters are honoured by construction, not by a library**:
  design.md line ~1001-1003 makes grapheme-cluster granularity mandatory
  for cursor movement/selection/backspace. For ASCII, a byte offset, a
  codepoint offset and a grapheme-cluster boundary are the same number, so
  scoping content to ASCII satisfies the letter of that requirement with
  zero ICU/HarfBuzz. Non-ASCII input is filtered at the boundary, not
  mis-segmented — section 1.2 below is the argument in full, and names
  what future work removes the restriction.
- **IME's interface slot is reserved for real, not as an unused
  placeholder**: `WindowManager::start_text_input()`/`stop_text_input()`
  are a genuine SDL3 passthrough, because SDL3 will not generate
  `SDL_EVENT_TEXT_INPUT` at all — IME active or not — until
  `SDL_StartTextInput()` has been called. This is required plumbing for
  plain ASCII typing to work in the first place, not a P7 placeholder built
  ahead of a consumer. `SDL_EVENT_TEXT_EDITING` (the composition preview)
  is deliberately not read anywhere — that is the actual IME feature, and
  it stays P7's, per design.md line ~1719.
- **No `SkParagraph`, no multi-line reflow** — a single-line field with
  ellipsis-on-overflow-while-unfocused and horizontal-scroll-on-overflow-
  while-focused, built directly on `SkFont::measureText` (the same
  primitive `src/render/skia_paint.cpp` already paints every string
  through). Section 1.4 is the scoping argument.
- **Text content, cursor and selection are runtime widget state, not
  properties** — the identical argument `doc/scrolling.md` section 2 and
  `doc/form-controls.md` section 1.3 already make for the scroll offset and
  the slider's value, extended one control further. Section 2 restates it.
- **A `dg::Focus` concept had to be introduced** — this engine had none.
  One optional `NodeId`, exclusive, no tab order, no focus tree — the
  simplest model this slice's two-field demo needs, matching
  `doc/widgets.md`'s own rule that an interface is extracted from a working
  implementation rather than written ahead of one. Section 3 is the
  argument.
- **Typing costs a repaint and never a relayout — but the reason is
  narrower than scroll's or slider's, and the task asked for the exact
  condition rather than the analogy.** Section 4 works it out: this
  slice's `TextField` is fixed-width, exactly like `Slider`'s track, so
  every `text_field_*` mutation stays inside `RenderTree`
  (`set_text()`/`set_local_bounds()`/`set_local_origin()`) and never
  reaches `LayoutTree::set_box()` — confirmed, not assumed, via
  `LayoutStats` on the actual demo scene. The condition under which typing
  *would* force a relayout — a shrink-to-fit `TextField` whose own width is
  derived from its content, unbuilt in this slice — is named explicitly.
- **14 defect injections, 12 caught (after closing 6 genuine test gaps), 2
  provably inert.** Section 7 is the full table. A sixth project failure
  mode is named: **a weak assertion on the right scene** — the code path
  and the scene that would reveal a defect were already exercised by an
  existing test, but that test's assertion was satisfied by a whole family
  of wrong answers, not just the right one.

---

## 1. The scoping decisions, made in writing

### 1.1 The MVP bar, confirmed from design.md itself before writing code

Design.md's MVP-8 widget list (line ~615) is: `Box` `Text` `Button`
`TextField` `ScrollView` `List` `Image` `Row/Column`. **`TextField`, not
`TextArea`** — single-line is the target this slice is scoped to, not an
arbitrary simplification. Design.md line ~547-548 states the MVP
concession for it directly, quoted because this slice is built to satisfy
it rather than to exceed it: **"MVP 的 `TextField` 只保证 ASCII 直接输入与
已确认文本的正确显示"** (the MVP `TextField` only guarantees ASCII direct
input and correct display of already-committed text). Everything in
section 5.5's wish list beyond that — modifier-key routing, right-click
menus, drag-and-drop, cross-line selection, double/triple-click word/line
selection, pointer capture across window bounds — is explicitly out for
the identical reason `doc/form-controls.md` section 2 declined dropdown:
naming a real prerequisite rather than half-building past a stated MVP
boundary.

### 1.2 Grapheme clusters: honoured by construction, not by a library

Design.md line ~1001-1003 is unambiguous and quoted in full because this
slice's whole ASCII scoping decision rests on it: **"光标移动、选区扩展、
退格删除的最小单位是 grapheme cluster，既不是字节，也不是 code point"**
(the minimum unit for cursor movement, selection extension and backspace
deletion is the grapheme cluster — neither a byte nor a code point). The
document's own example is `👨‍👩‍👧‍👦`, seven code points joined by ZWJ,
which a correct backspace must delete as one unit; implementing that needs
`SkUnicode`'s grapheme-cluster segmentation, itself the ICU dependency
design.md section 5.10.5 already flags as a P3/P4 cost this project has
not taken on.

**The scoping move**: restrict `TextField`'s content boundary to printable
ASCII (`0x20`-`0x7E`), enforced once at every mutation entry point
(`text_field_insert()`'s `filter_ascii()`). For ASCII, a byte offset, a
UTF-8 codepoint offset and a grapheme-cluster boundary are provably the
same number — no ASCII byte is a UTF-8 continuation byte and no ASCII
codepoint ever combines with an adjacent one into a multi-codepoint
grapheme cluster. This is not a workaround that happens to pass today's
tests; it is why `Widget::cursor` can be declared as a plain `int` byte
offset and still satisfy design.md's mandatory minimum-edit-unit
requirement **by construction**, with zero segmentation code. `doc/text-
input.md` (this document) is the written record the task asked for; `dg::
ByteOffset` (design.md line ~1164's ABI type, guaranteed to land on a
grapheme boundary) is unaffected because no ABI exists yet for this slice
to wire it through.

**What is declined, named explicitly**: any non-ASCII byte a keystroke,
paste or IME commit might produce is filtered out at
`WidgetSet::text_field_insert()`'s boundary rather than accepted and
mis-segmented — `tests/unit/test_text_input.cpp`'s
`"insert filters non-ASCII bytes and control characters, keeping the
rest"` pins this by hand (a two-byte UTF-8 sequence for é is dropped
whole, the ASCII either side of it survives). Lifting this restriction
needs `SkUnicode`'s grapheme API wired into `dg::Focus`-adjacent cursor
math — a real, scoped slice of its own, not a small patch, matching the
same "subsystem-level gap, not a few-properties gap" shape design.md
section 5.12.1 already uses for rich-text editing.

### 1.3 IME: the interface slot is real plumbing, not a placeholder

Design.md's own words (line ~547-551), quoted because this slice's
decision follows the passage's own logic rather than merely citing it:
IME is a **validation risk, not an architectural one** — "接口位置已经留
好，后续填充实现不影响任何其他子系统" (the interface's position is
already reserved; filling in the implementation later touches no other
subsystem). Concretely, `IWindow::start_text_input(rect)`/
`stop_text_input()` (design.md line ~142-143) is the named hook.

**The decision made here**: build `start_text_input()`/`stop_text_input()`
as a real, working SDL3 passthrough (`SDL_StartTextInput`/
`SDL_SetTextInputArea`/`SDL_StopTextInput`) — not a stub, and not deferred
as "premature." The reason is mechanical rather than aspirational: **SDL3
will not emit `SDL_EVENT_TEXT_INPUT` at all, IME active or not, until
`SDL_StartTextInput()` has been called on the window.** Without this call,
plain ASCII keystrokes never reach the application as committed text in
the first place — this is required plumbing for THIS slice's stated goal
(direct ASCII input), discovered by reading SDL3's own event model rather
than assumed. `examples/12_text_input/text_field_window.cpp`'s `Runner`
calls it once, for the window's whole lifetime, on first frame.

**What is explicitly not built, and stays P7's** (design.md line ~1719):
`SDL_EVENT_TEXT_EDITING` — the in-progress composition preview an IME
sends while the user is still choosing candidates — is never read.
`TextInputEvent` (the event type this slice's `WindowManager::pump()`
reports) carries only committed text, and a composed non-ASCII character
an IME commits is treated identically to any other non-ASCII byte: it
reaches `text_field_insert()` and is dropped at the same ASCII filter,
with no special case distinguishing "typed" from "IME-committed." No
candidate window, no composition string, no cancel-on-Escape handling
exists anywhere in this codebase. This satisfies design.md's own
downgrade rationale precisely: the interface position is real and
load-bearing today (ASCII typing depends on it), and the day P7 adds
composition handling, it extends this same call rather than replacing it.

### 1.4 换行 / 省略: no `SkParagraph`, single-line with ellipsis-or-scroll

`grep -rn "SkParagraph" include/ src/` returns zero hits — confirmed, not
assumed, before scoping this slice. `Paragraph` (design.md section 5.3's
planned wrapper "measurement / line-break / hit-test / cursor-position /
selection-rects") does not exist. Building it — even a minimal subset —
is a full slice's worth of work in its own right: line-breaking, run
management and a `Paragraph` abstraction layer over `SkParagraph` that no
other part of this codebase has ever needed (`doc/font-fallback.md`
records that today's text rendering is one `SkFont::measureText` +
`SkCanvas::drawSimpleText` call per string, with no shaping, no BiDi and
no line-breaking at all).

**The decision, made the same way `doc/sizing.md` section 1 scoped the
second sizing stage down to what needed no second measurement**:
`TextField` is single-line, matching the MVP-8 list's own naming (section
1.1). Overflow is handled two ways depending on focus, both built directly
on `measure_ascii_width()`/`ascii_offset_at_x()` (`src/render/
text_metrics.cpp`, the same `SkFont::measureText` primitive `skia_paint.
cpp` already paints every string through — never an independently-derived
metric that could disagree with what is actually rasterized):

- **Unfocused**: the displayed string is truncated to the longest prefix
  that, with an appended `"..."`, still fits the field's own pixel width —
  `ellipsize()`, hand-pinned in `tests/unit/test_text_input.cpp` against
  the deterministic monospaced test font (`"A..."` is exactly the correct
  truncation of an 8-glyph string in a 50px/4-glyph field; see section
  1.2's font comment for why this font makes an exact answer checkable by
  counting characters).
- **Focused**: the full model string is always displayed, horizontally
  scrolled (`Widget::scroll_x`, a derived paint-time quantity, never
  declared) so the caret stays inside the visible width — the identical
  shape a text editor's own viewport scrolling takes, built on the same
  primitive `doc/scrolling.md` already uses for `ScrollView`'s offset:
  clamped to `[0, total_width - visible_width]`, adjusted only when the
  caret would otherwise leave the visible band.

**What this declines, named**: multi-line reflow, word-wrap, BiDi,
run-based mixed-script text inside a single field, and any
`Paragraph`-shaped abstraction. The prerequisite is the same one
`doc/form-controls.md` section 2.4 names for dropdown's `PopupHost`: a
platform/graphics-layer slice (building `Paragraph` over `SkParagraph`)
upstream of anything a widget can compose, not a small addition to this
one.

## 2. Text content, cursor and selection: state, not properties

`Widget::text`/`cursor`/`selection_anchor`/`scroll_x` are **not** in
`props/drawgui.props.toml`. The argument is the identical one
`doc/scrolling.md` section 2 makes for the scroll offset and
`doc/form-controls.md` section 1.3 makes for the slider's value, extended
one control further, restated because the precedent is the standard this
slice is held to:

A `TextField`'s text accumulates across an **unbounded stream of
keystrokes** — every character typed, every Backspace, every paste (were
paste built) is one more mutation with no natural "declared" value a
caller would set once through `set_prop()`. The cursor and selection are
even more clearly per-frame derived facts: they move on every arrow key,
every click, every drag-continuation, read back on the very next paint to
decide the caret's pixel position. Putting any of the three in the
property table means every keystroke becomes a `dg_node_set_prop()` call
across the eventual C ABI — the identical anti-pattern design.md section
5.15.3 already rejects for per-node hash maps, restated for text the same
way `doc/form-controls.md` restated it for a slider's drag.

`scroll_x` is the clearest case of all: it is **never** set by a caller in
any code path in this slice — it is entirely derived, inside
`text_field_refresh_display()`, from the cursor and the field's own pixel
width. It is not merely "not a property"; it could not be one without
inventing a caller that has no reason to exist.

**No new property this slice.** 46 properties total, unchanged:
**33 implemented / 10 partial / 3 not-yet.** `doc/properties.md` needs no
update — `TextField`'s children (`content`/`caret`/`selection_highlight`)
reuse existing `NodeStyle` fields (`fill`, `text`, `border_color`, etc.)
exactly the way `kCheckbox`'s indicator and `kSlider`'s thumb already do.

## 3. Focus: a concept this engine had none of, introduced at the smallest scope this slice needs

`grep -rn "focus" include/drawgui/widget/interaction.h` (before this
slice) returns nothing — `dg::Interaction` is a stateless hover/press
machine with no notion of "which widget receives keyboard events."
Keyboard routing needs one, so `dg::Focus` is new this slice, in its own
header/source pair rather than folded into `WidgetSet` or `Interaction`,
for the same reason `Interaction` itself is split from `WidgetSet`
(`include/drawgui/widget/interaction.h`'s own argument, restated for
focus): focus is state that outlives a single event, is a pure function of
"which widget was last given it," and is unit-testable with no tree and no
window (`tests/unit/test_text_input.cpp`'s `dg::Focus` test cases
construct one with zero `RenderTree`/`LayoutTree` involvement at all).

**The model built is the simplest one that satisfies this slice's actual
demo**: one `std::optional<NodeId>`, exclusive — focusing a widget blurs
whatever was focused before, in one atomic `FocusChange` (mirroring
`InteractionChange`'s identical atomicity argument, so a caller never
observes an instant where two widgets are focused or where the blurred one
has not yet been told to repaint itself unfocused). **No tab order, no
focus tree** — design.md section 5.2's per-window `FocusManager` with a
tab sequence is P4 scope; a two-field demo needs to answer exactly one
question ("which field, if any, is focused") and `dg::Focus` answers
exactly that question and no other, matching `doc/widgets.md`'s own rule
that an interface is extracted from a working implementation rather than
written ahead of one.

**Click routing**: `TextField` genuinely `accepts_pointer()` (unlike
`kSlider`/`kScrollView`), because a click has to resolve *to* the field
itself (to focus it and place the cursor), not merely to a plain child of
it. This needed no new climb — `owner_of()`/`widget_at()` already resolve
correctly since `interactive()` now returns `true` for `kTextField`.
`examples/12_text_input/text_field_window.cpp`'s `Runner::handle_down()`
focuses whatever field (if any) a click's hit-test resolves to, and blurs
the field when a click resolves to nothing or to a non-`TextField` widget
— an ordinary click-outside-blurs behaviour built directly on the existing
hit-test/`owner_of()` machinery, adding no new one.

## 4. The relayout finding, worked out rather than assumed

The task asked this directly: does typing ever force a relayout, and
under exactly what condition? Measured, not asserted from the code that is
supposed to make it true — the identical technique `doc/scrolling.md`
section 4 and `doc/form-controls.md` section 3 both used for the scroll
offset and the slider's thumb.

**In this slice: no. Character insertion, deletion, cursor movement,
selection and focus changes all cost a repaint and never a relayout.**
`examples/12_text_input/text_field_check.cpp`'s
`check_editing_costs_no_relayout()` calls `LayoutTree::layout()`
immediately after an insert and after a Backspace and asserts
`LayoutStats::nodes_visited == 0` and `nodes_relaid_out == 0` both times,
on the actual demo scene — not inferred from reading `WidgetSet`'s source.

**Why, structurally**: every `WidgetSet::text_field_*` mutator
(`insert`/`backspace`/`delete_forward`/`move`/`click`/`set_focus`) routes
exclusively through `text_field_replace_range()` and
`text_field_refresh_display()`, and both are written entirely against
`RenderTree` — `set_text()`, `set_local_bounds()`, `set_local_origin()`.
`grep -n "LayoutTree" src/widget/widget_set.cpp` matches nothing inside
any `text_field_*` function. There is no code path from a keystroke to
`LayoutTree::set_box()` for this slice's `TextField` to take, which is a
structural guarantee rather than an artifact of the test suite's own
coverage — the same shape `doc/scrolling.md` section 4 found for the
scroll offset ("not incremental layout skipping work — layout is never
entered at all").

**The condition under which this claim would flip, named exactly, because
the task asked for the exact condition rather than a hand-wave**: this
slice's `TextField` is **fixed-width** — `BoxStyle::width` is a declared
constant set once at construction (`kFieldWidth == 260` in the demo,
whatever width a caller declares generally), exactly the same shape
`Slider`'s track already is (`doc/form-controls.md` section 3's identical
finding: "slider's thumb position is a paint-time offset, never a layout
concern"). **A *shrink-to-fit* `TextField`** — one whose own declared
width is absent and instead derived from its current text's measured
width, the way an un-styled HTML `<input>` with no explicit width behaves
— would break this guarantee on every edit that changes the string's
measured width: the field's *own* `BoxStyle::width` would have to change,
which is a `LayoutTree::set_box()` call on the field's own node, which
*is* a relayout, and (because the field sits inside whatever container
holds it) potentially a relayout of siblings that reflow around the
field's new width. **This is not built in this slice** — `TextField`'s
own node is always constructed with an explicit, unconditionally fixed
`width`/`height` (`examples/12_text_input/text_field_scene.cpp`'s
`add_field()`, `tests/unit/test_text_input.cpp`'s `build_field()`) — named
here as the exact prerequisite the day a shrink-to-fit variant is wanted,
matching every prior slice's practice of naming a real, checked
precondition rather than gesturing at "future work."

**This is a genuinely different shape from scroll's and slider's "never a
relayout" claims**, worth stating plainly: those two are unconditional —
nothing about a future extension of `ScrollView` or `Slider` reintroduces
a layout dependency on their runtime state, because a scroll offset and a
slider's value are never inputs to any node's *size*. A `TextField`'s
*text* can, in principle, be an input to its own node's size (shrink-to-
fit), which is exactly the feature this slice does not build. The claim
"no relayout" is therefore conditional on "fixed-width," not absolute —
and this document says so rather than letting `doc/scrolling.md`'s
stronger, unconditional precedent bleed into a weaker case by analogy.

## 5. What was built, concretely

- **`WidgetKind::kTextField`** — the seventh enumerator, forcing the two
  existing `switch (kind)` sites (`WidgetSet::interactive()`,
  `examples/05_widgets/widget_scene.cpp::describe()`) to add a case, the
  same compiler-enforced discipline `doc/widgets.md` names as the whole
  point of a closed enum ("a fifth kind is a fifth enumerator and a fifth
  case in one switch, which the compiler will demand").
- **`Widget::content`/`caret`/`selection_highlight`** — three `NodeId`
  children the widget positions/sizes at paint time, the same shape
  `indicator` and `thumb` already are.
- **`Widget::text`/`cursor`/`selection_anchor`/`scroll_x`** — the model,
  runtime state (section 2).
- **`WidgetSet::text_field_insert/backspace/delete_forward/move/click/
  set_focus`** — the whole editing surface, all routed through
  `text_field_replace_range()`/`text_field_refresh_display()` so the
  projection logic (ellipsis vs. scroll, caret/highlight geometry) exists
  in exactly one place rather than six copies of it.
- **`dg::Focus`** (section 3) — new, in `include/drawgui/widget/focus.h` +
  `src/widget/focus.cpp`.
- **`dg::measure_ascii_width()`/`dg::ascii_offset_at_x()`**
  (`include/drawgui/render/text_metrics.h` + `src/render/text_metrics.
  cpp`) — the two ASCII-scoped text primitives caret positioning and
  click-to-offset are built on, both directly on `SkFont::measureText`.
- **`dg::Key`/`dg::KeyAction`/`dg::KeyEvent`/`dg::TextInputEvent`** in
  `include/drawgui/window/window_manager.h`, plus
  `WindowManager::start_text_input()`/`stop_text_input()`/`post_key()`/
  `post_text_input()` in the SDL3 backend — real keyboard + text-input
  plumbing, matching the exact style pointer/wheel events already use
  (`PumpResult::key`/`text_input`, populated by `dispatch_keyboard()`,
  split out of `dispatch()`'s own switch once three more cases pushed it
  over clang-tidy's cognitive-complexity budget — the same trap
  `doc/form-controls.md`'s own defect table already names for
  `-Werror=unused-variable`, a sibling tooling limit rather than a design
  choice).
- **`examples/12_text_input`** — a real SDL3 window (`text_field_window.
  cpp`) with two `TextField`s (`field_a` pre-filled and overflowing,
  `field_b` empty), a headless oracle (`text_field_check.cpp`, five
  claims: focus exclusivity, typing/editing, overflow ellipsis/scroll, the
  no-relayout finding, and repaint identity), and a `--script` mode that
  drives a real click, a real `SDL_EVENT_TEXT_INPUT` and a real
  `SDL_EVENT_KEY_DOWN` through the platform's own event queue, matching
  every prior example's script-mode precedent.

## 6. Verification performed

### 6.1 Automated

- **17 CTest entries green** (16 inherited + new
  `text_input.verify_demo_scene`). `unit` (doctest) gained 25 new test
  cases in `tests/unit/test_text_input.cpp`: the two text-metrics
  primitives (hand-derived against a deterministic monospaced test font),
  insert/backspace/delete/move/click model behaviour, caret and selection
  geometry (hand-derived pixel values, not read back and trusted), the
  ellipsis/scroll overflow projection (hand-derived exact truncation), and
  `dg::Focus`'s exclusivity/atomicity.
- **Golden image unchanged**: `drawgui_render_png`'s sha256 is still
  `f635028e6e1e92349d23f0575f1e39e7ca8b05e5974492641ee610fe520df12e` — this
  slice touches no default-rendered scene.
- **g++ and clang++, Debug and Release, `-Werror`**: all green. One real
  cross-tool conflict surfaced and was fixed rather than suppressed —
  section 7's defect-injection table's own build-time catch, and section 8
  below.
- **`-DDG_SANITIZE=ON`, both compilers, full 17-entry suite**: green —
  string/buffer editing (`std::string::replace` inside
  `text_field_replace_range()`, byte-offset arithmetic in every
  `text_field_*` mutator) is exactly where ASan/UBSan earn their keep, and
  they ran clean including the deliberately hostile boundary cases this
  slice's own test suite constructs (Backspace at offset 0, Delete at the
  string's end, a click far past the end of the string).
- **clang-tidy `-p build` and clang-format**: exit 0 on every changed and
  new file (102 total in the CI TU list), added to
  `.github/workflows/ci.yml`.

### 6.2 Hand-derived geometry, not byte-identity alone

`tests/unit/test_text_input.cpp` builds against `DgTest Latin`
(`tests/fonts/gen_test_fonts.py`), a monospaced test font at exactly 12
device pixels per glyph at size 20 — chosen so every expected pixel value
below is an integer a reader can check by counting characters, the same
technique `test_form_controls.cpp` and `test_scroll.cpp` already
established for exact geometry:

| what | hand-derived | measured |
| --- | --- | --- |
| caret x at cursor offset 3, `"ABCDE"` | `3 * 12 = 36` px | 36px |
| selection highlight `[0, 3)` width | `3 * 12 = 36` px | 36px |
| click at x=26 resolves to offset 2 (past midpoint 18, before midpoint 30) | offset 2 | offset 2 |
| click at x=50 (after a first click at 26) resolves selection `[2, 4)` | `[2, 4)` | `[2, 4)` |
| overflow: 8 glyphs (96px) in a 50px/4-glyph field, cursor at the end | `scroll_x = 46`, `caret_x = 48` | 46, 48 |
| unfocused ellipsis of an 8-glyph string in a 50px field | `"A..."` (4 glyphs = 48px, the longest that fits) | `"A..."` |
| a 3-point drag's selection after the anchor is fixed at the first extending click | `[2, 5)` | `[2, 5)` |

### 6.3 On screen

`examples/12_text_input --script` opens a real window and drives a real
click, a real committed-text event and a real key event through
`WindowManager::warp_pointer()`/`post_pointer_button()`/`post_text_input()`
/`post_key()` — the platform's own SDL event queue, reported back through
the ordinary `pump()` path used everywhere else in this project:

```
clicked field b at ...: 1 real down event(s), 1 real up event(s) delivered
typed "hi": 1 real text-input event(s) delivered
pressed Backspace: 1 real key-down event(s) delivered
```

Per this slice's reduced verification scope (the task's own instruction):
no screenshot review or image-viewing was performed; the headless oracle
plus the hand-derived `CTest` values above are the acceptance evidence.

## 7. Defect injection: 14 injections, 12 caught (after closing 6 test gaps), 2 provably inert

Each injected one at a time against the working tree, built, run through
the targeted suite (`unit` + `text_input.verify_demo_scene` — sufficient
isolation here because every injection touches only `TextField`-specific
code no other widget's scene exercises, unlike a shared render/layout
primitive), then reverted by hand with the `edit` tool rather than `git
checkout` — this project's own standing lesson
(`doc/sizing.md`/`doc/scrolling.md`/`doc/opacity.md`) that a checkout-based
revert can silently erase a test written *after* the last commit, turning
"caught" into a false "survived." No commits existed yet for this slice at
injection time, so every new regression test the campaign produced is
preserved by construction rather than by discipline alone.

| # | injection | result |
| --- | --- | --- |
| A | `normalize_selection()` stops taking `min`/`max`, returns `{cursor, anchor}` unswapped | **caught immediately** — `unit` only (a backward selection, cursor before anchor, is the only shape that shows it) |
| B | `kCharLeft`'s `std::max(0, ...)` clamp removed | **survived** — no test moved a plain (non-extending) Left at cursor 0; closed by adding a boundary test, then **caught** |
| C | `kCharRight`'s `std::min(size, ...)` clamp removed | **caught immediately**, using the boundary test B's fix added (it covers both directions) |
| D | Backspace's `cursor <= 0` guard weakened to `cursor < 0` | **survived, provably inert** — `text_field_replace_range()`'s own `std::clamp` on `[lo, hi]` independently clamps the resulting `(-1, 0)` range to `(0, 0)`, an empty replacement that returns `false` without mutating anything; the caller-side guard is redundant with a real second bound, not a gap |
| E | Delete-forward's `cursor >= size` guard weakened to `cursor > size` | **survived, provably inert** — identical mechanism to D: `replace_range()`'s clamp reduces `(size, size+1)` to `(size, size)`, an empty no-op |
| F | The caret's `set_local_bounds()` call skipped whenever the computed `caret_x` is exactly 0 | **survived** — every existing caret test only ever moves the cursor in one direction from a field's initial state, never back to offset 0 after moving away from it; closed by adding a there-and-back-again test, then **caught** |
| G | `ascii_offset_at_x()`'s midpoint formula changed | **caught immediately** — the first variant (drop `previous_width` entirely) was caught by GCC's `-Werror=unused-variable` at *build* time before a single test ran, the same tooling trap `doc/scrolling.md`/`doc/form-controls.md` already name; a second variant (sum instead of average, keeping the variable "used") was caught at runtime by the existing hand-derived offset tests |
| H | Click-to-offset dropped the `+ widget->scroll_x` term | **survived** — no existing click test ever clicked on a field that had actually scrolled (all used content narrow enough to fit); closed by adding a click test against the same overflowing/scrolled scene the geometry tests already build, then **caught** |
| I | `ellipsize()`'s `kept = prefix > 0 ? prefix - 1 : 0` weakened to `kept = prefix` | **survived — a NEW failure mode** (section 8): the exact right scene was already exercised by an existing test (`"unfocused display ellipsizes..."`), but that test only checked `.ends_with("...")` and `!= original`, both of which an off-by-one-glyph-too-wide ellipsis still satisfies. Closed by adding a hand-derived exact-string assertion (`"A..."`, not merely "ends in `...`"), then **caught** |
| J | `text_field_set_focus(focused=false)` stopped clearing `selection_anchor` | **survived** — the unfocused display branch always hides the highlight regardless of the model, so blurring alone shows nothing wrong; the SAME selection silently reappears on the next refocus, which no existing test checked for; closed by adding a blur-then-refocus test, then **caught** |
| K | `Focus::set()`'s redundant-refocus no-op guard (`focused_ == target`) removed | **caught immediately** — re-focusing the already-focused widget now reports a spurious change |
| L | `text_field_insert()`'s active-selection branch disabled (`if (false && ...)`) | **caught immediately** — typing over a selection now appends instead of replacing it |
| M | The content child's `tree.set_text()` call removed from the focused display path | **caught immediately by both** `unit` and `text_input.verify_demo_scene` — the model text changes but the painted string does not |
| N | `text_field_click()`'s drag-continuation re-anchors on every extending click instead of only the first | **survived** — the only existing drag test performs exactly one extending click, where "no prior anchor" and "preserve the prior anchor" produce an identical result; closed by adding a three-point-drag test, then **caught** |

**Tally**: 6 caught immediately (A, C, G, K, L, M); 5 survived due to a
missing scene shape and were closed with a new test (B, F, H, J, N); 1
survived due to a weak assertion on an already-exercised scene and was
closed the same way (I); 2 survived and are **provably inert**, not test
gaps (D, E) — a genuine second, independent bound inside
`text_field_replace_range()` neutralizes an incorrect boundary guard at
either call site, so no caller-observable defect exists for a test to
catch regardless of how it is written.

## 8. Diagnosing the survivors against this project's failure-mode history, and a sixth mode

Checked against the five modes this project has already named across
4-3 through 4-8 (`doc/clipping.md`, `doc/compositing.md`, `doc/sizing.md`,
`doc/scrolling.md`, `doc/form-controls.md`):

1. **Stale build/mtime** — not encountered. Every injection here was
   reverted with the `edit` tool (a real file write, a real new mtime),
   never `git checkout`.
2. **`git checkout` erasing a newly written test** — not encountered, for
   the same reason: no `git checkout` was used at any point in the
   campaign, and no commits existed yet to check out against.
3. **Wrong-copy injection** (editing a file the build does not actually
   compile) — not encountered; every injected file
   (`src/widget/widget_set.cpp`, `src/widget/focus.cpp`, `src/render/
   text_metrics.cpp`) is on the CI TU list and was confirmed rebuilt (a
   changed object file in `ninja`'s own build log) before each test run.
4. **Provably-inert code** — **encountered twice**, D and E, both
   diagnosed above: `text_field_replace_range()`'s own clamp is doing real
   defensive work here, independent of the caller-side guards it makes
   redundant. This is the identical shape `doc/form-controls.md`'s
   injection B found for `group_members()` (`refresh()` reads the actual
   `checked` bit regardless of an over-broad member list) — a second,
   independent reader neutralizing an upstream defect.
5. **Missing scene shape** — **encountered four times**, B/C (one root
   cause, one fix), F, H, N: in each case, the specific interaction
   *sequence* needed to distinguish correct from incorrect behaviour
   (return to cursor 0 after moving away; click on a field that has
   actually scrolled; continue a drag past its second point) had simply
   never been built into any existing scene.

**A sixth mode, found for real rather than invented to fill a slot**:
**a weak assertion on the right scene** (injection I). This is distinct
from mode 5 in a specific, checkable way: the scene that *would* reveal
the defect was already being exercised by an existing test
(`build_field(50, 30, "ABCDEFGH")`, the identical overflowing/50px-field
scene the caret and content-x tests already use) — there was no missing
interaction sequence, no missing scene shape. What was missing was
assertion *precision*: `.ends_with("...")` and `!= original` are true for
an entire family of different, wrong truncation lengths, not only the one
correct answer. This differs from `doc/compositing.md`'s standing warning
about byte-identity ("verifies the damage system, not the widget
semantics") in kind — byte-identity is *maximally* precise but proves the
wrong thing (two equally-wrong copies agree); a weak string-shape
assertion is *insufficiently* precise and proves too little. The fix in
both cases is the same discipline the task demanded up front: a
hand-derived, independently-computed expected value (`"A..."`, computed
from the font's own glyph width against the field's own pixel width) —
not a looser property the defect happens to preserve.

## 9. What this does not do

Everything design.md's own wish list (section 5.5) names beyond the MVP
concession: modifier-key-bound shortcuts and the intent-binding system
(design.md section 5.5.1) they would route through, right-click context
menus, drag-and-drop (in-app or OS-level), cross-line selection (there are
no lines), double/triple-click word/line selection, and pointer capture
that survives leaving the window. No IME composition (section 1.3). No
grapheme-cluster segmentation or non-ASCII cursor/selection movement
(section 1.2) — non-ASCII bytes are dropped at the model boundary, not
mis-handled, and lifting this needs `SkUnicode`'s grapheme API, a real
slice of its own. No multi-line reflow, no `Paragraph`/`SkParagraph`
integration (section 1.4). No shrink-to-fit sizing (section 4) — every
`TextField` in this slice has a declared, fixed width, exactly like
`Slider`'s track. No undo/redo. No clipboard. No vertical text, no RTL,
no BiDi. No caret blink — a caret is drawn steady, matching
`doc/scrolling.md`'s declined-fling reasoning for the identical
prerequisite this project still lacks: there is no animation clock
anywhere in this codebase to blink one against, and building a one-off
timer for a single cosmetic detail this slice's acceptance criteria never
asked for was declined the same way fling was.

## 10. Cross-reference: 7-2b superseded section 1.2's ASCII scoping (append-only)

This section is appended rather than editing sections 1-9 above, which
remain the accurate historical record of what this slice decided and why -
correct for its time, and worth keeping exactly as written rather than
edited over, per this project's own standing practice for a later slice
that changes an earlier one's premise (`doc/completeness.md`'s "historical
snapshot, left unchanged" rows are the identical shape one document over).

**What changed, and why it was safe to change**: section 1.2's whole
argument was conditional on one fact - "implementing that needs
`SkUnicode`'s grapheme-cluster segmentation, itself the ICU dependency
design.md section 5.10.5 already flags as a P3/P4 cost this project has not
taken on." That fact stopped being true at slice 7-1: `SkUnicode`'s
libgrapheme backend links, initializes, and (`tests/unit/
test_skia_textlayout_smoke.cpp`) demonstrably segments a ZWJ family emoji
into one grapheme cluster. Slice 7-2b (`.omo/plans/drawgui-phase7.md`) is
the follow-up section 1.2 itself named ("Lifting this restriction needs
`SkUnicode`'s grapheme API wired into `dg::Focus`-adjacent cursor math - a
real, scoped slice of its own") and it does exactly that:

- **`filter_ascii()` is gone.** `WidgetSet::text_field_insert()` now calls
  `sanitize_insertable_text()` (`src/widget/widget_set.cpp`): well-formed
  UTF-8 (repaired via `dg::sanitize_utf8()`, 7-2's own
  `paragraph_build.cpp` substitution policy reused rather than duplicated)
  with ASCII control characters (0x00-0x1F, 0x7F) still dropped - a literal
  newline/tab has no meaning in a single-line field, which is a scope
  boundary this slice keeps, not a segmentation concern.
- **Cursor movement, backspace, delete and selection are grapheme-cluster
  aware**, using a new font-independent seam, `dg::grapheme_boundaries()`
  (`include/drawgui/render/grapheme.h`, `SkUnicode::
  computeCodeUnitFlags()`) - measured to be necessary rather than assumed:
  `skia::textlayout::Paragraph::getGlyphClusterAt()` (the seam this slice
  first tried) turned out to cluster by SHAPING outcome, not Unicode
  grapheme rules, and reports one cluster PER CODEPOINT when the active
  font has no ligature/colour glyph for a ZWJ/skin-tone/flag sequence -
  exactly the case a `TextField`'s content font cannot be guaranteed to
  avoid. `dg::Paragraph` gained one new method instead, `caret_x(int
  offset)`, for the pixel-position half only.
- **`measure_ascii_width()`/`ascii_offset_at_x()` (`text_metrics.h/.cpp`)
  are deleted**, replaced by `dg::Paragraph::caret_x()` plus the
  grapheme-boundary-walking helpers in `widget_set.cpp` - shaping-aware
  rather than a raw `SkFont::measureText` per byte, and (measured on the
  existing ASCII test suite before and after) numerically IDENTICAL for
  every ASCII case this slice already had hand-derived pixel values for.
- **`ellipsize()` was rewritten to cut at a grapheme-cluster boundary**,
  never a byte offset - section 1.4's ellipsis behaviour is unchanged in
  shape, only in what a "character" means, and a defect-injection
  re-derivation of 4-9's own famous off-by-one bug (this section's own
  historical section 8 discovery) confirmed the new hand-derived exact-
  string assertions still catch it.
- **What is declined by name, distinct from what 7-2b lifted**: `dg::
  ByteOffset`/`Utf16Offset`/`GraphemeIndex` (design.md section 5.13.2's
  three strong-typed index spaces) were NOT built. The measurement that
  found `getGlyphClusterAt()` insufficient also found, empirically, that
  every Skia call this slice's editing surface needs
  (`getGlyphClusterAt`/`getClosestGlyphClusterAt`, the "Editing API"
  `modules/skparagraph/include/Paragraph.h` itself groups together) is
  UTF-8 byte-offset native, not UTF-16 - the UTF-16 semantics design.md
  section 5.13.2 warns about belong to a DIFFERENT, older Skia API
  (`getGlyphPositionAtCoordinate`/`getRectsForRange`) this slice never
  calls. Building `Utf16Offset` with no caller would be exactly the
  ahead-of-a-working-implementation infrastructure this project's own
  precedent (this document's own section 3, `doc/widgets.md`) already
  argues against; the day a caller needs the UTF-16-native API, that is
  when the conversion function - and the type - earns its place.
- **Full details, the ZWJ/skin-tone/flag measurements, the defect-injection
  campaign and the re-verified relayout claim under CJK/emoji input** are
  in `doc/text-layout.md` (7-2's own document, extended by 7-2b) rather
  than duplicated here.

**What remains of this document's original ASCII argument**: nothing, for
`TextField`'s editing surface - every byte this section's own words describe
as "filtered at the boundary" is now accepted. Sections 1-9 above are kept
verbatim as the reasoning that was correct when written and the record of
what 7-2b's own justification for changing course rests on.
