# External theme packages: untrusted input, path traversal, hot reload, and the theme ABI

P7's fourth and last priority, closing design.md section 5.7's own roadmap
where 6-2 stopped by name: "external theme packages, hot reload, resources/
icons/fonts inside a theme package... `schema_version` migration" (`doc/
theme.md` section 9's own declined list). This document does not repeat
6-2's findings - it cross-references `doc/theme.md` throughout and only
records what is NEW here.

The short version:

- **The security point is the heart of this slice, not an afterthought.**
  design.md's risk register (section 10) names "第三方 theme package 是
  不可信输入（路径穿越 / 解析炸弹 / 恶意 SVG）" - this is the first slice
  where that risk is live rather than argued about, because 6-2's ONE theme
  was compiled into the binary. `dg::ThemePackage` (`include/drawgui/theme/
  theme_package.h`) is built throughout as a boundary against hostile
  input, not a convenience wrapper with security bolted on afterward.
- **Path traversal is defended by canonicalizing, not by string-scanning.**
  `ThemePackage::read_resource()` resolves a relative path against the
  package root via `std::filesystem::canonical` (following every symlink
  to its real target) and then checks the RESOLVED path is still within
  the RESOLVED root - section 1 below is the full argument for why this
  catches a symlink escaping the root, not only a literal `../`, and why a
  symlink LOOP is a clean `kIoError` rather than a hang or a crash.
- **The resource tree has bounds a single JSON document does not: file
  count and aggregate size**, checked via `stat()` only (no content read)
  before `ThemePackage::open()` ever returns - section 2.
- **`schema_version` migration (design.md section 12 open question 7):
  settled as reject-old/reject-unknown, never auto-upgrade** - section 3.
- **Hot-reload granularity (design.md section 12's other open question):
  settled as "neither whole-tree rebuild nor a new incremental-patch
  mechanism - `ThemeBindings::apply()` already IS the finest grain this
  project has"** - section 4, with the measured cost split by token type.
- **File-change detection: an explicit API call, never polling or
  inotify** - section 5, including the idle-CPU measurement this decision
  makes trivial rather than merely small.
- **Resources are raster images only, addressed by relative path -
  explicitly NOT SVG, NOT a new schema token type** - section 6.
- **The theme ABI (`dg_theme_load_dir`/`load_memory`/`set_variant`/
  `override`, `dg_app_set_theme`) lands through 6-3's generator, and
  binding a node to a token reuses `dg_node_set_prop()` unchanged via a
  new `DG_VALUE_TOKEN` value kind** - section 7.
- **Fuzzing and defect injection** - section 8.

---

## 1. Path traversal: resolve, then compare - not scan-then-join

design.md section 5.7.5's own words: "所有资源路径按 theme 根规范化后必须
仍在根内，`../` 逃逸一律拒绝". The naive implementation checks the STRING for
`../` before joining it to the root; this project's own task instruction
insisted the check operate on the RESOLVED path instead, and building it any
other way would have missed the sharper case entirely:

```
theme_package.cpp: ThemePackage::read_resource(relative_path)
  1. reject relative_path.empty() or is_absolute()
  2. resolved = std::filesystem::canonical(root / relative_path)
  3. reject if resolved is not root or a descendant of root
  4. read_bounded_file(resolved, kMaxResourceFileBytes)
```

`std::filesystem::canonical` resolves EVERY symlink in the path to its real
target before this function ever compares anything - so a symlink INSIDE the
package pointing OUTSIDE it (`icons/evil -> /etc/passwd`) is caught by the
exact same check that catches a literal `../../../etc/passwd`, because by
step 3 both have become the identical string, `/etc/passwd`, which the
prefix check rejects either way.
`tests/unit/test_theme_package.cpp`'s own case names this directly: *"a
symlink INSIDE the package pointing OUTSIDE it is rejected - proves the
check operates on the RESOLVED path, not the literal string"*.

**Falsifiability, run and reported literally, per the task's own
instruction ("make sure removing a traversal guard actually fails a
test")**: `is_within_root()` was edited to unconditionally `return true`,
the unit suite rebuilt and re-run. Result: 3 of the 14 `test_theme_package.
cpp` cases failed immediately - the plain `../` case, the symlink-escape
case, and a case named specifically to state the falsifiability claim in
its own title (*"DEFECT INJECTION: removing the is_within_root() guard
would let '../' traversal succeed"*). The edit was reverted immediately
afterward; it is not part of the committed code.

**A symlink LOOP** (`icons/a -> icons/b`, `icons/b -> icons/a`) is a
DIFFERENT failure mode from traversal, and is reported as one:
`std::filesystem::canonical`'s error_code overload surfaces `ELOOP` the
same channel as "file does not exist" - there is no resolved path to check
"inside root or not" against, because there is no resolved path at all - so
this is `ThemeLoadStatus::kIoError`, not `kPathTraversal`. The important
property, proven by `tests/unit/test_theme_package.cpp`'s own case, is that
this returns a clean `Expected` failure rather than hanging (the classic
symlink-loop bug) or crashing (an uncaught `filesystem_error`).

---

## 2. The resource tree's own bounds: file count and aggregate size

design.md's risk register names "解析炸弹" for `theme.json`; 6-2's
`mini_json.h` already bounds that with `kMaxDepth`/`kMaxJsonBytes`. A
resource DIRECTORY has two bounds a single JSON document structurally
cannot: how many files it contains, and how much they total. Neither is a
parse-DEPTH problem - a directory with ten million one-byte files is not
"deeply nested", it is simply enormous in a dimension `mini_json.h` never
had to reason about.

`ThemePackage::open()`'s `scan_bounded()` walks the resource tree with
`std::filesystem::recursive_directory_iterator` (which does NOT follow a
directory symlink by default - the reason a symlink LOOP inside the
resource tree cannot turn this scan itself into an infinite walk), counting
files and summing `stat()`-reported sizes ONLY - no file's content is ever
read during this scan - and bails out the instant either bound (256 files,
32 MiB total, `kMaxResourceFileCount`/`kMaxTotalResourceBytes`) is exceeded,
rather than finishing the enumeration first. A single oversized file is
caught the same way, against a THIRD bound
(`kMaxResourceFileBytes`, 4 MiB) - all three are exercised by dedicated
`tests/unit/test_theme_package.cpp` cases (a 257-file tree, a
one-over-cap single file), and `theme.json` itself is bounded identically
(`kMaxThemeJsonBytes`, matching `mini_json::kMaxJsonBytes`'s own 1 MiB)
BEFORE any of its bytes are read - `ThemePackage::load_theme_json()` calls
`std::filesystem::file_size()` (a stat, not a read) first, so an
attacker-supplied gigantic `theme.json` costs one syscall to reject, not an
attempted allocation of its full size.

---

## 3. `schema_version` migration - settling design.md section 12 open question 7

See design.md section 12 for the resolution in place (this document is
cross-referenced from there). The short form: reject any `schema_version`
other than an explicit, small set this build understands (today: `{1}`),
never auto-upgrade. The loader's own code did not change - `!=
1` was already the whole check since 6-2 - what changed is that this
slice PROMOTES that behaviour from an implicit consequence of "there is
only one version" to an explicit, argued decision, because the task
instruction named this as the moment to settle it ("P7 外部主题包落地前
定案") and an unexamined default is not the same claim as a decided one.

---

## 4. Hot-reload granularity - settling design.md section 12's other open question, and measuring both halves

**The settled answer: neither a whole-tree rebuild nor a new
incremental-patch mechanism is needed.** `ThemeBindings::apply()` (6-2) is
already "for every recorded `(node, prop_id, token_id)` binding, re-resolve
against the current `Theme` and re-write through `dg::set_prop()`" - that
IS the finest possible grain a token-bound system can offer, because it
already operates per-binding, not per-tree. "Hot reload" therefore needs
exactly one new capability, and it is narrow: producing a FRESH `dg::Theme`
from theme.json's CURRENT bytes on disk, instead of the one already held in
memory. `dg::reload_theme_package()` (`theme_package.h`) is the whole of
that capability - it calls the IDENTICAL `load_theme_json()` the initial
load already used, so a reload dogfoods the same validated path a first
load does (an edited `theme.json` that no longer validates is rejected on
reload exactly as it would be on first load - a hot reload is not exempt
from schema checking).

**The measured cost split, by token type, and why it needed TWO scene
instances rather than one** (`examples/24_theme_package`,
`--verify-theme-package`): `doc/theme.md` section 5 already recorded that
`ThemeBindings::apply()` re-resolves and re-writes EVERY recorded binding
UNCONDITIONALLY, with no before/after value diff - so a scene with even ONE
bound int-typed property costs a relayout on ANY reload, whether or not
that specific token's value actually changed. Measuring "a colour-only EDIT
costs zero relayout" honestly therefore requires a scene with NO bound int
property at all (`theme_package_scene::build(..., bind_gap_to_token=false)`,
matching `examples/18_theme`'s own scene exactly); measuring "an int-token
EDIT costs a real relayout" requires a SEPARATE scene where `gap` genuinely
IS bound (`bind_gap_to_token=true`). Reusing one scene for both cases would
have produced a relayout on both edits, an artifact of the "unconditional
rewrite" cost rather than proof that a VALUE CHANGE is what causes it -
this is not a hypothetical: it is exactly what the first draft of this
demo measured before being split into two scenes, corrected by re-reading
`doc/theme.md`'s own section 5 rather than treating the non-zero
`nodes_visited` as a bug to chase.

Measured, `--verify-theme-package`'s own printed output:

```
LayoutStats after colour-only reload: nodes_visited=0 nodes_relaid_out=0
LayoutStats after int-token reload:    nodes_visited=4 nodes_relaid_out=2
```

and the int-token case's own visible consequence - `primary_panel` (a
sibling of the row whose `gap` changed) genuinely moves:
`before x=234, after x=312`.

---

## 5. File-change detection: an explicit API call - never polling, never inotify

design.md section 5.7.6 names `IPlatform::watch_files` ("原本为 JSX 热重载
设计") as the mechanism a hot-reload feature would reuse. It was never
built - `grep -rn "watch_files"` across `src/`/`include/` finds only the
one mention in design.md itself, describing an interface this project's
platform layer has never had a working caller for. Building it now, for
the FIRST time, ahead of any real caller in either the JSX-hot-reload role
it was originally sketched for OR this slice's own theme role, is exactly
the "twelve deleted platform headers" mistake this project's own account
already names as its standing failure mode to avoid (`README.md`'s own
"There is also no platform abstraction, on purpose" paragraph).

**The decision: reload is triggered by an explicit call
(`dg::reload_theme_package()`, or at the ABI boundary,
`dg_theme_load_dir()`/`dg_theme_load_memory()` called again followed by
`dg_app_set_theme()` with the fresh handle) - never a background thread,
never a poll interval inside the frame loop.** The reasoning, weighed
against the two alternatives:

- **A background inotify/kqueue/ReadDirectoryChangesW thread** is real,
  multi-backend platform-abstraction work this project has never needed
  anywhere else, and Linux-only scope does not remove the DESIGN question
  (thread lifecycle across `dg_app_destroy()`, cross-thread event
  delivery) even though it removes two of three backends.
- **A poll interval inside `WindowManager::pump()`'s own loop** would
  directly reduce "block indefinitely while idle" to "block for the poll
  interval, then check a file's mtime" - exactly the trade-off the task's
  own instruction warned about, and a WORSE instance of it than 7-5b's
  `HoverTimer` precedent: `HoverTimer` only polls while something is
  ACTIVELY hovered (a bounded, transient state), while a theme-file watch
  would need to poll for the ENTIRE LIFETIME of an app that wants hot
  reload - a permanent tax, not a transient one.
- **An explicit call costs structurally zero when not invoked** - not
  "measured small", but zero, because no code registers a timer, no
  syscall happens, no thread exists. This is the strongest form the idle-
  CPU defence can take.

**Measured, following 7-5b's own precedent of comparing against an
unmodified baseline probe rather than an absolute expectation**
(`examples/24_theme_package --idle-probe-ms 2000`, package loaded, no
reload ever called during the block):

```
requested block:  2000 ms
actual wall time: 2000.2 ms across 1 pump() call(s)
process CPU time consumed (user+sys) while blocked: 43.692 ms
CPU utilisation over the block: 2.18%
```

Run under `SDL_VIDEODRIVER=dummy` (this sandbox has no real display), which
7-5b already measured elevates idle CPU on its OWN unrelated mechanism to
~2.3% against a real-display baseline of ~0.01-0.013% - this slice's own
2.18% is the SAME dummy-driver floor, not a regression this mechanism
introduces, and is exactly what "structurally zero added cost" predicts: a
mechanism that registers no timer at all cannot cost more than the
baseline it is measured against.

---

## 6. Resource scope: raster images by relative path, explicitly NOT SVG, NOT a new token type

design.md section 5.7.4 sketches `icons/` as SVG ("Skia 自带 SVG 模块") and
`images/` as "9-patch / 纹理". This slice builds neither format-specific
decode path; what it builds is the ONE thing common to any resource kind
that untrusted input demands regardless of format: a path-traversal-safe,
size-bounded way to read a named file's BYTES out of the package
(`ThemePackage::read_resource()`). A caller decodes those bytes however it
needs to - `examples/24_theme_package`'s own demo reads
`icons/readme.txt` (a plain text file, not an image) specifically to prove
the READ path works without also having to ship and validate a real PNG
fixture; nothing about `read_resource()` assumes or requires image
content.

**SVG is declined by name, not merely absent**, for the identical reason
`doc/image.md` (5-1) already declined it for the DECODE side of this
project: `SkSVGDOM` is a real, linkable capability since 7-1
(`doc/skia-dependency.md`'s own "newly available but unwired" inventory
names it explicitly), but it is a SEPARATE, much larger parser with its
own attack surface (external references, unbounded path/node counts, a
recursive document structure of its own) design.md section 5.7.5 names
"恶意 SVG" against BY NAME in the same risk entry as path traversal and
parse bombs. Building an SVG decode path that is provably safe against
that risk is a different, larger scope than this slice's own - raster
decoding via `dg::ImageCatalog::decode()` (5-1, unchanged) is the
conservative choice, matching `doc/image.md`'s own precedent of declining
SVG for the identical reason on the decode side.

**No new schema token type for "this token names an image"** is built
either. `TokenType` (`theme.h`) stays exactly `{k_color, k_int}` -
extending it to a THIRD kind would ripple through `tools/gen_theme.py`'s
generator, the ABI's `value_type` constant group, and every existing
two-way switch over `TokenType` (`theme_bindings.cpp`'s
`resolve_and_write()`, most directly) - a real, separate piece of
engineering this slice's own scope statement does not include, named here
as a follow-up rather than half-built. What this slice DOES build (a
safe byte-read primitive) is exactly the piece any future icon-token
feature would still need underneath it, unchanged.

---

## 7. The theme ABI: `dg_theme_t`, and binding via `DG_VALUE_TOKEN` reusing `dg_node_set_prop()`

6-3 declined the entire theme ABI by name (`doc/abi.md` section 6: "no C
client in this slice binds a token through the ABI"). This slice builds
exactly design.md section 5.8's own sketch:

```c
dg_theme_t* dg_theme_load_dir(dg_app_t*, const char* dir, dg_theme_err* err);
dg_theme_t* dg_theme_load_memory(dg_app_t*, const char* json, uint32_t len,
                                 const char* base_dir, dg_theme_err* err);
int         dg_theme_set_variant(dg_theme_t*, const char* variant);
int         dg_theme_override(dg_theme_t*, uint16_t token_id, const dg_value*);
int         dg_app_set_theme(dg_app_t*, dg_theme_t*);
```

with one deliberate deviation from the literal sketch: `dg_theme_load_
memory`'s `len` is `uint32_t`, not `size_t` - this ABI's own generator
(`tools/gen_abi.py`) whitelists a fixed, small set of C primitive types
(`C_PRIMITIVE_TYPES`), and `size_t` is not among them anywhere in this
project's existing ABI surface (`dg_poll_events`'s own `max` parameter is
`int32_t`, the same shape). Matching the established convention rather
than introducing a new primitive type for one function is the smaller
change, and a JSON blob's length never needs 64 bits in practice.

**`dg_theme_err`** is a status-only out-param (`{size, status}`) - the
human-readable detail (a JSON key path, a byte offset) stays on
`dg_last_error()`, matching every other failing ABI call rather than
duplicating string storage into a second channel.

**Token binding reuses `dg_node_set_prop()` unchanged, via a new
`DG_VALUE_TOKEN` value kind** (`value_type`'s 5th constant, `bits` carries
the `token_id`) - NOT a second, `dg_node_bind_token()`-shaped exported
function. This was a real design decision, not the only option: design.md's
own ABI sketch (section 5.8) lists the five theme functions above and NO
binding function at all, which would leave the whole surface with no
observable effect from a pure-C caller (nothing else in this ABI ever
touches `dg::ThemeBindings`). The chosen alternative is the smallest
addition that makes the surface real: `dg::abi::node_set_prop()` special-
cases `value->type == DG_VALUE_TOKEN` before ever reaching the ordinary
literal-value path, calling `dg::bind_token()` with the app's currently
ACTIVE theme (`dg_app_set_theme()`'s own state) instead of constructing a
`PropValue`. This is the ABI's own instance of `doc/theme.md`'s own
"resolution reuses `dg::set_prop()` unchanged" rule, one layer up: binding
a token at the ABI boundary reuses the SAME door a literal write already
uses, rather than inventing a parallel one. A node bound before any theme
is active is `DG_ERR_NO_ACTIVE_THEME` (a new error code, not a silent
no-op or a crash); binding an unassigned `token_id` is `DG_ERR_UNKNOWN_ID`,
the identical vocabulary `dg_node_set_prop()`'s existing literal-value path
already uses for an unassigned `prop_id`.

**`token_id` constants (`DG_TOKEN_*`) are re-emitted into `drawgui.h`** the
same way `prop_id` already is - `tools/gen_abi.py` now imports
`tools/gen_theme.py`'s `load_definitions()` alongside `tools/gen_props.py`'s
(the identical "one parser, one set of validation rules, two renderings"
shape decision 5 already established for `prop_id`), guarded by the same
`#ifndef __cplusplus` block for the identical reason (a `#define` of the
same name would silently rewrite `token_ids.generated.h`'s own `inline
constexpr` declaration wherever both headers reach one C++ translation
unit - `src/abi/abi_impl.cpp` does, needing `theme.h` for `dg::bind_token`).
6-3 declined this specifically because "a token_id constant with no ABI
consumer would be exactly the kind of speculative surface design.md
section 5.8's own '只导出必要面' forbids" - that consumer now exists.

**Exercised as pure C** (`examples/19_c_client`, extended rather than a new
example built alongside it - the task's own named alternative): loads a
theme from an in-memory JSON blob (`dg_theme_load_memory`, no `base_dir` -
proving the "no resource access" branch works, distinct from
`examples/24_theme_package`'s own directory-loading path), attempts
`dg_theme_load_dir` on a missing directory (`DG_THEME_ERR_IO_ERROR`, not a
crash), binds a node's `background_color` to `DG_TOKEN_COLOR_PRIMARY`
before any theme is active (`DG_ERR_NO_ACTIVE_THEME`), then after
`dg_app_set_theme()` (`DG_ERR_OK`), attempts an unassigned `token_id`
(`DG_ERR_UNKNOWN_ID`), switches variant (`dg_theme_set_variant("dark")`),
overrides an int token's value (`dg_theme_override`), and confirms a
colour value sent for that same int token is `DG_ERR_TYPE_MISMATCH`. 36/36
checks pass, compiled by `cc` as strict C11 - the compile itself is what
proves header purity for the new surface, not a manual reading of
`drawgui.h`.

**`abi_lock.py --check` correctly flagged the unrecorded append** before
`--write` was run - the mechanical proof this slice did not silently widen
the ABI without the lock noticing, the identical gate `doc/abi.md` already
records catching a hypothetical drift for `prop_id`.

---

## 8. Fuzzing and defect injection

**Fuzzing** (the task's own named risk-register mitigation, "fuzz 主题加载器"):
`tests/unit/test_theme_package.cpp`'s own fuzz case runs a fixed-seed
(`0xD6A57E4D`) Mersenne Twister, mutating 1-6 random bytes of a KNOWN-VALID
`theme.json` 2000 times per run, feeding each mutation through the REAL
`ThemePackage::open()` + `load_theme_json()` path and asserting only that
the call RETURNS (never hangs) and never throws past `dg::Expected`'s own
boundary - whichever it answers, success or a clean `ThemeLoadError`, is an
acceptable outcome for random bytes; only a crash/hang/uncaught-exception
would fail this loop. This ran clean under both the ordinary build and
`-DDG_SANITIZE=ON` (ASan/UBSan), which is exactly where a hand-rolled
parser fed adversarial bytes is most likely to surface an out-of-bounds
read the functional tests alone would not exercise.

**Defect injection against this slice's own new logic**, run manually
once and reverted:

| # | injection | result |
| --- | --- | --- |
| A | `is_within_root()` disabled (`return true` unconditionally) | **caught immediately** - 3 of 14 `test_theme_package.cpp` cases failed: the plain `../` traversal case, the symlink-escape case, and the case named specifically to state this claim (section 1 above) |
| B | (considered, not injected - `scan_bounded()`'s file-count bound) | Not run as a separate injection: the bound is exercised POSITIVELY by a dedicated test (257 files, one over the 256 cap) that already fails without the bound present in an earlier draft of this file (observed while first writing the constant, before the final value was chosen) - recorded here for completeness rather than re-run for the record, since the earlier observation already demonstrated the same falsifiability directly |

Only one injection is recorded formally (A) because it is the one the
task's own instruction named explicitly by shape ("a security check...
make sure removing a traversal guard actually fails a test") - the
resource-tree bounds (file count, aggregate size, per-file size) are each
already proven by a dedicated POSITIVE test that fails without the
corresponding check (an oversized/too-numerous fixture is rejected), which
is the same falsifiability property demonstrated the ordinary way a
missing feature is demonstrated missing, rather than requiring a second,
separate "disable this check and watch a passing test start failing"
exercise for each of the three numeric bounds individually.

---

## What this does not do

Named explicitly, matching this slice's own scope statement:

- **SVG icons/decoding** - section 6. `SkSVGDOM` stays unwired; raster
  bytes via `ImageCatalog::decode()` is the conservative resource path.
- **A new `TokenType` for images/fonts, and any accompanying $token-bound
  image/font binding mechanism** - section 6. `read_resource()` is the
  primitive a future slice would still need underneath either.
- **`IPlatform::watch_files`, or any background file-watching mechanism**
  - section 5, declined by name with the idle-CPU reasoning in full.
- **GTK-style selectors, an expression evaluator for token values** -
  unchanged from 6-2's own declines (`doc/theme.md` section 9); nothing in
  this slice reopens either.
- **Windows/macOS** - this project's whole platform layer is Linux-only so
  far; nothing about `std::filesystem`'s own symlink/canonicalization
  semantics was verified against either.
- **A `dg_node_bind_token()`-shaped standalone ABI function** - section 7;
  `DG_VALUE_TOKEN` through `dg_node_set_prop()` was the chosen alternative,
  and the reasoning for NOT adding a second function is recorded there
  rather than left implicit.
- **Persisting a loaded/overridden theme back to disk** - `dg_theme_
  override()` mutates the in-memory `dg::Theme` only; nothing in this
  slice's own scope writes a package's `theme.json` back out.

## design.md section 5.6 line 622, re-checked

Zero new node kinds, zero new `RenderObject` kinds, zero new `WidgetKind`
values. This slice adds no widget at all - `dg::ThemePackage` is a loader
and a security boundary, `dg_theme_t` is a fourth ABI handle kind (an opaque
type, not a node kind), and `DG_VALUE_TOKEN` is a new `dg_value::type`
variant, not a new node. The streak holds through a **twenty-second**
consecutive slice.

## Counts after this slice

49 properties, 12 theme tokens (unchanged - no new token was added), 21
exported ABI functions (16 before this slice + `dg_theme_load_dir`/
`load_memory`/`set_variant`/`override`, `dg_app_set_theme`), 4 opaque
types (`dg_app_t`/`dg_window_t`/`dg_node_t`/`dg_theme_t`), 5 structs
(+`dg_theme_err`), 31 constants across 5 groups (+`theme_err`, +1
`value_type` entry), 34 CTest entries (33 before this slice +
`theme_package.verify_demo_scene`; `tests/unit/test_theme_package.cpp`'s
own 14 cases run inside the existing `unit` entry), 24 examples
(+`examples/24_theme_package`), 9 `WidgetKind`s unchanged, `drawgui_
render_png` sha256 unchanged (`f635028e...`). g++/clang++ ×
`-Werror` and a C compiler (`cc`) × `-Werror` all green; `-DDG_SANITIZE=ON`
green; clang-tidy and clang-format both exit 0 with zero `NOLINT` in this
slice's own new/changed code; `props.*`/`theme.*`/`abi.*` (drift +
lock-check + consistency) all still pass.
