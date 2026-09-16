# Theme tokens: the schema/data split, $token live references, and the light/dark runtime switch

Slice 6-2, the other half of P3 design.md's own roadmap left at zero after
Phase 5's completeness audit (`doc/completeness.md`: "no theme system"). This
is the record of closing it - `themes/schema.toml` as a compile-time contract
generated the same way `props/drawgui.props.toml` already is, a runtime
loader for the shipped `theme.json`, a `$token` live-reference binding side
table, and the P3 acceptance bar itself: a light/dark switch that touches no
widget tree structure.

The short version:

- **Schema is a contract, values are data - design.md section 5.7.1's own
  distinction, kept literally.** `themes/schema.toml` declares WHICH tokens
  exist (`color.surface`, `radius.md`) and is generated exactly like
  `props/drawgui.props.toml`: `tools/gen_theme.py` emits `token_id` C++
  constants, the loader's name->id table, and a generated markdown reference;
  `tools/theme_lock.py` mirrors `tools/prop_lock.py`'s ABI lock precisely
  (renumber/rename/delete are MAJOR breaks, append is MINOR and must be
  recorded in the same change). `themes/builtin/theme.json` is the one
  shipped instance of DATA - what each token equals under light/dark -
  embedded as a string (`include/drawgui/theme/builtin_theme.generated.h`,
  design.md section 5.7.4: "内置主题同样是 JSON，以字符串嵌入二进制"), loaded
  through the SAME `dg::load_theme()` a future external theme package would
  use, so the builtin theme dogfoods the one loading path rather than a
  shortcut around it.
- **11 tokens, deliberately small but real** (`themes/schema.toml`): seven
  colour tokens (`color.surface`/`on-surface`/`border`/`primary`/`on-primary`,
  plus `color.primary-hover`/`primary-pressed` - proving design.md section
  5.11.4's rule that a pseudo-state is its OWN token, never an alpha
  expression, from the very first schema rather than discovering the rule
  later) and four integer tokens (`radius.sm`/`md`, `space.sm`/`md`).
  design.md section 5.7.4's own JSON example puts int tokens in `theme.json`'s
  variant-INDEPENDENT `"base"` and colour tokens in `"variants.light"`/
  `"variants.dark"` - this project follows that shape exactly rather than
  inventing a different one.
- **The JSON parser decision (design.md section 12 open question 3, "P3 前定
  案") is a hand-rolled ~300-line parser, not nlohmann/json.** Section 3 is
  the argument in full; the short version is that `theme.json`'s own grammar
  (a number, a string, one flat `string->number` object, one object of
  `string->string` objects) is closed and small enough that a purpose-built
  parser is both correct and auditable, and this project has pulled in
  exactly three third-party dependencies in its whole history (Skia, SDL3,
  FreeType) - a fourth was not a decision to make lightly.
- **`$token` live references are a side table (`ThemeBindings`), not a
  sentinel inside `NodeStyle`/`BoxStyle`, and not a generation-counter
  handle either.** Section 4 is the full defence against this project's own
  precedent: it is `WidgetSet`'s shape (per-node state that outlives the
  event that set it), and explicitly NOT `AnimHandle`'s shape, because a
  render node's count here is bounded and append-only the way an animation
  slot's is not.
- **Resolving a token reuses `dg::set_prop()` unchanged.** Binding a token
  writes the CURRENT resolved value through the exact same property-write
  door a literal write already uses, so "which invalidation a switch costs"
  is answered by node_props.h's own existing rule (paint property -> repaint
  only; layout property -> relayout) rather than needing a new one. Section 5
  measures both cases: a colour-only variant switch costs zero relayout
  (`nodes_visited == 0`), and an int-token (spacing/radius) value change
  through the identical `apply()` path costs a real one.
- **Unknown token name and type mismatch are both `dg::Expected` failures
  naming the exact JSON key path** (design.md section 5.7.5), never a
  silent skip - section 6.
- **`tools/check_consistency.py`** verifies the shipped `theme.json` covers
  every schema token in both directions (schema token missing from the JSON,
  JSON key naming no schema token) - section 7.
- **design.md line 622's acceptance bar holds for a 14th consecutive slice**:
  zero new node/`RenderObject`/`WidgetKind` kinds. A token-bound property is
  an ordinary `NodeStyle`/`BoxStyle` write; the theme system adds a side
  table and a loader, nothing to the node model itself.
- **Six defect injections, three caught immediately, two real coverage gaps
  found and closed, one architectural regression caught by the demo's own
  oracle** - section 8.
- **Explicitly not built** (named, not merely absent): external theme
  packages, hot reload, resources/icons/fonts inside a theme package, an
  expression evaluator for token values, `.d.ts`/ABI-constant-table
  generation (deferred to 6-3, alongside `curve_id`), `schema_version`
  migration. Section 9.

---

## 1. The schema/data split, and why the generator reuses props' shape rather than a new one

design.md section 5.7.1 draws one line: the SCHEMA (which tokens exist) is a
compile-time API contract, because three things derive from it - the ABI's
numeric `token_id`, `.d.ts` type hints (6-3), and the ability to error on a
misspelled token name instead of silently falling back. The VALUES (what a
token equals under light/dark) are runtime data, loadable without a rebuild.

`props/drawgui.props.toml` + `tools/gen_props.py` + `tools/prop_lock.py`
already solved the identical shape of problem for `prop_id`, survived a real
bug (the unrecorded-append hole `tools/prop_lock_selftest.py` now guards
against), and is CI-enforced. This slice's task was explicit: reuse that
proven machinery rather than standing up a parallel one. What was reused
versus rebuilt:

**Reused (shape and spirit, not a shared code path - see below for why not
literally shared code):**

- The TOML-source-of-truth -> Python-generator -> committed-generated-files
  -> separate-ABI-lock pipeline, four files deep exactly the way the property
  system is: `themes/schema.toml`, `tools/gen_theme.py`, the generated
  header/`.inc`/doc, `themes/token_ids.lock` + `tools/theme_lock.py`.
- The numbering discipline verbatim: `id` is a `uint16_t`, id 0 reserved,
  append-only, `next_id` bumped explicitly, ascending-order enforcement so an
  append is visibly one line in a diff.
- **Plain constants, never an enumeration** - `dg_token_id` is
  `std::uint16_t` plus `inline constexpr` values, the identical decision
  `prop_ids.generated.h` already made and already measured against
  clang-tidy's `cppcoreguidelines-use-enum-class`/`performance-enum-size`
  (both fire on the `enum class` form and both would make the ABI worse:
  scoped enums forbid casting a host-supplied `uint16_t` the only way it can
  be dispatched on, and a narrower base type truncates a 16-bit id the ABI
  transports in full). A token id arrives from a runtime string lookup
  exactly the way a prop id arrives from an ABI call - the set of values a
  variable of this type may hold is every `uint16_t`, not the set spelled in
  the header, so an enum would claim something untrue.
- The `--check`/`--write` CLI split and the "never edit the lock to make a
  failing check pass" doctrine, spelled out identically in
  `tools/theme_lock.py`'s own docstring.
- CMake wiring shape: `cmake/GenerateTheme.cmake` mirrors
  `cmake/GenerateProps.cmake` line for line - a custom command producing the
  generated files, an `ALL`-built generate target, a `--check` gate target,
  a lock-check gate target, `CMAKE_CONFIGURE_DEPENDS` on the source TOML.

**Rebuilt rather than shared, and why:**

- **A separate CLI tool (`tools/gen_theme.py`), not a mode of
  `tools/gen_props.py`.** The two source-of-truth shapes genuinely differ:
  properties have `parent_data`/`applies_to`/`consumed_by`/enum `values` -
  none of which a token has - and a token has a dotted namespace
  (`color.primary-hover`) a property name does not. Bolting token support
  onto `gen_props.py` would mean every property-specific validation branch
  also has to reason about "unless this is a token", which is exactly the
  kind of conditional complexity this project's own property generator
  avoids by having one job per field. Two small, single-purpose generators
  are simpler to read than one generator with two purposes.
- **No `tools/theme_lock_selftest.py`.** `tools/prop_lock_selftest.py` exists
  because prop_lock.py's FIRST version had a real, shipped bug (an
  unrecorded-append hole that silently passed); `theme_lock.py` is the exact
  same code shape, written with that bug already known and avoided from line
  one (the `compare()` function's `appended` return value is checked by
  `--check` from the start - see `tools/theme_lock.py`'s own `main()`).
  Standing up a second self-test harness to re-verify a bug class this file
  never had would be scaffolding for a defect this design does not
  reintroduce, not a scope cut - if `theme_lock.py` is ever modified in a way
  that could reopen that class of hole, the property system's own
  self-test is a direct template to copy.
- **The token TABLE (`token_table.generated.inc`) is plain initializers, not
  an X-macro.** `gen_props.py`'s `prop_dispatch.generated.inc` genuinely
  needs the X-macro shape (`DG_PROP_ASSIGN`/`DG_PROP_ASSIGN_COMPLEX`/
  `DG_PROP_UNKNOWN`) because it is INCLUDED TWICE with different macro
  bodies (once to build a dispatch `switch`, once to build the `prop_type()`
  lookup) - the X-macro is what lets one generated file serve two different
  call sites. The token table has exactly one reader
  (`src/theme/theme.cpp`'s `kTokenTable`) and no second call site to serve,
  so an X-macro would have been indirection with no second consumer to
  justify it - and clang-tidy's `cppcoreguidelines-macro-usage` agreed,
  flagging the X-macro form as "consider a constexpr template function"
  where it never once flags `prop_dispatch.generated.inc`'s own macros
  (whose bodies are full statements pasted into a `switch`, not a value
  expression a function could return instead).
- **No `consistency` counterpart in the property system.**
  `tools/check_consistency.py` is new because the property system has
  nothing analogous to check - a property's "value" is always supplied at
  the call site (`dg::set_prop()`), there is no shipped, hand-authored data
  file whose coverage of the schema needs verifying the way `theme.json`'s
  does. This is the one genuinely new piece of machinery this slice adds
  rather than reusing, because it answers a question the property system
  was never asked.

---

## 2. Alpha stays out of the token model, by construction, not by omission

design.md section 5.11.4 forbids an expression syntax for "the same colour at
a different opacity" (Material's state layers: primary at 12% as a hover
mask) - `"$color.primary @ 0.12"` would violate section 5.9.2's "no second
parsing layer inside a string" rule. `color.primary-hover` and
`color.primary-pressed` are independent tokens instead, id 6 and id 7 in the
FIRST version of `themes/schema.toml` this slice wrote, not added after
someone asked "what about hover?" - the discipline was applied before it was
needed rather than discovered as an afterthought. The cost is exactly what
design.md names: a longer token table (11 instead of 9). The benefit is
exactly what design.md names: `src/theme/mini_json.cpp` needs no expression
evaluator at all, and every token - including the hover/pressed variants - is
schema-validated by `tools/check_consistency.py` the same way every other
token is.

---

## 3. The JSON parser decision - settling design.md section 12's open question 3

design.md's own open-questions list (section 12, item 3) named this
explicitly: "JSON 解析器选型（nlohmann/json 便利 vs 更轻量的方案）- P3 前定案，
权衡二进制体积与编译时间" (JSON parser choice - nlohmann/json's convenience vs.
a lighter alternative - to be settled before P3, weighing binary size and
compile time). This slice settles it: **a hand-written, purpose-built parser
(`src/theme/mini_json.h`/`.cpp`), not a third-party library.**

**The case for a library** (nlohmann/json specifically, the obvious choice):
a single header, an extremely well-tested implementation, full JSON grammar
support with no maintenance burden on this project. That case is real and is
not being dismissed lightly.

**The case against, and why it wins here:**

- **This project has pulled in exactly three third-party dependencies in its
  whole history** - Skia (the reason the project exists at all), SDL3 (the
  only windowing backend), FreeType (Skia's own hard dependency, not this
  project's choice) - plus doctest for tests only, never linked into the
  shipped library. Every one of those is load-bearing in a way a JSON parser
  for an ~1KB configuration file is not. Adding a fourth dependency to parse
  data this small is a different kind of decision than any of the first
  three, and deserves to be argued rather than defaulted into for
  convenience.
- **`theme.json`'s own grammar is closed and small.** Per design.md section
  5.7.4's own example and this slice's own scope (external theme packages,
  fonts/icons/images arrays: all explicitly out - section 9), the format
  this project actually needs to parse is: a top-level object with a number
  (`schema_version`), a string (`name`), one flat object of
  `string -> number` (`base`), and one object of objects of
  `string -> string` (`variants.<name>`). No arrays, no nested objects past
  three levels, no need for JSON's full generality. A parser scoped to
  exactly this grammar is not a "cut corner" version of a general parser -
  it is the RIGHT SIZE of parser for what this project actually reads,
  matching the same "build what the second consumer needs, not what a
  hypothetical third one might" discipline this project applies everywhere
  else (doc/animation.md's `curve_id`, this project's own repeated "an
  interface is extracted from at least one working implementation" rule).
- **Binary size and compile time, per design.md's own weighing criteria**:
  nlohmann/json's header is large enough that including it anywhere adds
  measurable seconds to an incremental build touching that translation
  unit, and its generality (arbitrary user-defined type serialization,
  SAX/DOM dual APIs, exception hierarchies) is surface this project would
  never exercise. `mini_json.h`/`.cpp` compiles in a fraction of a second and
  the whole parser (excluding comments) is under 250 lines - genuinely
  auditable in one sitting, which a general-purpose JSON library's tens of
  thousands of lines are not.
- **A JSON parser consuming untrusted-shaped input is exactly ASan/UBSan's
  domain, doubly so for one this project wrote itself rather than adopted
  from a heavily-fuzzed library** - this slice's own task named this
  concern directly. The mitigation is not "trust a library instead" but
  bounding the parser explicitly: `kMaxDepth` (32) caps recursive descent so
  a maliciously or accidentally deep document fails with a clean
  `ParseError` rather than exhausting the call stack, and `kMaxJsonBytes`
  (1 MiB) rejects an oversized document before any parsing work happens.
  design.md's own risk register (section 10) names "解析炸弹" (a parse
  bomb) for theme loading by name; `tests/unit/test_theme.cpp`'s
  `"a deeply nested document is rejected, not a stack overflow"` and
  `"an oversized document is rejected before it is parsed"` cases feed the
  parser exactly that shape of hostile input and assert it fails cleanly,
  and the whole sanitized test suite (section 8, `-DDG_SANITIZE=ON`) runs
  these cases under ASan+UBSan.

**What the parser does NOT do, on purpose**: it parses the FULL JSON grammar
(objects, arrays, strings, numbers, `true`/`false`/`null`), not only the
`theme.json` subset, even though the subset is all this slice needs. The
reason is a better error message, not generality for its own sake:
`theme_loader.cpp`'s OWN schema validation (against `themes/schema.toml`) is
what rejects the wrong SHAPE with a location-carrying error naming the exact
problem (an unknown token, a type mismatch); a parser that assumed the
theme.json shape up front would fail confusingly on anything else, one level
earlier and one level less specifically than the validation this project
actually wants a caller to see.

---

## 4. `$token` live references: a side table, defended against this project's own two precedents

This is the design problem the task named as "the interesting design problem
of the slice": a node's `fill` is a concrete `dg::Color` today; a `$token`
reference means a node can hold "the current value of token N" instead, and
a theme switch must update it without rebuilding the widget tree.

**The representation: `ThemeBindings`, a side table indexed by `NodeId`,
recording `(prop_id, token_id)` pairs per node** - `include/drawgui/theme/
theme_bindings.h`. Two alternatives were considered and rejected, both
against this project's own established precedent rather than from taste:

**Rejected: a sentinel value inside `NodeStyle`/`BoxStyle`** (a magic
`Color`/`int` meaning "resolve token N at paint time"). This is the identical
argument `doc/scrolling.md` and `doc/list.md` already made for scroll offset
and list bookkeeping, restated for a new field: every READER of `NodeStyle`
(the painter, hit testing, damage) would have to learn a SECOND meaning for a
field it already understands as a concrete value. `doc/clipping.md` section
6's `overflow` argument is the general form: a field is only the right shape
when every reader needs to agree on ONE rule about it. A theme binding is not
a fact the painter needs to know about the SHAPE of a value - it is a fact
about WHERE a concrete value came from, and only the theme switch itself
ever needs to re-consult that fact. So it belongs beside `WidgetSet`, not
inside `NodeStyle`.

**Rejected: a generation-counter handle**, `TokenBinding{index, generation}`
mirroring `doc/animation.md`'s `AnimHandle`. This is the more interesting
rejection because it is the OPPOSITE of the reason a generation counter was
RIGHT for animations. `doc/animation.md`'s own argument: a generation counter
exists because an animation SLOT's count is unbounded over a session (many
short-lived hovers each spend a slot that must be reclaimed, reopening the
classic ABA problem the moment a slot is reused) while a render NODE's count
is not (`WidgetSet`'s `std::vector<std::optional<Widget>>` never reclaims an
index, because `doc/list.md` section 1 already established that nodes are
never removed in this engine). A theme binding's lifetime is tied to a
NODE's lifetime one-to-one - a binding names "this node's this property" -
and nodes here are append-only exactly like `WidgetSet`'s own entries. There
is no reuse, so there is no ABA problem to guard against, and a generation
counter would have been solving a problem this slice's own data structurally
does not have. **The two rejections are not independent points - they are
the same lesson (`WidgetSet`'s shape is right when state outlives an event
without the underlying identity ever being reclaimed; `AnimHandle`'s shape is
right only when identity IS reclaimed) applied correctly on both sides.**

**Resolution reuses `dg::set_prop()` UNCHANGED, which is what removes the
need for any new invalidation logic.** Binding a token
(`dg::bind_token()`) does not invent a new write path: it resolves the
theme's CURRENT value for the given variant and writes it through the
EXACT SAME `dg::set_prop()` door an ordinary literal write already uses.
`node_props.h`'s own comment already states the rule this buys for free:
"WHICH INVALIDATION A WRITE COSTS is decided by which struct the property
lands in" - a colour property lands in `NodeStyle` and costs a repaint; a
box-model property (`gap`, `padding_*`) lands in `BoxStyle` and costs a
relayout. A resolved token write is, as far as that rule is concerned, an
ordinary property write - the rule applies to it without any new code
having to say so. `ThemeBindings::apply()` (the whole of a theme switch) is
therefore just "for every recorded binding, re-run `resolve_and_write()`" -
no per-property-type branching of its own, because `dg::set_prop()` already
does that branching.

**The compatibility rule** (`resolve_and_write()`, `src/theme/
theme_bindings.cpp`): a `k_color` token may bind any `k_color` property; a
`k_int` token may bind any `k_float` or `k_length` property - the two scalar
`PropType`s `node_props.cpp`'s own `to_pixels()` already rounds into device
pixels for an ordinary literal write. Anything else is `kTypeMismatch`,
reported through `PropWrite` - the SAME vocabulary `dg::set_prop()` and the
dedicated-setter channel already use, deliberately NOT a new
`dg::Expected`-based error type. The reason is a distinction in KIND: the
loader's errors (section 6) are about DATA - a `theme.json`'s shape, decided
once at load time - while `bind_token()`'s errors are about an API CALL - a
caller's `prop_id`/`token_id` pairing, decided per call exactly like every
other property write in this project. Reusing `PropWrite` here is reusing
the established vocabulary for that shape of problem, not avoiding
`dg::Expected` out of inconsistency.

---

## 5. The measurement: colour-only costs zero relayout, an int-token value change costs a real one

design.md's P3 acceptance criterion names light/dark runtime switching
explicitly, and this slice's task asked for both halves of the
invalidation question to be MEASURED, not assumed - twelve prior slices'
`nodes_visited == 0` findings (`doc/scrolling.md`, `doc/list.md`) made no
claim about a property that changes a BOX, only about ones that do not.

**Colour-only switch (`tests/unit/test_theme.cpp`'s `"switching light->dark
updates every bound node, color-only costs zero relayout"`, and
`examples/18_theme --verify-theme`'s own oracle on the real demo scene):**
binding `background_color`/`border_color` to colour tokens and switching
`ThemeBindings::apply()` from light to dark writes only `NodeStyle` fields
(`node_props.cpp`'s `apply_background_color`/`apply_border_color` never
touch `BoxStyle`). Measured on a real `LayoutTree`, after `layout_full()`:

```
nodes_visited    = 0
nodes_relaid_out = 0
```

**An int-token value change (`tests/unit/test_theme.cpp`'s `"an int-token
(radius/spacing) switch DOES relayout the bound nodes"`):** binding a row's
`gap` to `space.md` and re-applying a THEME whose `space.md` genuinely
differs writes `BoxStyle::gap` through `node_props.cpp`'s `apply_gap()`,
which calls `LayoutTree::set_box()` - marking the node dirty. Measured, same
technique:

```
nodes_visited    > 0
nodes_relaid_out > 0
```

**Why this test uses two DIFFERENT `Theme` objects rather than the shipped
`theme.json`'s own light/dark variants**: design.md section 5.7.4's own
JSON shape makes `space.md`/`radius.md` variant-INDEPENDENT (they live in
`"base"`, not inside `"variants.light"`/`"variants.dark"`) - so switching
the SHIPPED theme's variant never moves an int token at all, by design.
`examples/18_theme`'s own demo scene therefore reads `space.md` as a plain
literal (not a token binding) specifically so its OWN light/dark switch
measures the pure colour case honestly; the int-token relayout case needed a
theme instance built to genuinely disagree on one int token; two Theme
objects with a different `space.md` prove the OTHER half of the same
mechanism (`ThemeBindings::apply()` re-resolving and re-writing) without
needing a fictitious third theme variant. Both measurements exercise the
IDENTICAL code path (`resolve_and_write()`) - the only thing that differs
between them is which struct the bound property happens to write into,
which is exactly the point being measured.

**A finding worth naming plainly, found while building the demo rather than
assumed from the design**: `ThemeBindings::apply()` re-resolves and
re-writes EVERY recorded binding unconditionally, with no before/after value
diff. This means a switch that includes even ONE int-token binding costs a
relayout regardless of whether that specific token's value actually moved
between the two variants - because `apply()` does not know or check. This is
why `examples/18_theme`'s own demo scene deliberately does NOT bind `gap` to
a token (a literal read once, at build time, documented in
`theme_scene.cpp`): keeping the demo's own switch colour-only is what makes
its OWN `LayoutStats` measurement honestly zero. A future optimisation
(skip the write when the resolved value is unchanged) is possible but was
not built here - it would need `ThemeBindings` to remember each binding's
LAST resolved value, a second piece of state this slice's scope does not
need, named rather than silently assumed away.

---

## 6. Error handling: unknown token name and type mismatch, both `dg::Expected`, both naming the exact location

design.md section 5.7.5's two rules, both routed through
`dg::Expected<Theme, ThemeLoadError>` (`include/drawgui/theme/
theme_loader.h`), never a bool or a log-and-continue:

- **Unknown token name** - a `theme.json` key inside `"base"` or
  `"variants.<name>"` that names no schema token at all -
  `ThemeLoadStatus::kUnknownToken`, with a message carrying the exact JSON
  key path (`"variants.light.color.mystery-token"`). Proven by
  `tests/unit/test_theme.cpp`'s `"load_theme: an unknown token name fails
  and reports its location"`.
- **Type mismatch**, in BOTH directions - a `"base"` key naming a colour
  token, or a `"variants.<name>"` key naming an int token
  (`ThemeLoadStatus::kTypeMismatch`), and a malformed colour string (not
  `"#RRGGBBAA"`, eight hex digits) or a non-number `"base"` value. Proven
  three times: `"a type mismatch (int token under variants) fails and names
  it"`, `"a color token used inside 'base' is a type mismatch too"` (the
  symmetric direction the defect-injection campaign found missing - section
  8), and `"a malformed colour string is a type mismatch, not a crash"`.

**Location reporting mechanism**: `theme_loader.cpp`'s `load_base()`/
`load_variant()` build a `path` string (`"base.radius.md"`,
`"variants.dark.color.surface"`) as they walk each JSON object's keys, and
every failure message is built from that path plus the offending key -
mirroring `PropWrite::message`'s node-path convention on the property side
(`node_props.h`'s `path_of()`). A PARSE failure (malformed JSON itself, not
a schema violation) carries a byte OFFSET instead (`mini_json::ParseError`),
since there is no key path to name before the document has parsed at all.

**The colour format is `#RRGGBBAA`**, design.md section 5.11.3 rule 1's own
convention ("`#RRGGBBAA` 的主题写法自然对应" the direct 0xAARRGGBB internal
value) applied literally: eight hex digits, red-green-blue-alpha in that
order, converted via `Color::rgba(r, g, b, a)`.

---

## 7. `tools/check_consistency.py`: what it actually verifies

Design.md section 5.7.7 names the CI gate by name: "内置主题覆盖了 schema 的
全部 token". `tools/check_consistency.py` checks BOTH directions, because
either alone misses half the drift class stonegui's four-file token split
already demonstrated:

- **MISSING**: a schema token (from `themes/schema.toml`) absent from
  `theme.json`'s `"base"` (for an int token) or from EITHER variant's colour
  table (for a colour token). The loader itself would catch this too, at
  first load - `check_consistency.py` catches it at CI time instead, before
  a binary that cannot construct a complete theme ever ships.
- **UNKNOWN**: a `theme.json` key that names no schema token at all - the
  same direction `theme_loader.cpp`'s own validation rejects at runtime,
  checked again here so a CI failure names the exact bad key without
  needing a full build first.

It is a separate, small, dependency-free script rather than a mode of
`tools/gen_theme.py`, because it reasons about DATA (a `theme.json`
instance) against a SCHEMA (a compile-time contract) - design.md section
5.7.1's own two-sides-of-one-line distinction - and keeping the two
concerns in different files is what keeps that line visible.

---

## 8. Defect injection, against this slice's new engine logic only

Per this slice's own scoping instruction, only the NEW logic (id mapping,
the live-reference resolve, the light/dark switch, the loader's error
paths) was targeted - not previously-existing code this slice merely calls.
Each injection was built with a small Python edit or `sed`, run against
`tests/unit/test_theme.cpp` and `examples/18_theme --verify-theme`, then
reverted with `git checkout --` (this repository's own version control,
simpler and more reliable than the `cp`+`touch` pattern earlier slices used
before a repo existed at each edit point) and the build rebuilt clean before
trusting the next result - the stale-build false-negative lesson
`doc/complex-properties.md` already recorded.

| # | injection | result |
| --- | --- | --- |
| A | `token_id_for_name()`'s match condition inverted (`==` -> `!=`) | **caught immediately, dramatically** - 5 of 13 cases failed and one CRASHED (`SIGABRT`), because `bind_token()`'s own unknown-id gate started reporting every REAL token as unknown and vice versa |
| B | `ThemeBindings::bind()`'s "find and replace an existing entry" branch removed - rebinding the same `(node, prop_id)` pair now appends a second entry instead of replacing the first | **survived** - nothing in the existing suite ever rebinds the same (node, prop) pair twice, so no assertion could see the duplicate. **Closed** with a new test, `"rebinding the same (node, prop_id) REPLACES, never accumulates a second entry"`, asserting `bindings_for(node).size() == 1` after two binds - fails immediately (`2 == 1`) against the injected build, passes cleanly reverted |
| C | `load_base()`'s type check dropped (a colour token used inside `"base"` no longer rejected) | **survived** - the existing type-mismatch test only exercised the OTHER direction (an int token inside `"variants"`). **Closed** with a new test, `"a color token used inside 'base' is a type mismatch too - the OTHER direction of the base/variants swap"`, which fails immediately (`REQUIRE_FALSE` sees `true`) against the injected build |
| D | `switch_variant()`'s statement order swapped - `ThemeBindings::apply()` resolves against the OLD variant, THEN the enum flips | **caught immediately** by `examples/18_theme --verify-theme`'s own pixel oracle - every panel reported the PREVIOUS variant's colour after a switch, an off-by-one-frame bug a unit test working purely on `Theme`/`ThemeBindings` in isolation (never calling `switch_variant()` itself) could not have exercised |
| E | `resolve_and_write()`'s `k_length`/`k_float` `PropValue` construction branches swapped | **caught immediately** - `dg::set_prop()`'s own `checked()` type-tag comparison (unrelated code, already proven) rejects the mismatched `PropValue` tag, failing the `REQUIRE(... .ok())` in the int-token relayout test |

Three caught immediately (A, D, E - two of them dramatically: a crash and a
wrong-colour demo screen), two survived the first pass and are now closed
with regression tests that assert the specific property each gap needed (B,
C) - the identical shape prior slices' campaigns already found repeatedly
(`doc/list.md` section 8's injections D/E, `doc/complex-properties.md`'s
spread-parameter gap): a scene or a test suite that LOOKS thorough can still
share one property across every one of its cases (here: no case ever
rebinds the same pair twice, no case ever exercises `load_base()`'s
type-check direction) that a new, deliberately-shaped case closes. Injection
D is worth stating plainly as the reason a demo's own end-to-end oracle
earns its place beside the unit suite: it is the one bug in this whole
campaign that a unit test exercising `ThemeBindings` and `Theme` in
isolation, however thorough, could not have found, because the bug was in
the ORDER of two statements in the DEMO's own client code, not in the
library the unit tests target.

---

## 9. What this does not do

Named explicitly, matching this slice's own out-of-scope list:

- **External theme packages and hot reload** (design.md section 5.7.6, P7).
  `dg::load_theme()` takes a `std::string_view` of JSON text, which is
  already the shape a future file-watching loader would call into - nothing
  about this slice's loader interface would have to change.
- **Resource/icon/font directories inside a theme package** (design.md
  section 5.7.4's `mytheme/fonts|icons|images/`). The schema and loader only
  know about `color`/`int` tokens; a font or icon reference is a different
  kind of value this slice's token model does not carry.
- **Theme package security limits** (path-traversal defence, SVG
  restrictions, design.md section 5.7.5's four restrictions). Those exist
  for THIRD-PARTY, UNTRUSTED theme packages - the one theme this slice loads
  is compiled into the binary, not an untrusted input a user supplied at
  runtime, so the restriction does not apply yet. The JSON parser's own
  depth/size limits (section 3) are a genuinely different, narrower
  precaution: parser robustness against malformed input, not a trust
  boundary around a third party's file system access.
- **An expression evaluator for token values** (design.md section 5.11.4,
  forbidden by name). `color.primary-hover` is a distinct token with its
  value written out in full, never `"$color.primary @ 0.12"`.
  Section 2 above.
- **`.d.ts` type hints and the ABI numeric-constant table** (design.md
  section 5.7.7 names four consumers of the schema; this slice builds two -
  the C++ `token_id` constants and the loader's name table). Both remaining
  consumers are meaningless before slice 6-3's C ABI exists at all - a
  `.d.ts` describes a JS binding with no host to bind to yet, and an "ABI
  constant table" is a table of exported symbols for an ABI this project has
  not exported. `token_id` itself is exactly what 6-3 folds into
  `prop_id`/`action_id`'s shared generated family, the identical deferral
  `doc/animation.md` already recorded for `curve_id`.
- **`schema_version` cross-version migration** (design.md section 12 open
  question 7, explicitly P7). This loader accepts exactly `schema_version
  == 1` and rejects anything else with `kUnsupportedSchemaVersion`.
- **Animating the theme switch as a crossfade.** `$token` live references
  plus an animation clock (6-1) are a natural pairing for a light/dark
  crossfade, but 6-1's own honest gap report (`doc/animation.md`: "an
  animated `opacity` write costs exactly what a hand-written one already
  did, not a cheaper recomposite-only path") means this would not be free,
  and building it was outside this slice's own scope statement.
- **GTK-style selector theming, `SkRuntimeEffect`/SkSL.** design.md section
  5.7.3's own decision (libadwaita's convergence, not GTK3 CSS's openness) -
  a theme assigns a fixed token table, never a selector reaching into a
  control's internal structure.

## Property and generator status after this slice

Unchanged: **49 properties, 38 fully implemented / 10 partial / 1 not-yet.**
No property was added or changed - a token BINDS an existing property, it
does not create a new one. **Two generator families now exist**
(`props/drawgui.props.toml` + `tools/gen_props.py`/`tools/prop_lock.py`, and
`themes/schema.toml` + `tools/gen_theme.py`/`tools/theme_lock.py`), matching
the shape design.md section 5.7.7 already anticipated for `prop_id`/
`token_id`/`action_id` to eventually share at 6-3. `WidgetKind` values:
unchanged, **8**. CTest entries: **26** (+4: `theme.no_drift`,
`theme.abi_lock`, `theme.consistency`, `theme.verify_demo_scene`).
Examples: **18** (+1, `examples/18_theme`).

---

## 10. (7-6, append-only) External theme packages, hot reload, and the theme ABI landed

Everything section 9 above named as explicitly NOT built here - external
theme packages, hot reload, resources/icons inside a theme package, theme-
package security limits (path traversal, resource bounds), the theme ABI,
and `schema_version` migration - has now landed in P7 slice 7-6. This
section is a pointer, not a restatement: `doc/theme-packages.md` carries
the full record, and this file's own text above is left exactly as 6-2
wrote it.

The one thing worth stating here, in this file, because it is a direct
extension of section 5's own finding rather than a new one: 7-6's
`examples/24_theme_package` re-confirms, on a theme reloaded from DISK
rather than swapped in-process, that `ThemeBindings::apply()` re-resolves
and re-writes EVERY recorded binding unconditionally (this section's own
"a finding worth naming plainly" paragraph above) - which is why 7-6's own
hot-reload measurement needed TWO separate scene instances (one with no
bound int property, one with exactly one) to isolate "a colour-only edit
costs zero relayout" from "an int-token edit costs a real one," rather than
one scene proving both. `doc/theme-packages.md` section 4 has the full
argument and the measured numbers.
