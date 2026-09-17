# The C ABI: single source of truth, generated try/catch, and what "creates two windows and responds to clicks" actually exercises

This is 6-3, phase 6's third and last slice. Two prior slices built shapes
specifically for it: 4-2's `prop_id` (numeric, generated, locked) and 6-2's
`token_id` (the same shape, same generator family). Both existed for exactly
one eventual consumer - a host language that sets a property by number rather
than by calling a C++ setter - and that consumer is this slice.

design.md section 5.8's own acceptance criterion for P5, verified in full by
this slice: **a pure-C example program creates two windows and responds to
clicks**. Everything below is in service of that sentence, and the
"defer/decline" table in section 6 is deliberately long: the discipline this
whole project has followed for fourteen slices - build the smallest thing a
working caller needs, do not build ahead of one - applies to the ABI surface
itself, not only to the engine underneath it.

## 1. The single source of truth: `abi/drawgui.def.toml`

Modelled on the two working precedents (`props/drawgui.props.toml` +
`tools/gen_props.py`, `themes/schema.toml` + `tools/gen_theme.py`), and
reusing one of them directly rather than re-deriving it: `tools/gen_abi.py`
imports `tools.gen_props.load_definitions()` and calls it against
`props/drawgui.props.toml` itself, so the `prop_id` constants this slice
re-emits in C form come from the SAME validated `Definitions` object the
existing C++ generator already produces - one parser, one set of validation
rules, two renderings (`inline constexpr` for C++, `#define` for C).

`themes/schema.toml` is deliberately NOT touched the same way. This slice
declines the theme ABI entirely (section 6), so importing `gen_theme` and
re-emitting `token_id` constants with no ABI consumer function would be
exactly the speculative surface design.md section 5.8's own design principle
- "只导出必要面", only export the necessary surface - forbids.

The TOML declares four kinds of thing:

- `[[opaque_type]]` - `dg_app_t`, `dg_window_t`, `dg_node_t`. Three handles,
  not four: `dg_theme_t` is not declared, because nothing in this slice's
  surface returns one (section 6).
- `[[struct]]` - `dg_value`, `dg_app_opts`, `dg_window_opts`, `dg_event`.
  Every one's first field is `size` (design.md section 5.8 decision 4's own
  rule: a struct is future-proofed by its size, so an old, shorter
  caller-built struct is detected rather than read past the end of).
- `[[constant_group]]` - `node_type`, `value_type`, `event_kind`, `error`.
  Plain `#define`, not an `enum`, for the identical reason `prop_id`/
  `token_id` already are constants rather than an enumeration
  (`prop_ids.generated.h`'s own comment: a value arriving from outside the
  process can hold any value the underlying integer type can, and an
  enumeration type claiming otherwise is exactly what clang-tidy's
  `cppcoreguidelines-use-enum-class`/`performance-enum-size` already flagged
  once for `prop_id`). Twenty values across four groups, described in full in
  section 3's error-code table and section 4's node-type table.
- `[[function]]` - sixteen entries, each naming `ret`, `params`, and `impl` -
  the `dg::abi::<impl>` C++ function (`src/abi/abi_impl.h`/`.cpp`) the
  generated trampoline calls. **`impl` is hand-written; the trampoline is
  not** - the whole of section 2.

## 2. The generator's one job: making the try/catch wrapping provably uniform

design.md section 5.17.1 states the risk in one sentence: "C++ 异常穿越 C ABI
是未定义行为" - a C++ exception crossing a C ABI boundary is undefined
behaviour, so every exported function must fully catch. It also states the
mitigation in one sentence: "由生成器统一包裹，不允许手写导出函数" - the
generator wraps uniformly; hand-writing an export function is not allowed.

`tools/gen_abi.py`'s `render_impl()` is the whole of that promise kept
mechanically. For every `[[function]]` entry it emits:

```cpp
extern "C" DG_EXPORT int32_t dg_node_set_prop(dg_node_t* node, uint16_t prop_id,
                                              const dg_value* value) {
  try {
    return dg::abi::node_set_prop(node, prop_id, value);
  } catch (const std::bad_alloc&) {
    dg::abi::set_last_error("dg_node_set_prop: out of memory");
    return DG_ERR_OOM;
  } catch (const std::exception& e) {
    dg::abi::set_last_error(std::string("dg_node_set_prop: ") + e.what());
    return DG_ERR_INTERNAL;
  } catch (...) {
    dg::abi::set_last_error("dg_node_set_prop: unknown exception");
    return DG_ERR_INTERNAL;
  }
}
```

Three things make this uniform by construction rather than by review:

1. **There is no per-function opt-out.** Every `[[function]]` entry goes
   through the same `render_impl()` loop; there is no `wrap = false` field to
   set. The only way to add an exported symbol without this wrapping is to
   hand-write one - which is section 7's own MUST NOT, verified by
   `tools/abi_lock.py` naming any function it did not already know about as
   an "append" that has to be recorded, not a silent addition.
2. **The failure VALUE is derived from `ret` alone** (`_classify_return()`):
   `void` returns nothing from a catch clause, any pointer type returns
   `nullptr`, `int32_t` returns `DG_ERR_OOM`/`DG_ERR_INTERNAL`, `uint32_t`
   returns `0`. There is no per-function "what should this return on
   failure" field either - the return TYPE decides, so a future function
   cannot land with its wrapping half-specified.
3. **`dg_last_error()` is populated from the SAME three catch clauses**,
   which is what makes it more than a status code: `catch (const
   std::exception& e)` calls `e.what()`, so a caller gets the real message
   crossing the boundary, not merely "something failed" - see the exception-
   boundary test below for what this makes provable.

### How a removal would be caught

The task's own defect-injection catalogue names "provably inert code" as the
mode most likely to hide in exactly this kind of wrapper: a try/catch nobody
ever exercises could be deleted with nothing turning red. This is answered
directly rather than assumed: `dg_debug_trigger_exception(int kind)`
(section 5) is a function whose ENTIRE hand-written implementation
unconditionally throws - `std::bad_alloc` for `kind == 1`, a
`std::runtime_error` for `kind == 2`, a bare `int` for anything else.
`examples/19_c_client`'s own oracle calls all three through the REAL
generated trampoline (not through `dg::abi::debug_trigger_exception()`
directly - there is no way to call it directly from C, which is the point)
and asserts:

- `dg_debug_trigger_exception(1)` returns `DG_ERR_OOM`.
- `dg_debug_trigger_exception(2)` returns `DG_ERR_INTERNAL`, and
  `dg_last_error()` contains the literal string the `std::runtime_error` was
  constructed with.
- `dg_debug_trigger_exception(3)` (an exception no `catch` clause names by
  type) still returns `DG_ERR_INTERNAL` via `catch (...)`.
- **The process is still running afterwards** - every one of the checks the
  oracle runs after these three still executes and still reports its own
  verdict, which is the actual claim being tested: not "the function returns
  the right code" alone, but "the process survived an exception it did not
  expect."

I ran this deliberately with the wrapping removed once, as the task asks: I
temporarily edited `render_impl()` to skip the `try`/`catch` for
`dg_debug_trigger_exception` specifically, regenerated, rebuilt, and ran the
oracle. Result: the process aborted (SIGABRT, `terminate called after
throwing an instance of 'std::bad_alloc'`) mid-run, and every check after
that point never printed - CTest reported the whole test as failed rather
than as "27 ok, 1 not ok". That is the "provably inert" failure mode this
task warns about, closed: removing the wrapper is caught by a crash, not by
silence. The edit was reverted immediately afterward; it is not part of the
generator as committed.

## 3. Error codes, `dg_value`, and what `dg_node_set_prop` actually validates

`DG_ERR_*` (eleven values, `int32_t`) is a direct restatement of
`dg::PropStatus` (`node_props.h`) plus three ABI-only additions
(`DG_ERR_OOM`/`DG_ERR_INTERNAL` from the exception boundary,
`DG_ERR_INVALID_HANDLE` from handle validation, section 4):

| `DG_ERR_*` | Source |
| --- | --- |
| `OK` (0) | `PropStatus::kApplied` |
| `UNKNOWN_ID` | `PropStatus::kUnknownId` |
| `TYPE_MISMATCH` | `PropStatus::kTypeMismatch` |
| `VALUE_OUT_OF_RANGE` | `PropStatus::kValueOutOfRange` |
| `NOT_APPLICABLE` | `PropStatus::kNotApplicable` (design.md section 5.8 decision 7's parentData case included - it is one more `PropStatus`, not a special ABI case) |
| `UNSUPPORTED` | `PropStatus::kUnsupported`, or `dg_node_insert_before`'s non-null-`ref` refusal (section 5) |
| `INVALID_ARGUMENT` | a null required pointer, or a struct's `size` shorter than this build's own `sizeof` |
| `INVALID_HANDLE` | section 4 |
| `ALREADY_ATTACHED` | section 5 |
| `NO_WINDOW` | a node still pending (section 5) |
| `OOM` / `INTERNAL` | section 2 |

`dg_value` is `dg::PropValue` at the boundary: `{size, type, number, bits}`,
where `type` is one of `DG_VALUE_FLOAT`/`DG_VALUE_LENGTH`/`DG_VALUE_COLOR`/
`DG_VALUE_ENUM`, `number` carries a float/length, and `bits` carries an ARGB
colour or an enum ordinal - the identical two-field representation
`PropValue` already used internally (`node_props.h`: "a tagged scalar...
`scalar_`/`bits_`"), so `dg::abi::to_prop_value()` is a direct field copy,
not a conversion. A `dg_value` with an unrecognised `type` is
`DG_ERR_INVALID_ARGUMENT` before it ever reaches `dg::set_prop()`.

`dg_node_set_prop()` requires a LIVE node (section 5) and otherwise IS
`dg::set_prop(tree, node_id, prop_id, value)` unchanged - the ABI adds
handle resolution and a value-type conversion, and nothing else. It does not
duplicate `dg::set_prop()`'s own validation; it cannot disagree with it.

## 4. Handle validation: why this is NOT AnimHandle, by the SAME argument doc/theme.md already made once

The task asks specifically to read `AnimHandle{index, generation}`
(`include/drawgui/anim/animation_engine.h`) before inventing a second
answer - 6-1's own generational-index solution to "a caller might hold a
stale handle." It is the wrong precedent here, and the reason is the
identical one `doc/theme.md`'s `ThemeBindings` comment already worked out for
a different pair of alternatives:

> a generation counter exists because an animation SLOT's count is unbounded
> over a session... while a render NODE's count is not... there is no reuse,
> so there is no ABA problem to guard against

`dg_app_t`/`dg_window_t`/`dg_node_t` handles in this ABI's own arenas are
shaped like the SECOND case, not the first: `dg_node_create()`,
`dg_window_create()` and `dg_app_create()` only ever **append** to their
arena (`std::deque<AppImpl>`-shaped, one entry per resource, `std::vector`
for apps). `dg_node_remove()` never reclaims the arena SLOT for a future
`dg_node_create()` call to reuse - it marks the slot `NodeSlot::State::
kRemoved` and stops there. An index is claimed once, forever; there is no
ABA problem an index-plus-generation pair would be guarding against, because
nothing ever reuses an index. Reaching for `AnimHandle`'s generation counter
here would be solving a problem this arena's own data does not have - the
exact sentence `doc/theme.md` already used for `ThemeBindings` versus
`AnimHandle`, applied to a THIRD data shape rather than re-derived.

What IS new, and load-bearing, is a different property: **the wrapper
pointer a host holds is never freed.** `dg_app_create()`/`dg_window_create()`/
`dg_node_create()` each `emplace_back()` a tiny `{app, index}` (or `{index}`
for an app) struct into a process-global `std::deque` and hand back a pointer
into it (`&handles.back()`), never `new` on its own. `std::deque` never
invalidates a previously-returned element's address as more are appended
(unlike `std::vector`), so that pointer stays dereferenceable for the whole
process, independent of how many more handles are created later. Dereferencing
it after `dg_node_remove()`/`dg_app_destroy()` therefore reads two ordinary,
still-live integers - never freed memory - and `resolve_app()`/
`resolve_window()`/`resolve_node_slot()` are what turn "the arena entry this
index names is no longer alive" into `DG_ERR_INVALID_HANDLE` rather than
either a crash or (the opposite failure) silently acting on stale state.

**This design was corrected once, under measurement, not assumed correct
from the start.** The first draft put each `dg_window_t`/`dg_node_t`
wrapper's storage INSIDE its owning `AppImpl`, freeing it the moment
`dg_app_destroy()` reset that `AppImpl` - which would have turned "the
handle is now stale" into an ACTUAL dangling pointer the instant a host
dereferenced it after `dg_app_destroy()`: a real use-after-free, exactly the
class of bug this design exists to prevent rather than the intended ordinary
error return. `-DDG_SANITIZE=ON` did not catch that particular defect
directly (the sequence the oracle drives never happens to touch the freed
bytes in a way ASan's shadow memory flags before the process exits), but a
DIFFERENT, real defect the first draft did have - `new`-ing each wrapper and
never freeing it - was caught immediately by LeakSanitizer, because an
allocation with no reachable owner IS what LSan defines a leak to be. Moving
the wrapper decks to process-global (rather than per-`AppImpl`) statics fixed
both problems at once: the deque is a reachable GC root LSan's own scan
already walks (so the bytes stop being a "leak"), and it is never destroyed
by `dg_app_destroy()` (so a handle never actually dangles). Both properties
are exercised by `examples/19_c_client --verify-c-client`, run clean under
`-DDG_SANITIZE=ON` (zero leaks, zero UB) as this slice's committed record of
it.

**The exercised bad-handle case** (`examples/19_c_client`, checks 7 and the
final one): `dg_node_remove()` on a live node succeeds once and
`DG_ERR_INVALID_HANDLE` on every later call, including a second
`dg_node_remove()`, a `dg_node_set_prop()`, and a `dg_node_insert_before()`
naming it as the child; and, one level up, using ANY node handle after
`dg_app_destroy()` on its owning app is `DG_ERR_INVALID_HANDLE` too - the
app-level instance of the identical design.

## 5. The node lifecycle: pending, live, removed - and where the design sketch and the working engine disagree

design.md section 5.8's own C sketch:

```c
dg_node_t* dg_node_create(dg_app_t*, uint16_t type_id);
int        dg_node_set_prop(dg_node_t*, uint16_t prop_id, const dg_value*);
int        dg_node_insert_before(dg_node_t* parent, dg_node_t* child, dg_node_t* ref);
int        dg_node_remove(dg_node_t*);
```

`dg_node_create()` takes no PARENT and no WINDOW, which only makes sense if a
node can exist independent of any tree until attached - and the sketch
supplies no `dg_window_set_root()`-equivalent detail for how that attachment
actually happens against a REAL `LayoutTree`, because none existed when
§5.8 was written. Grounding the sketch in the engine `LayoutTree`/`RenderTree`
actually have (checked, not assumed) surfaces two real gaps between what the
abstract signature implies and what fourteen prior slices actually built:

**Gap 1 - there is no "detached node."** `LayoutTree::add_child(parent,
box, style)` is the ONLY way a node comes into existence in this engine, and
it always requires an ALREADY-LIVE `parent` NodeId. There is no free-standing
"build a node, attach it later" primitive underneath the ABI sketch's
implied one. This slice's own `NodeSlot` (`src/abi/abi_types.h`) is what
supplies it instead: `dg_node_create()` allocates a PENDING slot (a `type_id`
and nothing else - no `LayoutTree` involvement at all), and either
`dg_window_set_root()` (parent = the tree's own structural root,
`LayoutTree::root()`) or `dg_node_insert_before()` (parent = an already-live
node) is the one operation that calls the real `add_child()` and transitions
the slot to LIVE, choosing type-appropriate default box dimensions at that
moment (§5.8's sketch is silent on where a node's initial size comes from at
all - `dg_node_set_prop()` requires a live node, so a node cannot start at
`0x0` waiting for a property write that cannot happen yet).

**Gap 2 - there is no insertion-order primitive, only append.** Both
`RenderTree::add_child()` and `LayoutTree::add_child()` always append to the
end of `parent`'s child list; neither has ever grown a way to insert before
an arbitrary sibling, because no prior slice's own caller needed one (every
example in this project builds its tree in one pass, front to back).
`dg_node_insert_before(parent, child, ref)` therefore accepts `ref == NULL`
(append - the only case exercised) and refuses a real `ref` with
`DG_ERR_UNSUPPORTED`, named by the ABI lock's own comment rather than
silently reordering the wrong way. Building real "insert before sibling"
support is an ENGINE-layer slice (`RenderTree`/`LayoutTree` themselves), not
an ABI-generator one - inventing the capability here, ungrounded in a
working `add_child()` that can do it, is the exact "twelve deleted platform
headers" mistake design.md's own account already names as this project's
standing failure mode to avoid.

**Gap 3 - `dg_node_remove()` cannot detach a node's tree structure, because
neither `RenderTree` nor `LayoutTree` has ever grown a removal primitive at
all.** This is not a 6-3 omission; it is a project-wide, previously-recorded
fact: `doc/list.md` section 1 evaluated exactly this question for list
virtualization (a much stronger motivating case - 1000 logical items behind
14 real nodes) and found no caller that needed node removal, choosing a
fixed recycled pool instead. `dg_node_remove()` in this slice therefore does
the one real thing available to it - invalidate the HANDLE (section 4),
turning every later call on it into `DG_ERR_INVALID_HANDLE` - and does NOT
detach the underlying node from its window's tree, which keeps painting and
laying it out exactly as before. This is a genuine, load-bearing scope
boundary, not a silent gap: a host that calls `dg_node_remove()` expecting
the node to vanish from the screen will be surprised, and that surprise is
the honest state of the engine underneath this ABI, not a bug in the ABI
layer. Building real node removal is the natural first task for whichever
future slice needs it (a dynamic list bound through the ABI is the nearest
candidate) - grounded in a real caller, exactly as this project's own
standing rule requires, rather than invented here to make one function's
name feel more complete than the engine can back up.

## 6. What this slice declines, named by name, and why

Every one of these is a genuine scope boundary, checked against a working
caller (or the absence of one) rather than against how complete the surface
would look with it included.

| Declined | Why |
| --- | --- |
| `dg_set_event_callback` | design.md section 5.8 decision 2's SECOND event mode (QuickJS/native embedding). No host in this project embeds QuickJS or any other native runtime, so there is no working caller to extract an interface from - poll mode (`dg_poll_events`/`dg_wait_events`) is the ONLY mode this slice's own C example exercises, and it is the mandatory one (decision 2: Node/Bun FFI cannot receive a cross-thread callback safely at all). |
| `dg_custom_paint_set_cmds` | Decision 3's escape hatch. Not on the two-window-plus-click acceptance path; nothing in this slice needs to draw a custom widget. |
| `dg_animate` / `dg_node_set_transition` | Section 5.16.1's animation ABI. `dg::AnimationEngine` is real and working in C++ (6-1), but nothing in this slice's own criterion needs an animated property, and wiring an `AnimationEngine` per window into the ABI trampoline layer ahead of a caller that actually animates something through it is the same "ahead of a working implementation" mistake. |
| `dg_theme_load_dir` / `dg_theme_load_memory` / `dg_theme_set_variant` / `dg_theme_override` / `dg_app_set_theme` | Decision 6's theme ABI. `dg::load_theme()`/`dg::ThemeBindings` are real and working in C++ (6-2), and the task itself flags this surface as "nearly free" - it likely is, but "nearly free" is not the same claim as "grounded in a working C caller", and no C client in this slice binds a token through the ABI. Declining this is the one call in this slice most worth a reader's second-guessing; it is recorded here rather than silently dropped so that judgement is visible rather than assumed. |
| QuickJS binding stubs | design.md section 5.8 decision 5 lists this as a fourth generated artifact. There is no QuickJS embedding anywhere in this project to bind against, so a stub here would be untested by construction. |
| Real `dg_node_insert_before` with a non-null `ref` | Section 5, gap 2: the engine has no insertion-order primitive underneath it. |
| Real `dg_node_remove` tree detachment | Section 5, gap 3: the engine has no removal primitive underneath it. |
| `dg_window_destroy` | Windows close via the user's own close button, observed through `dg_poll_events`' `DG_EVENT_WINDOW_CLOSED` (mirroring `WindowManager::request_close()`'s own async-via-`pump()` shape) - matching every prior example's own multi-window convention (`examples/01_sdl3_multi_window`), rather than inventing a second, synchronous close path nothing in this project's window layer offers. |
| `dg_app_bind_shortcut` | design.md section 5.5.3's runtime-rebinding signature (`dg_app_t*, const dg_shortcut*, uint16_t`). 8-1 made the binding table GENERATED at build time from `input/shortcuts.toml`, and every shipped action already has a binding there - no host anywhere in this project needs a chord DIFFERENT from the generated table, so a C `dg_shortcut` struct here would be ABI surface invented for a caller that does not exist. PREREQUISITE: a host that genuinely needs a binding the generated table does not provide - a user-configurable keymap is the obvious one. |

## 7. `dg_dump_layout_tree`: what it actually reports, and what it honestly cannot

design.md section 5.8's sketch: a JSON dump of "类型/约束/尺寸/偏移/
parentData/边界标记" - type, constraints, size, offset, parentData,
boundary flags. `doc/completeness.md` section 7 named this P2's own missed
acceptance item, "small but conspicuous", and it landed in this slice because
`RenderTree` already stored everything a subtree walk needs except a public
way to enumerate children - `RenderTree::children(NodeId)` (five lines,
`src/render/render_tree.cpp`) is the one small engine addition this slice
made to unlock it, exposing `tree_impl.h`'s already-existing `Node::children`
vector rather than adding new bookkeeping.

What is emitted, per node, and what is honestly not:

| Field | Status |
| --- | --- |
| type | `BoxStyle::kind` (leaf/row/column/wrap\_row/wrap\_column/absolute) - EMITTED |
| size, offset | the resolved absolute border box (`LayoutTree::bounds()`) - EMITTED, one rectangle carries both |
| parentData | `grow`/`shrink`/`basis`/`left`/`top`/`right`/`bottom`, each only when set to something other than its default - EMITTED |
| constraints | NOT stored per-node anywhere `LayoutTree` exposes publicly once `layout()` returns; only the resolved size survives a pass. OMITTED, named rather than approximated |
| boundary flags | "is this node a relayout boundary" is computed and discarded inside `layout_impl.h` per pass; no public accessor exists. OMITTED, named rather than approximated |

Both omissions are the honest state of what `LayoutTree` currently makes
public, not an oversight in the dump function - adding either would be an
`LayoutTree` change, not a `dg_dump_layout_tree` one, and neither had a
caller in this slice asking for it.

## 8. `.d.ts`: settling 6-2's deferral

6-2 deferred `.d.ts` generation to this slice by name (`doc/theme.md`'s own
closing table: "`.d.ts` 与 ABI 常量表明确推迟到 6-3"). The decision, made
explicitly rather than deferred a third time: **generate it.**
`tools/gen_abi.py --root .` writes `abi/drawgui.d.ts` - ambient ``export type``
declarations for the three opaque handles, `export interface` for the four
structs, `export const` objects for the four constant groups and the
re-emitted `prop` id table, and an ambient `DrawguiAbi` namespace with one
`function` declaration per exported symbol. It costs one more render function
over a `Definitions` object this slice already has to build in full for
`drawgui.h`/`drawgui_abi.generated.cpp` - genuinely close to free, exactly as
the theme token table's own C-constant re-emission already proved for a
different artifact.

**What is NOT settled, named rather than silently included:** QuickJS
binding stubs (section 6) - decision 5 names this file's SIBLING, not the
same artifact, and it is declined for an unrelated reason (no QuickJS host
exists to bind). Nothing in this project's own build reads `abi/drawgui.d.ts`
yet; there is no JS framework layer (design.md section 6, phase P6) to
consume it. It is a settled TARGET shape, not a proven one - stated plainly
here rather than left to look like more than it is.

## 9. `examples/19_c_client`: how the acceptance criterion was actually run

The example is genuinely pure C: `main.c`, compiled by `cc` (gcc's C front
end) and by `clang` under `-std=c11 -Wall -Wextra -Wpedantic -Wconversion
-Wsign-conversion -Werror`, linking `libdrawgui.a` (a C++ static library)
through `LINKER_LANGUAGE CXX`. `#if defined(_WIN32)... #elif defined
(__GNUC__)` are the only preprocessor conditionals in `drawgui.h` itself; no
C++ keyword, no Skia type and no `RenderObject`/render-tree type appears
anywhere in it - the compile itself is what proves this, not a manual
reading of the header.

Two modes:

- **No arguments**: opens two real windows (`dg_window_create` x2), each with
  a `DG_NODE_TYPE_BUTTON` root node (`dg_node_create` + `dg_window_set_root`),
  and runs `dg_wait_events`/`dg_poll_events` until both close. A click on
  either button's `DG_EVENT_CLICK` is used to set the OTHER window's
  background colour via `dg_node_set_prop` - proof that an event naming a
  node and a window round-trips through the ABI into a property write the
  HOST itself chose to make, not merely that a click was detected internally.
  This is the literal, human-runnable form of the acceptance criterion, and
  it has no CTest entry for the identical reason `examples/01_sdl3_multi_window`
  does not: it needs a display (or an explicit `SDL_VIDEODRIVER=dummy`) and
  runs until closed, which is not a shape CTest asserts against.
- **`--verify-c-client`**: the CTest oracle (`c_client.verify_demo_scene`),
  run under `SDL_VIDEODRIVER=dummy` (`examples/19_c_client/CMakeLists.txt`'s
  own `ENVIRONMENT` property on the test, not `setenv()` inside `main.c` -
  see the file's own top comment for why: `setenv()` needs
  `_POSIX_C_SOURCE`, a reserved identifier `bugprone-reserved-identifier`
  rightly objects to a strict-C11 file defining, with no alternative
  spelling POSIX permits; `examples/14_popup`'s own C++ oracle is not held to
  that bar and calls `setenv()` directly). `SDL_VIDEODRIVER=dummy` opening a
  REAL window (not a fake one) was measured once already, in `doc/popup.md`
  ("SDL_CreatePopupWindow producing a real, bounds-escaping OS window... on
  both x11 and wayland" is the STRONGER claim there; ordinary
  `SDL_CreateWindow` under `dummy` is the weaker, already-proven half of it).
  The click itself is driven through the REAL SDL event queue via
  `dg_debug_warp_pointer`/`dg_debug_post_pointer_button` - two TEST-ONLY
  functions (named `dg_debug_*`, a deliberate, narrow exception to "只导出
  必要面" stated in `abi/drawgui.def.toml`'s own header) that are the ABI-
  boundary wrapping of `WindowManager::warp_pointer()`/
  `post_pointer_button()`, the identical mechanism every OTHER example's own
  `--script` mode already uses in C++ to drive a scripted pointer through the
  real queue rather than around it. Twenty-eight checks in one run: version,
  app/window/node lifecycle, property writes (including a deliberately
  unknown `prop_id` and a deliberate type mismatch), `dg_node_insert_before`'s
  append-only refusal, `dg_dump_layout_tree`'s shape, a real click's event
  delivery, handle-validation after `dg_node_remove()`, the exception
  boundary (section 2), and handle validation again after `dg_app_destroy()`.

**A real bug this oracle found, and how it was diagnosed rather than
guessed at:** the first working version of `process_pump()` hit-tested
pointer events BEFORE calling `LayoutTree::layout()` for the first time -
correct on every SECOND and later pump, because `repaint_and_present()` had
already laid the tree out at the end of the PREVIOUS call, but wrong on the
very FIRST pump of a freshly-attached node, whose bounds are all-zero until
`layout()` has run at least once (`LayoutTree::add_child()`'s own header
comment: "reading `absolute_bounds()` before then answers with zero"). The
click-through-real-SDL-queue check failed with exactly this symptom (no
`DG_EVENT_CLICK` ever arrived), and the fix - laying out every open window
BEFORE processing this pump's own pointer events, not only at the end - is
recorded in `abi_impl.cpp`'s own comment on `process_pump()`.

**Run, and the result reported literally**: `ctest --test-dir build` (gcc),
`--test-dir build-clang` (clang), and `--test-dir build-san`
(clang, `-DDG_SANITIZE=ON`) each report `c_client.verify_demo_scene ...
Passed`, 29/29 overall in all three configurations. The interactive,
no-argument mode was run manually under `SDL_VIDEODRIVER=dummy` for a fixed
duration (`timeout --signal=KILL 3`) as a smoke test - no crash, exits only
when killed (there is no scripted close path under a driver with no window
manager chrome to click), which is the expected shape of an event loop that
is genuinely blocking in `dg_wait_events` rather than busy-spinning.

## 10. design.md section 5.6 line 622, re-checked

Zero new node kinds, zero new `RenderObject` kinds, zero new `WidgetKind`
values. This slice adds no widget at all - `DG_NODE_TYPE_BOX`/
`DG_NODE_TYPE_BUTTON` are `WidgetKind::kPanel`/`WidgetKind::kButton`, both
already existing (`kPanel` since sub-step 3, `kButton` since 4-8's era). The
streak holds through a **fifteenth** consecutive slice.

## 11. What this does not do

- No new property, node kind, `RenderObject` kind or `WidgetKind`.
- No animation ABI (`dg_animate`/`dg_node_set_transition`), no theme ABI, no
  callback event mode, no custom-paint escape hatch, no QuickJS stub - all
  named in section 6.
- No real tree-structural node removal or mid-list insertion - section 5's
  two engine gaps, named rather than papered over.
- No JS framework layer, no Bun/Node FFI runtime adapter (design.md section
  6, phase P6) - `abi/drawgui.d.ts` is a settled target for it, not a
  consumer of it.
- No Windows/macOS verification of the ABI (this project's whole platform
  layer is Linux-only so far; `DG_EXPORT`'s `__declspec(dllexport)` branch is
  written but untested).

## Counts after this slice

16 exported functions, 3 opaque types, 4 structs, 20 constants across 4
groups, 49 properties unchanged (38/10/1), 11 theme tokens unchanged, 29
CTest entries (26 before this slice + `abi.no_drift` + `abi.abi_lock` +
`c_client.verify_demo_scene`), 19 examples, 8 `WidgetKind`s unchanged, 6
`LayoutKind`s unchanged. `drawgui_render_png` sha256 unchanged
(`f635028e...`). g++/clang++ x `-Werror` and a C compiler (`cc`/`clang`) x
`-Werror` all green; `-DDG_SANITIZE=ON` green (zero leaks, zero UB);
clang-tidy and clang-format both exit 0 with zero `NOLINT` anywhere in this
slice's own new code.

## 12. (7-6, append-only) The theme ABI, declined here by name, lands

Section 6's own table names `dg_theme_load_dir`/`load_memory`/
`set_variant`/`override`, `dg_app_set_theme` as declined - "no C client in
this slice binds a token through the ABI, so there is nothing this slice's
own acceptance criterion can use to prove the wrapper is right rather than
merely plausible." P7 slice 7-6 is that C client: all five functions land
through this SAME generator (`tools/gen_abi.py`, extended to import
`tools/gen_theme.py`'s `load_definitions()` alongside `tools/gen_props.py`'s,
re-emitting `DG_TOKEN_*` constants the identical way `DG_PROP_*` already
is), a new opaque type (`dg_theme_t`, a fourth handle, same never-freed
arena shape as the other four), and one new `dg_value` kind
(`DG_VALUE_TOKEN`) rather than a second, `bind_token()`-shaped exported
function - `dg_node_set_prop()` is reused unchanged, exactly the way this
document's own section 3 already describes `dg::set_prop()` being reused
for every ordinary property write. `doc/theme-packages.md` section 7 has
the full record, including why `dg_theme_load_memory`'s `len` is
`uint32_t` rather than design.md's own literal `size_t` sketch (this ABI's
generator has never had a `size_t` primitive, and this slice matches the
existing convention rather than introducing one), the `DG_ERR_NO_ACTIVE_
THEME` addition, and the 36-check pure-C exercise
(`examples/19_c_client`, extended rather than duplicated) that proves the
new surface compiles as C the same way the original acceptance criterion
did. `abi.abi_lock` (this document's own section 1) correctly flagged the
unrecorded append before `--write` was run - the mechanical proof this
slice did not silently widen the ABI surface.

## 13. (8-3d, append-only) The shortcut/action ABI lands

design.md section 5.5.3 names four things; this slice builds three and
declines the fourth (section 6's own new `dg_app_bind_shortcut` row).

`dg_node_scope_action(dg_node_t*, uint16_t action_id)` wraps
`dg::ActionScopes::scope()` unchanged - a new per-window side table
(`WindowImpl::action_scopes`, the same per-window placement
`theme_bindings` already has, for the identical reason: a `NodeId`
numbering space belongs to one window's `RenderTree`). It requires a LIVE
node (`DG_ERR_NO_WINDOW` otherwise), because `ActionScopes` is keyed by
`dg::NodeId`, which a pending node does not have yet - the same rule
`dg_node_set_prop()`'s `DG_VALUE_TOKEN` path already applies for the
identical reason.

`dg_shortcut_label(uint16_t action_id)` wraps `dg::shortcut_label()`
(chord.h, unit-tested since 8-1/8-2) rather than re-implementing label
generation, fixed to `Platform::kLinux` (the only backend this project
builds - README's own "no platform abstraction ahead of a second backend").
Its `const char*` lifetime is solved the SAME way `dg_last_error()` and
`dg_dump_layout_tree()` already solve theirs: a `thread_local std::string`
(`shortcut_label_storage()`) the C++ side owns, valid until the next call
on that thread. An action_id no binding names returns NULL - a real lookup
failure (mirroring `dg::shortcut_label()`'s own `std::optional` empty
case), deliberately not an empty string, which would instead claim "this
action has a binding with no displayable text", a different fact this ABI
never needs to state.

`DG_EVENT_ACTION` (`event_kind` = 3) is delivered through the existing
`dg_poll_events()` queue - `dg_event::node` is the resolved target (null
for a level-4 app-wide action), `dg_event::action_id` is the new field
`dg_event` gained (appended after `y`; `abi.abi_lock`'s own append-only
rule allows a struct field append unconditionally, confirmed by running
`tools/abi_lock.py --check` against the addition before recording it).
Producing it needed two small pieces of new plumbing inside `abi_impl.cpp`,
neither of them new ABI surface: `WindowImpl` gained a `dg::Focus` (a
pointer press on a focusable widget focuses it, blurring otherwise - the
same rule `examples/21_focus`'s own `Scene::dispatch_pointer()` already
applies, generalised to this ABI's own `process_pointer_event()`) so that
`dg::route_key_event()`'s `focused` parameter has a real answer, and
`process_key_event()`, this ABI's second caller of `route_key_event()`
after `examples/10_scrolling`'s C++ one (8-3c) - gated to `KeyAction::kDown`
only, since `route_key_event()` itself does not discriminate on it and a
single key press must not fire its resolved action twice.

Exercising this over a real SDL key event from pure C needed one more
`dg_debug_*` function, the same narrow, named exception this file's own
header already grants `dg_debug_warp_pointer`/`dg_debug_post_pointer_button`:
`dg_debug_post_key()` wraps `WindowManager::post_logical_key()`, parsing its
`logical_key_name` argument with `dg::parse_chord()` (the one existing
parser for this grammar) and refusing a modifier chord or an unknown name
with `DG_ERR_INVALID_ARGUMENT` - `post_logical_key()` itself has no
modifier parameter, so this hook only ever exercises an unmodified app-scope
chord (`PageUp`/`PageDown`/`Home`/`End`), not an arbitrary shortcut.

`examples/19_c_client`'s `--verify-c-client` gained ten checks (10-3, 10-4):
handle-validity for `dg_node_scope_action` (null and already-removed),
scoping `DG_ACTION_SCROLL_PAGE_UP` onto a live button, focusing it with a
real click, posting a real `PageUp` key, and receiving `DG_EVENT_ACTION`
naming the right `action_id` and target node - plus `dg_shortcut_label()`
for both a bound and an unassigned `action_id`. `dg_event::node`'s own
summary in `abi/drawgui.def.toml` is corrected in the same change (it used
to read "DG_EVENT_CLICK only; null otherwise", which `DG_EVENT_ACTION`'s
own target-node use makes false), and `abi.no_drift`/`abi.abi_lock` both
stayed green throughout - the append-only proof this slice, like 7-6
before it, did not silently widen the ABI surface beyond what it records.

`action_id` itself needed one generator addition this slice's own task
description did not name up front: `tools/gen_abi.py` had re-emitted
`prop_id`/`token_id` as C-compatible `#define`s from `gen_props`/`gen_theme`
since 6-3/7-6, but never `action_id` from `gen_shortcuts` - a pure C caller
had no way to spell `DG_ACTION_SCROLL_PAGE_UP` at all until this slice
taught `gen_abi.py` to import `gen_shortcuts.load_definitions()` the
identical way it already imports the other two, mirrored into
`abi_lock.py` so the append-only guard covers it too.
