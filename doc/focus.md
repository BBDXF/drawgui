# `dg::Focus` grows a Tab order, scopes, and a ring

Slice 7-4. design.md section 5.2 names a per-window `FocusManager` ("每窗口
独立焦点树 + tab 序") as P4 scope; 4-9 built the smallest thing its own
demo needed instead - `dg::Focus`, one optional `NodeId`, with a header
comment stating plainly what it was not: "No tab order, no focus TREE".
P4's own acceptance line is "Tab 序正确" ("Tab order is correct"). This is
the record of building it.

The short version:

- **No third tree.** `RenderTree` already exposes `children()`/`parent()`
  (added for 6-3's `dg_dump_layout_tree`) - a pre-order walk of those two
  primitives already *is* DOM-shaped Tab order, and an ancestor-or-self
  climb over `parent()` already *is* what a focus scope's containment
  question needs. The one genuinely new piece of state is a single
  optional scope-root `NodeId` inside `Focus` itself.
- Tab order is tree order by default, overridden by an explicit,
  HTML-`tabindex`-shaped `Widget::tab_index`: positive values sort to the
  front (ascending, ties broken by tree order), unset/zero values follow in
  tree order, and negative values are still focusable by a direct click but
  are never a Tab stop.
- `kButton`/`kCheckbox`/`kSlider`/`kTextField` are focusable; `kPanel`/
  `kLabel`/`kScrollView`/`kList` are not - the last two declined by name,
  not overlooked.
- `Focus::enter_scope()`/`exit_scope()` confine Tab to a popup's own
  subtree and blur a widget still focused inside it when the popup closes -
  the popup-close hazard the task named by name.
- A themed focus ring - four thin strips in the margin *outside* a widget's
  own bounds, never overlapping it, so it cannot steal a future click.
- Cross-window focus (a popup's native branch) needed no new struct at
  all: a native popup gets its own, entirely separate `Focus` instance.
- `examples/21_focus` demonstrates all of the above over a five-widget,
  four-`WidgetKind`, two-container scene with a non-focusable widget in
  the middle and one sibling whose Tab position is the reverse of its tree
  position, plus Tab crossing into both `PopupHost` branches.
- Zero new node/`RenderObject`/`WidgetKind` kinds - a nineteenth
  consecutive slice against design.md section 5.6 line 622's own
  acceptance bar.

---

## 1. The structure decision, defended against this project's own precedent

design.md says "焦点树" (a focus *tree*). Taken literally that reads as a
mandate to build a fourth data structure beside `LayoutTree`, `RenderTree`
and `WidgetSet`'s own side table - exactly the shape `doc/widgets.md`
already argues against for a widget tree, and the shape `doc/list.md`
already argues against for `kList`'s own bookkeeping. This project's
standing rule (`doc/widgets.md`'s own words, restated by nearly every
subsequent slice's own header comments) is that a structure is extracted
from what a working implementation is proven to need, never written ahead
of one because a document used a noun.

Asking what "焦点树" actually has to **do**, rather than what it is
**called**, resolves the question:

1. **Tab order needs a traversal sequence.** `RenderTree::children(id)`
   already returns "`id`'s children, in paint order" (its own doc
   comment), added for 6-3's `dg_dump_layout_tree` - a pre-order depth-
   first walk of that primitive, starting from a scope root, *is* the
   DOM-shaped tab order every desktop toolkit this project's MVP-8 list is
   modelled on already uses as its own default. Nothing new to walk.
2. **A popup needs a boundary Tab does not cross.** `RenderTree::parent(id)`
   already exists, with the self-parent-at-root sentinel
   `WidgetSet::owner_of()` already climbs through. `dg::is_within()`
   (`focus.h`) is the identical climb, one direction, answering "is `id`
   inside `scope_root`'s own subtree" - the whole of what a scope's
   containment question needs.
3. **Modal trapping** (a `Dialog` refusing Tab OR a click to escape it
   entirely) is explicitly 7-5's job, not this slice's - `enter_scope()`/
   `exit_scope()` bound *Tab traversal* only, and a direct `set()` or a
   click still reaches anything, on purpose (see section 4).

So the "focus tree" this slice needed turns out to be: `RenderTree`'s own
tree, walked two ways it was not walked before, plus **one optional
`NodeId`** - the current scope root - added to `Focus`. That is a smaller
footprint than a generation-counter handle (`AnimHandle`, `doc/animation.md`)
or a `ThemeBindings`-shaped side table, both of which this project already
built for state that genuinely needed a NEW table. A focus scope does not:
it needs exactly one more optional field on a class this project already
had, and the argument against inventing a fourth tree is the identical
argument `doc/widgets.md` made against a third one - "an interface is
extracted from a working implementation, not written ahead of one," and
walking `RenderTree` twice is the working implementation.

`Focus` itself stays what it always was: a small class holding a NodeId
(now two: the focused widget and, optionally, the active scope root),
handed `RenderTree`/`WidgetSet` **per call** rather than storing a
reference to either - the identical style every `WidgetSet` method already
uses, and the reason `focus.h`'s own header can still say "testable with no
tree and no window" for `set()`/`current()`/`is_focused()`, even though
`focus_next()`/`focus_previous()` now take one.

---

## 2. Tab order: default and override

`dg::focus_order(tree, widgets, scope_root)` walks `scope_root`'s own
subtree in pre-order, collecting every node that:

- has an attached `Widget` (`WidgetSet::has()`),
- whose `kind` `is_focusable()` returns true for (section 3), and
- whose `RenderTree::absolute_bounds()` is not zero-area.

The collected list is then `std::stable_sort`-ed by one rule: a widget
with a **positive** `Widget::tab_index` sorts before every widget without
one, ascending by that value; everything else (unset, or exactly zero)
keeps the tree order `collect_focus_order()` already produced, because
`std::stable_sort` preserves relative order among equal keys. A
**negative** `tab_index` is filtered out of the collected list entirely,
one level up - never sorted at all.

This is HTML's own `tabindex` semantics, transplanted rather than
reinvented: `tabindex > 0` groups and orders explicitly; `tabindex="0"`
(or its absence) means "in document order"; `tabindex="-1"` means
"focusable, not a Tab stop." The default (tree order, no override) needed
building; the override was **built deliberately**, not defaulted into by
accident, for a concrete reason: without it, a caller wanting one widget
to jump the queue (a "primary action" button, say) would have no lever at
all except reordering the whole tree - which this project's append-only
`RenderTree` cannot do after the fact anyway.

`Widget::tab_index` is `std::optional<int>`, a plain construction-time
field - the identical shape `min_value`/`max_value`/`step` already are on
the same struct, for the identical reason: design.md section 5.9.6 names
`tab_index` as an intended CSS-adjacent property, but it was never added
to `props/drawgui.props.toml` (confirmed: `grep -n tab_index
props/drawgui.props.toml` finds nothing), so there is no generated
`prop_id` for it to bind to through `dg::set_prop()` yet. This follows
`doc/properties.md`'s own standing rule - a representation is not added
before the code that consumes it exists - literally: `Widget::tab_index`
is real, tested, and consumed by `focus_order()` today; a C ABI setter for
it is future work with no caller yet.

`examples/21_focus`'s own scene is shaped to prove the override is not
window dressing: `reversed` (`tab_index = 1`) is the **last** child added
to the tree, after every other focusable widget, yet it is **first** in
Tab order - the default (tree order) and the override necessarily disagree
on this one widget, so a broken override reads as a broken test rather
than a coincidentally-correct one.

---

## 3. Focusability: which `WidgetKind`s, and the opacity question

| `WidgetKind` | Focusable | Why |
| --- | --- | --- |
| `kButton` | yes | the canonical tab stop |
| `kCheckbox` | yes | toggled by activation; needs a steady-state appearance a ring can outline |
| `kSlider` | yes | conventionally focusable even without this slice building arrow-key value adjustment (declined, section 6) |
| `kTextField` | yes | the widget this project's Focus concept was built FOR (4-9) |
| `kPanel` | no | carries no interaction at all (`WidgetSet::interactive()`'s own identical exclusion) |
| `kLabel` | no | static text, same reasoning |
| `kScrollView` | no, DECLINED | see below |
| `kList` | no, DECLINED | see below |

`kScrollView`/`kList` declined by name rather than overlooked: a desktop
toolkit conventionally makes a scrollable region a Tab stop so PageUp/
PageDown/arrow keys can scroll it without a pointer - but **this project
has no keyboard-scrolling code at all**. `doc/scrolling.md` section 1
already named design.md section 5.5.1's intent-binding mechanism as the
missing precondition for keyboard scrolling, and that mechanism is this
slice's own sibling decline (section 6) - still absent. Making `kScrollView`
focusable now would draw a visible ring around a widget Tab can reach that
no key then does anything to - the "field nobody reads" anti-pattern this
project already declines elsewhere (`focus.h`'s own predecessor comment
named it for `kSlider`'s hover/press glow), applied here to a *kind* rather
than a field. **This slice does change the answer to "should keyboard
scrolling be built next"**: the focus/Tab plumbing PageUp/Home/End would
need to route through is now real, so the remaining blocker is
exclusively design.md section 5.5.1's intent-binding system - named,
explicitly, as this slice's sibling decline (section 6), not built here.

**The opacity question, and 4-5's own divergence, carried forward rather
than reopened.** `RenderTree::hit_test()`'s own header states plainly that
hit testing ignores `NodeStyle::opacity` entirely, including at exactly
`0` - contradicting design.md section 5.11.2's literal text (which asks
`opacity == 0` to skip hit testing too), a divergence `doc/compositing.md`
section 4 already argued for: a continuous fade has no natural threshold to
place an interaction cliff at, and a hard cutoff would make one frame of an
otherwise-smooth animation silently stop responding. This slice's own rule
- `focus_order()` does **not** skip a zero-opacity widget either - is the
CONSISTENT reading of that argument, not a second, independent decision: a
click already reaches a faded-to-nothing widget (4-5's own pixel oracle,
`tests/unit/test_hit_test.cpp`), so Tab reaching the identical set of
widgets a click can reach is what "consistent" means. Diverging here -
skipping faded widgets from Tab while still letting a click through -
would have been the genuinely inconsistent choice.

**What IS skipped**: zero-area (`absolute_bounds().is_empty()`). This has
two free consequences, neither special-cased:

- A **closed overlay popup** clips its container to an empty rectangle
  rather than removing it (`doc/popup.md` section 3, `RenderTree` being
  append-only) - any widget still attached inside it falls out of
  `focus_order()` automatically, with no code here naming popups at all.
- An **unassigned `kList` pool slot** (before its first `list_sync()`) has
  no meaningful position yet; this project declined making `kList` itself
  focusable anyway (above), so this consequence is currently inert, named
  for completeness rather than because anything depends on it today.

**No `disabled`/`hidden` property exists to consult.** design.md section
5.9.6 names `disabled`/`focusable`/`hit_test_behavior` alongside
`tab_index` as intended interaction properties; none of the three has ever
been added to `props/drawgui.props.toml`, and no `Widget` field named
`disabled` exists either. There is therefore nothing for this slice's
focusability rule to skip on that account - a real property, not a gap
this slice quietly leaves open, is future work for whichever slice first
has a caller needing to disable a widget.

---

## 4. Wrapping, and focus scopes (not Dialog's modal trap)

`Focus::focus_next()`/`focus_previous()` wrap at both ends: Tab past the
last stop returns to the first, Shift-Tab past the first returns to the
last. design.md names no "wrap or stop" choice for this; every desktop
toolkit this project's MVP-8 widget set is modelled on wraps, so this does
too, named rather than defaulted into silently.

**Scopes are not modal trapping.** `Focus::enter_scope(root)` confines
Tab/Shift-Tab traversal to `root`'s own subtree; `exit_scope()` releases
it. Nothing else changes: a direct `Focus::set()` (a click) still reaches
any node, in or out of the scope, and nothing here refuses to blur a
scoped widget from outside. This is deliberately the SMALLEST thing that
serves the popup case (section 5), not `Dialog`'s modal focus trap
(7-5's own job, per the task): a `Dialog` additionally needs to refuse
losing focus to anything outside it at all (including a click), which
this mechanism does not attempt.

**A single active scope, not a stack.** `PopupHost` itself never nests one
popup inside another - `doc/popup.md` section 6 states plainly "no second
popup ever opens from within a first one" and that `PopupHost` "holds no
parent-popup relationship." A stack of scopes deeper than one therefore
has no working caller to justify it, matching this project's own
"extracted from a working implementation" rule; `enter_scope()` while a
scope is already active simply replaces it.

**`exit_scope()` is where the popup-close hazard is handled**, by
construction: it takes the `RenderTree` it is called against, checks
whether the CURRENTLY focused widget `is_within()` the scope root that is
about to be released, and blurs it first if so - before forgetting the
scope. `tests/unit/test_focus.cpp`'s
`"exit_scope() blurs a focused widget still inside the closing scope"`
case pins this directly; `examples/21_focus`'s own headless check
(`check_overlay_scope_and_close`) exercises the same thing three times in
a row through the real `PopupHost` API (see section 5).

---

## 5. Crossing both `PopupHost` branches - what it proved about the abstraction

`doc/popup.md` section 5 named the exact gap this slice closes: "nothing
wires window-level (as opposed to widget-level) focus tracking yet." The
task frames this as the hard question; building it found the answer is
**structural, not a new mechanism**, and the two `PopupHost` branches
genuinely diverge, which is precisely what a real second-branch exercise
is supposed to surface (`doc/popup.md` section 4's own framing: "a branch
that has never run has never been falsified").

**Native branch: a separate `Focus` for a separate `RenderTree`.** A
native popup already forces a second `RenderTree` instance (`doc/popup.md`
section 3 - a second OS window needs a second surface, and
`RenderTree::root()` is always `NodeId{0}`, so two windows cannot share
one tree's node table). `examples/21_focus`'s `Runner` therefore builds an
entirely separate `dg::WidgetSet`/`dg::Focus`/`FocusRing` triple for the
popup's own tree the moment `PopupHandle::is_native` is true. This is not
an extra abstraction invented for this slice - it falls out of a decision
5-2 already made. The OS genuinely moves real keyboard focus to the new
window when it opens (`SDL_WINDOW_POPUP_MENU` can gain it,
`doc/popup.md` section 1), and this engine's own model mirrors that
exactly: the host's `Focus` is simply never consulted while the popup's
window id owns incoming key events (routing already existed - `KeyEvent::
window`, since 4-6), so it stays exactly as it was, untouched, the entire
time the popup is open.

**This is also the answer to why cross-window focus needed no
`NodeId`-plus-`window_id` struct.** Two independent `Focus` instances
never share a `NodeId` numbering space - `examples/21_focus`'s
`check_two_focus_instances_never_collide()` constructs the concrete case
directly: two separate `RenderTree`s, each handing its own first
attached button the identical numeric `NodeId` value (both are literally
node index 1, the first child of their own `root()`), and confirms each
`Focus` still names only its own window's widget. A single `Focus` per
window/tree, not a global one keyed by a compound identifier, is what
makes this true for free.

**Overlay branch: the SAME `Focus`, scoped - and a real finding about
`WidgetSet` that `PopupHost`'s own header does not mention.** An overlay
popup appends its content into the CALLER's own `RenderTree`
(`doc/popup.md` section 3), so Tab order for it must be computed against
the SAME tree - which `dg::focus_order()` already handles, since it just
walks whatever tree it is given. But `focus_order()` takes exactly ONE
`WidgetSet`, and `PopupHost`'s own header is silent about which one an
overlay popup's content should be attached to. Building `examples/21_focus`
surfaced the answer as a real bug caught before it shipped: `popup_menu::
build()` takes the `WidgetSet` to attach into as an explicit parameter,
and the OVERLAY caller MUST pass the host's own `scene.widgets` - a
second, separate `WidgetSet` for overlay content would put its buttons in
a table `focus_order()`'s host-tree walk never consults, silently making
them untabbable. The native branch, symmetrically, MUST use its own fresh
`WidgetSet`, because its node ids are a different space entirely and
attaching them to the host's `WidgetSet` would look up the wrong node on
every read. `PopupHost`'s own two-branch split (`RenderTree` instance vs.
append) turns out to extend one layer up, to `WidgetSet`, for the
identical structural reason - not stated in `popup_host.h` because
`WidgetSet` is a widget-layer concept `PopupHost` (window layer) does not
know about, but true regardless, and now recorded here.

`examples/21_focus`'s `check_overlay_scope_and_close()` opens, Tabs into,
and closes an overlay popup **three times in a row** - the ASan-relevant
repeat cycle the task's verification section names, each cycle building
brand-new `NodeId`s (RenderTree is append-only) and confirming
`exit_scope()`'s blur is correct against the CURRENT cycle's scope, not a
stale one.

**What this did NOT need to build**: a candidate-list-shaped UI, a
`Dialog`'s modal trap, or the shortcut/intent system's own routing - all
named as out of scope in the task and untouched here.

---

## 6. Two hazards, handled and tested: list recycling and mid-composition Tab-away

**List recycling** (`doc/list.md` section 1: a `kList`'s pool nodes are
NEVER freed, only reassigned to a different logical item as the visible
range moves). 5-3 already made a recycled `NodeId` safe to *hold* - the
node object itself never dies. This slice's own question is different:
is it safe to keep **focus** pointed at one? Without a fix, Tab-ing to
logical item 3, scrolling the list so pool slot now shows item 47, and
pressing a key would act on item 47 while the ring (and the user) still
believe they are looking at item 3. `Focus::blur_if_any_of(recycled)`
answers this: a caller passes the plain `NodeId`s `WidgetSet::list_sync()`/
`list_scroll_by()` just returned as reassigned (`ListSlot::node`), and
focus is blurred if it names any of them. `tests/unit/test_focus.cpp`'s
own case pins both directions - an unrelated recycle leaves focus alone,
a recycle of the FOCUSED slot blurs it. `focus.h` takes plain `NodeId`s
here rather than `WidgetSet::ListSlot` specifically, keeping this file
free of a `kList`-shaped dependency it does not otherwise need - the same
decoupling `focus.h`'s own header argues `Focus` should keep from
`WidgetSet` wherever a call does not strictly require it.

This is exercised by a direct unit test rather than re-demonstrated inside
`examples/21_focus`: the demo scene has no `kList` (it was declined as
non-focusable, section 3, so a live list would add scene complexity
without adding Tab-order coverage) - named here rather than silently
substituted.

**Mid-composition Tab-away** (7-3's own crash hazard, doc/ime.md section
10's finding: a stale composition offset caused an actual
`std::out_of_range`, not merely a wrong answer). `WidgetSet::
text_field_set_focus(tree, fonts, id, false)` already cancels an
in-progress composition internally (7-3's own code, unchanged by this
slice) - so the fix this slice needed was purely to make sure Tab actually
calls it on blur, which `focus_scene::apply_side_effects()` does
unconditionally whenever `change.blurred == handles.textfield`, alongside
`WindowManager::clear_composition()` so the platform's own IME state
agrees (the identical "both sides, not one ahead of the other" rule 7-3's
own Escape path already established). `examples/21_focus --verify-focus`'s
`check_tab_away_mid_composition()` exercises this end to end through the
REAL event queue - a synthesized `SDL_EVENT_TEXT_EDITING`
(`WindowManager::post_text_editing()`, 7-3's own precedent) starts a real
composition, a real posted `SDLK_TAB` (`WindowManager::post_key()`) moves
focus away, and the check confirms `text_field_is_composing()` is false
afterward - not assumed fixed because 7-3's own code exists, verified
because Tab is a new, different caller of it.

**A real bug this exact wiring caught while building the demo**: the
first version of `focus_scene::tab()` called `dg::Focus::set()` a SECOND
time (after `focus_next()`/`focus_previous()` had already performed the
transition) purely to obtain a `FocusChange` to drive side effects from -
and `set()` correctly reported a no-op (the target already matched),
silently skipping every side effect, composition-cancel included. The fix
(section 7) is what actually makes the mid-composition check above pass;
before it, the check failed exactly the way the hazard predicts.

---

## 7. Focus-change side effects, and the bug found wiring them

`focus_scene::apply_side_effects(scene, manager, window, change)` is the
one function every focus transition - a click (`dispatch_pointer()`) or a
Tab (`tab()`) - routes through:

- **Blur**: if the blurred widget is the `kTextField`,
  `WidgetSet::text_field_set_focus(..., false)` (cancels composition,
  reverts the display projection to the unfocused/ellipsized mode) and
  `WindowManager::stop_text_input()`/`clear_composition()`.
- **Focus**: if the newly focused widget is the `kTextField`,
  `text_field_set_focus(..., true)` and `WindowManager::start_text_input()`,
  passed the field's own `absolute_bounds()` as the IME anchor rectangle -
  standing in for the precise caret sub-rectangle 7-3's internal
  `focused_display_projection()` computes (not public API, and an IME only
  needs to land near the field, not on the exact glyph - promoting that
  helper to a public getter has exactly one caller so far, this one, which
  is not enough to justify it per this project's own standing rule).
- **The ring, always**: `dg::update_focus_ring()` is called with
  `scene.focus.current()` (whatever it now is, including `nullopt`) every
  time, which is what makes the PREVIOUSLY focused widget drop its ring -
  the ring is a single, shared, repositioned set of nodes (section 8), not
  one the old widget owned and has to be told to hide.

The bug found while wiring this (section 6, restated precisely): the
first draft of `tab()` discarded `focus_next()`'s own returned
`FocusChange` and instead called `Focus::set(scene.focus.current())` to
get "a" `FocusChange` to apply side effects from - but by that point
`focus_next()` had already performed the transition, so `set()` saw the
target already matching the current state and correctly reported a
no-op. The fix threads `focus_next()`/`focus_previous()`'s OWN returned
`FocusChange` straight into `apply_side_effects()`, and `apply_focus_change()`
(the click path, which has not transitioned yet) is the only caller that
still calls `set()` itself. `examples/21_focus --verify-focus` failed
exactly one check before this fix (`check_tab_away_mid_composition`'s
final assertion) and passed after - the bug was real, not hypothetical.

---

## 8. The focus ring: theming, geometry, and why it is four strips

A focus ring is basic keyboard usability, not accessibility plumbing -
design.md section 11 declines a11y IMPLEMENTATION (no AT-SPI/UIA/
NSAccessibility bridge) while keeping architectural hooks, but a sighted
keyboard user needs to SEE where Tab put them regardless of whether a
screen reader exists, so this belongs to 7-4 rather than to a future a11y
slice.

**Four thin strips, not one rectangle.** `RenderTree::hit_test()` has no
"ignore me" flag - its own header names exactly two things that change
what a point resolves to (`Overflow::kClip`, and explicitly NOT
`opacity`). A single ring rectangle sized to the focused widget's own
bounds would sit ABOVE it in paint order (rings are added after existing
content, so they always visually win) and would therefore win every
FUTURE hit test against that rectangle too - silently making a focused
button unclickable the moment it gained a ring. Four strips confined to
the margin OUTSIDE the widget's bounds - never inside them - cannot have
this effect, because the widget's own bounds are never covered by the
ring's. `tests/unit/test_focus.cpp`'s own case asserts this geometrically
(`dg::intersects()` against the target's bounds, for all four strips),
not merely visually.

**Permanent, lazily created, never removed** - `FocusRing::created`
guards a one-time `RenderTree::add_child()` for all four strips; every
subsequent call only repositions them (`set_local_bounds()`), collapsing
all four to an empty rectangle when nothing is focused (the same "empty
rect removes a subtree from painting and hit testing" rule
`doc/clipping.md` already established, applied here to a ring rather than
a popup). This matches 5-3's list pool and `PopupHost`'s own overlay-close
precedent (`doc/popup.md` section 3): `RenderTree` has no removal
primitive at all, so a "hide the ring" operation is necessarily "shrink it
to nothing," never "delete it."

**A NEW `FocusRing` per popup scope, not the host's own reused.** Because
an overlay popup's content is appended AFTER the host's ring already
exists (paint order = add order), reusing the host's ring instance for a
focused popup widget would draw the ring UNDERNEATH the popup's own
background - invisible. `examples/21_focus`'s `Runner` therefore builds a
fresh `FocusRing` for the popup's own content immediately after building
it (overlay: `scene.ring` is left alone, a SECOND ring is not built for
the overlay case at all in this slice's own demo since Tab already scopes
correctly and the ring update call is routed to the SAME shared
`scene.ring` positioned against the popup's own widgets - see
`focus_window.cpp`; native: a dedicated `popup_native_ring_`, matching its
dedicated `Focus`/`WidgetSet`). `tests/unit/test_focus.cpp`'s
`"two FocusRing instances... never share a node"` case pins the
underlying guarantee this depends on: two `update_focus_ring()` calls
against two different `FocusRing` instances allocate entirely disjoint
node ids.

**Colour comes from `color.focus-ring` (token id 12, light `#FF8800FF`/
dark `#FFA733FF`), read directly via `Theme::color_value()`, NOT through
`dg::bind_token()`/`ThemeBindings`.** This is a deliberate, defended
choice, not an oversight: `ThemeBindings::apply()` re-resolves through
`dg::set_prop()`, which is a `LayoutTree`-shaped door - but the ring's
nodes, like `TextField`'s `caret`/`selection_highlight` before them, are
added directly to `RenderTree` (bypassing `LayoutTree` entirely, the exact
technique `PopupHost`'s own overlay branch already established as safe),
because their position is derived from a focused widget's bounds at
FOCUS-CHANGE time, not declared once at construction time the way a
literal property value is. `set_prop()`'s door is therefore the wrong
shape for it, for the identical reason it was the wrong shape for the
caret. A live theme switch mid-focus is real but out of this slice's own
demo (no example combines theme switching and focus together yet) -
named as the one thing this colour choice does not yet re-apply
automatically, rather than silently assumed to.

---

## 9. Measured: a focus change costs a repaint, never a relayout

Every side effect a focus change performs - `RenderTree::set_local_bounds()`/
`set_style()` for the ring, `WidgetSet::text_field_set_focus()`'s own
internal `RenderTree::set_text()`/`set_style()` calls - goes through
`RenderTree` alone. None of it ever calls a `LayoutTree` dirty-marking
API, so `LayoutTree::layout()` is never told anything changed.

`examples/21_focus --verify-focus` measures this directly rather than
assuming it: after 13 focus changes (a full Tab cycle through five
widgets, two wraps, five more Tabs probing the `tab_index=-1` case, and
one direct `set()`), a real `LayoutTree::layout()` call reports:

```
nodes_visited=0 nodes_relaid_out=0
```

Zero - the identical shape doc/scrolling.md's offset, doc/form-controls.md's
slider value, and doc/list.md's recycling already measured for their own
runtime-state mutations, extended here to focus.

---

## 10. design.md section 5.6 line 622, re-verified

**Zero new `RenderObject`/node kinds were needed.** The ring is four
ordinary filled `NodeStyle` rectangles through the existing
`RenderTree::add_child()`; the scope root is a plain `NodeId`;
`Widget::tab_index` is a plain `std::optional<int>` field, not a new
`WidgetKind`. The streak `doc/completeness.md` measured through 7-3 (an
eighteenth consecutive slice) extends to a **nineteenth**: `Box`/`Text`/
`Button`/`Checkbox`/`Radio`/`ScrollView`/`Slider`/`TextField`/`Image`/
`PopupHost`/`List`/gradient+shadow+`set_image`/`AnimationEngine`/theme
tokens/the C ABI/multi-line text/grapheme-cluster editing/IME
composition/**focus** - nineteen slices deep, zero new primitives.

---

## 11. What this slice explicitly does not build, named

- **The shortcut/intent system and its four-level routing**
  (design.md sections 5.5.1/5.5.2) - a sibling P4 item large enough to be
  its own slice. **What this slice unblocks for it**: the routing table's
  own second level ("焦点节点起，沿祖先链向上冒泡") now has a real focus
  concept to route FROM - a `TextField`'s own editing intents already
  route to whichever node `dg::Focus::current()` names, which is exactly
  what that level needs, but the intent-binding table itself (Ctrl+C-shaped
  bindings, platform-specific `Mod` resolution, the four-level fallthrough)
  is untouched.
- **The gesture arena** (§5.16.3, P4) - unrelated to focus, named because
  the task names it as a sibling decline.
- **`Dialog` and modal focus trapping** - 7-5's job. This slice's own
  `enter_scope()`/`exit_scope()` bound Tab traversal only (section 4), not
  a refusal to lose focus to a click or a programmatic `set()` from
  outside - the smaller mechanism a modal trap could be built ON TOP of,
  not the trap itself.
- **a11y node trees** - design.md section 11's own declared boundary,
  unaffected by a focus ring existing (section 8's own framing: a focus
  ring is basic usability, not a11y plumbing, and this slice does not
  reach further than that one visible affordance).
- **Keyboard scrolling** (PageUp/PageDown/Home/End for `kScrollView`) -
  4-7 declined it pending the intent mechanism; this slice does NOT
  change that verdict, because `kScrollView` was deliberately NOT made
  focusable (section 3) - the remaining blocker is still exclusively the
  intent-binding system named above, unbuilt here.
- **Pointer capture** - checked, not assumed absent: `dg::Interaction`
  already has an equivalent (`holding()`, "the widget holding the press...
  distinct from 'drawn pressed'" - its own header comment), predating this
  slice and untouched by it. design.md section 5.2 mentions capture in the
  context of a drag continuing after the pointer leaves the WINDOW
  (cross-window), which `Interaction` does not attempt; this slice adds
  nothing to that gap either way.
- **Virtual/on-screen keyboards** - untouched, unrelated to focus as this
  slice built it.
- **Multi-line `TextField`/`TextArea` focus, rich text, arrow-key value
  adjustment on a focused `kSlider`, Enter/Space activation of a focused
  `kButton`/`kCheckbox`** - all real, plausible next steps a real focus
  system enables, all declined by name: none has a working caller in this
  slice's own demo yet, matching this project's own standing rule.

## 12. Verification performed

- **Full CTest, once: 31/31** (30 inherited + `focus.verify_demo_scene`).
- **Golden sha256 unchanged**:
  `f635028e6e1e92349d23f0575f1e39e7ca8b05e5974492641ee610fe520df12e` -
  confirmed on both g++ and clang++ builds. The focus ring is new painted
  output, opt-in by construction (a `FocusRing`'s four strips start
  collapsed to empty and are never touched unless a caller focuses
  something), so the pre-existing golden scene (which builds no `Focus`
  at all) is unaffected structurally, not merely by coincidence.
- **g++ 15 and clang++ 21, one `-Werror` build each**: both clean. Pure-C
  `examples/19_c_client/main.c` unaffected, confirmed still compiled by
  `cc -x c -std=c11` in `compile_commands.json`.
- **`-DDG_SANITIZE=ON`**: the full unit suite and `focus.verify_demo_scene`
  both green, exercising all three named hazards - a `NodeId` held across
  a popup close (three open/focus/close overlay cycles in
  `check_overlay_scope_and_close`), list recycling
  (`tests/unit/test_focus.cpp`'s `blur_if_any_of` cases), and window
  destruction (every headless check's own `WindowManager` destructor runs
  at scope exit, closing every window it opened, including mid-popup).
- **clang-tidy `-p build` and clang-format**: exit 0 on every changed
  file, zero `NOLINT`. Two real findings fixed rather than suppressed: a
  DeMorgan simplification in `focus_order()`'s collect step, and explicit
  `has_value()` guards on the native popup's own `Focus`/`WidgetSet`
  before every access in `focus_window.cpp` (an invariant - only accessed
  while `is_native` holds - that was true but unchecked).
- **`props.*`/theme lock/ABI lock/drift tests**: unaffected, still green -
  this slice added one THEME token (`color.focus-ring`, via the existing
  generator/lock machinery, `theme.no_drift`/`theme.abi_lock`/
  `theme.consistency` all re-verify it) and no property, no ABI surface.
- **Defect injection, against this slice's own new logic**: two, both
  caught immediately by `tests/unit/test_focus.cpp` (reverted after,
  never shipped) - making `tab_index_precedes()` ignore priority entirely
  (the order/wrapping/override test failed exactly where predicted), and
  making `exit_scope()` never blur (the popup-close hazard test failed
  exactly where predicted). A THIRD, real (not injected) bug was caught
  by `examples/21_focus`'s own end-to-end check rather than by unit
  tests: the double-`set()` bug section 7 describes, found because the
  mid-composition-Tab-away check is an END-TO-END assertion through the
  real event queue rather than a call directly against `Focus`.

## 13. Final counts

49 properties (unchanged - `tab_index` is a plain `Widget` field, not a
property); 12 theme tokens (11 + `color.focus-ring`); 8 `WidgetKind`s
(unchanged); 31 CTest entries (30 + `focus.verify_demo_scene`); 21
examples (20 + `21_focus`); zero new node/`RenderObject` kinds, a
nineteenth consecutive slice.
